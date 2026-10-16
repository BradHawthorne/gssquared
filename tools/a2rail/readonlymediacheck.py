"""Native launch and nested-batch proof for the explicit media policy."""
import argparse
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug.mcp import Backend
sys.path.insert(0,str(Path(__file__).resolve().parent))
from p0check import check,Session


def run(exe):
    for platform in (3,5):
        with tempfile.TemporaryDirectory(prefix='a2readonly-media-') as temporary:
            root=Path(temporary);marker=root/'marker.bin';marker.write_bytes(b'\xa9')
            args=SimpleNamespace(exe=exe,socket=None,out_dir=root/'session',out_root=None,artifact_root=None,
                platform=platform,config=None,seed=2,rail=[],read_only_media=True)
            backend=Backend(args)
            try:
                backend.open()
                pins=backend.client.agent_command('rail read A2GSPU_RAMDISK')['text']
                check('ramdisk=1' in pins,'managed read-only launch has native RAMDISK pin p'+str(platform))
                policy=backend.client.agent_command('rail read A2GSPU_NO_WRITEBACK')['data']
                check(policy['schema']=='a2writeback-policy-v1' and policy['suppressed'] and not policy['flush_at_spike_end'],'native typed writeback policy proves the launch guard')
                rails={'A2GSPU_LOAD':str(marker)+'@0800','A2GSPU_LOAD_BOOT':'1','A2GSPU_ASSERT':'peek:000800 == $A9'}
                batch=backend.batch({'frames':1,'rails':rails})
                check(batch['ok'] and re.search(r'^A2GSPU PINS: .*\bramdisk=1\b',batch['stdout_excerpt'],re.MULTILINE),'nested batch inherits media policy and passes native assertion')
                for name in ('A2GSPU_RAMDISK','A2GSPU_NO_WRITEBACK','WIZ5_NO_WRITEBACK'):
                    try:backend.batch({'frames':1,'rails':{name:'0'}})
                    except ValueError:check(True,'contradictory nested launch refuses: '+name)
                    else:raise AssertionError('media policy silently overridden: '+name)
            finally:backend.close()
        cases=[({},False,False,False),
            ({'A2GSPU_WRITEBACK':'0','WIZ5_NO_WRITEBACK':'0'},True,True,False),
            ({'WIZ5_WRITEBACK':'0','A2GSPU_NO_WRITEBACK':'0'},True,True,False),
            ({'WIZ5_WRITEBACK':'0'},True,False,True)]
        for rails,requested,suppressed,flush in cases:
            session=Session(exe,platform,rails)
            try:
                for name in ('A2GSPU_WRITEBACK','A2GSPU_NO_WRITEBACK','WIZ5_WRITEBACK','WIZ5_NO_WRITEBACK'):
                    import json
                    policy=json.loads(session.cmd('json rail read '+name))['data']
                    check((policy['requested'],policy['suppressed'],policy['flush_at_spike_end'])==(requested,suppressed,flush),'same typed presence/alias policy via '+name+' p'+str(platform))
            finally:session.close()
    with tempfile.TemporaryDirectory(prefix='a2readonly-refusal-') as temporary:
        root=Path(temporary)/'not-created'
        args=SimpleNamespace(exe=exe,socket=None,out_dir=root,out_root=None,artifact_root=None,
            platform=3,config=None,seed=1,rail=['A2GSPU_RAMDISK=0'],read_only_media=True)
        try:Backend(args).open()
        except ValueError:check(not root.exists(),'contradictory startup media policy refuses before launch/output')
        else:raise AssertionError('contradictory startup media policy accepted')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',type=Path,required=True)
    run(parser.parse_args().exe.resolve())
