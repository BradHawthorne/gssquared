"""Independent protocol bytes and negative cases for structured observations."""
import struct
from types import SimpleNamespace
from gs2debug.observations import decode_trace_entry,observe

def test_trace_wire_offsets_and_u64_precision():
    blob=bytearray(40)
    struct.pack_into('<Q',blob,0,2**63+7)
    struct.pack_into('<I',blob,8,0x123456)
    blob[12:16]=bytes([0x8d,0x34,0x56,0x12])
    for offset,value in [(16,0x800),(18,0xabcd),(20,0x2345),(22,0x6789),(24,0x1ff),(26,0x400),(28,0x42)]:struct.pack_into('<H',blob,offset,value)
    blob[30:32]=b'\xaa\xbb'  # ABI padding must not leak into eaddr.
    struct.pack_into('<I',blob,32,0xabcdef);struct.pack_into('<H',blob,36,0x1d)
    row=decode_trace_entry(bytes(blob))
    assert row['clock']==str(2**63+7) and row['pc']==0x120800
    assert row['a']==0xabcd and row['direct_page']==0x400 and row['effective_address']==0xabcdef
    assert row['write'] and row['interrupt'] and row['data_bits']==16 and row['operand_bytes']==2
    for blob in (b'',bytes(39),bytes(41)):
        try:decode_trace_entry(blob)
        except ValueError:pass
        else:raise AssertionError('malformed trace record accepted')

class Paused:
    def get_status(self):return SimpleNamespace(execution_mode=2)
    def get_trace(self,ago,count):
        entries=[]
        for cycle in (100,102,3):
            blob=bytearray(40);struct.pack_into('<Q',blob,0,cycle);entries.append(bytes(blob))
        return SimpleNamespace(available=3,entries=entries)

def test_trace_backward_clock_is_not_a_duration():
    result=observe(Paused(),'gs2_trace_read',{})
    assert [row['cycle_delta'] for row in result['entries']]==[None,'2',None]

def test_read_observations_refuse_bad_types_ranges_and_parameters():
    for name,args in [('gs2_trace_read',{'count':True}),('gs2_trace_read',{'count':513}),('gs2_trace_read',{'ago':-1}),('gs2_memory_read',{'domain':'main','address':0xffffff,'length':2}),('gs2_memory_read',{'domain':'main','address':0,'length':1,'extra':0}),('gs2_memory_search',{'domain':'main','address':0,'length':1,'pattern_hex':'a9','mask_hex':'ffff'}),('gs2_video_text_read',{'page':3})]:
        try:observe(Paused(),name,args)
        except ValueError:pass
        else:raise AssertionError('invalid observation accepted')
