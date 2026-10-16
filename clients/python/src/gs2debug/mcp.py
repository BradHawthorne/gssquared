"""Small stdio MCP tool server backed by the emulator's native tool registry.

No shell, expression evaluation, implicit pause, mutation retry or HTTP server.
An attached session remains alive on EOF; a launched session is owned and closed.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
import hashlib
from .client import Client
from .errors import ProtocolError
from .observations import TOOLS as OBSERVATION_TOOLS, TOOL_NAMES as OBSERVATION_NAMES, observe
from .artifacts import Artifacts, TOOLS as ARTIFACT_TOOLS, TOOL_NAMES as ARTIFACT_NAMES
from .context import TOOLS as CONTEXT_TOOLS, TOOL_NAMES as CONTEXT_NAMES, call as context_call

VERSION='2025-11-25'
MAX_LINE=1024*1024
RESERVED_RAILS={'A2GSPU_AGENT','A2GSPU_CTRL','A2GSPU_CTRL_ATOMIC','A2GSPU_CTRL_TIMEOUT','A2GSPU_COPILOT','A2GSPU_SPIKE_FRAMES','A2GSPU_BACKGROUND_WINDOW','A2GSPU_OUT_DIR'}
# These execute only in the one-shot tail, after interactive bootstrap returns.
# Expose them through gs2_batch instead of silently accepting them at launch.
BATCH_RAILS={'A2GSPU_CPUTEST','A2GSPU_MMUTEST','A2GSPU_SNAP_SAVE','A2GSPU_SNAP_LOAD','A2GSPU_RESTORE','A2GSPU_RUN_BIN','A2GSPU_RUN_BOOT','A2GSPU_LOAD','A2GSPU_LOAD_BOOT','A2GSPU_VRAM_LOAD','A2GSPU_VRAM_PAGE','A2GSPU_SPIKE_KEYS','A2GSPU_SPIKE_KEYS_AT','A2GSPU_VIDEOSUM','A2GSPU_VIDEOMAP','A2GSPU_TEXT40','A2GSPU_TEXTDUMP','A2GSPU_DUMP','A2GSPU_COVERAGE_OUT','A2GSPU_GOLDEN','A2GSPU_EXPECT_HASH','A2GSPU_EXPECT_RUN','A2GSPU_ASSERT','A2GSPU_STRICT','A2GSPU_HGR_PAGE','A2GSPU_DHGR_GOLDEN_PROFILE','A2GSPU_DHGR_PAGE','A2GSPU_DHGR_GOLDEN','A2GSPU_WRITEBACK','A2GSPU_NO_WRITEBACK','WIZ5_WRITEBACK','WIZ5_NO_WRITEBACK'}

def encoded(value):return json.dumps(value,ensure_ascii=False,allow_nan=False,separators=(',',':'))

class Backend:
    def __init__(self,args):
        self.args=args;self.client=Client();self.process=None;self.log=None;self.root=None;self.ended=False;self.catalog=None;self.rails={};self.startup_proof=None;self.artifacts=None;self.executable_sha256=None

    def receipt(self,closed=False):
        if self.root is not None:
            value={'schema':'a2mcp-session-v1','owned':True,'pid':self.process.pid if self.process else None,
                   'closed':closed and (self.process is None or self.process.poll() is not None),'exit_code':self.process.poll() if self.process else None,'executable':str(self.args.exe),'executable_sha256':self.executable_sha256,'identity_model':'SHA256 of selected executable bytes at launch; assets/media are not a complete replay capsule','requested_rails':self.rails,'startup_proof':self.startup_proof,'artifact_root':str(self.artifacts.root) if self.artifacts else None,
                   'read_only_media_launch':bool(getattr(self.args,'read_only_media',False))}
            (self.root/'mcp-session.json').write_text(encoded(value)+'\n',encoding='utf-8')

    def open(self):
        endpoint=self.args.socket
        for setting in self.args.rail:
            name,separator,value=setting.partition('=')
            if not separator or not name.startswith(('A2GSPU_','MEMVU_','WIZ5_')) or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789' for c in name) or '\0' in value or len(value.encode('utf-8'))>16384:raise ValueError('--rail requires NAME=value with a bounded instrumentation value')
            if name in RESERVED_RAILS:raise ValueError('rail conflicts with the managed socket/background/output lifecycle: '+name)
            if name in BATCH_RAILS:raise ValueError('one-shot rail requires gs2_batch or an explicit interactive command: '+name)
            if name in self.rails:raise ValueError('duplicate startup rail: '+name)
            self.rails[name]=value
        if getattr(self.args,'read_only_media',False) and self.rails.get('A2GSPU_RAMDISK','1')!='1':raise ValueError('startup rail contradicts --read-only-media: A2GSPU_RAMDISK')
        explicit_artifact_root=getattr(self.args,'artifact_root',None)
        if explicit_artifact_root is not None:self.artifacts=Artifacts(explicit_artifact_root)
        if self.args.exe is not None:
            identity=hashlib.sha256()
            with self.args.exe.open('rb') as executable:
                for chunk in iter(lambda:executable.read(1024*1024),b''):identity.update(chunk)
            self.executable_sha256=identity.hexdigest()
            if self.args.out_dir is None and self.args.out_root is None:raise ValueError('--exe requires --out-dir or --out-root')
            if self.args.out_root is not None:
                parent=self.args.out_root.resolve();parent.mkdir(parents=True,exist_ok=True);root=parent/('mcp-'+uuid.uuid4().hex)
            else:root=self.args.out_dir.resolve()
            root.mkdir(parents=True,exist_ok=False);self.root=root
            endpoint=str(self.root/'debug.sock')
            env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
            env.update(A2GSPU_AGENT='1',A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(self.root),A2GSPU_BRAM='default',A2GSPU_SEED=str(self.args.seed if self.args.seed is not None else 1),A2GSPU_FAKETIME='1000000000')
            env.update(self.rails)
            if getattr(self.args,'read_only_media',False):env.update(A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
            self.log=(self.root/'emulator.log').open('wb')
            selection=[str(self.args.config.resolve())] if self.args.config else ['-p',str(self.args.platform if self.args.platform is not None else 3)]
            self.process=subprocess.Popen([str(self.args.exe.resolve()),'--debug',endpoint,*selection,'-n','--no-quit-confirm'],cwd=self.root,env=env,stdout=self.log,stderr=subprocess.STDOUT)
            self.receipt()
        elif self.args.out_dir is not None or self.args.out_root is not None or self.args.config is not None or self.args.platform is not None or self.args.seed is not None or self.rails or getattr(self.args,'read_only_media',False):raise ValueError('launch options require --exe')
        if self.artifacts is None and self.root is not None:self.artifacts=Artifacts(self.root)
        deadline=time.monotonic()+20
        while True:
            try:self.client.connect(endpoint);break
            except OSError:
                if time.monotonic()>deadline or (self.process and self.process.poll() is not None):raise RuntimeError('emulator socket unavailable; inspect the session log')
                time.sleep(.025)
        self.client.hello();self.catalog=self.client.agent_tools()
        known={row['name'] for row in self.catalog['capabilities']['rails']}
        if set(self.rails)-known:raise ValueError('unknown startup rails: '+','.join(sorted(set(self.rails)-known)))
        if self.process is not None:
            if 'A2GSPU_AGENT' not in known:raise RuntimeError('selected emulator lacks the shared agent bootstrap')
            self.startup_proof=self.client.agent_command('rail read A2GSPU_AGENT')['data']
            if not isinstance(self.startup_proof,dict) or self.startup_proof.get('schema')!='a2agent-mode-v1' or not self.startup_proof.get('enabled') or not self.startup_proof.get('bootstrap_complete'):raise RuntimeError('native agent bootstrap was not completed; refusing to serve tools')
            self.receipt()

    def call(self,verb,arguments):
        if self.ended:raise RuntimeError('emulator session has ended')
        if verb=='agent_tools':return self.client.agent_tools()
        if verb=='quit':
            if arguments:raise ValueError('quit takes no arguments')
            self.client.quit();self.ended=True
            return {'schema':'a2ctrl-reply-v2','command':'quit','ok':True,'status':'OK','text':'QUIT acknowledged; shutdown requested','data':None}
        if verb=='pause' and not arguments:
            self.client.pause()
            return self.client.agent_command('pause-status',check=False)
        return self.client.agent_command(verb+(' '+arguments if arguments else ''),check=False)

    def observation(self,name,params):
        if self.ended:raise RuntimeError('emulator session has ended')
        return observe(self.client,name,params)

    def artifact(self,name,params):
        if self.artifacts is None:raise ValueError('artifact access requires managed --exe output or explicit --artifact-root when attaching')
        return self.artifacts.call(name,params)

    def context(self,name,params):
        if self.ended:raise RuntimeError('emulator session has ended')
        return context_call(self.client,name,params)

    def batch(self,params):
        if self.args.exe is None or self.root is None:raise ValueError('gs2_batch requires a managed --exe server; attached sessions do not authorize another executable')
        if not isinstance(params,dict) or set(params)-{'frames','rails','platform','config','timeout_seconds'}:raise ValueError('unsupported batch parameters')
        frames=params.get('frames');timeout=params.get('timeout_seconds',60);rails=params.get('rails',{})
        if type(frames) is not int or not 1<=frames<=1000000 or type(timeout) is not int or not 1<=timeout<=300:raise ValueError('frames must be 1..1000000; timeout_seconds 1..300')
        if not isinstance(rails,dict):raise ValueError('batch rails must be an object')
        known={row['name'] for row in self.catalog['capabilities']['rails']}
        for name,value in rails.items():
            if name not in known or name in RESERVED_RAILS:raise ValueError('unknown or lifecycle-reserved batch rail: '+name)
            if not isinstance(value,str) or '\0' in value or len(value.encode('utf-8'))>16384:raise ValueError('batch rail values must be bounded strings')
            if getattr(self.args,'read_only_media',False) and name in ('A2GSPU_RAMDISK','A2GSPU_NO_WRITEBACK','WIZ5_NO_WRITEBACK') and value!='1':raise ValueError('batch rail contradicts --read-only-media: '+name)
        if 'platform' in params and 'config' in params:raise ValueError('choose platform or config')
        if 'config' in params:
            if not isinstance(params['config'],str) or not params['config']:raise ValueError('config must be a path string')
            selection=[str(Path(params['config']).resolve())]
        elif 'platform' in params:
            if type(params['platform']) is not int:raise ValueError('platform must be an integer')
            selection=['-p',str(params['platform'])]
        else:selection=[str(self.args.config.resolve())] if self.args.config else ['-p',str(self.args.platform if self.args.platform is not None else 3)]
        root=self.root/('batch-'+uuid.uuid4().hex);root.mkdir();log_path=root/'emulator.log'
        env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
        env.update(A2GSPU_SEED=str(self.args.seed if self.args.seed is not None else 1),A2GSPU_BRAM='default',A2GSPU_FAKETIME='1000000000',A2GSPU_PACE='fast')
        env.update(rails);env.update(A2GSPU_SPIKE_FRAMES=str(frames),A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(root))
        if getattr(self.args,'read_only_media',False):env.update(A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
        timed_out=False
        with log_path.open('wb') as log:
            process=subprocess.Popen([str(self.args.exe.resolve()),*selection,'-n','--no-quit-confirm'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
            try:process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out=True;process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
        length=log_path.stat().st_size
        with log_path.open('rb') as log:log.seek(max(0,length-65536));excerpt=log.read(65536).decode('utf-8',errors='replace')
        result={'schema':'a2mcp-batch-v1','ok':process.returncode==0 and not timed_out,'exit_code':process.returncode,'timed_out':timed_out,
                'read_only_media_launch':bool(getattr(self.args,'read_only_media',False)),
                'frames_requested':frames,'requested_rails':rails,'artifact_directory':str(root),'log_path':str(log_path),'stdout_bytes':length,
                'stdout_excerpt':excerpt,'stdout_truncated':length>65536,'evidence_complete':not timed_out and length<=65536,
                'verdict_model':'emulator process exit; supply assertions/goldens for guest behavior; separate CPU/RAM, configured media/host endpoints may be shared; not checkpoint branching'}
        (root/'batch-result.json').write_text(encoded(result)+'\n',encoding='utf-8');return result

    def close(self):
        try:
            if self.process and self.process.poll() is None:
                if not self.ended:
                    try:self.client.quit()
                    except (OSError,ProtocolError,RuntimeError) as error:print('MCP close: '+str(error),file=sys.stderr)
                try:self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    # Only our own --no-quit-confirm child, after graceful quit failed.
                    print('MCP close: owned emulator did not exit; terminating',file=sys.stderr)
                    self.process.kill();self.process.wait(timeout=5)
        finally:
            self.client.close()
            if self.log:self.log.close()
            self.receipt(closed=True)

class RpcError(Exception):
    def __init__(self,code,message):self.code=code;super().__init__(message)

class Server:
    def __init__(self,backend):
        self.backend=backend;self.initialized=False;self.ready=False;self.tools=[];self.verbs={}
        rows=backend.catalog['capabilities']['commands']+[{'name':'agent_tools','synopsis':'Discover native tools, installed device coverage, rails, companion tools and limitations.'}]
        for row in sorted(rows,key=lambda row:row['name']):
            name='gs2_'+row['name'].replace('-','_')
            if name in self.verbs:raise ValueError('duplicate normalized tool name')
            self.verbs[name]=row['name']
            self.tools.append({'name':name,'description':row['synopsis']+' Explicit PAUSE required before ordinary commands. Mixed commands may write guest/host state; see native catalog. No shell expansion.',
                'inputSchema':{'type':'object','properties':{'arguments':{'type':'string','description':'Only the arguments after the tool verb, exactly as in its synopsis; quote paths containing spaces.','maxLength':2047}},'additionalProperties':False}})
        if 'gs2_batch' in self.verbs:raise ValueError('native tool collides with batch adapter name')
        self.tools.append({'name':'gs2_batch','description':'Run a separate cold one-shot machine with explicit frames/startup rails, including assertion/golden/microtest rails excluded from interactive launch. Retains logs and native exit status; does not change the current machine. Requires managed --exe server.',
            'inputSchema':{'type':'object','properties':{'frames':{'type':'integer','minimum':1,'maximum':1000000},'rails':{'type':'object','additionalProperties':{'type':'string'}},'platform':{'type':'integer'},'config':{'type':'string'},'timeout_seconds':{'type':'integer','minimum':1,'maximum':300}},'required':['frames'],'additionalProperties':False}})
        self.verbs['gs2_batch']='__batch__'
        for tool in OBSERVATION_TOOLS:
            if tool['name'] in self.verbs:raise ValueError('native tool collides with debugger observation adapter')
            self.verbs[tool['name']]='__observation__';self.tools.append(tool)
        for tool in ARTIFACT_TOOLS:
            if tool['name'] in self.verbs:raise ValueError('native tool collides with artifact adapter')
            self.verbs[tool['name']]='__artifact__';self.tools.append(tool)
        if any(row['name']=='context' for row in backend.catalog['capabilities']['commands']):
            for tool in CONTEXT_TOOLS:
                if tool['name'] in self.verbs:raise ValueError('native tool collides with context adapter')
                self.verbs[tool['name']]='__context__';self.tools.append(tool)

    def handle(self,message):
        identity=None
        try:
            if not isinstance(message,dict) or message.get('jsonrpc')!='2.0' or not isinstance(message.get('method'),str):raise RpcError(-32600,'Invalid JSON-RPC request')
            if 'id' not in message:
                if message['method']=='notifications/initialized' and self.initialized:self.ready=True
                return None  # Never execute a tool from a notification.
            identity=message['id']
            if type(identity) not in (int,str):identity=None;raise RpcError(-32600,'Request id must be a string or integer')
            params=message.get('params',{})
            if not isinstance(params,dict):raise RpcError(-32602,'params must be an object')
            method=message['method']
            if method=='initialize':
                if self.initialized:raise RpcError(-32600,'Already initialized')
                if not isinstance(params.get('protocolVersion'),str) or not isinstance(params.get('capabilities'),dict) or not isinstance(params.get('clientInfo'),dict):raise RpcError(-32602,'initialize requires protocolVersion, capabilities and clientInfo')
                if any(not isinstance(params['clientInfo'].get(key),str) or not params['clientInfo'][key] for key in ('name','version')):raise RpcError(-32602,'clientInfo requires nonempty name and version')
                self.initialized=True
                result={'protocolVersion':VERSION,'capabilities':{'tools':{}},'serverInfo':{'name':'gssquared-agent-tools','version':'1.0.0'},
                        'instructions':'Use gs2_pause explicitly before ordinary tools; gs2_context_read also works while running at machine-thread boundaries. Tools are generated from the running emulator; check native schemas and loss/coverage flags. Use gs2_artifact_list/read/put for content-bound output and create-once test inputs. No implicit cancellation or mutation retry. Closing this stdio server quits only an emulator it launched.'}
            elif method=='ping':result={}
            elif not self.ready:raise RpcError(-32002,'Initialize and send notifications/initialized first')
            elif method=='tools/list':
                if set(params)-{'_meta'}:raise RpcError(-32602,'This fixed catalog has no pagination or list parameters')
                result={'tools':self.tools}
            elif method=='tools/call':
                if set(params)-{'name','arguments','_meta'}:raise RpcError(-32602,'Unsupported tool call fields; task execution is not advertised')
                name=params.get('name');args=params.get('arguments',{})
                if not isinstance(name,str) or name not in self.verbs:raise RpcError(-32602,'Unknown tool')
                if not isinstance(args,dict):raise RpcError(-32602,'Tool arguments must be an object')
                if name!='gs2_batch' and name not in OBSERVATION_NAMES|ARTIFACT_NAMES|CONTEXT_NAMES and (set(args)-{'arguments'} or not isinstance(args.get('arguments',''),str)):raise RpcError(-32602,'Tool arguments must contain only the optional arguments string')
                try:
                    media=[]
                    if name=='gs2_batch':value=self.backend.batch(args)
                    elif name in OBSERVATION_NAMES:value=self.backend.observation(name,args)
                    elif name in ARTIFACT_NAMES:value,media=self.backend.artifact(name,args)
                    elif name in CONTEXT_NAMES:value=self.backend.context(name,args)
                    else:value=self.backend.call(self.verbs[name],args.get('arguments',''))
                    result={'content':[{'type':'text','text':encoded(value)},*media],'structuredContent':value,'isError':value.get('ok') is False}
                except (ValueError,RuntimeError,OSError,ProtocolError) as error:
                    value={'schema':'a2mcp-error-v1','ok':False,'message':str(error),'retry_model':'no automatic retry; a transport failure does not prove cancellation'}
                    result={'content':[{'type':'text','text':encoded(value)}],'structuredContent':value,'isError':True}
            else:raise RpcError(-32601,'Method not found')
            return {'jsonrpc':'2.0','id':identity,'result':result}
        except RpcError as error:return {'jsonrpc':'2.0','id':identity,'error':{'code':error.code,'message':str(error)}}

def serve(backend,source,sink):
    server=Server(backend)
    while True:
        line=source.readline(MAX_LINE+1)
        if not line:return
        if len(line)>MAX_LINE or not line.endswith(b'\n'):
            while line and not line.endswith(b'\n'):line=source.readline(MAX_LINE+1)
            reply={'jsonrpc':'2.0','id':None,'error':{'code':-32600,'message':'Message exceeds byte limit or lacks newline'}}
        else:
            try:
                message=json.loads(line.decode('utf-8'),parse_constant=lambda value:(_ for _ in ()).throw(ValueError('nonfinite JSON')))
                reply=server.handle(message)
            except (ValueError,UnicodeError,RecursionError):reply={'jsonrpc':'2.0','id':None,'error':{'code':-32700,'message':'Invalid UTF8 JSON'}}
        if reply is not None:sink.write((encoded(reply)+'\n').encode('utf-8'));sink.flush()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    target=parser.add_mutually_exclusive_group(required=True);target.add_argument('--socket');target.add_argument('--exe',type=Path)
    output=parser.add_mutually_exclusive_group();output.add_argument('--out-dir',type=Path);output.add_argument('--out-root',type=Path)
    parser.add_argument('--artifact-root',type=Path,help='explicit existing root for artifact access; managed launch defaults to its fresh output directory')
    parser.add_argument('--read-only-media',action='store_true',help='launch with RAMDISK=1 and NO_WRITEBACK=1; explicit later commands/host endpoints remain under caller control')
    selection=parser.add_mutually_exclusive_group();selection.add_argument('--platform',type=int);selection.add_argument('--config',type=Path);parser.add_argument('--seed',type=int);parser.add_argument('--rail',action='append',default=[],metavar='NAME=VALUE')
    backend=Backend(parser.parse_args())
    try:backend.open();serve(backend,sys.stdin.buffer,sys.stdout.buffer)
    except (OSError,RuntimeError,ValueError,ProtocolError) as error:print('MCP server: '+str(error),file=sys.stderr);return 1
    finally:backend.close()
    return 0

if __name__=='__main__':raise SystemExit(main())
