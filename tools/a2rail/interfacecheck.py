"""Native interface checks; run with python -I and --exe, no prose-derived values."""
import argparse
import json
import subprocess
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session, check
from guestview import Memory, ViewError
from gs2debug import CtrlClient

def run(exe):
    for platform in (3,5):
        session=Session(exe,platform)
        try:
            target=session.dir/'typed state.json'
            limitations=session.dir/'limitations.tsv'
            session.cmd(f'limitations {limitations}')
            known=sum(line.startswith('limitation\t') for line in limitations.read_text().splitlines())
            check(f'known={known} ' in session.cmd('limitations'),'limitation count agrees with its own exported table')
            session.cmd('setreg a 34')
            session.cmd(f'inspect cpu "{target}"')
            state=json.loads(target.read_text())
            check(state['schema']=='a2inspect-v1' and state['data']['registers']['a']==0x34,'native CPU register value')
            cpu=json.loads(session.cmd('json cpu',ok=False))
            check(cpu['ok'] and cpu['data']['state']['registers']['a']==0x34,'native inline CPU payload')
            check(state['data']['register_widths']['a'] in (8,16),'declared CPU width')
            before=session.cmd('cpu')
            snap_before=session.dir/'before.snap';snap_after=session.dir/'after.snap'
            session.cmd(f'save {snap_before}')
            for kind in ('devices','signals','capabilities'):
                session.cmd(f'inspect {kind} "{target}"')
                state=json.loads(target.read_text())
                check(state['kind']==kind and state['model']=='internal-state-observation',kind+' typed artifact')
                if kind=='devices' and platform==5:
                    devices={d['name']:d for d in state['data']['devices']}
                    check(all(d['typed_available'] for d in devices.values()),'all default IIgs handlers have native observers')
                    check(all(devices[n]['typed_available'] for n in ('rtc','scc8530','adb')),'native RTC/SCC/ADB fields available')
                    fields={f['name']:f for f in devices['scc8530']['fields']}
                    check(fields['a.rx_count']['value']==0 and isinstance(fields['a.cts']['value'],bool),'SCC FIFO count and line types')
            session.cmd(f'save {snap_after}')
            diff=subprocess.run([str(exe.parent/'snapdiff.exe'),str(snap_before),str(snap_after)],capture_output=True)
            check(diff.returncode==0,'inspection preserves saved CPU/MMU/device state')
            check(session.cmd('cpu')==before,'inspection preserves CPU')
            session.cmd(f'inspect signals "{target}" definitely-no-such-signal')
            state=json.loads(target.read_text())
            check(state['data']['signals']==[],'unmatched signal selection explicit empty list')
            check('status=FAIL' in session.cmd(f'inspect unknown "{target}"',ok=False),'unknown inspection refuses')
            check('status=FAIL' in session.cmd('inspect cpu',ok=False),'missing output refuses')
            session.code([0xea,0xea])
            executed=json.loads(session.cmd('json step 1',ok=False))
            check(executed['data']['instructions']=='1' and int(executed['data']['cycles'])>0,'native in-command execution counts')
            session.cmd(f'inspect memory "{target}" 000800 2')
            memory=Memory(json.loads(target.read_text()))
            actual=memory.read(0x800,2)
            check(actual==bytes([0xea,0xea]),f'native memory feeds guest views: platform={platform} bytes={actual.hex()}')
            session.cmd(f'inspect memory "{target}" 00C000 1')
            memory=Memory(json.loads(target.read_text()))
            try:memory.read(0xc000,1)
            except ViewError:check(True,'unreadable I/O refuses guest interpretation')
            else:raise AssertionError('unreadable memory was accepted')
            oracle=session.cmd('oracle',ok=False)
            check('excluded (rtc, clock' not in oracle,'oracle has no stale RTC exclusion')
            check(json.loads(session.cmd('json oracle',ok=False))['ok'],'JSON oracle has an explicit successful status')
            device_stream=session.dir/'device-events.jsonl'
            session.cmd(f'subscribe on device "{device_stream}"')
            session.cmd('paddle 0 73');session.cmd('subscribe off')
            records=[json.loads(line) for line in device_stream.read_text().splitlines()]
            changes=[r['payload']['data'] for r in records if r['event']=='subscription.event']
            check(any(r['event']=='device.baseline' for r in changes),'device subscription begins with native baseline')
            check(any(r['event']=='device.change' and r['field'].endswith('.paddles') and r['after']['value'][0]==73 for r in changes),'native device field change is published at command boundary')
            stream=session.dir/'events.jsonl'
            session.cmd(f'subscribe on command "{stream}" 10')
            session.cmd('cpu')
            session.cmd('subscribe off')
            events=[json.loads(line) for line in stream.read_text().splitlines()]
            check(events[0]['event']=='subscription.started' and events[-1]['event']=='subscription.stopped','subscription has lifecycle records')
            check([e['sequence'] for e in events]==list(range(len(events))),'event ordering is explicit')
            commands=[e['payload']['data']['command'] for e in events if e['event']=='subscription.event']
            check(commands[-1]=='cpu' and len(commands)==2,'command events exactly once')
            filtered=session.dir/'filtered.jsonl'
            session.cmd(f'subscribe on trap "{filtered}"')
            session.cmd('cpu');session.cmd('subscribe off')
            check(len(filtered.read_text().splitlines())==2,'unsubscribed commands not delivered')
            trapped=session.dir/'trapped.jsonl'
            session.code([0xea,0xea])
            session.cmd(f'subscribe on trap "{trapped}"')
            session.cmd('trap add name=typed_event at=800 once')
            session.cmd('step 1')
            session.cmd('subscribe off');session.cmd('trap clear typed_event')
            records=[json.loads(line) for line in trapped.read_text().splitlines()]
            records=[e for e in records if e['event']=='subscription.event']
            check(len(records)==1 and records[0]['payload']['data']['event']=='trap.entry','central trap record fans out exactly once')
            overflow=session.dir/'overflow.jsonl'
            session.cmd(f'subscribe on command "{overflow}" 1')
            session.cmd('cpu')
            status=json.loads(session.cmd('json subscribe status',ok=False))
            check(int(status['data']['dropped'])>=1,'subscription loss is visible')
            session.cmd('subscribe off',ok=False)
            check(any(json.loads(line)['event']=='subscription.overflow' for line in overflow.read_text().splitlines()),'overflow emits terminal warning record')
            check('status=FAIL' in session.cmd(f'subscribe on imaginary "{stream}"',ok=False),'unknown event filter refuses')
            try:
                with CtrlClient(session.dir) as client:
                    state=client.cpu()
                    check(isinstance(state['registers']['pc'],int),'public file client reads native CPU state')
                    captured=client.inspect('memory','000800','2')
                    check(Memory(captured).read(0x800,2)==bytes([0xea,0xea]),'public file client consumes native memory artifact')
            finally:
                session.seq=max(int(p.name.split('.')[1]) for p in session.dir.glob('cmd.*') if p.name.split('.')[1].isdigit())
        finally:
            session.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True,type=Path)
    run(parser.parse_args().exe.resolve())
