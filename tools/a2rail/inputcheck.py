"""Device-edge and macro replay checks, including lateness and stale bytes."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check
from gs2debug import CtrlClient
import inputmacro as im

def run(exe):
    for platform in (3,5):
        s=Session(exe,platform)
        try:
            program=bytes([0xea,0xea,0x4c,0,8]);s.code(program)
            if platform==5:s.cmd('setreg e 1')
            s.cmd('setreg p 34')
            with CtrlClient(s.dir) as client:
                plan={'schema':'a2input-plan-v1','clock':'cycles','initial_pc':0x800,'max_lateness':1,
                      'bindings':[{'start':0x800,'length':len(program),'sha256':hashlib.sha256(program).hexdigest()}],
                      'actions':[{'at':2,'op':'paddle','index':0,'value':73},{'at':6,'op':'pbutton','index':1,'down':1}]}
                first=im.run(client,plan,s.dir/'macroA');check(first['pass'] and first['receipts'][1]['lateness']==1,'cycle macro records actual boundary and lateness')
                client.command('setreg pc 000800');second=im.run(client,plan,s.dir/'macroB',first)
                check(second['pass'],'input macro replays matching relative timestamps')
                strict=copy.deepcopy(plan);strict['max_lateness']=0;client.command('setreg pc 000800')
                check(not im.run(client,strict,s.dir/'macroStrict')['pass'],'exact-cycle demand refuses an overshot boundary')
                client.command('setreg pc 000800');client.command('poke 000800 00')
                result=im.run(client,plan,s.dir/'macroStale');check(not result['pass'] and not result['receipts'],'stale loaded code refuses before any input')
                client.command('poke 000800 EA')
                frames=copy.deepcopy(plan);frames['clock']='frames';frames['max_lateness']=0;frames['actions']=[{'at':1,'op':'paddle','index':2,'value':99}]
                client.command('setreg pc 000800');check(im.run(client,frames,s.dir/'macroFrames')['pass'],'frame macro uses native video-frame clock')
                # Clear any old strobe by a real guest I/O read before the edge.
                s.seq=max(int(p.name.split('.')[1]) for p in s.dir.glob('cmd.*') if p.name.split('.')[1].isdigit())
                s.code([0xad,0x10,0xc0,0xea]);client.command('step 1')
                before=client.inspect('cpu');client.command('input keydown 41');after=client.inspect('cpu')
                check(before['cycles']==after['cycles'],'key edge advances no guest time')
                check(not client.command('input keydown 42',check=False)['ok'],'second owned key refuses overwrite')
                check(not client.command(f'save "{s.dir}/held-key.gsps"',check=False)['ok'],'held ownership cannot silently enter snapshot')
                client.command('input keyup');check(not client.command('input keyup',check=False)['ok'],'keyup refuses no-op')
                if platform==5:
                    client.command('input mouse adb 2 -3 1')
                    check(not client.command('input mouse adb 1 1 0',check=False)['ok'],'unread mouse packet refuses overwrite')
                    state=client.inspect('devices')['data']['devices'];adb=next(d for d in state if d['name']=='adb')
                    fields={f['name']:f['value'] for f in adb['fields']}
                    check(fields['mouse_data']==[0x7d,0x82] and fields['mouse_data_full'],'ADB packet exposes exact motion/button encoding')
                else:check(not client.command('input mouse adb 1 1 0',check=False)['ok'],'IIe refuses missing ADB endpoint')
        finally:
            commands=[int(p.name.split('.')[1]) for p in s.dir.glob('cmd.*') if p.name.split('.')[1].isdigit()]
            if commands:s.seq=max(commands)
            s.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
