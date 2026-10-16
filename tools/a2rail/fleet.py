"""Run bounded independent native command/assertion cells, retaining every result.

CPU/RAM/socket/output roots are per cell; declared inputs are staged separately.
This is not checkpoint branching or an OS sandbox. Explicit literal paths,
configuration references and host endpoints can still identify shared resources.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import time
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug import RequestPending
from gs2debug.mcp import Backend
from gs2debug.types import EVT_AGENT_RECORD


def encoded(value):return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False)


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()


def relative(value):
    if not isinstance(value,str) or not value or not PurePosixPath(value).parts or '\\' in value or ':' in value or any(part in ('..','.git') for part in PurePosixPath(value).parts) or PurePosixPath(value).is_absolute():
        raise ValueError('fleet paths require relative forward-slash names')
    return Path(*PurePosixPath(value).parts)


def number(value,low,high):
    if type(value) is not int or not low<=value<=high:raise ValueError(f'fleet integer must be {low}..{high}')


def command(value):
    if not isinstance(value,str) or not value.strip() or len(value.encode('utf-8'))>1800 or any(ord(c)<32 or ord(c)==127 for c in value):
        raise ValueError('fleet commands must be bounded, single-line native commands')


def expand(text,root,inputs,create_outputs=False):
    def replace(match):
        kind,name=match.groups();path=relative(name)
        if kind=='input' and name not in inputs:raise ValueError('undeclared fleet input: '+name)
        result=(root/('inputs' if kind=='input' else 'artifacts')/path).resolve()
        if not result.is_relative_to(root.resolve()):raise ValueError('fleet artifact escapes cell')
        if kind=='output' and create_outputs:result.parent.mkdir(parents=True,exist_ok=True)
        return '"'+result.as_posix()+'"'
    result=re.sub(r'\{(input|output):([^{}]+)\}',replace,text)
    if re.search(r'\{[A-Za-z_][A-Za-z0-9_]*:',result):raise ValueError('unknown or unterminated fleet placeholder')
    command(result)
    return result


def validate(plan):
    if not isinstance(plan,dict) or set(plan)!={'schema','workers','jobs'} or plan['schema']!='a2fleet-plan-v1':raise ValueError('invalid fleet schema/fields')
    number(plan['workers'],1,8)
    if not isinstance(plan['jobs'],list) or not 1<=len(plan['jobs'])<=64:raise ValueError('fleet requires 1..64 explicit cells')
    ids=set()
    for job in plan['jobs']:
        if not isinstance(job,dict) or set(job)-{'id','platform','config','seed','files','commands','assertions','timeout_seconds','event_queue_cap','event_kinds'} or not {'id','seed','files','commands','assertions','timeout_seconds'}<=set(job):raise ValueError('invalid fleet cell fields')
        if not isinstance(job['id'],str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,47}',job['id']) or job['id'].casefold() in ids:raise ValueError('invalid/duplicate fleet cell id')
        ids.add(job['id'].casefold());number(job['seed'],1,0xffffffff);number(job['timeout_seconds'],1,600)
        number(job.get('event_queue_cap',1024),1,4096)
        kinds=job.get('event_kinds',['stop','irq','context','scheduler','device'])
        if not isinstance(kinds,list) or not 1<=len(kinds)<=16 or any(not isinstance(kind,str) or not re.fullmatch('[a-z_]{1,32}',kind) for kind in kinds) or len(set(kinds))!=len(kinds):raise ValueError('invalid fleet event filters; discover native kinds via MANIFEST')
        if ('platform' in job)==('config' in job):raise ValueError('fleet cell requires platform or staged config')
        if 'platform' in job:number(job['platform'],0,100)
        if not isinstance(job['files'],list) or len(job['files'])>32:raise ValueError('fleet cell requires up to 32 pinned inputs')
        names=set()
        for row in job['files']:
            if not isinstance(row,dict) or set(row)!={'source','path','sha256'} or not isinstance(row['source'],str) or not Path(row['source']).is_absolute():raise ValueError('fleet input requires absolute source/path/sha256')
            relative(row['path'])
            if row['path'].casefold() in names or not isinstance(row['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',row['sha256']):raise ValueError('duplicate fleet input or invalid digest')
            names.add(row['path'].casefold())
        inputs={row['path'] for row in job['files']}
        if 'config' in job and job['config'] not in inputs:raise ValueError('fleet config must be a pinned input')
        for key,minimum,maximum in [('commands',1,64),('assertions',1,16)]:
            if not isinstance(job[key],list) or not minimum<=len(job[key])<=maximum:raise ValueError('invalid fleet '+key)
            for text in job[key]:command(text);expand(text,Path.cwd().resolve()/'fleet-validation',inputs)


def cell(job,exe,identity,root):
    root.mkdir(exist_ok=False)
    (root/'inputs').mkdir();(root/'artifacts').mkdir()
    (root/'job.json').write_text(encoded(job)+'\n',encoding='utf-8')
    result={'schema':'a2fleet-cell-v1','id':job['id'],'ok':False,'outcome':'inconclusive',
        'started_host_ns':str(time.time_ns()),'steps':[],'assertions':[],'inputs':job['files'],
        'artifact_directory':str(root),'executable_sha256':identity,'seed':job['seed'],
        'model':'independent CPU/RAM/socket/staged-input/output roots; literal external paths/endpoints are caller-controlled; not checkpoint branching'}
    backend=None;event_stream=None
    capture={'complete':False,'received':0,'written':0,'client_dropped':0,'sequence_ok':True,
        'queue_cap':job.get('event_queue_cap',1024),
        'client_limit':4096,'kinds':job.get('event_kinds',['stop','irq','context','scheduler','device']),
        'model':'declared native event subset; bounded producer queue and client file; not full-machine replay'}
    result['event_capture']=capture
    try:
        for row in job['files']:
            source=Path(row['source']);target=root/'inputs'/relative(row['path'])
            if digest(source)!=row['sha256']:raise ValueError('fleet input changed before staging: '+row['path'])
            target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as stream:stream.write(source.read_bytes())
            if digest(target)!=row['sha256']:raise ValueError('fleet input changed during staging')
        if digest(exe)!=identity:raise ValueError('fleet executable changed before launch')
        args=SimpleNamespace(exe=exe,socket=None,out_dir=root/'session',out_root=None,artifact_root=None,
            platform=job.get('platform'),config=root/'inputs'/relative(job['config']) if 'config' in job else None,
            seed=job['seed'],rail=[],read_only_media=True)
        backend=Backend(args);backend.open()
        result['pid']=backend.process.pid
        result['native_companion_discovery']=next((row for row in backend.catalog['capabilities'].get('companion_tools',[]) if row.get('name')=='fleet'),None)
        result['native_ready_host_ns']=str(time.time_ns())
        deadline=time.monotonic()+job['timeout_seconds']
        def call(text):
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError('fleet native-operation budget exhausted')
            try:return backend.client.agent_command(text,check=False,timeout=min(remaining,1))
            except RequestPending:
                result.setdefault('pending_recoveries',[]).append(dict(backend.client.pending_request,command=text))
                while True:
                    remaining=deadline-time.monotonic()
                    if remaining<=0:raise TimeoutError('fleet pending native operation exceeded budget; effects unknown')
                    try:return backend.client.wait_agent_command(check=False,timeout=min(remaining,1))
                    except RequestPending:continue
        pins=call('rail read A2GSPU_RAMDISK');result['pins_readback']=pins
        if not pins['ok'] or not re.search(r'(?:^|\s)ramdisk=1(?:\s|$)',pins['text']) or not re.search(r'(?:^|\s)seed='+str(job['seed'])+r'(?:\s|$)',pins['text']):raise ValueError('fleet native launch pins disagree')
        result['writeback_policy']=call('rail read A2GSPU_NO_WRITEBACK')
        policy=result['writeback_policy'].get('data') or {}
        if policy.get('schema')!='a2writeback-policy-v1' or policy.get('suppressed') is not True or policy.get('flush_at_spike_end') is not False:
            raise ValueError('fleet requires native writeback-policy readback; select the current fork build')
        event_stream=(root/'artifacts/events.jsonl').open('w',encoding='utf-8')
        def event(kind,sequence,data):
            if kind!=EVT_AGENT_RECORD:return
            row=json.loads(data.decode('utf-8'))
            if row.get('schema')!='a2subscription-v1' or row.get('sequence')!=capture['received']:capture['sequence_ok']=False
            capture['received']+=1
            if capture['written']<capture['client_limit']:
                event_stream.write(encoded(row)+'\n');capture['written']+=1
            else:capture['client_dropped']+=1
        backend.client.on_event(event)
        capture['start']=backend.client.subscribe(tuple(capture['kinds']),queue_cap=capture['queue_cap'])
        if not capture['start']['complete']:raise ValueError('fleet event producer did not arm completely')
        if not call('context on 256')['ok']:raise ValueError('fleet context observer refused')
        result['before']=call('context overview')
        for text in job['commands']:
            text=expand(text,root,{row['path'] for row in job['files']},create_outputs=True)
            reply=call(text);result['steps'].append(reply)
            if not reply['ok']:
                result['outcome']='native-command-refusal';break
        else:
            for expression in job['assertions']:
                reply=call('assert '+expand(expression,root,{row['path'] for row in job['files']},create_outputs=True))
                result['assertions'].append(reply)
            result['ok']=all(reply['ok'] for reply in result['assertions'])
            result['outcome']='pass' if result['ok'] else 'assertion-failure'
        result['after']=call('context overview')
        result['context_snapshot']=call('context snapshot "'+(root/'artifacts/context.json').as_posix()+'"')
        if not result['context_snapshot']['ok']:raise ValueError('fleet final snapshot refused')
        capture['close']=backend.client.unsubscribe()
        capture['complete']=bool(capture['close']['complete'] and int(capture['close']['retained'])==capture['received'] and not capture['client_dropped'] and capture['sequence_ok'])
        if not capture['complete']:raise ValueError('fleet event evidence is incomplete or lost')
        for row in job['files']:
            if digest(Path(row['source']))!=row['sha256'] or digest(root/'inputs'/relative(row['path']))!=row['sha256']:
                raise ValueError('fleet declared input mutated during cell')
        if digest(exe)!=identity:raise ValueError('fleet executable changed during cell')
    except Exception as error:
        result.update(ok=False,outcome='inconclusive',error=str(error))
        if backend is not None and backend.client.pending_request is not None:
            result['pending_request']=backend.client.pending_request;result['effects_unknown']=True
    finally:
        if backend is not None:
            # A pending synchronous machine command cannot accept a second
            # mutation. Stop only our own child after its explicit budget.
            if result.get('effects_unknown') and backend.process is not None and backend.process.poll() is None:
                backend.process.kill();backend.process.wait(timeout=5);result['termination']='owned-child-killed-after-pending-budget'
            try:backend.close()
            except Exception as error:result.update(ok=False,outcome='inconclusive',shutdown_error=str(error))
            if backend.process is not None:
                result['exit_code']=backend.process.poll()
                if result['exit_code']!=0:result.update(ok=False,outcome='inconclusive')
        if event_stream is not None:event_stream.close()
        result['finished_host_ns']=str(time.time_ns())
        artifacts=[]
        for path in sorted(root.rglob('*')):
            if path==root/'session/debug.sock':
                # Windows AF_UNIX endpoints can look like regular files after
                # an owned forced shutdown, but ReadFile returns EINVAL.
                result.setdefault('non_payload_artifacts',[]).append({'path':'session/debug.sock','kind':'owned-closed-IPC-endpoint'})
                continue
            try:
                if path.is_file() and path.name!='result.json':artifacts.append({'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':digest(path)})
            except OSError as error:
                result.setdefault('artifact_errors',[]).append({'path':path.relative_to(root).as_posix(),'error':str(error)})
                result.update(ok=False,outcome='inconclusive')
        result['artifacts']=artifacts
        (root/'result.json').write_text(encoded(result)+'\n',encoding='utf-8')
    return result


def run(plan,executable,directory):
    validate(plan);exe=Path(executable).resolve();identity=digest(exe);root=Path(directory).resolve()
    for job in plan['jobs']:
        for text in job['commands']+job['assertions']:expand(text,root/job['id'],{row['path'] for row in job['files']})
    root.mkdir(parents=True,exist_ok=False)
    (root/'plan.json').write_text(encoded(plan)+'\n',encoding='utf-8')
    results={}
    with ThreadPoolExecutor(max_workers=plan['workers']) as pool:
        futures={pool.submit(cell,job,exe,identity,root/job['id']):job['id'] for job in plan['jobs']}
        for future in as_completed(futures):
            identifier=futures[future]
            try:results[identifier]=future.result()
            except Exception as error:results[identifier]={'schema':'a2fleet-cell-v1','id':identifier,'ok':False,'outcome':'inconclusive','error':'worker failure: '+str(error)}
            print(encoded({'id':identifier,'ok':results[identifier]['ok'],'outcome':results[identifier]['outcome']}),flush=True)
            # A crash can leave useful completed-cell evidence in this journal.
            with (root/'completed.jsonl').open('a',encoding='utf-8') as stream:stream.write(encoded(results[identifier])+'\n')
    ordered=[results[job['id']] for job in plan['jobs']]
    report={'schema':'a2fleet-run-v1','ok':all(row['ok'] for row in ordered),'workers':plan['workers'],
        'executable':str(exe),'executable_sha256':identity,'plan_sha256':digest(root/'plan.json'),'cells':ordered,
        'counts':{name:sum(row['outcome']==name for row in ordered) for name in sorted({row['outcome'] for row in ordered})},
        'model':'all explicit cells reported; assertion failure is distinct from inconclusive transport/budget/setup; no full-machine replay or hardware claim'}
    (root/'fleet.json').write_text(encoded(report)+'\n',encoding='utf-8')
    return report


def schema():
    def integer(low,high):return {'type':'integer','minimum':low,'maximum':high}
    def array(item,low,high):return {'type':'array','items':item,'minItems':low,'maxItems':high}
    def obj(properties,required):return {'type':'object','properties':properties,'required':required,'additionalProperties':False}
    text={'type':'string','minLength':1,'maxLength':1800,'pattern':r'^[^\x00-\x1f\x7f]+$'}
    file=obj({'source':{'type':'string','minLength':1},'path':{'type':'string','minLength':1},'sha256':{'type':'string','pattern':'^[0-9a-f]{64}$'}},['source','path','sha256'])
    job=obj({'id':{'type':'string','pattern':'^[A-Za-z0-9][A-Za-z0-9_-]{0,47}$'},'platform':integer(0,100),'config':{'type':'string','minLength':1},
        'seed':integer(1,0xffffffff),'timeout_seconds':integer(1,600),'event_queue_cap':dict(integer(1,4096),default=1024),
        'event_kinds':dict(array({'type':'string','pattern':'^[a-z_]{1,32}$'},1,16),uniqueItems=True),
        'files':array(file,0,32),'commands':array(text,1,64),'assertions':array(text,1,16)},['id','seed','timeout_seconds','files','commands','assertions'])
    job['oneOf']=[{'required':['platform'],'not':{'required':['config']}},{'required':['config'],'not':{'required':['platform']}}]
    value=obj({'schema':{'const':'a2fleet-plan-v1'},'workers':integer(1,8),'jobs':array(job,1,64)},['schema','workers','jobs'])
    value.update({'$schema':'https://json-schema.org/draft/2020-12/schema','title':'Explicit native fleet plan',
        'description':'Structural schema; runtime additionally validates UTF8 byte lengths, absolute sources, relative/unique paths/IDs, placeholders and pinned artifacts.'})
    return value


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--plan',type=Path);parser.add_argument('--exe',type=Path);parser.add_argument('--out',type=Path);parser.add_argument('--schema',action='store_true',help='emit the structural JSON Schema without launching')
    args=parser.parse_args()
    if args.schema:print(encoded(schema()));raise SystemExit(0)
    if args.plan is None or args.exe is None or args.out is None:parser.error('run requires --plan, --exe and --out')
    try:
        report=run(json.loads(args.plan.read_text(encoding='utf-8')),args.exe,args.out)
        print(encoded({'ok':report['ok'],'counts':report['counts'],'report':str(args.out/'fleet.json')}))
        raise SystemExit(0 if report['ok'] else 1)
    except (ValueError,OSError) as error:
        print('fleet: '+str(error),file=sys.stderr);raise SystemExit(2)
