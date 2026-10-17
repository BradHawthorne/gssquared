"""A file acknowledgement is either fully published or a visible fatal failure."""
import argparse,json,shutil,subprocess,sys,tempfile,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session

def run(exe,out=None):
    temp=tempfile.TemporaryDirectory(prefix='a2ack-') if out is None else None
    root=Path(temp.name if temp else out).resolve();root.mkdir(parents=True,exist_ok=temp is not None);rows=[]
    def note(ok,label,**data):
        rows.append(dict(ok=bool(ok),label=label,**data));(root/'checks.json').write_text(json.dumps({'checks':rows},indent=2)+'\n',encoding='utf-8');print(('PASS' if ok else 'FAIL'),label,flush=True)
    try:
        for platform in (3,5):
            for mode in ('tmp-directory','ack-directory','old-receipt'):
                s=Session(exe,platform);case=root/f'p{platform}-{mode}';case.mkdir()
                try:
                    s.cmd('cpu');seq=s.seq+1;ack=s.dir/f'ack.{seq}';tmp=s.dir/f'ack.{seq}.tmp'
                    if mode=='tmp-directory':tmp.mkdir()
                    elif mode=='ack-directory':ack.mkdir();(ack/'marker').write_text('preserve',encoding='utf-8')
                    else:ack.write_text('prior-receipt\n',encoding='utf-8')
                    artifact=case/'text.bin';(s.dir/'pending').write_text('text '+str(artifact)+'\n',encoding='utf-8');(s.dir/'pending').replace(s.dir/f'cmd.{seq}');s.seq=seq
                    deadline=time.monotonic()+5
                    while time.monotonic()<deadline and s.process.poll() is None and not artifact.exists():time.sleep(.02)
                    note(artifact.exists() and artifact.stat().st_size==2048,f'p{platform} {mode}: command effects precede failed ACK')
                    try:s.process.wait(timeout=1)
                    except subprocess.TimeoutExpired:pass
                    note(s.process.poll()==2,f'p{platform} {mode}: publication failure exits 2',exit_code=s.process.poll())
                    if mode=='old-receipt':note(ack.read_text(encoding='utf-8')=='prior-receipt\n',f'p{platform} old receipt is preserved')
                    elif mode=='ack-directory':note((ack/'marker').read_text(encoding='utf-8')=='preserve',f'p{platform} blocked destination is preserved')
                    else:note(not ack.exists(),f'p{platform} no partial ACK is published')
                finally:
                    if s.process.poll() is None:
                        try:s.cmd('quit');s.process.wait(timeout=5)
                        finally:
                            if s.process.poll() is None:s.process.kill();s.process.wait(timeout=5)
                    s.log.close();shutil.copyfile(s.dir/'session.log',case/'emulator.log');s.temp.cleanup()
        assert all(r['ok'] for r in rows),'ACK publication silently failed; see checks.json'
    finally:
        if temp:temp.cleanup()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args();run(a.exe.resolve(),a.out)
