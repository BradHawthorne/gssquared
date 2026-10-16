"""Evidence streams finish while machine clocks and owners are still alive."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import Session,check
import outputassert

def run(exe):
    with tempfile.TemporaryDirectory(prefix='a2evidence-lifetime-') as temporary:
        root=Path(temporary)
        for platform in (3,5):
            s=Session(exe,platform);stream=root/f'events{platform}.jsonl'
            try:
                s.code([0x4c,0,8]);s.cmd(f'subscribe on command {stream}')
                if platform==5:s.cmd(f'audio capture doc {root}/doc.jsonl')
                s.cmd('run 1')
            finally:s.close()
            rows=[json.loads(line) for line in stream.read_text().splitlines()]
            check(rows[-1]['event']=='subscription.stopped','quit finalizes event stream before owner teardown')
            if platform==5:check(outputassert.audio(root/'doc.jsonl')['frames']>0,'quit finalizes complete generated-audio evidence')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);run(p.parse_args().exe.resolve())
