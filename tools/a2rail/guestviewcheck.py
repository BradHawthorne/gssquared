"""Independent source/kernel fixture values, with planted corruptions."""
import copy
import hashlib
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import guestview as view

class GuestViews(unittest.TestCase):
    def setUp(self):
        self.data=bytearray(4096)
        def put(at,value,width):self.data[at:at+width]=value.to_bytes(width,'little')
        put(0x20,20,4);put(0x24,14,4)
        for i,value in enumerate(b'hello\n'):self.data[0x300+(14+i)%16]=value
        put(0x100,0x120,3);put(0x120,0x160,3);put(0x160,0x100,3)
        for node,pid,name in ((0x120,1,b'init'),(0x160,7,b'sh')):
            put(node+3,pid,2);put(node+5,2,2);self.data[node+7:node+7+len(name)]=name
        self.data[0x800:0x804]=b'\xea\xea\xea\x60'
        put(0x30,0x200,3);put(0x200,0x240,3);put(0x203,0x802,3);put(0x240,0,3);put(0x243,0x800,3)
        self.layout={'schema':'a2kernel-layout-v1','endian':'little','pointer_bytes':3,
            'binding':{'regions':[{'start':0x800,'length':4,'sha256':hashlib.sha256(self.data[0x800:0x804]).hexdigest()}]},
            'ring':{'capacity':16,'buffer':0x300,'head':0x20,'tail':0x24},
            'tasks':{'head':0x100,'pid_offset':3,'state_offset':5,'comm_offset':7,'comm_length':16},
            'stack':{'frame_pointer':0x30,'return_offset':3,'return_adjust':1},'panic':{'entries':[0x800]}}

    def memory(self):
        return view.Memory({'schema':'a2memory-image-v1','regions':[{'start':0,'bytes':list(self.data),'readable':[True]*len(self.data)}]})

    def test_ring_wrap(self):
        result=view.dmesg(self.memory(),self.layout,{})
        self.assertEqual(result['text'],'hello\n');self.assertEqual(result['bytes'],6)
        self.data[0x20:0x24]=(100).to_bytes(4,'little')
        with self.assertRaisesRegex(view.ViewError,'corrupt ring'):view.dmesg(self.memory(),self.layout,{})

    def test_tasks_and_cycle(self):
        result=view.tasks(self.memory(),self.layout,{})
        self.assertEqual([(t['pid'],t['comm']) for t in result['tasks']],[(1,'init'),(7,'sh')])
        self.data[0x160:0x163]=(0x160).to_bytes(3,'little')
        with self.assertRaisesRegex(view.ViewError,'cyclic'):view.tasks(self.memory(),self.layout,{})

    def test_bounds_and_names(self):
        self.layout['tasks']['max_tasks']=1
        with self.assertRaisesRegex(view.ViewError,'over-limit'):view.tasks(self.memory(),self.layout,{})
        self.layout['tasks']['max_tasks']=1024
        self.data[0x127:0x137]=b'x'*16
        with self.assertRaisesRegex(view.ViewError,'unterminated'):view.tasks(self.memory(),self.layout,{})

    def test_unreadable_and_overlap(self):
        row={'start':0,'bytes':[0,1,2],'readable':[True,False,True]}
        memory=view.Memory({'schema':'a2memory-image-v1','regions':[row]})
        with self.assertRaisesRegex(view.ViewError,'unreadable'):memory.read(1,1)
        with self.assertRaisesRegex(view.ViewError,'not captured'):memory.read(3,1)
        with self.assertRaisesRegex(view.ViewError,'overlapping'):view.Memory({'schema':'a2memory-image-v1','regions':[row,row]})

    def source(self):
        debug='.debug_info\n producer "fixture"\n.debug_files\n file 0 "main.c"\n.debug_lines\n segment "code" num 1\n addr $000000 file 0 line 7 column 1 stmt\n addr $000002 file 0 line 8 column 1 stmt\n.debug_symbols\n'
        placement={'schema':'a2source-placement-v1','segments':{'1':{'start':0x800,'length':4,'sha256':hashlib.sha256(self.data[0x800:0x804]).hexdigest()}}}
        return view.normalize_rosetta(debug,placement)

    def test_compiler_marker_chain(self):
        assembly=Path('compiler fixture.s').resolve();filename='source with spaces.c'
        text='; heading\n;@rosetta-source-v1 line=42 filehex='+filename.encode().hex()+'\n LDA #$42\n;@rosetta-source-v1 line=0 filehex=-\n RTL\n'
        debug='.debug_files\n file 0 '+__import__('json').dumps(str(assembly))+'\n.debug_lines\n segment "code" num 1\n addr $0 file 0 line 3 column 1 stmt\n addr $2 file 0 line 5 column 1 stmt\n'
        placement={'schema':'a2source-placement-v1','segments':{'1':{'start':0x800,'length':4,'sha256':hashlib.sha256(self.data[0x800:0x804]).hexdigest()}}}
        result=view.normalize_rosetta(debug,placement,[(assembly,text)])
        rows=result['regions'][0]['lines'];self.assertEqual((rows[0]['file'],rows[0]['line']),(filename,42));self.assertEqual(rows[1]['file'],str(assembly))
        self.assertEqual(result['compiler_assemblies'][0]['sha256'],hashlib.sha256(text.encode()).hexdigest())
        self.assertFalse(result['source_text_verified'])
        with self.assertRaisesRegex(view.ViewError,'marker'):view.normalize_rosetta(debug,placement,[(assembly,text.replace('line=42','line=wrong'))])
        with self.assertRaisesRegex(view.ViewError,'matched no'):view.normalize_rosetta(debug,placement,[(Path('other.s'),text)])

    def test_source_binding(self):
        source=self.source();result=view.source_lookup(source,0x803,self.memory())
        self.assertEqual(result['locations'][0]['line'],8);self.assertTrue(result['binding_verified'])
        self.assertFalse(view.source_lookup(source,0x803)['binding_verified'])
        self.data[0x800]^=1
        with self.assertRaisesRegex(view.ViewError,'binding'):view.source_lookup(source,0x803,self.memory())

    def test_unmapped_and_corrupt_source(self):
        source=self.source()
        result=view.source_lookup(source,0x900,self.memory())
        self.assertEqual(result['locations'],[]);self.assertFalse(result['binding_verified'])
        source['regions'][0]['lines'][0]['offset']=-1
        with self.assertRaises(view.ViewError):view.source_lookup(source,0x800,self.memory())

    def test_stack_and_panic(self):
        result=view.stack(self.memory(),self.layout,{},source_map=self.source())
        self.assertEqual([row['resume_pc'] for row in result['frames']],[0x803,0x801])
        cpu={'schema':'a2inspect-v1','kind':'cpu','data':{'registers':{'pc':0x800}}}
        self.assertTrue(view.panic(self.memory(),self.layout,{},cpu)['matched'])
        cpu['data']['registers']['pc']=0x900
        self.assertFalse(view.panic(self.memory(),self.layout,{},cpu)['matched'])
        self.data[0x240:0x243]=(0x200).to_bytes(3,'little')
        with self.assertRaisesRegex(view.ViewError,'cyclic'):view.stack(self.memory(),self.layout,{})

    def test_kernel_binding(self):
        unbound=copy.deepcopy(self.layout);unbound.pop('binding')
        with self.assertRaisesRegex(view.ViewError,'binding'):view.tasks(self.memory(),unbound,{})
        self.data[0x800]^=1
        with self.assertRaisesRegex(view.ViewError,'binding'):view.dmesg(self.memory(),self.layout,{})

if __name__=='__main__':unittest.main(verbosity=2)
