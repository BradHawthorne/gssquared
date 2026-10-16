"""Live capture bindings: independent bytes, ownership errors and recovery."""
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
            out=s.dir/'live console.bin'
            if platform==3:
                for source in ('scc-a','scc-b','wdm'):
                    check('status=FAIL' in s.cmd(f'capture {source} on "file:{out}"',ok=False),'unsupported source refuses '+source)
                continue
            check('status=FAIL' in s.cmd('capture wdm off',ok=False),'unarmed off refuses no-op')
            check('status=FAIL' in s.cmd(f'capture wdm on "file:{s.dir}/absent/x"',ok=False),'bad live path fails without ending emulator')
            s.cmd(f'capture wdm on "file:{out}"')
            check('status=FAIL' in s.cmd(f'capture scc-a on "file:{out}"',ok=False),'duplicate sink refuses without truncation')
            s.code([0xa9,0x5a,0x42,0xfe,0xea]);s.cmd('step 2')
            check(out.read_bytes()==b'Z','live WDM byte capture exact')
            status=json.loads(s.cmd('json capture wdm status',ok=False))['data']
            check(status['enabled'] and status['bytes']=='1','native live sink readback')
            check('status=FAIL' in s.cmd(f'capture wdm on "file:{s.dir}/other"',ok=False),'armed capture refuses implicit replacement')
            s.cmd('setreg pc 000802');s.cmd('step 1')
            check(out.read_bytes()==b'ZZ','failed rebind preserves old capture')
            final=json.loads(s.cmd('json capture wdm off',ok=False))['data'];check(final['closed_bytes']=='2' and not final['enabled'],'off receipt preserves final byte count')
            s.cmd('setreg pc 000802');s.cmd('step 1')
            check(out.read_bytes()==b'ZZ','off releases WDM handlers')
            s.cmd(f'capture scc-a on "file:{out}"');s.cmd('capture scc-a off')
            check(out.read_bytes()==b'','off releases endpoint for another source')
        finally:s.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
