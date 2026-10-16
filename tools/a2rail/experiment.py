"""Build/run a declared raw CPU fixture and retain context-bound efficiency evidence.

An additive Rosetta companion, not a new Rosetta v3 CLI feature. Build tools
are explicit executable/argv lists; no shell. Each invocation owns a fresh
directory and a fresh machine. A raw load is never a GS/OS boot claim.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'clients/python/src'))
from gs2debug.mcp import Backend
from gs2debug.observations import observe
from gs2debug import RequestPending, ProtocolError
import cidarbindings


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            value.update(chunk)
    return value.hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def child(root, name):
    if not isinstance(name, str) or not name or Path(name).is_absolute() or ':' in name or '..' in Path(name).parts:
        raise ValueError('artifact name must be relative to the experiment')
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise ValueError('artifact escapes experiment directory')
    return path


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'expected integer in {low}..{high}')
    return value


def validate(plan):
    allowed = {'schema', 'workload', 'platform', 'files', 'build', 'image', 'registers', 'entry', 'target', 'attempts', 'checks', 'loads', 'preserve_registers', 'profile', 'footprint', 'cidar'}
    if not isinstance(plan, dict) or set(plan)-allowed or plan.get('schema') != 'a2experiment-plan-v1':
        raise ValueError('invalid experiment schema or fields')
    if not isinstance(plan.get('workload'), dict) or set(plan['workload']) != {'id', 'case'} or any(not isinstance(v, str) or not v for v in plan['workload'].values()):
        raise ValueError('workload requires nonempty id and case strings')
    integer(plan.get('platform'), 0, 100)
    for key in ('entry', 'target'):
        integer(plan.get(key), 0, 0xffffff)
    integer(plan.get('attempts'), 1, 10000000)
    if not isinstance(plan.get('image'), dict) or set(plan['image']) != {'path', 'address', 'format'} or plan['image']['format'] != 'raw':
        raise ValueError('explicit raw image path/address/format required; OMF needs an independent load plan or OS loader')
    integer(plan['image']['address'], 0, 0xffffff)
    if not isinstance(plan.get('registers', {}), dict) or set(plan.get('registers', {})) - {'a', 'x', 'y', 'sp', 'p', 'e', 'd', 'dbr'}:
        raise ValueError('unsupported explicit CPU registers')
    for key, value in plan.get('registers', {}).items():
        integer(value, 0, 1 if key == 'e' else 255 if key in ('p', 'dbr') else 65535)
    checks = plan.get('checks')
    if not isinstance(checks, list) or not 1 <= len(checks) <= 16:
        raise ValueError('one to sixteen independent result memory checks required')
    for check in checks:
        if not isinstance(check, dict) or set(check)-{'address','bytes','role'} or not {'address','bytes'} <= set(check) or check.get('role','output') not in ('output','preserved-input'):
            raise ValueError('check requires address/bytes')
        integer(check['address'], 0, 0xffffff)
        if not isinstance(check['bytes'], list) or not 1 <= len(check['bytes']) <= 256:
            raise ValueError('check requires 1..256 expected bytes')
        for byte in check['bytes']:
            integer(byte, 0, 255)
        if check['address'] + len(check['bytes']) > 0x1000000:
            raise ValueError('check exceeds CPU address space')
    if not any(check.get('role','output')=='output' for check in checks):
        raise ValueError('an output check is required for poisoning/negative control')
    if type(plan.get('profile',False)) is not bool:
        raise ValueError('profile must be boolean')
    if 'cidar' in plan:cidarbindings.validate_declaration(plan['cidar'])
    registers=plan.get('preserve_registers',[])
    if not isinstance(registers,list) or len(registers)>8 or any(not isinstance(key,str) or key not in {'a','x','y','sp','p','e','d','dbr'} for key in registers) or len(set(registers))!=len(registers):
        raise ValueError('invalid register preservation list')
    loads=plan.get('loads',[])
    if not isinstance(loads,list) or len(loads)>16:
        raise ValueError('invalid declared input loads')
    for row in loads:
        if not isinstance(row,dict) or set(row)!={'path','address'} or not isinstance(row['path'],str):
            raise ValueError('load requires path/address')
        integer(row['address'],0,0xffffff)
    if 'footprint' in plan:
        policy=plan['footprint']
        if not isinstance(policy,dict) or set(policy)-{'windows','allowed','max_records','exact_writes'} or not {'windows','allowed','max_records'} <= set(policy):
            raise ValueError('invalid write footprint policy')
        integer(policy['max_records'],1,100000)
        if 'exact_writes' in policy:integer(policy['exact_writes'],0,policy['max_records']-1)
        if not isinstance(policy['windows'],list) or not 1<=len(policy['windows'])<=8 or not isinstance(policy['allowed'],list) or not 1<=len(policy['allowed'])<=16:
            raise ValueError('invalid footprint windows/allowed regions')
        for window in policy['windows']:
            if not isinstance(window,dict) or set(window)!={'bank','lo','hi'}:raise ValueError('invalid footprint window')
            integer(window['bank'],0,255);integer(window['lo'],0,65535);integer(window['hi'],window['lo'],65535)
        for region in policy['allowed']:
            if not isinstance(region,dict) or set(region)!={'address','length'}:raise ValueError('invalid footprint allowed region')
            integer(region['address'],0,0xffffff);integer(region['length'],1,0x1000000-region['address'])
            cursor=region['address']
            for lo,hi in sorted(((window['bank']<<16)+window['lo'],(window['bank']<<16)+window['hi']+1) for window in policy['windows']):
                if lo<=cursor<hi:cursor=hi
            if cursor<region['address']+region['length']:raise ValueError('allowed writes are not covered by watched windows')
    if not isinstance(plan.get('files'), list) or not 1 <= len(plan['files']) <= 64:
        raise ValueError('explicit digest-bound build inputs required')
    for row in plan['files']:
        if not isinstance(row, dict) or set(row) != {'source', 'path', 'sha256'} or not Path(row['source']).is_absolute():
            raise ValueError('input requires absolute source, relative path and sha256')
        if not isinstance(row['sha256'], str) or len(row['sha256']) != 64 or any(c not in '0123456789abcdef' for c in row['sha256']):
            raise ValueError('input sha256 must be lowercase hex')
    if len({row['path'] for row in plan['files']})!=len(plan['files']):raise ValueError('duplicate input destination')
    if not isinstance(plan.get('build', []), list) or len(plan.get('build', [])) > 16:
        raise ValueError('build must be a bounded list of explicit argument lists')
    for argv in plan.get('build', []):
        if not isinstance(argv, list) or not argv or any(not isinstance(arg, str) or '\0' in arg for arg in argv) or not Path(argv[0]).is_absolute():
            raise ValueError('build commands require absolute executable and string argv; no shell')


def check_profile(profile, execution):
    valid=profile.get('schema')=='a2profile-v1' and profile.get('model')=='instruction-cycles'
    rows=profile.get('pcs',[])
    valid=valid and isinstance(rows,list) and all(isinstance(row,dict) and all(type(row.get(key)) is int and row[key]>=0 for key in ('pc','instructions','cycles')) for row in rows)
    if valid:
        valid=len({row['pc'] for row in rows})==len(rows) and all(row['pc']<=0xffffff for row in rows)
    if valid:
        valid=all(sum(row[key] for row in rows)==profile.get('total_'+key)==int(execution.get(key,'-1')) for key in ('instructions','cycles'))
    return {'ok':bool(valid),'model':'per-PC instruction counts/cycles must cover this measured interval','pcs':rows}


def check_footprint(records, policy):
    writes=[];unexpected=[];loss=False
    for row in records:
        if row.get('event')!='watch.write' or row.get('kind')!='write' or any(type(row.get(key)) is not int or not 0<=row[key]<=limit for key,limit in [('pc',0xffffff),('addr',0xffffff),('data',255)]):
            loss=True;continue
        writes.append(row)
        if not any(region['address']<=row['addr']<region['address']+region['length'] for region in policy['allowed']):unexpected.append(row)
    counts=Counter(row['pc'] for row in writes)
    exact=len(writes)==policy.get('exact_writes',len(writes))
    return {'ok':not loss and not unexpected and exact,'writes':len(writes),'loss_or_unrecognized_records':loss,
            'unexpected':unexpected,'exact_write_count_matches':exact,
            'writers':[{'pc':pc,'writes':count} for pc,count in sorted(counts.items())],
            'model':'CPU bus writes in explicitly watched bank windows; no unwatched-region/DMA/transient-device completeness claim'}


def run(plan, executable, directory):
    validate(plan)
    directory = Path(directory).resolve()
    # Validate all destination paths before creating output or launching tools.
    for row in plan['files']:
        child(directory, row['path'])
    for row in plan.get('loads',[]):child(directory,row['path'])
    image = child(directory, plan['image']['path'])
    directory.mkdir(parents=True, exist_ok=False)
    (directory/'plan.json').write_text(encoded(plan)+'\n', encoding='utf-8')
    report = {'schema': 'a2experiment-run-v1', 'ok': False, 'outcome': 'setup-failure', 'workload': plan['workload'],
              'platform': plan['platform'], 'checks': plan['checks'], 'build': [], 'inputs': plan['files'],
              'claim': 'declared raw CPU fixture on the selected emulator; not physical hardware or application boot acceptance'}
    report['contract_schema']='a2experiment-contract-v2'
    report['directory']=str(directory)
    report['plan_sha256']=digest(directory/'plan.json')
    backend = None
    try:
        for row in plan['files']:
            source = Path(row['source']); target = child(directory, row['path'])
            if digest(source) != row['sha256']:
                raise ValueError('input identity mismatch: '+str(source))
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(source.read_bytes())
            if digest(target) != row['sha256']:
                raise ValueError('input changed during staging')
        for index, argv in enumerate(plan.get('build', [])):
            tool = Path(argv[0]).resolve(); before = digest(tool)
            command = [arg.replace('{work}', str(directory)) for arg in argv]
            with (directory/f'build-{index}.stdout').open('wb') as stdout, (directory/f'build-{index}.stderr').open('wb') as stderr:
                result = subprocess.run(command, cwd=directory, stdout=stdout, stderr=stderr, timeout=120)
            report['build'].append({'argv': command, 'tool_sha256': before, 'exit_code': result.returncode})
            if digest(tool) != before or result.returncode:
                raise ValueError('build failed or executable identity changed')
        for row in plan['files']:
            if digest(child(directory,row['path']))!=row['sha256']:raise ValueError('build changed a digest-pinned input')
        size = image.stat().st_size
        if not size or size > 512*1024 or plan['image']['address']+size > 0x1000000:
            raise ValueError('raw CPU image range invalid')
        if any(max(plan['image']['address'], check['address']) < min(plan['image']['address']+size, check['address']+len(check['bytes'])) for check in plan['checks']):
            raise ValueError('result checks overlap loaded program; declare separate fixture output')
        report['image'] = {'path': str(image), 'sha256': digest(image), 'size': size, 'address': plan['image']['address']}
        report['loads']=[]
        staged={row['path']:row['sha256'] for row in plan['files']}
        regions=[(plan['image']['address'],plan['image']['address']+size)]
        for row in plan.get('loads',[]):
            path=child(directory,row['path']);length=path.stat().st_size
            if not 1<=length<=65536 or row['address']+length>0x1000000 or staged.get(row['path'])!=digest(path):
                raise ValueError('data load requires a bounded digest-pinned input')
            interval=(row['address'],row['address']+length)
            if any(max(interval[0],lo)<min(interval[1],hi) for lo,hi in regions):raise ValueError('data loads overlap each other or the program')
            if any(check.get('role','output')=='output' and max(interval[0],check['address'])<min(interval[1],check['address']+len(check['bytes'])) for check in plan['checks']):
                raise ValueError('output checks overlap a declared input')
            regions.append(interval)
            report['loads'].append({'path':str(path),'address':row['address'],'size':length,'sha256':digest(path)})
        for check in plan['checks']:
            if check.get('role')=='preserved-input':
                match=next((row for row in report['loads'] if row['address']<=check['address'] and check['address']+len(check['bytes'])<=row['address']+row['size']),None)
                if match is None:raise ValueError('preservation check requires declared input bytes')
                offset=check['address']-match['address']
                if list(Path(match['path']).read_bytes()[offset:offset+len(check['bytes'])])!=check['bytes']:raise ValueError('preservation oracle disagrees with staged input')
        contract={'schema':report['contract_schema'],'workload':plan['workload'],'checks':plan['checks'],
                  'inputs':[{key:row[key] for key in ('address','size','sha256')} for row in report['loads']],
                  'registers':plan.get('registers',{}),'preserve_registers':plan.get('preserve_registers',[]),
                  'profile':plan.get('profile',False),'footprint':plan.get('footprint')}
        report['workload_sha256']=hashlib.sha256(encoded(contract).encode()).hexdigest()
        report['contract']=contract
        if 'cidar' in plan:
            report['cidar']=cidarbindings.verify(directory,plan['cidar'],staged,contract,report['image'],plan['entry'],plan['target'])
        args = SimpleNamespace(exe=Path(executable).resolve(), socket=None, out_dir=directory/'machine', out_root=None,
                               artifact_root=None, config=None, platform=plan['platform'], seed=1, rail=[])
        backend = Backend(args); backend.open()
        client = backend.client
        def command(text, check=True):
            try:
                return client.agent_command(text, check=check, timeout=30)
            except RequestPending:
                report.setdefault('pending_recoveries', []).append(dict(client.pending_request, command=text))
                return client.wait_agent_command(check=check, timeout=30)
        command('context on 256')
        command(f'load {plan["image"]["address"]:06X} "{image.as_posix()}"')
        for row in report['loads']:command(f'load {row["address"]:06X} "{Path(row["path"]).as_posix()}"')
        for check in plan['checks']:
            if check.get('role')=='preserved-input':continue
            poison = ' '.join(f'{byte ^ 255:02X}' for byte in check['bytes'])
            command(f'poke {check["address"]:06X} '+poison)
        for key, value in plan.get('registers', {}).items():
            command(f'setreg {key} {value:X}')
        command(f'setreg pc {plan["entry"]:06X}')
        if plan.get('preserve_registers'):
            report['registers_before']=command('cpu')['data']['state']['registers']
        if plan.get('profile'):
            command(f'cov on {plan["image"]["address"]:06X}-{plan["image"]["address"]+size-1:06X}')
            command('cov counts on')
        if 'footprint' in plan:
            command('watch bus clear');command('watch bus read off');command('watch bus change off')
            command('rail set A2GSPU_WATCH_MAX '+str(plan['footprint']['max_records']))
            command('rail set A2GSPU_WATCH_OUT write-footprint.jsonl')
            for window in plan['footprint']['windows']:command(f'watch bus add {window["bank"]:02X}:{window["lo"]:04X}-{window["hi"]:04X}')
        report['before'] = command('context overview')['data']
        execution = command(f'run-until {plan["target"]:06X} {plan["attempts"]}', check=False)
        report['execution'] = execution
        report['after'] = command('context overview')['data']
        effects_ok=True
        if plan.get('preserve_registers'):
            report['registers_after']=command('cpu')['data']['state']['registers']
            report['preserved_registers']={key:key in report['registers_before'] and report['registers_before'][key]==report['registers_after'].get(key) for key in plan['preserve_registers']}
            effects_ok=all(report['preserved_registers'].values())
        if plan.get('profile'):
            command('cov counts write instruction-profile.json');command('cov off')
            artifact=directory/'machine'/'instruction-profile.json'
            profile=json.loads(artifact.read_text())
            report['profile']=check_profile(profile,execution.get('data') or {})
            report['profile'].update(path=str(artifact),sha256=digest(artifact))
            effects_ok=effects_ok and report['profile']['ok']
        if 'footprint' in plan:
            command('watch bus clear');command('rail unset A2GSPU_WATCH_OUT')
            artifact=directory/'machine'/'write-footprint.jsonl'
            rows=[json.loads(line) for line in artifact.read_text().splitlines() if line]
            report['footprint']=check_footprint(rows,plan['footprint'])
            report['footprint'].update(path=str(artifact),sha256=digest(artifact))
            effects_ok=effects_ok and report['footprint']['ok']
        report['observations'] = []
        for check in plan['checks']:
            report['observations'].append(observe(client, 'gs2_memory_read', {'domain': 'main', 'address': check['address'], 'length': len(check['bytes'])}))
        first = next(check for check in plan['checks'] if check.get('role','output')=='output')
        report['negative_control'] = command(f'assert peek:{first["address"]:06X}==${first["bytes"][0]^1:02X}', check=False)
        snapshot = directory/'machine'/'context.json'
        report['context_snapshot'] = command('context snapshot "'+snapshot.as_posix()+'"')['data']
        report['context_snapshot_sha256'] = digest(snapshot)
        completed = execution.get('data') or {}
        reached = completed.get('completed') and completed.get('target_reached') and int(completed.get('instructions', '0')) > 0
        correct = all(item['bytes'] == expected['bytes'] and not item['unreadable_bytes'] for item, expected in zip(report['observations'], plan['checks']))
        negative = report['negative_control'].get('ok') is False
        report['ok'] = bool(reached and correct and negative and effects_ok)
        report['outcome'] = 'pass' if report['ok'] else 'incomplete-execution' if not reached else 'assertion-mismatch' if not correct else 'side-effect-or-measurement-mismatch' if not effects_ok else 'negative-control-failure'
        report['executable_sha256'] = backend.executable_sha256
        report['pins'] = {'seed': 1, 'faketime': 1000000000, 'bram': 'default'}
    except Exception as error:
        report['error'] = str(error)
        if isinstance(error, (RequestPending, OSError, ProtocolError)) and backend is not None:
            report['outcome'] = 'transport-or-native-refusal'
            report['pending_request'] = backend.client.pending_request
            report['outcome_unknown'] = isinstance(error, RequestPending) or isinstance(error, OSError)
    finally:
        if backend is not None:
            try:
                backend.close()
            except Exception as error:
                report['ok'] = False
                report['outcome'] = 'shutdown-failure'
                report['shutdown_error'] = str(error)
            if backend.process is not None:
                report['exit_code'] = backend.process.poll()
                if report['exit_code'] != 0:
                    report['ok'] = False
                    report['outcome'] = 'shutdown-failure'
        path = directory/'run.json'
        path.write_text(encoded(report)+'\n', encoding='utf-8')
    return report


def compare(left, right):
    for report in (left, right):
        if report.get('schema') != 'a2experiment-run-v1' or report.get('ok') is not True or report.get('outcome') != 'pass' or not report.get('checks'):
            raise ValueError('efficiency comparison requires two passing experiment records')
        execution = report.get('execution', {}).get('data', {})
        if execution.get('schema') != 'a2execution-v1' or execution.get('cycle_model') != 'in-command' or not execution.get('completed') or not execution.get('target_reached'):
            raise ValueError('incomplete or incompatible native measurement')
        if not report.get('negative_control', {}).get('ok') is False:
            raise ValueError('missing failing negative control')
        before, after = report['before']['stamp'], report['after']['stamp']
        if before['context_session'] != after['context_session'] or before['clock_epoch'] != after['clock_epoch']:
            raise ValueError('measurement crosses a session or observed clock discontinuity')
        if int(after['cpu_cycles'])-int(before['cpu_cycles']) != int(execution['cycles']):
            raise ValueError('context CPU interval disagrees with execution receipt')
        if not all(item['bytes'] == expected['bytes'] and item['unreadable_bytes'] == 0 for item, expected in zip(report['observations'], report['checks'])) or len(report['observations']) != len(report['checks']):
            raise ValueError('incorrect measured output')
        contract=report.get('contract')
        if contract is not None:
            if contract.get('checks')!=report['checks'] or hashlib.sha256(encoded(contract).encode()).hexdigest()!=report['workload_sha256']:
                raise ValueError('experiment contract identity mismatch')
            if contract.get('inputs')!=[{key:row[key] for key in ('address','size','sha256')} for row in report.get('loads',[])]:
                raise ValueError('experiment input load contract mismatch')
            for key in contract.get('preserve_registers',[]):
                if key not in report.get('registers_before',{}) or report['registers_before'][key]!=report.get('registers_after',{}).get(key):
                    raise ValueError('preserved register mismatch')
            if contract.get('profile') and report.get('profile',{}).get('ok') is not True:raise ValueError('missing complete instruction profile')
            if contract.get('footprint') and report.get('footprint',{}).get('ok') is not True:raise ValueError('missing complete write footprint')
        verify_artifacts(report)
    if left.get('contract_schema')!=right.get('contract_schema') or ('cidar' in left)!=('cidar' in right):
        raise ValueError('incompatible experiment contract/binding versions')
    if 'cidar' in left and left['cidar']['primitive']!=right['cidar']['primitive']:
        raise ValueError('incomparable CIDAR primitive/spec identities')
    for key in ('workload_sha256', 'platform', 'executable_sha256', 'pins'):
        if left[key] != right[key]:
            raise ValueError('incomparable experiment '+key)
    if left['before']['data']['clock'] != right['before']['data']['clock']:
        # Absolute frame/phase state is part of this contract, not merely MHz.
        raise ValueError('clock mode/phase setup differs')
    metrics = {}
    for name in ('cycles', 'instructions'):
        values = [report['execution']['data'][name] for report in (left, right)]
        if any(not isinstance(value, str) or not value.isascii() or not value.isdecimal() or int(value) <= 0 for value in values):
            raise ValueError('invalid or zero measurement '+name)
        a, b = map(int, values)
        metrics[name] = {'baseline': str(a), 'candidate': str(b), 'delta': str(b-a), 'reduction_percent': 100*(a-b)/a}
    metrics['image_bytes'] = {'baseline': left['image']['size'], 'candidate': right['image']['size'], 'delta': right['image']['size']-left['image']['size']}
    for name in ('master_14m_ticks', 'video_cycles'):
        intervals = [int(report['after']['stamp'][name])-int(report['before']['stamp'][name]) for report in (left, right)]
        if any(value < 0 for value in intervals):
            raise ValueError('native clock moved backward during measurement')
        a, b = intervals
        metrics[name] = {'baseline': str(a), 'candidate': str(b), 'delta': str(b-a), 'reduction_percent': 100*(a-b)/a if a else None}
    return {'schema': 'a2efficiency-comparison-v1', 'ok': True, 'workload': left['workload'], 'metrics': metrics,
            'bounds': {'kind': 'observed-pair', 'proven_floor': False, 'proven_ceiling': False},
            'hardware_status': 'emulator measurements; no physical-IIgs result implied',
            'model': 'same declared workload/results, emulator, platform, pins and clock setup; correctness and negative controls required'}


def verify_artifacts(report):
    """Recompute evidence at comparison time; serialized success flags are insufficient."""
    for row in [report['image'],*report.get('loads',[])]:
        if digest(row['path'])!=row['sha256'] or Path(row['path']).stat().st_size!=row['size']:
            raise ValueError('retained image/input identity mismatch')
    if digest(report['context_snapshot']['path'])!=report['context_snapshot_sha256']:
        raise ValueError('retained context identity mismatch')
    contract=report.get('contract',{})
    for name,enabled in [('profile',contract.get('profile')),('footprint',contract.get('footprint'))]:
        if not enabled:continue
        row=report[name]
        if digest(row['path'])!=row['sha256']:raise ValueError('retained '+name+' identity mismatch')
        text=Path(row['path']).read_text(encoding='utf-8')
        recomputed=check_profile(json.loads(text),report['execution']['data']) if name=='profile' else check_footprint([json.loads(line) for line in text.splitlines() if line],enabled)
        if not recomputed['ok'] or any(row.get(key)!=value for key,value in recomputed.items()):raise ValueError('retained '+name+' evidence mismatch')
    if 'cidar' in report:
        root=Path(report['directory']).resolve()
        if digest(root/'plan.json')!=report['plan_sha256']:raise ValueError('retained plan identity mismatch')
        plan=json.loads((root/'plan.json').read_text(encoding='utf-8'))
        actual=cidarbindings.verify(root,report['cidar']['declaration'],{row['path']:row['sha256'] for row in report['inputs']},contract,report['image'],plan['entry'],plan['target'])
        if actual!=report['cidar']:raise ValueError('retained CIDAR binding mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    runner = sub.add_parser('run'); runner.add_argument('--plan', type=Path, required=True); runner.add_argument('--exe', type=Path, required=True); runner.add_argument('--out', type=Path, required=True)
    comparison = sub.add_parser('compare'); comparison.add_argument('baseline', type=Path); comparison.add_argument('candidate', type=Path); comparison.add_argument('--out', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'run':
            report = run(json.loads(args.plan.read_text(encoding='utf-8')), args.exe, args.out)
        else:
            records = []
            for path in (args.baseline, args.candidate):
                record = json.loads(path.read_text(encoding='utf-8'))
                records.append(record)
            report = compare(*records)
            if args.out:
                args.out.write_text(encoded(report)+'\n', encoding='utf-8')
        print(encoded(report))
        return 0 if report['ok'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('experiment: '+str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
