"""Owned Rosetta _BlockMoveBytes invocation versus an explicit unroll-four candidate.

No source-library replacement/promotion. Count 1..255, 8-bit A/index, bank-zero
nonoverlapping RAM, ascending copy, pointer/X/SP preservation. Outputs are
independent host bytes; source, pointer and guard preservation are observed.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import experiment as ex


def contract(plan, loads):
    return {'schema':'a2experiment-contract-v2','workload':plan['workload'],'checks':plan['checks'],
            'inputs':[{key:row[key] for key in ('address','size','sha256')} for row in loads],
            'registers':plan['registers'],'preserve_registers':plan['preserve_registers'],
            'profile':plan['profile'],'footprint':plan['footprint']}


def prepare(root, assembler, library, count, source, variant, platform, mutation=None):
    if type(count) is not int or not 1<=count<=255 or source not in (0x1000,0x10fe) or variant not in ('reference','unroll4'):
        raise ValueError('pilot requires count 1..255, declared nonoverlapping RAM placement and known variant')
    root.mkdir(parents=True,exist_ok=False)
    def write(name,value):
        path=root/name
        path.write_text(ex.encoded(value)+'\n' if not isinstance(value,str) else value,encoding='utf-8')
        return ex.digest(path)
    (root/'blockmove.macros.asm').write_bytes(library.read_bytes())
    header="        INCLUDE 'blockmove.macros.asm'\n        ORG $0800\nentry\n"
    if variant=='reference':
        body=f'        _BlockMoveBytes $20,$22,${count:02X}\n'
    else:
        body='        LDY #$00\n'
        bulk=count//4*4
        if bulk:
            body+='bulk\n'+('        LDA ($20),Y\n        STA ($22),Y\n        INY\n'*4)+f'        CPY #${bulk:02X}\n        BNE bulk\n'
        body+='        LDA ($20),Y\n        STA ($22),Y\n        INY\n'*(count-bulk)
    if mutation=='unexpected-write':body+='        STA $4300\n'
    if mutation=='omitted-store':body=body.replace('        STA ($22),Y','        NOP\n        NOP',1)
    if mutation=='clobbered-x':body+='        INX\n'
    write('fixture.s',header+body+'done    NOP\n')
    argv=[str(assembler),'--cpu','65C02','--format','bin','fixture.s','-o','program.bin','-g','--debug-file','program.dbg','--build-json','assembler.json']
    toolhash=ex.digest(assembler)
    result=subprocess.run(argv,cwd=root,capture_output=True,timeout=30)
    (root/'assembler.stdout').write_bytes(result.stdout);(root/'assembler.stderr').write_bytes(result.stderr)
    if result.returncode or ex.digest(assembler)!=toolhash:raise ValueError('pilot assembly failed/tool changed: '+result.stderr.decode(errors='replace'))
    symbols={name:int(address,16) for name,address in re.findall(r'symbol "([^"]+)" type LABEL[^\n]* addr \$([0-9A-Fa-f]+)',(root/'program.dbg').read_text())}
    sources=[{'path':'fixture.s','sha256':ex.digest(root/'fixture.s'),'role':'owned-reference-invocation' if variant=='reference' else 'owned-candidate'},
             {'path':'blockmove.macros.asm','sha256':ex.digest(root/'blockmove.macros.asm'),'role':'owned-library'}]
    buildsha=write('build.json',{'schema':'cidar-build-receipt-v1','tool':{'path':str(assembler),'sha256':toolhash},
        'argv':argv,'exit_code':result.returncode,'inputs':sources,
        'outputs':[{'path':name,'sha256':ex.digest(root/name)} for name in ('program.bin','program.dbg','assembler.json')]})
    values=[(i*73+(i>>2)*19+count*11)&255 for i in range(count)]
    blobs=[('source.bin',source-1,bytes([0x96,*values,0x69])),
           ('pointers.bin',0x20,bytes([source&255,source>>8,0,0x40])),
           ('guard-before.bin',0x3fff,b'\x5a'),('guard-after.bin',0x4000+count,b'\xa5')]
    loads=[]
    for name,address,data in blobs:
        (root/name).write_bytes(data)
        loads.append({'path':name,'address':address,'size':len(data),'sha256':ex.digest(root/name)})
    checks=[{'address':0x4000,'bytes':values},{'address':source,'bytes':values,'role':'preserved-input'},
            {'address':0x20,'bytes':list(blobs[1][2]),'role':'preserved-input'}]
    for address,value in [(source-1,0x96),(source+count,0x69),(0x3fff,0x5a),(0x4000+count,0xa5)]:
        checks.append({'address':address,'bytes':[value],'role':'preserved-input'})
    plan={'schema':'a2experiment-plan-v1','workload':{'id':'prim.system.blockmove.bytes','case':f'n{count}-src{source:04x}'},
        'platform':platform,'files':[],'build':[],'image':{'path':'program.bin','address':0x800,'format':'raw'},
        'entry':symbols['entry'],'target':symbols['done'],'attempts':4096,'registers':{'e':1,'p':0x34,'x':0xa5,'sp':0x1ef},
        'loads':[{key:row[key] for key in ('path','address')} for row in loads],
        'checks':checks,'preserve_registers':['x','sp'],'profile':True,
        'footprint':{'windows':[{'bank':0,'lo':0,'hi':65535}],
            'allowed':[{'address':0x4000,'length':count}],'max_records':count+2,'exact_writes':count}}
    specsha=write('spec.json',{'schema':'cidar-primitive-spec-v1','id':'prim.system.blockmove.bytes','version':1,
        'contract':contract(plan,loads),'lineage':{'status':'owned-reexpression','reference':'Rosetta stdlib/primitives/system/blockmove.macros.asm::_BlockMoveBytes',
        'historical_origin':'curator-reported ABM move_data $0951; no original binary witness joined by this pilot',
        'constraints':'count 1..255; 8-bit A/index; bank-zero disjoint RAM; ascending copy; X/SP/pointers/source/guards preserved; A/Y/status clobbered'}})
    imagesha=ex.digest(root/'program.bin')
    mapsha=write('loadmap.json',{'schema':'cidar-raw-loadmap-v1','address':0x800,'size':(root/'program.bin').stat().st_size,
        'image_sha256':imagesha,'debug':{'path':'program.dbg','sha256':ex.digest(root/'program.dbg')},
        'entry':{'symbol':'entry','address':symbols['entry']},'target':{'symbol':'done','address':symbols['done']}})
    write('candidate.json',{'schema':'cidar-candidate-v1','id':'blockmove-bytes-'+variant,'version':1,
        'spec_sha256':specsha,'image_sha256':imagesha,'loadmap_sha256':mapsha,'build':{'path':'build.json','sha256':buildsha},'sources':sources})
    for path in sorted(root.iterdir()):
        if path.is_file():plan['files'].append({'source':str(path),'path':path.name,'sha256':ex.digest(path)})
    plan['cidar']={'spec':'spec.json','candidate':'candidate.json','loadmap':'loadmap.json'}
    return plan


def run(executable,assembler,library,out,quick=False):
    out.mkdir(parents=True,exist_ok=False)
    cases=[(4,0x1000)] if quick else [(count,source) for count in (1,3,4,5,16,63,64,255) for source in (0x1000,0x10fe)]
    comparisons=[];controls=[]
    for platform in (3,5):
        for count,source in cases:
            records=[]
            for variant in ('reference','unroll4'):
                tag=f'p{platform}-n{count}-s{source:04x}-{variant}'
                plan=prepare(out/(tag+'-inputs'),assembler,library,count,source,variant,platform)
                report=ex.run(plan,executable,out/tag)
                if not report['ok']:raise AssertionError(tag+': '+str(report.get('error',report['outcome'])))
                crossing=max(0,count-(256-(source&255)))
                expected=18*count+1 if variant=='reference' else 57*(count//4)+1+13*(count%4) if count>=4 else 2+13*count
                if int(report['execution']['data']['cycles'])!=expected+crossing:
                    raise AssertionError('independent declared instruction-cost model disagrees: '+tag)
                records.append(report)
            comparison=ex.compare(*records)
            comparison['platform']=platform;comparisons.append(comparison)
            print(ex.encoded({'case':comparison['workload'],'platform':platform,'metrics':comparison['metrics']}),flush=True)
        # Same native fixture with observers disabled must retain bits, cycle and clock interval.
        plan=copy.deepcopy(plan);plan.pop('cidar');plan['profile']=False;plan.pop('footprint')
        unarmed=ex.run(plan,executable,out/f'p{platform}-unarmed')
        if not unarmed['ok'] or unarmed['execution']['data']['cycles']!=records[1]['execution']['data']['cycles'] or unarmed['observations']!=records[1]['observations']:
            raise AssertionError('armed/unarmed measurement or bits changed')
        controls.append({'platform':platform,'control':'armed/unarmed','ok':True})
        badplan=prepare(out/f'p{platform}-unexpected-inputs',assembler,library,4,0x1000,'unroll4',platform,'unexpected-write')
        wrong=ex.run(badplan,executable,out/f'p{platform}-unexpected')
        if wrong['ok'] or wrong['outcome']!='side-effect-or-measurement-mismatch' or not wrong['footprint']['unexpected']:
            raise AssertionError('extra write was not detected')
        controls.append({'platform':platform,'control':'unexpected-write','ok':True})
        badplan=copy.deepcopy(badplan);badplan.pop('cidar');badplan['footprint']['max_records']=1;badplan['footprint'].pop('exact_writes')
        loss=ex.run(badplan,executable,out/f'p{platform}-loss')
        if loss['ok'] or not loss['footprint']['loss_or_unrecognized_records']:raise AssertionError('trace loss was accepted')
        controls.append({'platform':platform,'control':'trace-loss','ok':True})
        for mutation in ('omitted-store','clobbered-x'):
            badplan=prepare(out/f'p{platform}-{mutation}-inputs',assembler,library,4,0x1000,'unroll4',platform,mutation)
            wrong=ex.run(badplan,executable,out/f'p{platform}-{mutation}')
            if wrong['ok'] or wrong['outcome'] not in ('assertion-mismatch','side-effect-or-measurement-mismatch'):
                raise AssertionError('bad candidate accepted: '+mutation)
            controls.append({'platform':platform,'control':mutation,'ok':True})
    summary={'schema':'cidar-blockmove-study-v1','ok':True,'comparisons':comparisons,'controls':controls,
        'curation':{'decision':'retain reference and specialized candidate; no automatic library replacement',
        'bounds':'observed finite workload family; neither proven floor nor ceiling',
        'hardware':'emulator only; no physical-IIgs witness','lineage':'owned Rosetta invocation; historical ABM recovery not independently authenticated',
        'next':'test wider caller ABI/bank/overlap/interrupt and hardware constraints before promotion; small-count and code-size tradeoffs remain explicit'}}
    (out/'study.json').write_text(ex.encoded(summary)+'\n',encoding='utf-8')
    return summary


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--asmiigs',type=Path,required=True)
    parser.add_argument('--library',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--quick',action='store_true')
    args=parser.parse_args()
    run(args.exe.resolve(),args.asmiigs.resolve(),args.library.resolve(),args.out.resolve(),args.quick)
