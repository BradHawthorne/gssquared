"""Cycle/frame-stamped agent input recording and replay, with explicit lateness.

Plans bind loaded code by SHA256 and describe device edges, not arbitrary shell
commands. This replays agent inputs, not complete machine or host state.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from guestview import Memory
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug import CtrlClient

class MacroError(ValueError):pass

def integer(v,low,high):
    if type(v) is not int or not low<=v<=high:raise MacroError('integer outside declared range')
    return v

def action(row):
    op=row['op']
    if op=='keydown':return f'input keydown {integer(row["ascii"],1,127):02X} {integer(row.get("modifiers",0),0,255):02X}'
    if op=='keyup':return 'input keyup'
    if op=='mouse':
        endpoint=row['endpoint']
        if endpoint not in ('adb','card'):raise MacroError('unknown mouse endpoint')
        return f'input mouse {endpoint} {integer(row["dx"],-63,63)} {integer(row["dy"],-63,63)} {integer(row["buttons"],0,3)}'
    if op=='paddle':return f'paddle {integer(row["index"],0,3)} {integer(row["value"],0,255)}'
    if op=='pbutton':return f'pbutton {integer(row["index"],0,2)} {integer(row["down"],0,1)}'
    raise MacroError('unknown macro operation')

def validate(plan):
    if plan.get('schema')!='a2input-plan-v1' or plan.get('clock') not in ('cycles','frames'):raise MacroError('unsupported input plan schema/clock')
    integer(plan['initial_pc'],0,0xffffff);integer(plan['max_lateness'],0,1000000)
    bindings=plan['bindings'];rows=plan['actions']
    if not bindings or len(bindings)>64 or not rows or len(rows)>10000:raise MacroError('plan binding/action limit')
    for binding in bindings:
        start=integer(binding['start'],0,0xffffff);length=integer(binding['length'],1,65536)
        if start+length>0x1000000 or len(binding['sha256'])!=64 or any(c not in '0123456789abcdef' for c in binding['sha256']):raise MacroError('invalid memory binding')
    previous=-1;held=False
    for row in rows:
        at=integer(row['at'],0,1000000000000)
        if at<previous:raise MacroError('action timeline is not ordered')
        previous=at;action(row)
        if row['op']=='keydown':
            if held:raise MacroError('plan presses a second key before release')
            held=True
        if row['op']=='keyup':
            if not held:raise MacroError('plan releases no held key')
            held=False
        for expected in row.get('expect_memory',[]):
            start=integer(expected['start'],0,0xffffff);values=expected['bytes']
            if not values or len(values)>65536 or start+len(values)>0x1000000:raise MacroError('invalid expected memory span')
            for value in values:integer(value,0,255)
    if held:raise MacroError('plan must release its held key')

def read(client,start,length):
    return Memory(client.inspect('memory',f'{start:06X}',str(length))).read(start,length)

def run(client,plan,directory,expected=None):
    validate(plan);directory=Path(directory).resolve();directory.mkdir()
    (directory/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    receipts=[];complete=False;error='';initial=None;steps=0;held=False
    with (directory/'events.jsonl').open('x',encoding='utf-8') as journal:
        def emit(event,payload):journal.write(json.dumps({'schema':'a2input-journal-v1','event':event,**payload})+'\n');journal.flush()
        emit('started',{'clock':plan['clock']})
        try:
            initial=client.inspect('cpu')
            if initial['surface']!='ctrl' or initial['data']['registers']['pc']!=plan['initial_pc']:raise MacroError('initial CTRL surface/PC mismatch')
            for binding in plan['bindings']:
                actual=hashlib.sha256(read(client,binding['start'],binding['length'])).hexdigest()
                if actual!=binding['sha256']:raise MacroError('loaded code binding mismatch')
            origin=int(initial[plan['clock']]);state=initial
            for index,row in enumerate(plan['actions']):
                while int(state[plan['clock']])-origin<row['at']:
                    if steps>=1000000:raise MacroError('macro execution budget exhausted')
                    before=int(state[plan['clock']]);reply=client.command('step 1' if plan['clock']=='cycles' else 'run 1');steps+=1
                    if reply['status']!='OK':raise MacroError('guest stopped before scheduled input')
                    state=client.inspect('cpu')
                    if int(state[plan['clock']])<=before:raise MacroError('macro clock made no progress')
                actual=int(state[plan['clock']])-origin;late=actual-row['at']
                if late>plan['max_lateness']:raise MacroError(f'input {index} exceeds lateness bound: {late}')
                before_cycles=int(state['cycles']);reply=client.command(action(row))
                if row['op']=='keydown':held=True
                elif row['op']=='keyup':held=False
                after=client.inspect('cpu')
                if int(after['cycles'])!=before_cycles:raise MacroError('input unexpectedly advanced guest cycles')
                for assertion in row.get('expect_memory',[]):
                    if read(client,assertion['start'],len(assertion['bytes']))!=bytes(assertion['bytes']):raise MacroError('guest memory assertion failed')
                receipt={'index':index,'requested_offset':row['at'],'actual_offset':actual,'lateness':late,
                         'cycles':after['cycles'],'frames':after['frames'],'operation':row['op'],
                         'guest_consumption_proved':False,'reply':reply}
                receipts.append(receipt);emit('input',receipt);state=after
            if expected is not None:
                if expected.get('schema')!='a2input-record-v1' or not expected.get('pass') or expected.get('plan')!=plan:raise MacroError('invalid/mismatched prior input record')
                if [(r['operation'],r['actual_offset']) for r in expected['receipts']]!=[(r['operation'],r['actual_offset']) for r in receipts]:raise MacroError('input replay timestamps differ from recording')
            complete=True
        except (ValueError,OSError,RuntimeError,KeyError,TypeError) as exc:error=str(exc)
        finally:
            if held and client.pending is None:
                try:client.command('input keyup');emit('cleanup',{'owned_key_released':True})
                except (ValueError,OSError,RuntimeError) as exc:error+='; key cleanup failed: '+str(exc)
        report={'schema':'a2input-record-v1','model':'agent-input-timeline; instruction-boundary-cycles or video-frame-counter',
                'complete_machine_replay':False,'pass':complete and not error,'complete':complete,'error':error,'plan':plan,
                'plan_sha256':hashlib.sha256(json.dumps(plan,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
                'initial':initial,'receipts':receipts,'execution_calls':steps}
        emit('finished',{'pass':report['pass'],'complete':complete,'error':error})
    (directory/'record.json').write_text(json.dumps(report,indent=2)+'\n');return report

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ctrl',required=True,type=Path);s=p.add_mutually_exclusive_group(required=True);s.add_argument('--plan',type=Path);s.add_argument('--replay',type=Path);p.add_argument('--out',required=True,type=Path);args=p.parse_args()
    try:
        previous=json.loads(args.replay.read_text()) if args.replay else None;plan=previous['plan'] if previous else json.loads(args.plan.read_text())
        with CtrlClient(args.ctrl) as client:report=run(client,plan,args.out,previous)
        print(json.dumps({'pass':report['pass'],'error':report['error'],'record':str(args.out/'record.json')}));return 0 if report['pass'] else 1
    except (ValueError,OSError,RuntimeError,KeyError,TypeError) as exc:print('input macro: FAIL '+str(exc),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
