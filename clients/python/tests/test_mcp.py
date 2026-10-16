import io
import json
from gs2debug.mcp import Server,serve,MAX_LINE

class Backend:
    catalog={'capabilities':{'commands':[{'name':'cpu','synopsis':'Read CPU state'}]}}
    def __init__(self):self.calls=[]
    def call(self,verb,arguments):self.calls.append((verb,arguments));return {'ok':False,'status':'FAIL','message':'independent fixture refusal'}
    def batch(self,params):self.calls.append(('batch',params));return {'schema':'a2mcp-batch-v1','ok':True,'exit_code':0}

def initialized():
    backend=Backend();server=Server(backend)
    result=server.handle({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'future-unsupported','capabilities':{},'clientInfo':{'name':'test','version':'1'}}})
    assert result['result']['protocolVersion']=='2025-11-25'
    assert server.handle({'jsonrpc':'2.0','method':'notifications/initialized'}) is None
    return backend,server

def test_notifications_never_execute_tools():
    backend,server=initialized()
    assert server.handle({'jsonrpc':'2.0','method':'tools/call','params':{'name':'gs2_cpu'}}) is None
    assert backend.calls==[]

def test_native_errors_remain_visible():
    backend,server=initialized()
    result=server.handle({'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'gs2_cpu','arguments':{}}})['result']
    assert result['isError'] and result['structuredContent']['status']=='FAIL'
    assert json.loads(result['content'][0]['text'])==result['structuredContent']
    assert backend.calls==[('cpu','')]

def test_bad_ids_arguments_and_unadvertised_tasks():
    backend,server=initialized()
    for identity,params in [(True,{'name':'gs2_cpu'}),(3,{'name':'gs2_cpu','arguments':[]}),(4,{'name':'gs2_cpu','arguments':{'extra':1}}),(5,{'name':'gs2_cpu','task':{}})]:
        result=server.handle({'jsonrpc':'2.0','id':identity,'method':'tools/call','params':params})
        assert 'error' in result
    assert not backend.calls
    assert 'error' in server.handle({'jsonrpc':'2.0','id':6,'method':'tools/list','params':{'cursor':'invented'}})
    assert 'result' in server.handle({'jsonrpc':'2.0','id':7,'method':'tools/list','params':{'_meta':{}}})

def test_bounded_lines_recover_without_stdout_noise():
    source=io.BytesIO(b'x'*(MAX_LINE+1)+b'\n{bad}\n'+json.dumps({'jsonrpc':'2.0','id':2,'method':'ping'}).encode()+b'\n')
    sink=io.BytesIO();serve(Backend(),source,sink)
    rows=[json.loads(line) for line in sink.getvalue().splitlines()]
    assert rows[0]['error']['code']==-32600 and rows[1]['error']['code']==-32700
    assert rows[2]=={'jsonrpc':'2.0','id':2,'result':{}}

def test_batch_has_explicit_structured_parameters():
    backend,server=initialized()
    tools=server.handle({'jsonrpc':'2.0','id':2,'method':'tools/list'})['result']['tools']
    assert any(tool['name']=='gs2_batch' and 'frames' in tool['inputSchema']['required'] for tool in tools)
    result=server.handle({'jsonrpc':'2.0','id':3,'method':'tools/call','params':{'name':'gs2_batch','arguments':{'frames':1,'rails':{}}}})['result']
    assert result['structuredContent']['exit_code']==0 and backend.calls==[('batch',{'frames':1,'rails':{}})]
