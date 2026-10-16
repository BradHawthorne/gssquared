"""Structured read-only adapters for the upstream debugger wire API.

The caller owns a paused session. CPU memory and its readability mask are two
bridge calls; no guest instructions run between them under that ownership.
"""
import struct
from .types import EXEC_PAUSED,MEM_MAIN,MEM_MEGAII,MEM_ENSONIQ,MEM_ADBMICRO,MEM_MAIN_RAW,MEM_MEGAII_RAW,MEM_MAIN_READABLE,MEM_MEGAII_READABLE

DOMAINS={'main':MEM_MAIN,'megaii':MEM_MEGAII,'ensoniq':MEM_ENSONIQ,'adbmicro':MEM_ADBMICRO,'main_raw':MEM_MAIN_RAW,'megaii_raw':MEM_MEGAII_RAW}

def integer(params,name,default,minimum,maximum):
    value=params.get(name,default)
    if type(value) is not int or not minimum<=value<=maximum:raise ValueError(f'{name} must be an integer in {minimum}..{maximum}')
    return value

def decode_trace_entry(blob):
    """Decode the protocol's fixed 40-byte LE record, independent of host ABI."""
    if len(blob)!=40:raise ValueError('trace entry must contain exactly 40 bytes')
    cycle,operand,opcode,p,db,pb,pc,a,x,y,sp,d,data,eaddr,flags,_=struct.unpack('<QIBBBBHHHHHHH2xIHH',blob)
    return {'clock':str(cycle),'pc':(pb<<16)|pc,'program_bank':pb,'data_bank':db,'a':a,'x':x,'y':y,'sp':sp,'direct_page':d,'p':p,
            'opcode':opcode,'operand':operand,'operand_bytes':(flags>>1)&3,'data':data,'data_bits':16 if flags&8 else 8,
            'effective_address':eaddr,'interrupt':bool(flags&1),'write':bool(flags&16),'raw_flags':flags}

def schema(properties,required=()):
    return {'type':'object','properties':properties,'required':list(required),'additionalProperties':False}

def number(low,high,default=None):
    return {'type':'integer','minimum':low,'maximum':high,**({'default':default} if default is not None else {})}

MEMORY={'domain':{'type':'string','enum':list(DOMAINS)},'address':number(0,0xffffff),'length':number(1,65536)}
TOOLS=[
    {'name':'gs2_trace_read','description':'Read the upstream instruction trace ring as native register/access fields, oldest to newest. Arm with gs2_tbuf on before running. Clock values are NClock get_cycles at instruction entry, not video position; deltas only within this window. Explicit pause required; no implicit execution.',
     'inputSchema':schema({'ago':number(0,0xffffffff,0),'count':number(1,512,100)})},
    {'name':'gs2_memory_read','description':'Read an explicit debugger memory domain with per-byte readability. main/megaii use side-effect-free probes; unreadable I/O bytes are null. Raw/device domains retain their native meaning and platform restrictions. Explicit pause and exclusive session ownership required for bytes/mask consistency.',
     'inputSchema':schema(MEMORY,('domain','address','length'))},
    {'name':'gs2_memory_search','description':'Masked byte-pattern search over a bounded paused debugger memory snapshot. Excludes unreadable I/O placeholders; reports capped hits and truncation. Hex strings encode bytes; mask bits 1 compare, 0 ignore. Explicit pause and exclusive session ownership required.',
     'inputSchema':schema(dict(MEMORY,pattern_hex={'type':'string','minLength':2,'maxLength':512,'pattern':'^(?:[0-9a-fA-F]{2})+$'},mask_hex={'type':'string','minLength':2,'maxLength':512,'pattern':'^(?:[0-9a-fA-F]{2})+$'},max_hits=number(1,512,16)),('domain','address','length','pattern_hex'))},
    {'name':'gs2_video_text_read','description':'Read upstream VIDEO_TEXT with resolved page/mode/flags, raw row-major character bytes and ASCII-ish lines. High-bit masking is presentation only; raw bytes preserve inverse/flashing glyph codes. Text-page capture does not represent graphics pixels. Explicit pause required.',
     'inputSchema':schema({'page':number(0,2,0),'mode':number(0,2,0)})}
]
TOOL_NAMES={row['name'] for row in TOOLS}

def memory(client,params):
    domain=params.get('domain')
    if not isinstance(domain,str) or domain not in DOMAINS:raise ValueError('unsupported memory domain')
    address=integer(params,'address',None,0,0xffffff);length=integer(params,'length',None,1,65536)
    limit=0x10000 if domain in ('ensoniq','adbmicro') else 0x1000000
    if address+length>limit:raise ValueError('memory window exceeds domain address space')
    # Native server applies narrower platform/device bounds. No bus-read domains.
    raw=client.read_mem(DOMAINS[domain],address,length)
    flags=client.read_mem(MEM_MAIN_READABLE if domain=='main' else MEM_MEGAII_READABLE,address,length) if domain in ('main','megaii') else bytes([1])*length
    return {'schema':'a2debug-memory-v1','ok':True,'domain':domain,'address':address,'length':length,
            'bytes':[byte if flag else None for byte,flag in zip(raw,flags)],'readable':[bool(flag) for flag in flags],
            'unreadable_bytes':flags.count(0),'consistency':'paused; data and readability are separate bridge calls; caller owns session'}

def hex_bytes(params,name,default=None):
    value=params.get(name,default)
    if not isinstance(value,str) or not 2<=len(value)<=512 or len(value)%2 or any(char not in '0123456789abcdefABCDEF' for char in value):raise ValueError(name+' requires 1..256 contiguous hex byte pairs')
    return bytes.fromhex(value)

def observe(client,name,params):
    if name not in TOOL_NAMES:raise ValueError('unknown debugger observation')
    spec=next(row['inputSchema'] for row in TOOLS if row['name']==name)
    if not isinstance(params,dict) or set(params)-set(spec['properties']) or set(spec['required'])-set(params):raise ValueError('unsupported or missing observation parameters')
    if client.get_status().execution_mode!=EXEC_PAUSED:raise ValueError('debugger observations require explicit pause and exclusive session ownership')
    if name=='gs2_memory_read':return memory(client,params)
    if name=='gs2_memory_search':
        pattern=hex_bytes(params,'pattern_hex');mask=hex_bytes(params,'mask_hex','ff'*len(pattern))
        if len(mask)!=len(pattern):raise ValueError('pattern and mask lengths must agree')
        cap=integer(params,'max_hits',16,1,512);window=memory(client,params);data=window['bytes'];hits=[];truncated=False
        for offset in range(len(data)-len(pattern)+1):
            if all(data[offset+i] is not None and (data[offset+i]&mask[i])==(byte&mask[i]) for i,byte in enumerate(pattern)):
                if len(hits)==cap:truncated=True;break
                hits.append(window['address']+offset)
        return {key:value for key,value in window.items() if key not in ('bytes','readable','schema')}|{'schema':'a2debug-search-v1','addresses':hits,'truncated':truncated,'max_hits':cap,'pattern_hex':pattern.hex(),'mask_hex':mask.hex(),'model':'masked search of paused memory; unreadable candidates excluded'}
    if name=='gs2_trace_read':
        ago=integer(params,'ago',0,0,0xffffffff);count=integer(params,'count',100,1,512);window=client.get_trace(ago,count)
        entries=[decode_trace_entry(blob) for blob in window.entries]
        for index,entry in enumerate(entries):
            previous=int(entries[index-1]['clock']) if index else None;now=int(entry['clock'])
            entry['cycle_delta']=str(now-previous) if previous is not None and now>=previous else None
        return {'schema':'a2debug-trace-v1','ok':True,'available':window.available,'returned':len(entries),'ago':ago,'count':count,'order':'oldest-to-newest','clock_model':'NClock.get_cycles at instruction entry; no video phase; first/backward delta unavailable','coverage_model':'retained ring only; overwritten history and unarmed periods are not counted; not complete execution history','entries':entries}
    page=integer(params,'page',0,0,2);mode=integer(params,'mode',0,0,2);text=client.video_text(page,mode)
    return {'schema':'a2debug-text-v1','ok':True,'cols':text.cols,'rows':text.rows,'page':text.page,'mode':text.mode,'flags':text.flags,'bytes':list(text.chars),'lines':text.as_lines(),'model':'native text-page bytes; ASCII-ish high-bit-masked lines; not a graphics raster'}
