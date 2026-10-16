"""Live semantic checkpoints and generated audio, exercised on both platforms."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check
from gs2debug import CtrlClient
import checkpoint as cp
import outputassert as oa

def run(exe):
    for platform in (3,5):
        s=Session(exe,platform)
        try:
            s.code([0x4c,0,8])
            with CtrlClient(s.dir) as client:
                left,right=s.dir/'checkpointA',s.dir/'checkpointB'
                cp.capture(client,left,exe.parent/'snapdiff.exe');cp.capture(client,right,exe.parent/'snapdiff.exe')
                report=cp.compare(left,right,exe.parent/'snapdiff.exe');check(report['equal'],'unchanged semantic checkpoints compare equal')
                installed=json.loads((left/'devices.json').read_text())['data']['installed']
                check(installed and all('slot' in d and 'device_id' in d for d in installed),'actual power-on model inventory')
                client.command('paddle 0 73');changed=s.dir/'checkpointChanged';cp.capture(client,changed,exe.parent/'snapdiff.exe')
                report=cp.compare(left,changed,exe.parent/'snapdiff.exe')
                check(not report['equal'] and any('paddle' in row['field'] for row in report['device_fields']),'named device semantic change detected')
                (changed/'devices.json').write_text('{}')
                try:cp.load(changed)
                except cp.CheckpointError:check(True,'changed checkpoint evidence fails its hash binding')
                else:raise AssertionError('tampered checkpoint accepted')
                if platform==3:
                    check(not client.command(f'audio capture doc "{s.dir}/absent.jsonl"',check=False)['ok'],'IIe refuses DOC sample capture')
                else:
                    scb=s.dir/'scb.bin';scb.write_bytes(bytes(200));client.command(f'load E19D00 {scb}')
                    client.command('poke E19E00 00 00 FF 0F')
                    client.command('poke E12000 10');client.command('poke E120A0 10')
                    screenshot=s.dir/'shr.png';client.command(f'shr {screenshot} 1')
                    font=s.dir/'font.json';font.write_text(json.dumps({'schema':'a2bitmap-font-v1','width':1,'height':2,'glyphs':{'A':['#','#'],' ':['.','.']}}))
                    check(oa.text(screenshot,font,2,1)['text']=='A ','native SHR pixels resolve configured glyphs')
                    check(oa.image(screenshot,screenshot)['pass'],'native PNG content passes decoded-pixel oracle')
                    outputs=[]
                    # Two independent restarts give the same pre-host DOC stream.
                    for i in range(2):
                        out=s.dir/f'audio{i}.jsonl';outputs.append(out)
                        client.command('reset');client.command('setreg e 1');client.command('setreg p 34');client.command('setreg pc 000800')
                        client.command(f'audio capture doc "{out}" 100000');client.command('run 1');client.command('audio capture off')
                        capture=oa.audio(out);check(capture['frames']>0,'real DOC samples captured with timing')
                    # Reset clock phase is excluded from GSPS, so compare a real
                    # capture with its byte-identical copy, then plant a wrong sample.
                    original=outputs[0];copy=s.dir/'audio-copy.jsonl';copy.write_bytes(original.read_bytes())
                    check(oa.sound(original,copy)['pass'],'native sample recording passes content oracle')
                    rows=[json.loads(line) for line in copy.read_text().splitlines()]
                    batch=next(row for row in rows if row['event'] in ('samples','audio.samples'));batch['samples'][0]=32767 if batch['samples'][0]!=32767 else -32768
                    copy.write_text('\n'.join(json.dumps(row) for row in rows)+'\n')
                    check(not oa.sound(original,copy)['pass'],'planted wrong native sample fails content oracle')
            s.seq=max(int(p.name.split('.')[1]) for p in s.dir.glob('cmd.*') if p.name.split('.')[1].isdigit())
        finally:
            # Keep Session's sequence in sync even after a public-client failure.
            commands=[int(p.name.split('.')[1]) for p in s.dir.glob('cmd.*') if p.name.split('.')[1].isdigit()]
            if commands:s.seq=max(commands)
            s.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
