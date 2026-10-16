"""Optional-card native observers and saved-state noninterference."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check

def run(exe):
    with tempfile.TemporaryDirectory(prefix='a2cardcoverage-') as tmp:
        for platform in (3,5):
            config=Path(tmp)/f'cards{platform}.gs2'
            cards={1:'uthernet2',2:'super_serial',4:'mockingboard',5:'mouse',7:'thunder_clock'}
            if platform==5:cards.pop(7);cards[3]='voc'
            config.write_text('gs2_version = 1\nid = "ad0087d9-8800-4dcb-a42c-c2b8d53df92a"\nname = "Native coverage fixture"\n'+
                f'platform = "{"apple2gs" if platform==5 else "apple2e_enhanced"}"\nclock = "ntsc"\nscanner = "{"apple2gs" if platform==5 else "apple2e"}"\nbuiltin = false\n'+
                ''.join(f'\n[[cards]]\nslot = {slot}\ncard = "{card}"\n' for slot,card in cards.items()))
            s=Session(exe,platform,configfile=config)
            try:
                before=s.dir/'before.gsps';after=s.dir/'after.gsps';target=s.dir/'devices.json'
                s.cmd(f'save {before}');s.cmd(f'inspect devices {target}')
                data=json.loads(target.read_text())['data'];devices={d['name']:d for d in data['devices']}
                required={'uthernet2','ssc','mockingboard','mouse'}|({'voc'} if platform==5 else {'thunderclock'})
                check(all(devices[n]['typed_available'] and devices[n]['fields'] for n in required),'optional cards expose native typed state')
                check(all('controls' in d and 'candidate_snapshot_blocks' in d for d in data['installed']),'every installed model declares control/snapshot coverage')
                s.cmd(f'save {after}');result=subprocess.run([str(exe.parent/'snapdiff.exe'),str(before),str(after)],capture_output=True)
                check(result.returncode==0,'optional native observers preserve saved machine state')
            finally:s.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
