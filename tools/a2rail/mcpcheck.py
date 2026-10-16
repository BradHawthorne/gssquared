"""Exercise an actual stdio MCP client/server/emulator lifecycle."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import runpy
import time
import re
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import environment,check
from gs2debug import Client

def request(index,method,params=None):
    return {'jsonrpc':'2.0','id':index,'method':method,**({'params':params} if params is not None else {})}

def run(exe):
    from gs2debug.mcp import BATCH_RAILS
    source=(Path(__file__).resolve().parents[2]/'src/a2gspu_spike.cpp').read_text(encoding='utf-8')
    tail=source.split('a2gspu_ctrl_loop(state);',1)[1]
    tail_rails=set(re.findall(r'getenv\s*\(\s*"((?:A2GSPU_|MEMVU_|WIZ5_)[A-Z0-9_]+)"',tail))
    check(tail_rails<=BATCH_RAILS,'all one-shot tail getenv rails are classified as batch-only')
    for filename in ('test_mcp.py','test_observations.py','test_artifacts.py','test_recovery.py','test_shutdown.py'):
        fixtures=runpy.run_path(str(Path(__file__).resolve().parents[2]/'clients/python/tests'/filename))
        for name,fixture in fixtures.items():
            if name.startswith('test_'):fixture();check(True,'MCP framing/observation fixture: '+name)
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2mcpcheck-') as temporary:
            root=Path(temporary);owned=root/'session';marker=root/'expected-marker.bin';marker.write_bytes(b'\xa9')
            batch_rails={'A2GSPU_LOAD':str(marker)+'@0800','A2GSPU_LOAD_BOOT':'1','A2GSPU_ASSERT':'peek:000800 == $A9'}
            calls=[request(1,'tools/list'),request(2,'initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'foundation-test','version':'1'}}),
                   {'jsonrpc':'2.0','method':'notifications/initialized'},request(3,'tools/list'),
                   request(4,'tools/call',{'name':'gs2_pause','arguments':{}}),
                   request(5,'tools/call',{'name':'gs2_poke','arguments':{'arguments':'000800 A9 42 EA'}}),
                   request(6,'tools/call',{'name':'gs2_setreg','arguments':{'arguments':'pc 000800'}}),
                   request(7,'tools/call',{'name':'gs2_step','arguments':{'arguments':'1'}}),
                   request(8,'tools/call',{'name':'gs2_ui','arguments':{'arguments':'monitor green'}}),
                   request(9,'tools/call',{'name':'gs2_ui','arguments':{'arguments':'monitor invented'}}),
                   request(10,'tools/call',{'name':'gs2_invented','arguments':{}}),
                   request(11,'tools/call',{'name':'gs2_cpu','arguments':{'arguments':'\nreset'}}),
                   request(12,'tools/call',{'name':'gs2_cpu','arguments':{}}),request(13,'ping'),
                   request(14,'tools/call',{'name':'gs2_rail','arguments':{'arguments':'read A2GSPU_PACE'}}),
                   request(15,'tools/call',{'name':'gs2_cov','arguments':{'arguments':'status'}}),
                   request(16,'tools/call',{'name':'gs2_batch','arguments':{'frames':1,'rails':batch_rails}}),
                   request(17,'tools/call',{'name':'gs2_batch','arguments':{'frames':1,'rails':dict(batch_rails,A2GSPU_ASSERT='peek:000800 == $42')}}),
                   request(18,'tools/call',{'name':'gs2_batch','arguments':{'frames':1,'rails':{'A2GSPU_FAKE_TOOL_RAIL':'1'}}}),
                   request(19,'tools/call',{'name':'gs2_cpu','arguments':{}})]
            data='\n'.join(json.dumps(row) for row in calls)+'\n{malformed-json}\n'
            argv=[sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--platform',str(platform),'--out-dir',str(owned),'--rail','A2GSPU_SEED=2','--rail','A2GSPU_PACE=fast','--rail','A2GSPU_COVERAGE=0800-08FF']
            result=subprocess.run(argv,input=data.encode(),capture_output=True,env=environment(),timeout=90)
            check(result.returncode==0,'MCP owned process and emulator close on EOF: '+result.stderr.decode(errors='replace')[-300:])
            rows=[json.loads(line) for line in result.stdout.decode('utf-8').splitlines()];byid={row['id']:row for row in rows}
            check(len(rows)==20 and len(byid)==20,'stdout contains only one JSON-RPC reply per request; notifications silent')
            check('error' in byid[1] and byid[2]['result']['protocolVersion']=='2025-11-25','MCP initialization order enforced')
            tools=byid[3]['result']['tools'];names={tool['name'] for tool in tools}
            check(len(tools)==85 and {'gs2_cpu','gs2_ui','gs2_capture','gs2_agent_tools','gs2_quit','gs2_batch','gs2_trace_read','gs2_memory_read','gs2_memory_search','gs2_video_text_read','gs2_artifact_put','gs2_artifact_read','gs2_artifact_list','gs2_context_read','gs2_context_track'}<=names,'MCP tools include complete runtime catalog, debugger observations, artifacts and context')
            executed=byid[7]['result'];native=executed['structuredContent']
            check(not executed['isError'] and native['data']['instructions']=='1' and native['data']['cpu_after']['registers']['a']==66,'MCP executes real guest code and returns native values')
            check(json.loads(executed['content'][0]['text'])==native,'structured and text evidence agree')
            check(byid[8]['result']['structuredContent']['data']['state']['monitor']=='green','MCP presentation tool reaches native handler')
            check(byid[9]['result']['isError'] and 'error' in byid[10] and byid[11]['result']['isError'],'execution, unknown tool and injection failures distinguished')
            check(byid[12]['result']['structuredContent']['data']['state']['registers']['a']==66,'rejected newline cannot execute reset')
            check('pace=fast' in byid[14]['result']['structuredContent']['text'],'managed startup actually applies requested pacing')
            check('cov on' in byid[15]['result']['structuredContent']['text'],'managed startup actually arms requested coverage')
            passed=byid[16]['result'];failed=byid[17]['result']
            check(not passed['isError'] and passed['structuredContent']['exit_code']==0,'independent batch uses native positive memory assertion')
            check(failed['isError'] and failed['structuredContent']['exit_code']!=0,'independent batch preserves planted wrong assertion failure')
            check(byid[18]['result']['isError'] and byid[19]['result']['structuredContent']['data']['state']['registers']['a']==66,'batch validation and jobs leave current machine untouched')
            check(Path(passed['structuredContent']['log_path']).is_file() and Path(failed['structuredContent']['artifact_directory'],'batch-result.json').is_file(),'batch artifacts and verdicts retained independently')
            check(byid[None]['error']['code']==-32700,'invalid JSON fails as protocol error')
            owner=json.loads((owned/'mcp-session.json').read_text(encoding='utf-8'))
            check(owner['closed'] and owner['exit_code']==0 and owner['requested_rails']=={'A2GSPU_SEED':'2','A2GSPU_PACE':'fast','A2GSPU_COVERAGE':'0800-08FF'},'retained owner receipt proves startup configuration and clean emulator exit')
            check(owner['startup_proof']['enabled'] and owner['startup_proof']['bootstrap_complete'],'native bootstrap proof retained; launch is not inferred from environment')
            receipt=(owned/'mcp-session.json').read_bytes()
            refused=subprocess.run(argv,input=b'',capture_output=True,env=environment(),timeout=10)
            check(refused.returncode!=0 and (owned/'mcp-session.json').read_bytes()==receipt,'existing output directory refuses without overwriting its receipt')
            if platform==3:
                invalid=root/'unknown-rail'
                unknown=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--out-dir',str(invalid),'--rail','A2GSPU_FAKE_TOOL_RAIL=1'],input=b'',capture_output=True,env=environment(),timeout=30)
                check(unknown.returncode!=0 and b'unknown startup rails' in unknown.stderr and json.loads((invalid/'mcp-session.json').read_text(encoding='utf-8'))['closed'],'unknown startup rail refuses and closes owned child')
                blocked=root/'transport-conflict'
                conflict=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--out-dir',str(blocked),'--rail','A2GSPU_CTRL=elsewhere'],input=b'',capture_output=True,env=environment(),timeout=10)
                check(conflict.returncode!=0 and not blocked.exists(),'conflicting transport rail refuses before creating a session')
                batch_only=root/'batch-only-conflict'
                conflict=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--out-dir',str(batch_only),'--rail','A2GSPU_ASSERT=peek:0800 == $00'],input=b'',capture_output=True,env=environment(),timeout=10)
                check(conflict.returncode!=0 and not batch_only.exists() and b'gs2_batch' in conflict.stderr,'one-shot rail redirects explicitly instead of silently ignoring it')
                invalid_scope=root/'invalid-scope-output'
                conflict=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--out-dir',str(invalid_scope),'--artifact-root',str(root/'missing-artifact-root')],input=b'',capture_output=True,env=environment(),timeout=10)
                check(conflict.returncode!=0 and not invalid_scope.exists(),'invalid artifact root refuses before launch or output directory creation')
    from observabilitycheck import run as observations_run
    observations_run(exe)
    from autonomycheck import run as autonomy_run
    autonomy_run(exe)
    from executionshapecheck import run as execution_run
    execution_run(exe)
    with tempfile.TemporaryDirectory(prefix='a2mcpattach-') as temporary:
        root=Path(temporary);endpoint=str(root/'debug.sock');client=Client()
        with (root/'emulator.log').open('wb') as log:
            process=subprocess.Popen([str(exe),'--debug',endpoint,'-p','3','-n','--no-quit-confirm'],cwd=root,env=environment(),stdout=log,stderr=subprocess.STDOUT)
            try:
                messages=[request(1,'initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'attach-test','version':'1'}}),{'jsonrpc':'2.0','method':'notifications/initialized'},request(2,'tools/list'),request(3,'tools/call',{'name':'gs2_artifact_list','arguments':{}})]
                attached=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--socket',endpoint],input=('\n'.join(json.dumps(row) for row in messages)+'\n').encode(),capture_output=True,env=environment(),timeout=30)
                check(attached.returncode==0 and process.poll() is None,'attached MCP EOF leaves externally owned emulator alive')
                replies={row['id']:row for row in map(json.loads,attached.stdout.splitlines())}
                check(replies[3]['result']['isError'],'attached session has no implicit artifact filesystem scope')
                (root/'fixture.txt').write_bytes(b'owned output\n')
                messages[-1]=request(3,'tools/call',{'name':'gs2_artifact_read','arguments':{'path':'fixture.txt','mode':'text'}})
                scoped=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--socket',endpoint,'--artifact-root',str(root)],input=('\n'.join(json.dumps(row) for row in messages)+'\n').encode(),capture_output=True,env=environment(),timeout=30)
                replies={row['id']:row for row in map(json.loads,scoped.stdout.splitlines())}
                check(scoped.returncode==0 and process.poll() is None and replies[3]['result']['structuredContent']['text']=='owned output\n','explicit artifact root works on attach without taking emulator ownership')
                deadline=time.monotonic()+10
                while True:
                    try:client.connect(endpoint);break
                    except OSError:
                        if time.monotonic()>deadline:raise
                        time.sleep(.02)
                client.hello();client.quit();process.wait(timeout=10)
                check(process.returncode==0,'attached emulator remains controllable after MCP disconnect')
            finally:
                client.close()
                if process.poll() is None:process.kill();process.wait(timeout=5)
    with tempfile.TemporaryDirectory(prefix='a2agent-rejections-') as temporary:
        root=Path(temporary)
        cases=[({'A2GSPU_AGENT':'invalid'},['--debug',str(root/'bad.sock'),'-p','3']),
               ({'A2GSPU_AGENT':'1'},['-p','3']),
               ({'A2GSPU_AGENT':'1','A2GSPU_CTRL':str(root/'ctrl')},['--debug',str(root/'ctrl.sock'),'-p','3']),
               ({'A2GSPU_AGENT':'1','A2GSPU_SPIKE_FRAMES':'1'},['--debug',str(root/'spike.sock'),'-p','3'])]
        for rails,arguments in cases:
            env=environment();env.update(rails)
            result=subprocess.run([str(exe),*arguments,'-n','--no-quit-confirm'],cwd=root,env=env,capture_output=True,timeout=10)
            check(result.returncode!=0 and b'A2GSPU_AGENT: FAIL' in result.stdout+result.stderr,'invalid agent bootstrap configuration refuses before machine start: '+str(rails))
    with tempfile.TemporaryDirectory(prefix='a2mcp-repeat-') as temporary:
        root=Path(temporary)
        argv=[sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--out-root',str(root)]
        for _ in range(2):
            result=subprocess.run(argv,input=b'',capture_output=True,env=environment(),timeout=30)
            check(result.returncode==0,'stable MCP configuration can launch again without deleting evidence')
        receipts=list(root.glob('mcp-*/mcp-session.json'))
        check(len(receipts)==2 and all(json.loads(path.read_text(encoding='utf-8'))['closed'] for path in receipts),'repeat launches retain separate closed ownership receipts')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);run(parser.parse_args().exe.resolve())
