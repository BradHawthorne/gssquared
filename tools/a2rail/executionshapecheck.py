"""Execution requests validate before effects and distinguish goal from budget."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check

def run(exe):
    for platform in (3,5):
        session=Session(exe,platform)
        try:
            session.cmd('poke 0800 E6 10 EA EA');session.cmd('poke 0010 00');session.cmd('setreg pc 0800')
            before=json.loads(session.cmd('json cpu',ok=False))['data']['state']
            for command in ('step 0','step -1','step 1junk','step 1 extra','run 0','run -1','run 1junk','run 1 extra','run-until 0800 0','run-until 0800 -1','run-until 0800 1junk','run-until 0800 1 extra'):
                result=json.loads(session.cmd('json '+command,ok=False))
                check(not result['ok'] and result['data']['instructions']=='0' and result['data']['cycles']=='0','invalid execution request has no work: '+command)
                check(json.loads(session.cmd('json cpu',ok=False))['data']['state']==before,'invalid execution preserves CPU: '+command)
            reached=json.loads(session.cmd('json run-until 0800 10',ok=False))
            check(reached['ok'] and reached['data']['target_reached'] and reached['data']['instructions']=='0' and reached['data']['cycles']=='0','already-satisfied PC goal does not execute another instruction')
            check(json.loads(session.cmd('json assert peek:0010==$00',ok=False))['ok'],'already-satisfied goal preserves the unexecuted memory effect')
            reached=json.loads(session.cmd('json run-until 0802 1',ok=False))
            check(reached['ok'] and reached['data']['target_reached'] and reached['data']['instructions']=='1','PC goal reached on final instruction is success')
            check(json.loads(session.cmd('json assert peek:0010==$01',ok=False))['ok'],'target execution produces exactly one known memory effect')
            budget=json.loads(session.cmd('json run-until 0900 1',ok=False))
            check(not budget['ok'] and budget['status']=='BUDGET' and not budget['data']['target_reached'] and budget['data']['stop_reason']=='budget_exhausted','budget exhaustion is not goal completion')
            session.cmd('setreg pc 0802')
            step=json.loads(session.cmd('json step 1',ok=False))
            check(step['ok'] and step['data']['instructions']=='1' and step['data']['completed'],'valid step completes its explicit instruction goal')
        finally:session.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);run(parser.parse_args().exe.resolve())
