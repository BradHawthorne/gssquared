from pathlib import Path
import hashlib
import json
import subprocess

repo=Path('D:/gsqs/traceio-20261017/src');root=Path('D:/gsqs/tracein-20261017')
exe=root/'baseline-fixture.exe'
command=['g++','-std=c++17','-I',str(repo/'src'),str(Path('D:/projects/gssquared-fork/Docs/reports/gssquared-trace-input-20261017/input_fixture.cpp')),
         str(repo/'src/debugger/trace.cpp'),str(repo/'src/debugger/trace_opcodes.cpp'),'-o',str(exe)]
subprocess.run(command,check=True,capture_output=True)
fixtures=root/'baseline-inputs'
result=subprocess.run([str(exe),str(fixtures)],check=True,capture_output=True)
(root/'baseline-reader.log').write_bytes(result.stdout+result.stderr)
assert b'loaded_count=1 expected=2 exact_binary=0' in result.stdout,result.stdout
cli=Path('D:/gsqs/traceio-20261017/build/gstrace.exe')
rows=[]
for name,args in [('valid',['65816',str(fixtures/'valid.bin')]),('empty',['65816',str(fixtures/'empty.bin')]),
                  ('missing',['65816',str(fixtures/'missing.bin')]),('partial',['65816',str(fixtures/'partial.bin')]),
                  ('large',['65816',str(fixtures/'large.bin')]),
                  ('missing-labels',['-l',str(fixtures/'missing.lbl'),'65816',str(fixtures/'valid.bin')]),
                  ('missing-cpu',['-l',str(fixtures/'labels.lbl'),str(fixtures/'valid.bin')])]:
    result=subprocess.run([str(cli),*args],capture_output=True,timeout=15)
    count=len(result.stdout.splitlines())
    assert result.returncode==0 and count>=100000,(name,result.returncode,count)
    # Never retain uninitialized decoded host memory from the baseline.
    rows.append({'case':name,'exit_code':result.returncode,'stdout_lines':count,
                 'defect_reproduced':True,'stdout_retained':False})
record={'source_commit':'db868e65c881a933b4f40033b3240d6987ba0d47',
        'baseline_cli':str(cli),'baseline_cli_sha256':hashlib.sha256(cli.read_bytes()).hexdigest(),
        'fixture_compile_argv':command,'checks':rows,
        'privacy':'Only row counts retained; baseline decodes uninitialized slots.'}
(root/'baseline-checks.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
print('PASS baseline reader corruption and seven CLI false-success/extra-row cases')
