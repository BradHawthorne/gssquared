import json
import hashlib
from pathlib import Path
import subprocess

root=Path('D:/gsqs/byteclose-20261016');rows=[]
baseline='3cec97671f81897b4253b6fc4188cb3a4ccd1f13'
headers=root/'baseline-includes';headers.mkdir(exist_ok=True)
identities={}
for name in ('a2gspu_output.hpp','a2gspu_outputs.hpp','a2gspu_artifact_io.hpp'):
    content=subprocess.run(['git','-C','D:/projects/gssquared-fork','show',baseline+':src/'+name],capture_output=True,check=True).stdout
    (headers/name).write_bytes(content);identities[name]=hashlib.sha256(content).hexdigest()
command=['g++','-std=c++17','-I',str(headers),str(root/'src/Docs/reports/gssquared-byte-close-20261016/close_fixture.cpp'),'-o',str(root/'baseline-fixture.exe')]
compiled=subprocess.run(command,capture_output=True);(root/'baseline-fixture-build.log').write_bytes(compiled.stdout+compiled.stderr)
assert compiled.returncode==0,compiled.stderr
for mode,expected in [('direct',1),('shutdown',1),('late',0),('destructor',0)]:
    run=subprocess.run([str(root/'baseline-fixture.exe'),str(root/('baseline-'+mode+'.bin')),mode],capture_output=True,timeout=10)
    output=run.stdout+run.stderr;(root/('baseline-'+mode+'.log')).write_bytes(output)
    assert run.returncode==expected,(mode,run.returncode,output)
    assert (b'finalization returned success' in output if mode in ('direct','shutdown')
            else b'capture-finalization' not in output if mode=='late' else b': closed ' in output),(mode,output)
    rows.append({'variant':'baseline','mode':mode,'expected_exit':expected,'exit_code':run.returncode,'old_false_success_reproduced':True})
    print('PASS baseline false-success reproducer',mode,flush=True)
for mode,expected in [('direct',0),('shutdown',0),('late',2),('destructor',2)]:
    run=subprocess.run([str(root/'committed-fixture.exe'),str(root/('committed-'+mode+'.bin')),mode],capture_output=True,timeout=10)
    output=run.stdout+run.stderr;(root/('committed-'+mode+'.log')).write_bytes(output)
    assert run.returncode==expected,(mode,run.returncode,output)
    assert b'capture-finalization' in output and b': closed ' not in output,(mode,output)
    rows.append({'variant':'candidate','mode':mode,'expected_exit':expected,'exit_code':run.returncode,'refusal_visible':True,'false_closed_receipt':False})
    print('PASS committed fixture',mode,flush=True)
(root/'fixture-checks.json').write_bytes((json.dumps({'baseline_commit':baseline,'baseline_header_sha256':identities,'baseline_compile_argv':command,'checks':rows},indent=2)+'\n').encode('utf-8'))
