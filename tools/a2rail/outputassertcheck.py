"""Independent content fixtures: filters, CRC, changed pixels, muted/pitched PCM."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
import outputassert as oa

def png(path,w,h,rgba):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(b''.join(b'\0'+rgba[y*w*4:(y+1)*w*4] for y in range(h))))+chunk(b'IEND',b''))

def audio(path,values,period=32):
    n=len(values)//2
    rows=[{'schema':'a2audio-v1','event':'started','source':'doc'},
          {'schema':'a2audio-v1','event':'samples','sequence':0,'frame_offset':'0','channels':2,'frames':n,
           'end_tick':str(n*period),'ticks_per_sample':period,'tick_domain':'c14m','sample_rate_hz':None,'samples':values},
          {'schema':'a2audio-v1','event':'stopped','status':{'complete':True,'active':False,'source':'doc','frames':str(n),'dropped':'0','error':''}}]
    path.write_text('\n'.join(json.dumps(row) for row in rows)+'\n')

class Outputs(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def test_image_tolerance_and_wrong_region(self):
        a,b=self.root/'a.png',self.root/'b.png';png(a,2,1,bytes([10,20,30,255,40,50,60,255]));png(b,2,1,bytes([11,20,30,255,40,50,60,255]))
        self.assertFalse(oa.image(a,b)['pass']);self.assertTrue(oa.image(a,b,1)['pass'])
        result=oa.image(a,b);self.assertEqual(result['bad_pixel_bounds_inclusive'],[0,0,0,0]);self.assertEqual(result['bad_pixel_fraction'],.5)
        broken=bytearray(b.read_bytes());broken[-5]^=1;b.write_bytes(broken)
        with self.assertRaises(oa.EvidenceError):oa.png(b)
    def test_png_filters_known_gray_bytes(self):
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        # Two grayscale rows [10,20] / [30,40]; residuals are independently
        # worked examples for Sub, Up, Average and Paeth predictors.
        expected=bytes(component for gray in (10,20,30,40) for component in (gray,gray,gray,255))
        for mode,residuals in ((1,[30,10]),(2,[20,20]),(3,[25,15]),(4,[20,10])):
            path=self.root/f'filter{mode}.png';raw=bytes([0,10,20,mode,*residuals])
            path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
            self.assertEqual(oa.png(path)['rgba'],expected)
    def test_audio_content_and_timing(self):
        a,b=self.root/'a.jsonl',self.root/'b.jsonl';wave=[-1000,1000,0,0,1000,-1000,0,0]*4
        audio(a,wave);audio(b,wave);self.assertTrue(oa.sound(a,b)['pass'])
        audio(b,[v if i%2==0 else 0 for i,v in enumerate(wave)]);self.assertFalse(oa.sound(a,b)['pass'])
        audio(b,wave,64);self.assertFalse(oa.sound(a,b)['pass'])
        rows=b.read_text().splitlines();rows[-1]=rows[-1].replace('"complete": true','"complete": false');b.write_text('\n'.join(rows))
        with self.assertRaises(oa.EvidenceError):oa.sound(a,b)
    def test_glyphs_and_ambiguity(self):
        image,font=self.root/'text.png',self.root/'font.json'
        png(image,2,2,bytes([255,255,255,255,0,0,0,255]*2))
        font.write_text(json.dumps({'schema':'a2bitmap-font-v1','width':1,'height':2,'glyphs':{'A':['#','#'],' ':['.','.']}}))
        self.assertEqual(oa.text(image,font,2,1)['text'],'A ')
        font.write_text(json.dumps({'schema':'a2bitmap-font-v1','width':1,'height':2,'glyphs':{' ':['.','.']}}))
        self.assertFalse(oa.text(image,font,2,1)['pass'])
        font.write_text(json.dumps({'schema':'a2bitmap-font-v1','width':1,'height':2,'glyphs':{'A':['#','#'],'B':['#','#']}}))
        with self.assertRaises(oa.EvidenceError):oa.text(image,font,2,1)

if __name__=='__main__':unittest.main()
