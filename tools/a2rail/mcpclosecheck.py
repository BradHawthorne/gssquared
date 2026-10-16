"""Owned MCP shutdown after pending work; attached machines keep their owner.

Retained observations precede fixture-only emergency cleanup, so an unreaped
child or failed close cannot be hidden by the test's finally block.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'clients/python/src'))
from gs2debug import Client,RequestPending
from gs2debug.mcp import Backend


def arguments(exe,root,platform):
    return SimpleNamespace(exe=exe,socket=None,out_dir=root,out_root=None,
        platform=platform,config=None,seed=1,rail=[],artifact_root=None,read_only_media=True)


def run(exe,out=None):
    temporary=tempfile.TemporaryDirectory(prefix='a2mcpclose-') if out is None else None
    root=Path(temporary.name if temporary else out).resolve()
    root.mkdir(parents=True,exist_ok=temporary is not None)
    observations=[]
    try:
        for platform in (3,5):
            for pending in (False,True):
                case=root/f"p{platform}-{'pending' if pending else 'graceful'}"
                b=Backend(arguments(exe,case,platform));row={'platform':platform,'pending':pending}
                try:
                    b.open()
                    if pending:
                        b.call('poke','000800 4C 00 08');b.call('setreg','pc 800')
                        try:b.client.agent_command('run 1000000',timeout=.01)
                        except RequestPending:row['pending_before_close']=b.client.pending_request
                        else:raise AssertionError('long native command unexpectedly finished')
                    started=time.monotonic()
                    try:b.close()
                    except Exception as error:row['close_error']=type(error).__name__+': '+str(error)
                    row['close_seconds']=time.monotonic()-started
                    row['live_before_fixture_cleanup']=b.process.poll() is None
                    row['receipt_before_fixture_cleanup']=json.loads((case/'mcp-session.json').read_text(encoding='utf-8'))
                    owner=row['receipt_before_fixture_cleanup']
                    row['ok']='close_error' not in row and not row['live_before_fixture_cleanup'] and owner['closed'] and row['close_seconds']<20
                    if pending:
                        shutdown=owner.get('shutdown') or {}
                        row['ok']=row['ok'] and shutdown.get('effects_unknown') is True and shutdown.get('pending_request')==row['pending_before_close']
                    else:row['ok']=row['ok'] and owner['exit_code']==0
                    if row['ok']:
                        before=(case/'mcp-session.json').read_bytes();b.close()
                        row['ok']=(case/'mcp-session.json').read_bytes()==before
                finally:
                    if b.process and b.process.poll() is None:
                        b.process.kill();b.process.wait(timeout=5)
                        row['fixture_emergency_reap']=True
                    b.client.close()
                    if b.log:b.log.close()
                    observations.append(row)
                    (root/'checks.json').write_text(json.dumps({'cases':observations},indent=2)+'\n',encoding='utf-8')
                print(('PASS' if row.get('ok') else 'FAIL'),f'p{platform}',('pending' if pending else 'graceful'),row.get('close_error',''),flush=True)
            case=root/f'p{platform}-attached';case.mkdir()
            endpoint=str(case/'debug.sock');row={'platform':platform,'attached':True}
            env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
            env.update(A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_BRAM='default',A2GSPU_SEED='1',A2GSPU_FAKETIME='1000000000',A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1',A2GSPU_OUT_DIR=str(case))
            b=Backend(arguments(None,None,None));b.args.socket=endpoint;b.args.read_only_media=False;b.args.seed=None
            client=Client()
            with (case/'emulator.log').open('wb') as log:
                process=subprocess.Popen([str(exe),'--debug',endpoint,'-p',str(platform),'-n','--no-quit-confirm'],cwd=case,env=env,stdout=log,stderr=subprocess.STDOUT)
                try:
                    b.open();b.close();row['shutdown']=b.shutdown
                    row['live_after_attached_close']=process.poll() is None
                    deadline=time.monotonic()+5
                    while True:
                        try:client.connect(endpoint);break
                        except OSError:
                            if time.monotonic()>deadline:raise
                            time.sleep(.02)
                    client.hello();client.quit();process.wait(timeout=10)
                    row['fixture_owner_exit_code']=process.returncode
                    row['ok']=row['live_after_attached_close'] and process.returncode==0 and not b.shutdown['owned'] and b.shutdown['outcome']=='attached-disconnected'
                finally:
                    b.client.close();client.close()
                    if process.poll() is None:process.kill();process.wait(timeout=5);row['fixture_emergency_reap']=True
                    observations.append(row)
                    (root/'checks.json').write_text(json.dumps({'cases':observations},indent=2)+'\n',encoding='utf-8')
            print(('PASS' if row.get('ok') else 'FAIL'),f'p{platform}','attached remains controllable',flush=True)
        assert all(row['ok'] for row in observations),'MCP shutdown failed; see checks.json before fixture cleanup'
    finally:
        if temporary:temporary.cleanup()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',required=True,type=Path);p.add_argument('--out',type=Path)
    args=p.parse_args();run(args.exe.resolve(),args.out)
