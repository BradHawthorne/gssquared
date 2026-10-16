"""Socket subscriptions: native stop records, filtering, bounds and reconnect."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import environment,check
from gs2debug import Client,ProtocolError
from gs2debug.types import SUBSCRIBE,EVT_AGENT_RECORD,MEM_MAIN,REG_PC,REG_PB,REG_P,REG_E

def run(exe):
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2socketstream-') as tmp:
            root=Path(tmp);socket=str(root/'debug.sock')
            with (root/'session.log').open('wb') as log:
                process=subprocess.Popen([str(exe),'--debug',socket,'-p',str(platform),'-n','--no-quit-confirm'],cwd=root,env=environment(),stdout=log,stderr=subprocess.STDOUT)
                client=Client()
                try:
                    deadline=time.monotonic()+20
                    while True:
                        try:client.connect(socket);break
                        except OSError:
                            if time.monotonic()>deadline or process.poll() is not None:raise AssertionError('socket did not start: '+(root/'session.log').read_text(errors='replace'))
                            time.sleep(.05)
                    client.hello()
                    manifest=client.manifest()
                    check({'SUBSCRIBE','MANIFEST'} <= {row['name'] for row in manifest['types']},'socket discovery names subscription requests')
                    check({'audio','device','irq','stop','all'} <= set(manifest['subscription']['kinds']),'socket discovery lists native filter kinds')
                    client.pause();client.write_mem(MEM_MAIN,0x800,b'\xea\xea\x4c\x00\x08')
                    client.set_regs(mask=REG_PC|REG_PB|REG_P|(REG_E if platform==5 else 0),pc=0x800,pb=0,p=0x34,e=1 if platform==5 else 0)
                    records=[]
                    def event(kind,sequence,data):
                        if kind==EVT_AGENT_RECORD:records.append(json.loads(data.decode()))
                    client.on_event(event)
                    status=client.subscribe(('stop',));check(status['active'] and status['complete'],'socket native subscription starts')
                    try:client.subscribe(('stop',))
                    except ProtocolError:check(True,'duplicate socket arm refuses')
                    else:raise AssertionError('duplicate socket arm accepted')
                    client.step_into(1);client.wait_stopped(timeout=10);status=client.unsubscribe()
                    check(not status['active'] and status['complete'] and int(status['retained'])==len(records) and bool(records),'socket close-out agrees with delivered record count')
                    check([r['sequence'] for r in records]==list(range(len(records))),'socket event sequence ordered exactly once')
                    check(all(r['schema']=='a2subscription-v1' and r['payload']['kind']=='stop' for r in records),'socket records retain native schema/filter')
                    records.clear();client.subscribe(('trap',));client.pause();status=client.unsubscribe()
                    check(not records and status['retained']=='0','unselected stop records absent')
                    try:client.subscribe(('imaginary',))
                    except ProtocolError:check(True,'unknown socket event kind refuses')
                    else:raise AssertionError('unknown kind accepted')
                    for payload in (b'',struct.pack('<II',0,1),struct.pack('<II',1,0)+b'all'):
                        try:client.request(SUBSCRIBE,payload)
                        except ProtocolError:pass
                        else:raise AssertionError('invalid subscription frame accepted')
                    check(True,'malformed socket subscription frames refuse')
                    client.subscribe(('stop',),queue_cap=1);client.step_into(1);client.wait_stopped(timeout=10);status=client.unsubscribe()
                    check(not status['complete'] and int(status['dropped'])>0,'bounded socket queue reports lost records')
                    client.subscribe(('stop',));client.close()
                    client=Client();client.connect(socket);client.hello();check(not client.subscription_status()['active'],'disconnect disarms prior subscription')
                    client.subscribe(('trap',));client.unsubscribe();client.quit();process.wait(timeout=10)
                    check(process.returncode==0,'socket stream session exits cleanly')
                finally:
                    client.close()
                    if process.poll() is None:process.kill();process.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True,type=Path);run(parser.parse_args().exe.resolve())
