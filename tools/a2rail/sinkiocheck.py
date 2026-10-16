"""Output failures must not become successful exports or partial tap sessions."""
import argparse,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session
from artifacts import read_snap_header

def run(exe,out=None):
    temporary=tempfile.TemporaryDirectory(prefix='a2sinkio-') if out is None else None
    root=Path(temporary.name if temporary else out).resolve();root.mkdir(parents=True,exist_ok=temporary is not None)
    rows=[]
    def note(ok,label,**data):
        rows.append(dict(ok=bool(ok),label=label,**data))
        (root/'checks.json').write_text(json.dumps({'checks':rows},indent=2)+'\n',encoding='utf-8')
        print(('PASS' if ok else 'FAIL'),label,flush=True)
    try:
        for platform in (3,5):
            s=Session(exe,platform)
            try:
                blocked=root/f'blocked-p{platform}';blocked.mkdir()
                for command in ('shot','text','save'):
                    reply=s.cmd(command+' '+str(blocked),ok=False)
                    note('status=FAIL' in reply,f'p{platform} {command} rejects blocked output',reply=reply)
                good=root/f'text-p{platform}.bin';s.cmd('text '+str(good));note(good.stat().st_size==2048,f'p{platform} valid text contains both pages')
                shot=root/f'shot-p{platform}.bmp';s.cmd('shot '+str(shot));note(shot.read_bytes()[:2]==b'BM',f'p{platform} valid shot is BMP')
                snap=root/f'snap-p{platform}.bin';s.cmd('save '+str(snap));read_snap_header(str(snap));note(True,f'p{platform} valid snapshot passes checked reader')
                note('status=OK' in s.cmd('cpu'),f'p{platform} session usable after refusals')
            finally:s.close()
            cases=[('bmp',{'blocked':'spike_frame.bmp'}),('coverage',{'blocked':'coverage.bin','A2GSPU_COVERAGE':'000000-00FFFF','A2GSPU_COVERAGE_OUT':'coverage.bin'})]
            if platform==5:cases.append(('snapshot',{'blocked':'snapshot.bin','A2GSPU_SNAP_SAVE':'snapshot.bin'}))
            for name in ('text_pages.ndjson','keypress_events.ndjson','disk_writes.ndjson'):
                cases.append(('tap-'+name,{'blocked':name,'tap':True}))
            for label,settings in cases:
                case=root/f'p{platform}-{label}';case.mkdir();(case/settings['blocked']).mkdir()
                env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
                env.update(A2GSPU_SPIKE_FRAMES='1',A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(case),A2GSPU_BRAM='default',A2GSPU_SEED='1',A2GSPU_FAKETIME='1000000000',A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
                env.update({k:str(case/v) if k.endswith(('_OUT','_SAVE')) else v for k,v in settings.items() if k.startswith('A2GSPU_')})
                if settings.get('tap'):env['A2GSPU_TAP_SESSION']=str(case)
                result=subprocess.run([str(exe),'-p',str(platform),'-n','--no-quit-confirm'],cwd=case,env=env,capture_output=True,timeout=30)
                (case/'emulator.log').write_bytes(result.stdout+result.stderr)
                note(result.returncode==2,f'p{platform} SPIKE refuses {label} output failure',exit_code=result.returncode)
        assert all(r['ok'] for r in rows),'output sink failures reported success; see checks.json'
    finally:
        if temporary:temporary.cleanup()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path)
    a=p.parse_args();run(a.exe.resolve(),a.out)
