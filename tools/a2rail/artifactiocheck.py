"""Native artifact failures must not turn into successful capture receipts."""
import argparse,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from artifacts import read_raw
from p0check import Session


def run(exe,out=None):
    temporary=tempfile.TemporaryDirectory(prefix='a2artifactio-') if out is None else None
    root=Path(temporary.name if temporary else out).resolve();root.mkdir(parents=True,exist_ok=temporary is not None)
    rows=[]
    def note(ok,label,**data):
        rows.append(dict(ok=bool(ok),label=label,**data))
        (root/'checks.json').write_text(json.dumps({'checks':rows},indent=2)+'\n',encoding='utf-8')
        print(('PASS' if ok else 'FAIL'),label,flush=True)
    try:
        for platform in (3,5):
            names=['spike_trace.bin','spike_slot.bin','spike_mmu_truth.bin']
            if platform==5:names+=['spike_e1_init.bin','spike_e1.bin']
            for blocked in [None,*names,*[name+'.meta.json' for name in names]]:
                case=root/f"p{platform}-{blocked or 'valid'}";case.mkdir()
                if blocked:(case/blocked).mkdir()
                env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
                env.update(A2GSPU_SPIKE_FRAMES='1',A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(case),A2GSPU_BRAM='default',A2GSPU_SEED='1',A2GSPU_FAKETIME='1000000000',A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
                result=subprocess.run([str(exe),'-p',str(platform),'-n','--no-quit-confirm'],cwd=case,env=env,capture_output=True,timeout=30)
                (case/'emulator.log').write_bytes(result.stdout+result.stderr)
                if blocked:note(result.returncode==2,f'p{platform} rejects blocked {blocked}',exit_code=result.returncode)
                else:
                    for name in names:read_raw(str(case/name))
                    note(result.returncode==0,f'p{platform} valid payloads/sidecars pass checked reader',exit_code=result.returncode)
            s=Session(exe,platform)
            try:
                payload=root/f'vram-p{platform}.bin';Path(str(payload)+'.meta.json').mkdir()
                failed=s.cmd('vram '+str(payload),ok=False)
                note('status=FAIL' in failed,f'p{platform} CTRL vram refuses sidecar failure',reply=failed)
                note('status=OK' in s.cmd('cpu'),f'p{platform} CTRL remains usable after failed artifact')
                good=root/f'vram-good-p{platform}.bin';s.cmd('vram '+str(good));read_raw(str(good))
                note(True,f'p{platform} CTRL vram succeeds after refusal')
            finally:s.close()
            for blocked in (None,'ram_dump_periodic_t001.bin','ram_dump_periodic_t001.bin.meta.json','ram_full_periodic_t001.bin','ram_full_periodic_t001.bin.meta.json'):
                case=root/f"tap-p{platform}-{blocked or 'valid'}";case.mkdir()
                if blocked:(case/blocked).mkdir()
                env={k:v for k,v in os.environ.items() if not k.startswith(('A2GSPU_','MEMVU_','WIZ5_'))}
                env.update(A2GSPU_SPIKE_FRAMES='70',A2GSPU_BACKGROUND_WINDOW='1',A2GSPU_OUT_DIR=str(case),A2GSPU_TAP_SESSION=str(case),A2GSPU_TAP_DUMP_EVERY='1',A2GSPU_BRAM='default',A2GSPU_SEED='1',A2GSPU_FAKETIME='1000000000',A2GSPU_RAMDISK='1',A2GSPU_NO_WRITEBACK='1')
                result=subprocess.run([str(exe),'-p',str(platform),'-n','--no-quit-confirm'],cwd=case,env=env,capture_output=True,timeout=30)
                (case/'emulator.log').write_bytes(result.stdout+result.stderr)
                if blocked:note(result.returncode==2,f'p{platform} tap rejects blocked {blocked}',exit_code=result.returncode)
                else:
                    for name in ('ram_dump_periodic_t001.bin','ram_full_periodic_t001.bin'):read_raw(str(case/name))
                    note(result.returncode==0,f'p{platform} tap CPU/RAM payloads pass checked reader',exit_code=result.returncode)
        assert all(row['ok'] for row in rows),'artifact I/O failures were reported as success; see checks.json'
    finally:
        if temporary:temporary.cleanup()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',required=True,type=Path);p.add_argument('--out',type=Path)
    args=p.parse_args();run(args.exe.resolve(),args.out)
