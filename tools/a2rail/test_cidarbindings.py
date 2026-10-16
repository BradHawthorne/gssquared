"""Binding refusal controls; no emulator or private corpus required."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cidarbindings as binding


class BindingTests(unittest.TestCase):
    def test_matching_bundle_and_mismatches(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve()
            def write(name, value):
                path=root/name
                path.write_text(json.dumps(value) if not isinstance(value,str) else value)
                return binding.digest(path)
            source=write('code.s','entry lda #1\ndone nop\n')
            image=write('code.bin','abc')
            debug=write('code.dbg','  symbol "entry" type LABEL segment "~MAIN" segnum 1 addr $000800 file 0 line 1\n  symbol "done" type LABEL segment "~MAIN" segnum 1 addr $000802 file 0 line 2\n')
            contract={'schema':'a2experiment-contract-v2','workload':{'id':'copy','case':'one'}}
            spec=write('spec.json',{'schema':'cidar-primitive-spec-v1','id':'copy','version':1,'contract':contract,'lineage':{'status':'owned-reexpression','reference':'fixture'}})
            loadmap=write('map.json',{'schema':'cidar-raw-loadmap-v1','address':2048,'size':3,'image_sha256':image,
                'debug':{'path':'code.dbg','sha256':debug},'entry':{'symbol':'entry','address':2048},'target':{'symbol':'done','address':2050}})
            sources=[{'path':'code.s','sha256':source,'role':'owned-candidate'}]
            receipt=write('build.json',{'schema':'cidar-build-receipt-v1','tool':{'path':str(root/'tool'),'sha256':write('tool','assembler')},
                'argv':[str(root/'tool'),'code.s'],'exit_code':0,'inputs':sources,
                'outputs':[{'path':'code.bin','sha256':image},{'path':'code.dbg','sha256':debug}]})
            candidate={'schema':'cidar-candidate-v1','id':'fixture','version':1,'spec_sha256':spec,
                'image_sha256':image,'loadmap_sha256':loadmap,'build':{'path':'build.json','sha256':receipt},'sources':sources}
            write('candidate.json',candidate)
            declaration={'spec':'spec.json','candidate':'candidate.json','loadmap':'map.json'}
            actual={'address':2048,'size':3,'sha256':image}
            pinned={p.name:binding.digest(p) for p in root.iterdir()}
            args=(root,declaration,pinned,contract,actual,2048,2050)
            self.assertEqual(binding.verify(*args)['primitive']['id'],'copy')
            for field in ('spec_sha256','image_sha256','loadmap_sha256'):
                bad=copy.deepcopy(candidate);bad[field]='0'*64;write('candidate.json',bad)
                pinned['candidate.json']=binding.digest(root/'candidate.json')
                with self.assertRaises(ValueError,msg=field):binding.verify(*args)
            write('candidate.json',candidate);pinned['candidate.json']=binding.digest(root/'candidate.json')
            original_spec=json.loads((root/'spec.json').read_text())
            bad=copy.deepcopy(original_spec);bad['contract']['workload']['case']='different'
            changed=write('spec.json',bad);pinned['spec.json']=changed
            alternate=copy.deepcopy(candidate);alternate['spec_sha256']=changed
            write('candidate.json',alternate);pinned['candidate.json']=binding.digest(root/'candidate.json')
            with self.assertRaisesRegex(ValueError,'contract'):binding.verify(*args)
            write('spec.json',original_spec);pinned['spec.json']=binding.digest(root/'spec.json')
            mapping=json.loads((root/'map.json').read_text());mapping['entry']['address']+=1
            changed=write('map.json',mapping);pinned['map.json']=changed
            alternate=copy.deepcopy(candidate);alternate['loadmap_sha256']=changed
            write('candidate.json',alternate);pinned['candidate.json']=binding.digest(root/'candidate.json')
            with self.assertRaisesRegex(ValueError,'entry'):binding.verify(*args)
            mapping['entry']['address']-=1;write('map.json',mapping);pinned['map.json']=binding.digest(root/'map.json')
            write('candidate.json',candidate);pinned['candidate.json']=binding.digest(root/'candidate.json')
            write('code.s','different source')
            with self.assertRaises(ValueError):binding.verify(*args)

    def test_contract_substitution_refuses(self):
        with self.assertRaises(ValueError):
            binding.validate_declaration({'spec':'s','candidate':'c','loadmap':'m','unknown':True})


if __name__=='__main__':unittest.main(verbosity=2)
