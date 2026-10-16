"""Reverify a retained real pilot and reject substituted bundles before launch."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import experiment as ex
from p0check import check


def run(study,out):
    study=study.resolve();out.mkdir(parents=True,exist_ok=False)
    pairs=0
    for path in sorted(study.glob('p*-reference/run.json')):
        other=path.parent.with_name(path.parent.name.replace('-reference','-unroll4'))/'run.json'
        result=ex.compare(json.loads(path.read_text()),json.loads(other.read_text()))
        check(result['ok'],'retained bound pair '+path.parent.name);pairs+=1
    check(pairs==32,'all 32 real pilot comparisons reverified')
    controls=[]
    for platform in (3,5):
        original=json.loads((study/f'p{platform}-n4-s1000-reference'/'plan.json').read_text())
        for mutation in ('source','image','map','spec'):
            root=out/f'p{platform}-{mutation}-inputs';root.mkdir()
            plan=copy.deepcopy(original)
            for row in plan['files']:
                target=root/row['path'];target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(row['source'],target);row['source']=str(target.resolve())
            name={'source':'fixture.s','image':'program.bin','map':'loadmap.json','spec':'spec.json'}[mutation]
            target=root/name
            if mutation in ('source','image'):target.write_bytes(target.read_bytes()+b'\xea')
            else:
                data=json.loads(target.read_text())
                if mutation=='map':data['entry']['address']+=1
                else:data['contract']['workload']['case']='substituted'
                target.write_text(ex.encoded(data)+'\n')
            # Repin the staged mutation so deeper source/build/spec/map joins
            # must reject it, rather than merely testing the initial copy hash.
            for row in plan['files']:row['sha256']=ex.digest(row['source'])
            if mutation in ('map','spec'):
                candidate=json.loads((root/'candidate.json').read_text())
                candidate['loadmap_sha256' if mutation=='map' else 'spec_sha256']=ex.digest(target)
                (root/'candidate.json').write_text(ex.encoded(candidate)+'\n')
                for row in plan['files']:row['sha256']=ex.digest(row['source'])
            destination=out/f'p{platform}-{mutation}'
            result=ex.run(plan,out/'no-emulator-needed.exe',destination)
            passed=not result['ok'] and result['outcome']=='setup-failure' and 'CIDAR' in result.get('error','') and not (destination/'machine').exists()
            check(passed,'substituted '+mutation+' refuses before machine launch p'+str(platform))
            controls.append({'platform':platform,'mutation':mutation,'ok':passed,'error':result.get('error')})
    report={'schema':'cidar-reverification-v1','ok':True,'pairs':pairs,'controls':controls}
    (out/'checks.json').write_text(ex.encoded(report)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--study',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();run(args.study,args.out.resolve())
