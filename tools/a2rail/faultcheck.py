"""Boundary interrupt entries: stack, vectors, timing, priority and refusals."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check

def run(exe):
    for platform in (3,5):
        s=Session(exe,platform)
        try:
            for native in ((False,True) if platform==5 else (False,)):
                for kind in (('nmi','abort') if platform==5 else ('nmi',)):
                    s.code([0xea]*8);s.cmd('setreg sp 01FF')
                    if platform==5:s.cmd('setreg e '+('0' if native else '1'))
                    s.cmd('setreg p 3C') # I=1, D=1: these must not mask NMI/ABORT.
                    vector=(0xffe8 if kind=='abort' else 0xffea) if native else (0xfff8 if kind=='abort' else 0xfffa)
                    artifact=s.dir/'memory.json';s.cmd(f'inspect memory {artifact} {vector:06X} 2')
                    data=json.loads(artifact.read_text())['data']['regions'][0]['bytes'];expected_pc=data[0]|data[1]<<8
                    events=s.dir/'faults.jsonl';s.cmd(f'subscribe on fault {events}')
                    s.cmd(f'fault schedule 0 {kind}')
                    check('status=FAIL' in s.cmd(f'save {s.dir}/fault.snap',ok=False),'active fault state refuses misleading snapshot')
                    check('stage=preflight' in s.cmd(f'restore {s.dir}/missing.snap',ok=False),'restore names injector preflight refusal')
                    reply=json.loads(s.cmd('json step 1',ok=False));state=reply['data']['cpu_after']
                    check(not reply['ok'] and reply['status']=='BUDGET' and not reply['data']['completed'] and reply['data']['instructions']=='0',f'{kind} entry is not counted as a completed opcode step')
                    check(int(reply['data']['cycles'])==(8 if native else 7),f'{kind} entry cycles native={native}')
                    check(state['registers']['pc']==expected_pc and state['registers']['sp']==(0x1fb if native else 0x1fc),f'interrupt vector and stack width: expected PC={expected_pc:x} state={state}')
                    check(state['registers']['p']&12==4,'interrupt clears decimal, leaves I set')
                    s.cmd(f'inspect memory {artifact} {state["registers"]["sp"]+1:06X} {4 if native else 3}')
                    stack=json.loads(artifact.read_text())['data']['regions'][0]['bytes']
                    check(stack[1:3]==[0,8] and (native or stack[0]&0x30==0x20),'stack return address and emulation B bit')
                    s.cmd('subscribe off')
                    records=[json.loads(line) for line in events.read_text().splitlines()]
                    check(any(e.get('payload',{}).get('data',{}).get('lateness_cycles')=='0' for e in records),'fault delivery has actual timestamp/lateness')
            s.code([0xea]*8)
            if platform==5:s.cmd('setreg e 1')
            s.cmd('fault schedule 3 nmi');s.cmd('step 3',ok=False)
            status=json.loads(s.cmd('json fault status',ok=False))['data']
            check(int(status['last_actual'])-int(status['last_due'])==1,'non-boundary deadline reports one-cycle lateness')
            if platform==3:check('status=FAIL' in s.cmd('fault schedule 0 abort',ok=False),'IIe refuses ABORT')
            s.cmd('fault schedule 0 irq-on');s.cmd('fault schedule 100 irq-off')
            s.cmd('step 1');status=json.loads(s.cmd('json fault status',ok=False))['data']
            check(status['irq_held'] and status['queue'],'independent IRQ line asserted with release queued')
            s.cmd('fault clear');status=json.loads(s.cmd('json fault status',ok=False))['data']
            check(not status['irq_held'] and not status['queue'],'clear releases only injector state')
            check('status=FAIL' in s.cmd('fault clear',ok=False),'clear refuses no-op')
        finally:s.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
