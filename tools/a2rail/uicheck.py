"""Native presentation tools must have checked effects and explicit limits."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check,environment
from gs2debug import CtrlClient

def run(exe):
    for platform in (3,5):
        session=Session(exe,platform)
        try:
            with CtrlClient(session.dir) as client:
                initial=client.command('ui status')['data']
                check(initial['schema']=='a2ui-v1' and initial['model']=='native-presentation-and-clock-settings','native menu state available')
                check(not initial['complete_gui_automation'] and 'controls' in initial,'UI discovery declares scope and unavailable paths')
                cpu=client.cpu()
                changed=client.command('ui monitor green')['data']
                check(changed['state']['monitor']=='green' and changed['effect']=='host-presentation','monitor action has native readback')
                check(client.cpu()==cpu,'presentation setting advances no guest time/state')
                check(not client.command('ui monitor green')['data']['changed'],'unchanged presentation request reports changed=false')
                failed=client.command('ui monitor invented',check=False)
                check(not failed['ok'],'unsupported monitor value refuses')
                check(client.command('ui status')['data']['monitor']=='green','failed action preserves setting')
                client.command('ui monitor rgb')
                target='14.3' if client.command('ui status')['data']['clock']['effective']!='14.3' else '1.024'
                client.command('ui speed '+target)
                pending=client.command('ui status')['data']
                check(pending['clock']['requested']==target and pending['clock']['pending'],'speed request declares deferred application')
                session_path=session.dir/'loop.bin';session_path.write_bytes(bytes.fromhex('4c0008'))
                client.command(f'load 000800 {session_path}');client.command('setreg pc 000800');client.command('run 1')
                effective=client.command('ui status')['data']
                check(effective['clock']['effective']==target and not effective['clock']['pending'],'speed applies through normal frame transition')
                check(not client.command('ui speed '+target)['data']['changed'],'effective speed repeat does not schedule a fake change')
                if not effective['crt']['available']:
                    refused=client.command('ui crt on',check=False)
                    check(not refused['ok'] and 'unavailable' in refused['text'],'missing GPU shader fails loud')
                else:
                    client.command('ui crt on');check(client.command('ui status')['data']['crt']['enabled'],'available shader action has readback');client.command('ui crt off')
                check(not client.command('ui fullscreen on',check=False)['ok'],'unsupported host-only action refuses with scope')
                for command in ('ui monitor','ui speed 99','ui crt perhaps','ui status extra'):
                    check(not client.command(command,check=False)['ok'],'malformed UI request refuses: '+command)
        finally:
            pending=[int(path.name.split('.')[1]) for path in session.dir.glob('cmd.*') if path.name.split('.')[1].isdigit()]
            if pending:session.seq=max(pending)
            session.close()
    for platform in (3,5):
        import tempfile
        with tempfile.TemporaryDirectory(prefix='a2ui-copilot-') as temporary:
            root=Path(temporary);env=environment();env['A2GSPU_COPILOT']=str(root)
            with (root/'session.log').open('wb') as log:
                process=subprocess.Popen([str(exe),'-p',str(platform),'--no-quit-confirm'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
                try:
                    with CtrlClient(root,timeout=20) as client:
                        client.command('pause')
                        check(client.command('ui monitor green')['data']['state']['monitor']=='green','windowed co-pilot shares native UI setting handler')
                        check(not client.command('ui crt invented',check=False)['ok'],'co-pilot UI malformed value refuses')
                        check(not client.command('poke 0400 00',check=False)['ok'],'UI access preserves co-pilot splice restriction')
                        client.command('quit')
                    process.wait(timeout=10);check(process.returncode==0,'windowed UI tool session exits cleanly')
                finally:
                    if process.poll() is None:process.kill();process.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True,type=Path);run(parser.parse_args().exe.resolve())
