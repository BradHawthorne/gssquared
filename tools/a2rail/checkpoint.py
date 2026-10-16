"""Hash-bound saved state plus native semantic observations. No full replay claim.

capture --ctrl DIR --snapdiff EXE --out NEW_DIRECTORY
diff BUNDLE_A BUNDLE_B --snapdiff EXE [--out REPORT]
Branches/reverse are deliberately unavailable until the complete clock contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug import CtrlClient
from gs2debug.ctrl import quoted

class CheckpointError(ValueError):pass

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def inventory(tool,snapshot):
    result=subprocess.run([str(tool),'--describe',str(snapshot)],capture_output=True,text=True)
    if result.returncode:raise CheckpointError('snapshot inventory refused: '+result.stderr)
    data=json.loads(result.stdout)
    if data.get('schema')!='a2snapshot-inventory-v1':raise CheckpointError('snapshot inventory schema mismatch')
    return data

def capture(client,directory,tool):
    directory=Path(directory).resolve();directory.mkdir()
    first=client.inspect('cpu')
    if first['surface']!='ctrl':raise CheckpointError('compound capture requires paused CTRL surface')
    snapshot=directory/'state.gsps';client.command('save '+quoted(snapshot))
    observations={'cpu':first}
    for kind in ('devices','signals','capabilities'):observations[kind]=client.inspect(kind)
    last=client.inspect('cpu')
    if any(v['cycles']!=first['cycles'] or v['surface']!='ctrl' for v in (*observations.values(),last)) or first['data']!=last['data']:
        raise CheckpointError('capture crossed a guest execution epoch')
    files={}
    for name,data in observations.items():
        path=directory/(name+'.json');path.write_text(json.dumps(data,indent=2)+'\n');files[path.name]=digest(path)
    info=inventory(tool,snapshot);path=directory/'snapshot.json';path.write_text(json.dumps(info,indent=2)+'\n')
    files[path.name]=digest(path);files[snapshot.name]=digest(snapshot)
    sections={row['name']:row for row in info['sections']};handlers=observations['devices']['data']['devices'];matrix=[]
    for model in observations['devices']['data'].get('installed',[]):
        names=set(filter(None,model['native_observer_names'].split(',')))
        blocks=set(filter(None,model['candidate_snapshot_blocks'].split(',')))
        selected=[h for h in handlers if h['name'] in names]
        matrix.append({**model,'native_field_count':sum(len(h['fields']) for h in selected),
                       'native_available':any(h['typed_available'] for h in selected),
                       'snapshot_blocks':{name:(name=='mmu' or 'device.'+name in sections) for name in sorted(blocks)},
                       'missing_native_observer':not selected,'complete_device_semantics':False})
    bundle={'schema':'a2checkpoint-v1','model':'paused-saved-state-with-semantic-observations','cycles':first['cycles'],
            'complete_replay':False,'excluded':'snapshot.json manifest plus host state; see capabilities.json',
            'files':files,'snapshot':'state.gsps','device_coverage':matrix,'semantic_device_state':'partial native fields; opaque payloads remain identified'}
    (directory/'checkpoint.json').write_text(json.dumps(bundle,indent=2)+'\n')
    return bundle

def load(directory):
    directory=Path(directory).resolve();manifest=json.loads((directory/'checkpoint.json').read_text())
    if manifest.get('schema')!='a2checkpoint-v1':raise CheckpointError('unsupported checkpoint schema')
    required={'state.gsps','snapshot.json','cpu.json','devices.json','signals.json','capabilities.json'}
    if set(manifest.get('files',{}))!=required:raise CheckpointError('checkpoint file inventory mismatch')
    for name,expected in manifest['files'].items():
        if digest(directory/name)!=expected:raise CheckpointError('checkpoint digest mismatch: '+name)
    data={name:json.loads((directory/(name+'.json')).read_text()) for name in ('cpu','devices','signals','capabilities')}
    for name,value in data.items():
        if value.get('schema')!='a2inspect-v1' or value.get('kind')!=name or value.get('cycles')!=manifest['cycles'] or value.get('surface')!='ctrl':
            raise CheckpointError('checkpoint observation epoch/schema mismatch')
    return manifest,data

def fields(data):
    result={}
    for device in data['devices']['data']['devices']:
        for field in device['fields']:
            key=f"{device['name']}[{device['instance']}].{field['name']}"
            if key in result:raise CheckpointError('duplicate native field identity')
            result[key]={k:field[k] for k in ('type','unit','encoding','value')}
    return result

def compare(left,right,tool):
    lm,ld=load(left);rm,rd=load(right)
    # Reparse saved state even when a maliciously rewritten bundle hash agrees.
    if inventory(tool,Path(left)/'state.gsps')['layout']!=inventory(tool,Path(right)/'state.gsps')['layout']:
        raise CheckpointError('checkpoint machine layouts differ')
    result=subprocess.run([str(tool),str(Path(left)/'state.gsps'),str(Path(right)/'state.gsps')],capture_output=True,text=True)
    if result.returncode not in (0,1):raise CheckpointError('saved-state comparison refused: '+result.stderr)
    saved=json.loads(result.stdout);a,b=fields(ld),fields(rd);changes=[]
    for name in sorted(a.keys()|b.keys()):
        if a.get(name)!=b.get(name):changes.append({'field':name,'before':a.get(name),'after':b.get(name)})
    comparable=ld['devices']['data'].get('installed')==rd['devices']['data'].get('installed')
    return {'schema':'a2checkpoint-diff-v1','model':'saved-state-and-partial-native-fields','saved_state':saved,
            'device_fields':changes,'installed_models_equal':comparable,'cpu_equal':ld['cpu']['data']==rd['cpu']['data'],
            'equal':saved['equal'] and not changes and comparable and ld['cpu']['data']==rd['cpu']['data'],
            'complete_device_semantics':False,'complete_replay':False,
            'uninterpreted_payloads':[s for s in inventory(tool,Path(left)/'state.gsps')['sections'] if s['model']=='opaque-device-payload']}

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    c=sub.add_parser('capture');c.add_argument('--ctrl',required=True,type=Path);c.add_argument('--snapdiff',required=True,type=Path);c.add_argument('--out',required=True,type=Path)
    d=sub.add_parser('diff');d.add_argument('left',type=Path);d.add_argument('right',type=Path);d.add_argument('--snapdiff',required=True,type=Path);d.add_argument('--out',type=Path)
    args=p.parse_args()
    try:
        if args.cmd=='capture':
            with CtrlClient(args.ctrl) as client:report=capture(client,args.out,args.snapdiff)
        else:report=compare(args.left,args.right,args.snapdiff)
        text=json.dumps(report,indent=2)+'\n'
        if args.cmd=='diff' and args.out:
            # Never overwrite evidence or an existing report.
            with args.out.open('x',encoding='utf-8') as out:out.write(text)
        else:print(text,end='')
        return 0 if args.cmd=='capture' or report['equal'] else 1
    except (ValueError,OSError,RuntimeError,KeyError,TypeError) as exc:
        print('checkpoint: FAIL '+str(exc),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
