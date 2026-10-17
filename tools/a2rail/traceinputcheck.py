"""Regression checks for the native legacy trace decoder, using host-ABI fixtures."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator',type=Path,required=True)
    args=parser.parse_args()
    emulator=args.emulator.resolve(strict=True)
    exe=emulator.with_name('gstrace'+emulator.suffix)
    fixture=emulator.with_name('interfacecontracttest'+emulator.suffix)
    if not exe.is_file() or not fixture.is_file():
        raise RuntimeError('Build gstrace and interfacecontracttest beside the selected emulator')
    with tempfile.TemporaryDirectory(prefix='a2trace-cli-') as temp:
        root=Path(temp)/'inputs'
        subprocess.run([str(fixture),'--trace-input-fixtures',str(root)],check=True,timeout=15,
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        checks=0

        def run(name,arguments,code,rows,needle=None):
            nonlocal checks
            result=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,timeout=15)
            assert result.returncode==code,(name,result.returncode,result.stderr)
            lines=result.stdout.decode('utf-8').splitlines()
            assert len(lines)==rows,(name,len(lines),rows)
            if code==2:
                assert b'status=FAIL' in result.stderr,(name,result.stderr)
            if needle is not None:
                assert needle in result.stdout,(name,result.stdout[:200])
            checks+=1
            print('PASS trace CLI:',name,flush=True)
            return result

        for cpu in ('6502','65c02','65816'):
            result=run(cpu+' exact rows',[cpu,root/'valid.bin'],0,2,b'NOP')
            lines=result.stdout.splitlines()
            assert b'1234' in lines[0] and b'5678' in lines[1],(cpu,lines)
            run(cpu+' empty trace',[cpu,root/'empty.bin'],0,0)
        run('missing input',['65816',root/'missing.bin'],2,0)
        run('directory input',['65816',root],2,0)
        run('partial record',['65816',root/'partial.bin'],2,0)
        run('oversized input',['65816',root/'large.bin'],2,0)
        run('usable capacity boundary',['65816',root/'maximum.bin'],0,99999)
        run('missing requested labels',['-l',root/'missing.lbl','65816',root/'valid.bin'],2,0)
        run('labels before CPU',['-l',root/'labels.lbl','65816',root/'valid.bin'],0,3,b'start')
        run('labels after CPU',['65816','-l',root/'labels.lbl',root/'valid.bin'],0,3,b'start')
        for name,arguments in (
            ('missing CPU',['-l',root/'labels.lbl',root/'valid.bin']),
            ('duplicate CPU',['6502','65816',root/'valid.bin']),
            ('unknown CPU',['68000',root/'valid.bin']),
            ('missing option operand',['65816','-l',root/'valid.bin']),
            ('duplicate labels',['-l',root/'labels.lbl','-l',root/'labels.lbl','65816',root/'valid.bin']),
            ('missing arguments',[])):
            run(name,arguments,1,0)
        before=(root/'valid.bin').read_bytes()
        with (root/'valid.bin').open('rb') as output:
            result=subprocess.run([str(exe),'65816',str(root/'valid.bin')],stdout=output,
                                  stderr=subprocess.PIPE,timeout=15)
        assert result.returncode==2 and b'status=FAIL' in result.stderr,result.stderr
        assert (root/'valid.bin').read_bytes()==before
        checks+=1;print('PASS trace CLI: read-only stdout refuses without altering input',flush=True)
        print(f'trace CLI: {checks} checks passed',flush=True)


if __name__=='__main__':
    main()
