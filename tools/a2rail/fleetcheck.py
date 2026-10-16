"""Real 2x2 fleets: isolated cells, one red cell and bounded pending cleanup."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import fleet
from p0check import check


def run(exe,out=None,expect_discovery=False):
    temporary=tempfile.TemporaryDirectory(prefix='a2fleetcheck-') if out is None else None
    root=Path(temporary.name if temporary else out).resolve();root.mkdir(parents=True,exist_ok=True)
    try:
        jobs=[]
        for platform in (3,5):
            for seed in (1,2):
                value=platform*16+seed;image=root/f'p{platform}-s{seed}.bin';image.write_bytes(bytes([0xa9,value,0x8d,0,0x40,0xea]))
                jobs.append({'id':f'p{platform}-s{seed}','platform':platform,'seed':seed,'timeout_seconds':30,
                    'files':[{'source':str(image),'path':'program.bin','sha256':fleet.digest(image)}],
                    'commands':['load 000800 {input:program.bin}','setreg pc 800','step 2'],
                    'assertions':[f'peek:004000 == ${value:02X}']})
        plan={'schema':'a2fleet-plan-v1','workers':2,'jobs':jobs}
        passed=fleet.run(plan,exe,root/'pass')
        check(passed['ok'] and passed['counts']=={'pass':4},'2x2 platform/seed fleet reports all four passing cells')
        if expect_discovery:
            check(all(row['native_companion_discovery'] and row['native_companion_discovery']['plan_schema']=='a2fleet-plan-v1' and row['native_companion_discovery'].get('schema_option')=='--schema' for row in passed['cells']),'native capabilities discover the fleet companion, plan schema and schema option')
        check([row['id'] for row in passed['cells']]==[row['id'] for row in jobs],'aggregate order is plan order despite concurrent completion')
        receipts=[json.loads((Path(row['artifact_directory'])/'session/mcp-session.json').read_text()) for row in passed['cells']]
        check(len({row['pid'] for row in receipts})==4 and all(row['owned'] and row['closed'] and row['exit_code']==0 and row['read_only_media_launch'] for row in receipts),'distinct owned processes exit cleanly with media launch policy retained')
        check(all(row['event_capture']['complete'] and row['event_capture']['received']>0 and (Path(row['artifact_directory'])/'artifacts/events.jsonl').is_file() for row in passed['cells']),'each cell retains a closed, complete native event subset')
        intervals=[(int(row['native_ready_host_ns']),int(row['finished_host_ns'])) for row in passed['cells']]
        check(any(max(a,c)<min(b,d) for index,(a,b) in enumerate(intervals) for c,d in intervals[index+1:]),'at least two live native cell intervals overlap')
        for row in passed['cells']:
            path=Path(row['artifact_directory'])
            check((path/'result.json').is_file() and (path/'artifacts/context.json').is_file() and (path/'inputs/program.bin').is_file(),'cell retains staged inputs, context and verdict: '+row['id'])
        wrong=copy.deepcopy(plan);wrong['jobs'][2]['assertions']=['peek:004000 == $00']
        failed=fleet.run(wrong,exe,root/'one-red')
        check(not failed['ok'] and failed['counts']=={'assertion-failure':1,'pass':3},'one planted bad cell is the only red cell')
        check(next(row for row in failed['cells'] if not row['ok'])['id']==jobs[2]['id'],'red cell identity is exact')
        cli_plan=root/'one-red.plan.json';cli_plan.write_text(fleet.encoded(wrong)+'\n')
        cli=subprocess.run([sys.executable,'-I',str(Path(fleet.__file__)),'--plan',str(cli_plan),'--exe',str(exe),'--out',str(root/'one-red-cli')],capture_output=True,text=True,timeout=60)
        check(cli.returncode==1 and json.loads((root/'one-red-cli/fleet.json').read_text())['counts']=={'assertion-failure':1,'pass':3},'CLI returns 1 and retains every cell for a failed matrix')
        invalid=copy.deepcopy(plan);invalid['workers']=True;invalid_path=root/'invalid.plan.json';invalid_path.write_text(fleet.encoded(invalid)+'\n')
        cli=subprocess.run([sys.executable,'-I',str(Path(fleet.__file__)),'--plan',str(invalid_path),'--exe',str(exe),'--out',str(root/'invalid-cli')],capture_output=True,text=True,timeout=10)
        check(cli.returncode==2 and not (root/'invalid-cli').exists(),'CLI distinguishes invalid plans before creating output')
        witness=(root/'pass/fleet.json').read_bytes()
        try:fleet.run(plan,exe,root/'pass')
        except FileExistsError:check((root/'pass/fleet.json').read_bytes()==witness,'reused output refuses without overwriting evidence')
        else:raise AssertionError('existing fleet overwritten')
        bad=copy.deepcopy(plan);bad['jobs']=bad['jobs'][:1];bad['jobs'][0]['files'][0]['sha256']='0'*64
        mismatch=fleet.run(bad,exe,root/'bad-input')
        check(not mismatch['ok'] and mismatch['counts']=={'inconclusive':1} and not (root/'bad-input'/jobs[0]['id']/'session').exists(),'input identity failure is inconclusive before native launch')
        bad=copy.deepcopy(plan);bad['jobs']=bad['jobs'][:1];bad['jobs'][0]['timeout_seconds']=1
        bad['jobs'][0]['commands']=['poke 000800 4C 00 08','setreg pc 800','run 1000000']
        pending=fleet.run(bad,exe,root/'budget')
        row=pending['cells'][0]
        check(not row['ok'] and row['outcome']=='inconclusive' and row.get('effects_unknown') and row.get('exit_code') is not None,'native budget expiry preserves unknown effects and reaps owned child')
        owner=json.loads((root/'budget'/jobs[0]['id']/'session/mcp-session.json').read_text())
        check(owner['closed'] and owner['exit_code'] is not None,'pending-budget receipt proves no owned process is left running')
        config=Path(__file__).resolve().parents[2]/'assets/gs2/IIe_65816.gs2'
        custom=copy.deepcopy(plan);custom['jobs']=custom['jobs'][:1];job=custom['jobs'][0]
        job.pop('platform');job['config']='machine.gs2'
        job['event_kinds']=['stop']
        job['files'].append({'source':str(config),'path':'machine.gs2','sha256':fleet.digest(config)})
        configured=fleet.run(custom,exe,root/'configured')
        check(configured['ok'],'explicit pinned .gs2 configuration launches from a staged cell input')
        capped=copy.deepcopy(plan);capped['jobs']=capped['jobs'][:1];capped['jobs'][0]['event_queue_cap']=1
        lost=fleet.run(capped,exe,root/'event-loss')
        check(not lost['ok'] and lost['counts']=={'inconclusive':1} and not lost['cells'][0]['event_capture']['complete'],'native event loss cannot pass the aggregate')
        (root/'checks.json').write_text(fleet.encoded({'ok':True,'pass_counts':passed['counts'],'one_red_counts':failed['counts'],
            'bad_input_counts':mismatch['counts'],'budget_counts':pending['counts'],'model':'native matrices and intentional failure controls; no physical-hardware result'})+'\n')
    finally:
        if temporary:temporary.cleanup()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--out',type=Path);parser.add_argument('--expect-discovery',action='store_true')
    args=parser.parse_args();run(args.exe.resolve(),args.out,args.expect_discovery)
