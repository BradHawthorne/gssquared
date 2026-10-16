"""An MCP-only build-input/run/assert/capture/evidence loop, plus SDK recovery."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import environment,check
from gs2debug import Client,RequestPending
from outputassert import png

class Wire:
    def __init__(self,exe,platform,root):
        self.root=root;self.sequence=0;self.responses=queue.Queue();self.transcript=[]
        self.stderr=(root/'mcp-stderr.log').open('wb')
        self.process=subprocess.Popen([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--platform',str(platform),'--out-dir',str(root/'session')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr,env=environment())
        def read():
            try:
                for line in self.process.stdout:self.responses.put(json.loads(line))
            except BaseException as error:self.responses.put(error)
        self.reader=threading.Thread(target=read,daemon=True);self.reader.start()
    def rpc(self,method,params=None,notification=False):
        self.sequence+=1
        message={'jsonrpc':'2.0','method':method,**({'params':params} if params is not None else {})}
        if not notification:message['id']=self.sequence
        self.process.stdin.write((json.dumps(message)+'\n').encode());self.process.stdin.flush()
        if notification:return
        result=self.responses.get(timeout=35)
        if isinstance(result,BaseException):raise result
        check(result.get('id')==self.sequence,'MCP response identity')
        self.transcript.append(result)
        if 'result' not in result:raise AssertionError('MCP request rejected: '+str(result))
        check(True,'MCP request accepted')
        return result['result']
    def tool(self,name,args=None,ok=True):
        result=self.rpc('tools/call',{'name':'gs2_'+name,'arguments':args or {}})
        check(result['isError'] is not ok,'MCP tool verdict '+name)
        return result
    def native(self,name,args='',ok=True):return self.tool(name,{'arguments':args},ok)['structuredContent']
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=5);raise
        finally:self.stderr.close();self.reader.join(2)
        check(self.process.returncode==0,'MCP owned process closes cleanly')
        owner=json.loads((self.root/'session/mcp-session.json').read_text(encoding='utf-8'))
        check(owner['closed'] and owner['exit_code']==0,'native owned emulator closes cleanly')

def run(exe):
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2autonomy-') as temporary:
            root=Path(temporary);wire=Wire(exe,platform,root)
            try:
                wire.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'autonomous-loop-test','version':'1'}})
                wire.rpc('notifications/initialized',notification=True)
                tools=wire.rpc('tools/list')['tools'];names={row['name'] for row in tools}
                check(len(tools)==85 and {'gs2_artifact_list','gs2_artifact_read','gs2_artifact_put','gs2_context_read','gs2_context_track'}<=names,'all native/observation/artifact/context tools discoverable')
                catalog=wire.tool('agent_tools')['structuredContent']
                check(len(catalog['capabilities']['commands'])==74,'native inventory retained')
                program=bytes.fromhex('a9428d0009ea');checksum=hashlib.sha256(program).hexdigest()
                upload={'path':'program input.bin','base64':base64.b64encode(program).decode(),'sha256':checksum}
                uploaded=wire.tool('artifact_put',upload)['structuredContent']
                wire.tool('artifact_put',upload,ok=False)
                wire.tool('artifact_put',dict(upload,path='wrong.bin',sha256='0'*64),ok=False)
                wire.native('pause');wire.native('load','0800 "'+uploaded['path']+'"');wire.native('setreg','pc 000800')
                wire.native('load','0800 "'+uploaded['path']+'" extra',ok=False)
                wire.native('load','0800 "unclosed path',ok=False)
                wire.native('step','3')
                wire.native('assert','peek:000900==$42')
                wire.native('assert','peek:000900==$43',ok=False)
                wire.native('inspect','cpu "state.json"')
                state=wire.tool('artifact_read',{'path':'state.json','mode':'text'})['structuredContent']
                check(json.loads(state['text'])['data']['registers']['a']==66,'MCP consumes native typed state artifact without filesystem access')
                wire.native('pngc','proof.png 1 1 4dot')
                image=wire.tool('artifact_read',{'path':'proof.png'})
                media=[item for item in image['content'] if item['type']=='image']
                check(len(media)==1 and media[0]['mimeType']=='image/png','native PNG delivered as viewable MCP image content')
                pixels=base64.b64decode(media[0]['data']);proof=image['structuredContent']
                check(hashlib.sha256(pixels).hexdigest()==proof['sha256'] and proof['complete'],'image content is bound to returned digest and completeness')
                saved=root/'transport-proof.png';saved.write_bytes(pixels)
                decoded=png(saved);check((decoded['width'],decoded['height'])==(560,384),'transported 4dot image independently decodes to expected doubled-height raster')
                wire.tool('artifact_read',{'path':'proof.png','sha256':'0'*64},ok=False)
                wire.tool('artifact_read',{'path':'../outside'},ok=False)
                read=wire.tool('artifact_read',{'path':uploaded['path'],'mode':'base64','sha256':checksum})['structuredContent']
                check(base64.b64decode(read['base64'])==program,'uploaded input can be inspected byte-for-byte')
                batch=wire.tool('batch',{'frames':1,'rails':{'A2GSPU_LOAD':uploaded['path']+'@0800','A2GSPU_LOAD_BOOT':'1','A2GSPU_ASSERT':'peek:000800 == $A9'}})['structuredContent']
                result=wire.tool('artifact_read',{'path':str(Path(batch['artifact_directory'])/'batch-result.json'),'mode':'text'})['structuredContent']
                check(json.loads(result['text'])['ok'],'cold batch evidence is accessible through same artifact root')
                listing=wire.tool('artifact_list',{})['structuredContent'];paths={item['path'] for item in listing['entries']}
                check('program input.bin' in paths and 'proof.png' in paths and 'wrong.bin' not in paths,'artifact discovery includes outputs and excludes rejected upload')
                receipt=wire.tool('artifact_read',{'path':'mcp-session.json','mode':'text'})['structuredContent']
                check(json.loads(receipt['text'])['executable_sha256']==hashlib.sha256(exe.read_bytes()).hexdigest(),'agent can retrieve exact emulator binary identity')
                wire.native('quit')
                retained=wire.tool('artifact_read',{'path':'proof.png','sha256':proof['sha256']})
                check(any(item['type']=='image' for item in retained['content']),'evidence remains readable after guest quit')
            finally:wire.close()
        recovery(exe,platform)

def recovery(exe,platform):
    with tempfile.TemporaryDirectory(prefix='a2recover-') as temporary:
        root=Path(temporary);endpoint=str(root/'debug.sock');env=environment();env.update(A2GSPU_AGENT='1',A2GSPU_PACE='realtime',A2GSPU_OUT_DIR=str(root))
        client=Client()
        with (root/'emulator.log').open('wb') as log:
            process=subprocess.Popen([str(exe),'--debug',endpoint,'-p',str(platform),'-n','--no-quit-confirm'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+20
                while True:
                    try:client.connect(endpoint);break
                    except OSError:
                        if process.poll() is not None or time.monotonic()>deadline:raise
                        time.sleep(.02)
                client.hello();client.agent_command('poke 0800 4C 00 08');client.agent_command('setreg pc 0800')
                try:client.agent_command('run 12',timeout=.005)
                except RequestPending:pass
                else:raise AssertionError('real timed command did not leave a recoverable request')
                next_sequence=client._next_seq
                try:client.agent_command('setreg a 99')
                except RequestPending:pass
                else:raise AssertionError('second mutation was published while first outcome was unknown')
                check(client._next_seq==next_sequence,'blocked mutation allocates no new wire sequence')
                result=client.wait_agent_command(timeout=5)
                check(result['ok'] and result['command']=='run 12' and int(result['data']['cycles'])>0,'real timed-out run recovers original native evidence')
                check(client.pending_request is None and client.agent_command('cpu')['data']['state']['registers']['pc']==0x800,'recovery leaves paused machine usable')
                client.quit();process.wait(timeout=10);check(process.returncode==0,'recovered session exits normally')
            finally:
                client.close()
                if process.poll() is None:process.kill();process.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);run(parser.parse_args().exe.resolve())
