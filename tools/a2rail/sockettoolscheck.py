"""The socket must reach the same instrument handlers as the file rail."""
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

TOOLS=8
COMMAND=9

def run(exe):
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2sockettools-') as temporary:
            root=Path(temporary);endpoint=str(root/'debug.sock');client=Client()
            with (root/'session.log').open('wb') as log:
                process=subprocess.Popen([str(exe),'--debug',endpoint,'-p',str(platform),'-n','--no-quit-confirm'],cwd=root,env=environment(),stdout=log,stderr=subprocess.STDOUT)
                try:
                    deadline=time.monotonic()+20
                    while True:
                        try:client.connect(endpoint);break
                        except OSError:
                            if process.poll() is not None or time.monotonic()>deadline:raise AssertionError('socket startup failed')
                            time.sleep(.02)
                    client.hello();client.pause()
                    # Raw request pins the missing feature independently of SDK sugar.
                    catalog=json.loads(client.request(TOOLS,b'').decode('utf-8'))
                    check(catalog['schema']=='a2agent-tools-v1','native socket tool catalog available')
                    commands=catalog['capabilities']['commands'];names={row['name'] for row in commands}
                    check(len(commands)==74 and {'inspect','capture','fault','input','save','restore','shr','rail','subscribe','ui','context'} <= names,'socket discovers every existing instrument')
                    check(catalog['control']['requires_paused'] and catalog['control']['quit_request']=='QUIT','catalog states ownership/pause/quit contract')
                    check(client.agent_tools()==catalog,'public SDK returns native tool catalog')
                    check(client.agent_command('ui status')['data']['schema']=='a2ui-v1','presentation tools accessible from socket')
                    before=client.agent_command('cpu')['data']['state']
                    check(before['schema']=='a2cpu-state-v1' if 'schema' in before else 'registers' in before,'CPU state native through shared handler')
                    bad=client.agent_command('made-up-tool',check=False)
                    check(not bad['ok'] and 'unknown' in bad['text'],'unknown tool refuses')
                    try:client.agent_command('made-up-tool')
                    except ProtocolError:pass
                    else:raise AssertionError('SDK swallowed a negative tool verdict')
                    client.agent_command('poke 000800 A9 42 EA EA')
                    client.agent_command('setreg pc 000800')
                    stepped=client.agent_command('step 1')['data']
                    check(stepped['instructions']=='1' and stepped['cpu_after']['registers']['a']==66,'shared execution returns independent native result')
                    path=root/'native memory with spaces.json'
                    client.agent_command(f'inspect memory "{path}" 000800 4')
                    memory=json.loads(path.read_text(encoding='utf-8'))
                    check(memory['data']['regions'][0]['bytes']==[0xA9,0x42,0xEA,0xEA],'shared artifact tool preserves quoted paths')
                    client.agent_command('paddle 0 73')
                    state=root/'devices.json';client.agent_command(f'inspect devices {state}')
                    check('73' in state.read_text(encoding='utf-8'),'socket controls expose live device readback')
                    client.agent_command('fault schedule 1 nmi');client.agent_command('fault clear')
                    snapshot=root/'checkpoint with spaces.bin'
                    client.agent_command(f'save "{snapshot}"')
                    client.agent_command('setreg a 0')
                    client.agent_command(f'restore "{snapshot}"')
                    check(client.agent_command('cpu')['data']['state']['registers']['a']==66,'socket snapshot tools restore native state')
                    for payload in (b'',b'cpu\0',b'cpu\nreset',b'\xff',b'x'*2048):
                        try:client.request(COMMAND,payload)
                        except ProtocolError:pass
                        else:raise AssertionError('malformed agent command accepted: '+repr(payload[:10]))
                    check(True,'malformed commands refuse before dispatch')
                    check(not client.agent_command('quit',check=False)['ok'],'quit uses acknowledged native shutdown path')
                    client.continue_()
                    denied=client.agent_command('poke 000800 00',check=False)
                    check(not denied['ok'] and 'paused' in denied['text'],'running machine refuses invasive tool takeover')
                    client.agent_command('pause');client.quit();process.wait(timeout=10)
                    check(process.returncode==0,'socket tool session exits cleanly')
                finally:
                    client.close()
                    if process.poll() is None:process.kill();process.wait(timeout=5)
    long_command(exe)

def long_command(exe):
    with tempfile.TemporaryDirectory(prefix='a2socket-longtool-') as temporary:
        root=Path(temporary);endpoint=str(root/'debug.sock');client=Client();env=environment();env['A2GSPU_PACE']='realtime'
        with (root/'session.log').open('wb') as log:
            process=subprocess.Popen([str(exe),'--debug',endpoint,'-p','3','-n','--no-quit-confirm'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+20
                while True:
                    try:client.connect(endpoint);break
                    except OSError:
                        if time.monotonic()>deadline:raise
                        time.sleep(.02)
                client.hello();client.pause();client.agent_command('poke 000800 4C 00 08');client.agent_command('setreg pc 000800')
                start=time.monotonic();result=client.agent_command('run 360')
                check(time.monotonic()-start>5 and result['ok'] and int(result['data']['cycles'])>0,'completed long tool retains its result after bridge wait deadline')
                check(client.agent_command('cpu')['data']['state']['registers']['pc']==0x800,'long command leaves owned machine paused and inspectable')
                client.quit();process.wait(timeout=10)
            finally:
                client.close()
                if process.poll() is None:process.kill();process.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);run(parser.parse_args().exe.resolve())
