"""Native observer and preservation controls without a toolchain dependency."""
import argparse
import copy
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import experiment as ex
from p0check import check


def run(exe,out=None):
    temporary=tempfile.TemporaryDirectory(prefix='a2experiment-inputs-') if out is None else None
    root=Path(temporary.name if temporary else out).resolve()
    root.mkdir(parents=True,exist_ok=True)
    try:
        for platform in (3,5):
            code=root/f'p{platform}.bin';code.write_bytes(bytes([0xa0,0]+[0xb1,0x20,0x91,0x22,0xc8]*4+[0xea]))
            source=root/f'p{platform}-source.bin';source.write_bytes(bytes([0,127,128,255]))
            pointers=root/f'p{platform}-ptrs.bin';pointers.write_bytes(bytes([0,0x10,0,0x40]))
            plan={'schema':'a2experiment-plan-v1','workload':{'id':'copy-four','case':'boundary-bytes'},'platform':platform,
                'files':[{'source':str(path),'path':name,'sha256':ex.digest(path)} for name,path in [('code.bin',code),('source.bin',source),('pointers.bin',pointers)]],
                'build':[],'image':{'path':'code.bin','format':'raw','address':0x800},'entry':0x800,'target':0x816,'attempts':32,
                'registers':{'e':1,'p':0x34,'x':0xa5,'sp':0x1ef},'preserve_registers':['x','sp'],
                'loads':[{'path':'source.bin','address':0x1000},{'path':'pointers.bin','address':0x20}],
                'checks':[{'address':0x4000,'bytes':[0,127,128,255]},
                    {'address':0x1000,'bytes':[0,127,128,255],'role':'preserved-input'},
                    {'address':0x20,'bytes':[0,0x10,0,0x40],'role':'preserved-input'}]}
            unarmed=ex.run(plan,exe,root/f'p{platform}-unarmed')
            check(unarmed['ok'],'declared source/pointer/register preservation p'+str(platform))
            plan.update(profile=True,footprint={'windows':[{'bank':0,'lo':0,'hi':65535}],
                'allowed':[{'address':0x4000,'length':4}],'max_records':8,'exact_writes':4})
            armed=ex.run(plan,exe,root/f'p{platform}-armed')
            check(armed['ok'] and armed['profile']['ok'] and armed['footprint']['writes']==4,'complete profiles and CPU write footprint')
            check(armed['execution']['data']['cycles']==unarmed['execution']['data']['cycles']=='54' and armed['observations']==unarmed['observations'],'armed/unarmed bits and independent 54-cycle count')
            ex.compare(armed,armed)
            artifact=Path(armed['profile']['path']);original=artifact.read_bytes();artifact.write_bytes(original+b' ')
            try:
                ex.compare(armed,armed)
            except ValueError:check(True,'retained profile tampering refuses comparison')
            else:raise AssertionError('changed profile accepted')
            finally:artifact.write_bytes(original)
            bad=copy.deepcopy(plan);bad['footprint']['allowed']=[{'address':0x4100,'length':4}]
            wrong=ex.run(bad,exe,root/f'p{platform}-wrong-region')
            check(not wrong['ok'] and len(wrong['footprint']['unexpected'])==4,'all out-of-contract writes detected')
            bad=copy.deepcopy(plan);bad['footprint']['max_records']=1;bad['footprint'].pop('exact_writes')
            loss=ex.run(bad,exe,root/f'p{platform}-loss')
            check(not loss['ok'] and loss['footprint']['loss_or_unrecognized_records'],'capped write evidence cannot pass')
    finally:
        if temporary:temporary.cleanup()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--out',type=Path)
    args=parser.parse_args();run(args.exe.resolve(),args.out)
