"""Checked successful exports and refusals across the file control surface."""
import argparse,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session

def run(exe,out=None):
    temp=tempfile.TemporaryDirectory(prefix='a2exportio-') if out is None else None
    root=Path(temp.name if temp else out).resolve();root.mkdir(parents=True,exist_ok=temp is not None);rows=[]
    def note(ok,label,**data):
        rows.append(dict(ok=bool(ok),label=label,**data));(root/'checks.json').write_text(json.dumps({'checks':rows},indent=2)+'\n',encoding='utf-8');print(('PASS' if ok else 'FAIL'),label,flush=True)
    try:
        for platform in (3,5):
            s=Session(exe,platform);blocked=root/f'blocked-p{platform}';blocked.mkdir()
            try:
                for index,command in enumerate(('help','manifest','systems','limitations','rail env-only','devstate all','read 000800 16','dis 000800 1','iolog dump')):
                    path=root/f'p{platform}-{index}.out';reply=s.cmd(command+' '+str(path));note(path.is_file() and path.stat().st_size>0,f'p{platform} {command} publishes output')
                    reply=s.cmd(command+' '+str(blocked),ok=False);note('status=FAIL' in reply,f'p{platform} {command} refuses blocked output')
                image=root/f'screen-p{platform}.png';sidecar=image.with_suffix('.txt');sidecar.mkdir()
                reply=s.cmd('screen '+str(image),ok=False);note('status=FAIL' in reply,f'p{platform} screen refuses blocked sidecar',reply=reply)
                note('status=OK' in s.cmd('cpu'),f'p{platform} remains usable after export refusal')
            finally:s.close()
            case=root/f'textdump-p{platform}';case.mkdir();(case/'text.bin').mkdir()
            env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
            env.update(A2GSPU_SPIKE_FRAMES='1',A2GSPU_TEXTDUMP='text.bin',A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(case),A2GSPU_BRAM='default',A2GSPU_SEED='1',A2GSPU_FAKETIME='1000000000',A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
            result=subprocess.run([str(exe),'-p',str(platform),'-n','--no-quit-confirm'],cwd=case,env=env,capture_output=True,timeout=30);(case/'emulator.log').write_bytes(result.stdout+result.stderr)
            note(result.returncode==2,f'p{platform} textdump failure exits 2',exit_code=result.returncode)
        assert all(r['ok'] for r in rows),'exports silently failed; see checks.json'
    finally:
        if temp:temp.cleanup()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args();run(a.exe.resolve(),a.out)
