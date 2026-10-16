"""Upstream debugger observations exposed to an actual MCP agent, on IIe and IIgs."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import environment,check

def run(exe):
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2observability-') as temporary:
            root=Path(temporary);first=root/'trace-a.txt';second=root/'trace-b.txt'
            calls=[]
            def rpc(method,params=None):
                identity=len(calls)+1
                calls.append({'jsonrpc':'2.0','id':identity,'method':method,**({'params':params} if params is not None else {})})
                return identity
            def tool(name,arguments=None):return rpc('tools/call',{'name':'gs2_'+name,'arguments':arguments or {}})
            def command(name,arguments=''):return tool(name,{'arguments':arguments})
            rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'observability-test','version':'1'}})
            calls.append({'jsonrpc':'2.0','method':'notifications/initialized'})
            listing=rpc('tools/list');command('pause');command('tbuf','clear');command('tbuf','on')
            command('poke','000800 A9 42 8D 00 09 EA');command('setreg','pc 000800');command('step','3');command('tbuf','off')
            trace=tool('trace_read',{'count':3});memory=tool('memory_read',{'domain':'main','address':0x800,'length':6})
            search=tool('memory_search',{'domain':'main','address':0x800,'length':6,'pattern_hex':'a940','mask_hex':'fff0'})
            io=tool('memory_search',{'domain':'main','address':0xc000,'length':16,'pattern_hex':'00'})
            limited=tool('memory_search',{'domain':'main','address':0x800,'length':6,'pattern_hex':'00','mask_hex':'00','max_hits':1})
            command('poke','000400 C1 C2');screen=tool('video_text_read')
            bad=tool('trace_read',{'count':True});unknown=tool('memory_read',{'domain':'host','address':0,'length':1})
            wrong=tool('memory_search',{'domain':'main','address':0x800,'length':6,'pattern_hex':'a9','mask_hex':'ffff'})
            doc=tool('memory_read',{'domain':'ensoniq','address':0,'length':1})
            command('rail','set A2GSPU_TRACE_EXT 1')
            command('tbuf',f'dump {first} 3');command('tbuf',f'dump {second} 3')
            command('resume');running=tool('trace_read',{'count':1});command('pause')
            data='\n'.join(json.dumps(row) for row in calls)+'\n'
            result=subprocess.run([sys.executable,'-I',str(Path(__file__).with_name('mcpserver.py')),'--exe',str(exe),'--platform',str(platform),'--out-dir',str(root/'session')],input=data.encode(),capture_output=True,env=environment(),timeout=50)
            check(result.returncode==0,'observation MCP lifecycle '+str(platform)+': '+result.stderr.decode(errors='replace')[-200:])
            replies={row['id']:row for row in map(json.loads,result.stdout.splitlines())}
            names={row['name'] for row in replies[listing]['result']['tools']}
            check({'gs2_trace_read','gs2_memory_read','gs2_memory_search','gs2_video_text_read'}<=names,'upstream structured observations discoverable through MCP')
            def value(identity):
                row=replies[identity]['result'];check(not row['isError'],'observation succeeds '+str(identity));return row['structuredContent']
            entries=value(trace)['entries']
            check([row['opcode'] for row in entries]==[0xa9,0x8d,0xea] and entries[1]['write'] and entries[1]['effective_address']==0x900 and entries[1]['data']==0x42,'trace decodes actual instructions, access direction, data and addresses')
            check(entries[0]['cycle_delta'] is None and int(entries[1]['cycle_delta'])==2 and int(entries[2]['cycle_delta'])==4,'trace deltas use adjacent retained records')
            check(value(memory)['bytes']==[0xa9,0x42,0x8d,0,9,0xea],'memory domain byte values retained')
            check(value(search)['addresses']==[0x800],'masked search finds independent fixture')
            check(value(io)['addresses']==[] and value(io)['unreadable_bytes']==16,'I/O placeholders cannot become false zero-pattern hits')
            check(value(limited)['truncated'] and len(value(limited)['addresses'])==1,'search reports truncated hit list')
            text=value(screen);check(text['lines'][0].startswith('AB') and text['bytes'][:2]==[0xc1,0xc2] and text['cols'] in (40,80),'text includes native page metadata, raw bytes and decoded lines')
            check(all(replies[i]['result']['isError'] for i in (bad,unknown,wrong,running)),'invalid inputs and running session explicitly refuse')
            check(replies[doc]['result']['isError']==(platform==3),'IIgs-only domain fails honestly on IIe')
            check(first.read_bytes()==second.read_bytes(),'decoding the same trace twice is stable')
            decoded=first.read_text(encoding='utf-8')
            check('clock=' in decoded and 'sl=' not in decoded and 'dcy=' not in decoded,'extended trace names recorded clock without false fixed-speed scanlines')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);run(parser.parse_args().exe.resolve())
