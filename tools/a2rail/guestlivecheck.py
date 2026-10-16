"""Run configured kernel/source views against actual IIe and IIgs CTRL memory."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session, check
from guestviewcheck import GuestViews
import guestview
from gs2debug import CtrlClient

def run(exe):
    for platform in (3,5):
        fixture=GuestViews();fixture.setUp()
        session=Session(exe,platform)
        try:
            session.code(fixture.data,address=0)
            session.cmd('setreg pc 800')
            with CtrlClient(session.dir) as client:
                memory=guestview.LiveMemory(client)
                check(guestview.dmesg(memory,fixture.layout,{})['text']=='hello\n','live configured guest log')
                check([t['pid'] for t in guestview.tasks(memory,fixture.layout,{})['tasks']]==[1,7],'live configured task list')
                check([f['resume_pc'] for f in guestview.stack(memory,fixture.layout,{})['frames']]==[0x803,0x801],'live configured frame chain')
                check(guestview.panic(memory,fixture.layout,{},client.command('cpu'))['matched'],'live configured panic entry')
                source=guestview.source_lookup(fixture.source(),0x803,memory)
                check(source['binding_verified'] and source['locations'][0]['line']==8,'live source binding and line lookup')
                check(memory.finish()['surface']=='ctrl','live observation epoch stable')
                client.command('poke 000160 60 01 00')
                try:guestview.tasks(guestview.LiveMemory(client),fixture.layout,{})
                except guestview.ViewError:check(True,'live corrupt task cycle refuses interpretation')
                else:raise AssertionError('corrupt live task list accepted')
                client.command('poke 000160 00 01 00')
            layout=session.dir/'layout.json';layout.write_text(json.dumps(fixture.layout))
            output=session.dir/'tasks.json'
            result=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('guestview.py')),'tasks',
                '--ctrl',str(session.dir),'--layout',str(layout),'--out',str(output)],capture_output=True)
            check(result.returncode==0 and len(json.loads(output.read_text())['tasks'])==2,
                'public guestview CLI reads live CTRL memory: '+result.stderr.decode(errors='replace'))
        finally:
            commands=[int(p.name.split('.')[1]) for p in session.dir.glob('cmd.*') if p.name.split('.')[1].isdigit()]
            session.seq=max(commands,default=0)
            session.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True,type=Path)
    run(parser.parse_args().exe.resolve())
