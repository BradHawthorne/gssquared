"""Checked source/kernel views over explicit, side-effect-free memory artifacts.

Layouts and segment placements are inputs; no product addresses are compiled in.
Use --help for commands. Corrupt/unreadable/ambiguous inputs exit 2.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import re
import sys


class ViewError(ValueError):
    pass


def number(value,label,maximum=0xffffff):
    if type(value) is not int or not 0<=value<=maximum:
        raise ViewError(f'invalid {label}')
    return value


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


class Memory:
    def __init__(self,document):
        if not isinstance(document,dict):raise ViewError('memory artifact must be an object')
        if document.get('schema')=='a2inspect-v1' and document.get('kind')=='memory':
            rows=document['data']['regions']
        elif document.get('schema')=='a2memory-image-v1':rows=document['regions']
        else:raise ViewError('unknown memory artifact schema')
        self.regions=[]
        for row in rows:
            start=number(row['start'],'region start')
            raw=row['bytes'];valid=row['readable']
            if not isinstance(raw,list) or not raw or len(raw)!=len(valid) or start+len(raw)>0x1000000:
                raise ViewError('invalid memory region length')
            if any(type(v) is not int or not 0<=v<=255 for v in raw) or any(type(v) is not bool for v in valid):
                raise ViewError('invalid memory values/readability')
            self.regions.append((start,bytes(raw),valid))
        self.regions.sort()
        for a,b in zip(self.regions,self.regions[1:]):
            if a[0]+len(a[1])>b[0]:raise ViewError('overlapping memory regions')

    def read(self,address,length):
        number(address,'address');number(length,'read length',0x1000000)
        if address+length>0x1000000:raise ViewError('read exceeds 24-bit space')
        out=bytearray()
        for start,data,valid in self.regions:
            cursor=address+len(out)
            if start<=cursor<start+len(data):
                offset=cursor-start;n=min(length-len(out),len(data)-offset)
                if not all(valid[offset:offset+n]):raise ViewError(f'unreadable guest memory at {cursor:06X}')
                out.extend(data[offset:offset+n])
                if len(out)==length:return bytes(out)
        if not length:return b''
        raise ViewError(f'memory not captured at {address+len(out):06X}')

    def uint(self,address,width):
        if type(width) is not int or width not in (1,2,3,4,8):raise ViewError('unsupported integer width')
        return int.from_bytes(self.read(address,width),'little')


class LiveMemory:
    """A paused CTRL observation epoch, cached in checked 4K pages."""
    def __init__(self,client):
        self.client=client;self.pages={};self.identity=client.inspect('cpu')
        if self.identity.get('surface')!='ctrl':raise ViewError('live guest views require a headless CTRL observation epoch')
        self.cycles=self.identity['cycles']

    def read(self,address,length):
        number(address,'address');number(length,'length',0x1000000)
        if address+length>0x1000000:raise ViewError('read exceeds 24-bit space')
        result=bytearray()
        while len(result)<length:
            cursor=address+len(result);base=cursor&~4095
            if base not in self.pages:
                data=self.client.inspect('memory',f'{base:06X}','4096')
                if data.get('surface')!='ctrl' or data['cycles']!=self.cycles:raise ViewError('machine advanced during guest observation')
                self.pages[base]=Memory(data)
            count=min(length-len(result),base+4096-cursor)
            result.extend(self.pages[base].read(cursor,count))
        return bytes(result)

    uint=Memory.uint

    def finish(self):
        after=self.client.inspect('cpu')
        if after.get('surface')!='ctrl' or after['cycles']!=self.cycles:raise ViewError('machine advanced during guest observation')
        return {'surface':'ctrl','cycles':self.cycles,'captured_pages':len(self.pages)}


def symbols(path):
    result={}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip():continue
        match=re.fullmatch(r'([0-9a-fA-F]+)\s+\S\s+(\S+)',line.strip())
        if not match:raise ViewError('unsupported System.map row')
        address=number(int(match[1],16),'symbol address');name=match[2]
        if name in result and result[name]!=address:raise ViewError('ambiguous symbol: '+name)
        result[name]=address
    return result


def address(spec,table):
    if isinstance(spec,dict):
        name=spec.get('symbol')
        if name not in table:raise ViewError('missing layout symbol: '+str(name))
        value=table[name]+number(spec.get('offset',0),'symbol offset')
    else:value=spec
    return number(value,'layout address')


def kernel_layout(document):
    if document.get('schema')!='a2kernel-layout-v1' or document.get('endian')!='little':
        raise ViewError('unsupported kernel layout schema/endianness')
    if document.get('pointer_bytes') not in (2,3,4):raise ViewError('unsupported kernel pointer width')
    return document


def bound_layout(memory,document):
    document=kernel_layout(document)
    regions=document.get('binding',{}).get('regions',[])
    if not regions:raise ViewError('kernel layout requires an immutable code-byte binding')
    for region in regions:
        start=number(region['start'],'kernel binding start');length=number(region['length'],'kernel binding length',0x1000000)
        if not length or not re.fullmatch('[0-9a-f]{64}',region['sha256']):raise ViewError('invalid kernel code binding')
        if hashlib.sha256(memory.read(start,length)).hexdigest()!=region['sha256']:raise ViewError('kernel layout code binding mismatch')
    return document


def layout_provenance(layout):
    return {'layout_sha256':hashlib.sha256(json.dumps(layout,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'binding_verified':True}


def dmesg(memory,layout,table):
    layout=bound_layout(memory,layout);cfg=layout['ring']
    cap=number(cfg['capacity'],'ring capacity',65536)
    if not cap:raise ViewError('empty ring')
    width=cfg.get('index_bytes',4)
    if width not in (1,2,4):raise ViewError('invalid ring index width')
    if cap>=(1<<(width*8)):raise ViewError('ring capacity is ambiguous at this counter width')
    head=memory.uint(address(cfg['head'],table),width);tail=memory.uint(address(cfg['tail'],table),width)
    count=(head-tail)&((1<<(width*8))-1)
    if count>cap:raise ViewError('corrupt ring head/tail distance')
    data=memory.read(address(cfg['buffer'],table),cap)
    raw=bytes(data[(tail+i)%cap] for i in range(count))
    encoding=cfg.get('encoding','utf-8')
    if encoding not in ('utf-8','ascii'):raise ViewError('unsupported ring text encoding')
    try:text=raw.decode(encoding)
    except UnicodeDecodeError as exc:raise ViewError('invalid ring text encoding') from exc
    return {'schema':'a2kernel-log-v1','model':'configured-byte-ring','head':head,'tail':tail,
            'capacity':cap,'bytes':count,'text':text,'sha256':hashlib.sha256(raw).hexdigest(),**layout_provenance(layout)}


def tasks(memory,layout,table):
    layout=bound_layout(memory,layout);cfg=layout['tasks'];width=layout['pointer_bytes']
    head=address(cfg['head'],table);link=number(cfg.get('list_offset',0),'list offset',65535)
    next_off=number(cfg.get('next_offset',0),'next offset',65535)
    bound=number(cfg.get('max_tasks',1024),'max tasks',4096)
    if not bound:raise ViewError('max_tasks must be positive')
    include=cfg.get('include_head',False)
    if type(include) is not bool:raise ViewError('include_head must be boolean')
    node=head if include else memory.uint(head+next_off,width)
    rows=[];seen=set()
    while node!=head or (include and not rows):
        number(node,'task pointer')
        if not node or node in seen or len(rows)>=bound:raise ViewError('corrupt/cyclic/over-limit task list')
        seen.add(node)
        if node<link:raise ViewError('task container underflow')
        base=node-link
        pid=memory.uint(base+number(cfg['pid_offset'],'pid offset',65535),cfg.get('pid_bytes',2))
        state=memory.uint(base+number(cfg['state_offset'],'state offset',65535),cfg.get('state_bytes',2))
        length=number(cfg['comm_length'],'comm length',256)
        if not length:raise ViewError('empty task name field')
        raw=memory.read(base+number(cfg['comm_offset'],'comm offset',65535),length)
        if b'\0' not in raw:raise ViewError('unterminated task name')
        try:name=raw.split(b'\0',1)[0].decode('utf-8')
        except UnicodeDecodeError as exc:raise ViewError('invalid task name') from exc
        rows.append({'address':base,'list_node':node,'pid':pid,'comm':name,'state_raw':str(state)})
        node=memory.uint(node+next_off,width)
    return {'schema':'a2kernel-tasks-v1','model':'configured-circular-intrusive-list','tasks':rows,'complete':True,**layout_provenance(layout)}


def stack(memory,layout,table,initial_frame=None,source_map=None):
    layout=bound_layout(memory,layout);cfg=layout['stack'];width=layout['pointer_bytes']
    frame=number(initial_frame,'frame pointer') if initial_frame is not None else memory.uint(address(cfg['frame_pointer'],table),width)
    next_offset=number(cfg.get('next_offset',0),'frame link offset',65535)
    return_offset=number(cfg['return_offset'],'frame return offset',65535)
    adjust=cfg.get('return_adjust',0)
    if type(adjust) is not int or not -3<=adjust<=3:raise ViewError('invalid saved return adjustment')
    bound=number(cfg.get('max_frames',64),'frame bound',1024)
    if not bound:raise ViewError('empty frame bound')
    frames=[];seen=set()
    while frame:
        if frame in seen or len(frames)>=bound:raise ViewError('corrupt/cyclic/over-limit frame chain')
        seen.add(frame)
        saved=memory.uint(frame+return_offset,cfg.get('return_bytes',3))
        pc=number(saved+adjust,'resume PC')
        row={'frame':frame,'saved_return_pc':saved,'resume_pc':pc}
        if source_map is not None:
            try:row['source']=source_lookup(source_map,pc,memory)
            except ViewError as exc:row['source_error']=str(exc)
        frames.append(row);frame=memory.uint(frame+next_offset,width);number(frame,'next frame')
    return {'schema':'a2stack-v1','model':'configured-frame-chain','frames':frames,'complete':True,**layout_provenance(layout)}


def panic(memory,layout,table,cpu,source_map=None):
    if cpu.get('schema')=='a2inspect-v1' and cpu.get('kind')=='cpu':state=cpu['data']
    elif cpu.get('schema')=='a2ctrl-reply-v2' and cpu.get('data',{}).get('schema')=='a2cpu-v1':state=cpu['data']['state']
    else:raise ViewError('panic view requires native CPU state')
    layout=bound_layout(memory,layout);cfg=layout['panic'];pc=number(state['registers']['pc'],'panic PC')
    entries=[address(item,table) for item in cfg['entries']]
    matched=pc in entries
    result={'schema':'a2panic-v1','model':'configured-panic-entry-and-frame-chain','pc':pc,'matched':matched,**layout_provenance(layout)}
    if matched:
        if 'frame_register' in cfg and cfg['frame_register'] not in state['registers']:raise ViewError('CPU frame register unavailable')
        frame=state['registers'].get(cfg['frame_register']) if 'frame_register' in cfg else None
        result['stack']=stack(memory,layout,table,frame,source_map)
        if 'ring' in layout:result['log']=dmesg(memory,layout,table)
    return result


def compiler_locations(assemblies):
    """Explicit compiler output files; never guess from a C filename or basename."""
    import os
    from bisect import bisect_right
    documents={};provenance=[]
    for path,content in assemblies:
        raw=content if isinstance(content,bytes) else content.encode('utf-8')
        text=raw.decode('utf-8')
        key=os.path.normcase(str(Path(path).resolve()))
        if key in documents:raise ViewError('duplicate compiler assembly identity')
        entries=[];location=None;physical=text.splitlines()
        for index,line in enumerate(physical,1):
            line=line.strip()
            if not line.startswith(';@rosetta-source-'):continue
            match=re.fullmatch(r';@rosetta-source-v1 line=(\d+) filehex=([0-9a-f]+|-)',line)
            if not match:raise ViewError('malformed/unsupported compiler source marker')
            source_line=int(match[1]);encoded=match[2]
            if source_line==0 and encoded=='-':location=None
            elif 0<source_line<=0x7fffffff and encoded!='-' and len(encoded)<=32768:
                try:filename=bytes.fromhex(encoded).decode('utf-8')
                except (ValueError,UnicodeError) as exc:raise ViewError('invalid compiler source filename') from exc
                if not filename or any(c in filename for c in ('\0','\n','\r')):raise ViewError('invalid compiler source filename')
                location={'file':filename,'line':source_line,'column':0,'column_model':'unavailable-statement-line-only'}
            else:raise ViewError('invalid compiler source marker location/reset')
            entries.append((index,location))
        if not entries:raise ViewError('compiler assembly has no source markers')
        documents[key]=(physical,entries)
        provenance.append({'path':str(path),'sha256':hashlib.sha256(raw).hexdigest(),'hash_model':'provided UTF8 bytes; str input reencoded as UTF8','marker_model':'rosetta-source-v1','source_text_verified':False})
    def transform(row):
        key=os.path.normcase(str(Path(row['file']).resolve()))
        if key not in documents:return row
        physical,entries=documents[key]
        if not 1<=row['line']<=len(physical):raise ViewError('assembler source row outside supplied compiler output')
        index=bisect_right([item[0] for item in entries],row['line'])-1
        location=entries[index][1] if index>=0 else None
        if location is None:return row
        return {**row,**location,'origin_assembly':{'file':row['file'],'line':row['line'],'column':row['column']}}
    return transform,provenance

def normalize_rosetta(debug,placement,compiler_assemblies=()):
    raw_debug=debug if isinstance(debug,bytes) else debug.encode('utf-8');debug=raw_debug.decode('utf-8')
    if placement.get('schema')!='a2source-placement-v1':raise ViewError('unknown source placement schema')
    files={};lines={};section='';segment=None;ignored=set()
    for raw in debug.splitlines():
        text=raw.strip()
        if not text:continue
        if text.startswith('.'):
            section=text
            if section not in ('.debug_info','.debug_files','.debug_lines'):ignored.add(section)
            continue
        if section=='.debug_files':
            match=re.fullmatch(r'file (\d+) (".*")',text)
            if not match:raise ViewError('unsupported Rosetta file row')
            index=int(match[1])
            if index in files:raise ViewError('duplicate debug file')
            files[index]=json.loads(match[2])
        elif section=='.debug_lines':
            match=re.fullmatch(r'segment (".*") num (\d+)',text)
            if match:segment=match[2];lines.setdefault(segment,[]);continue
            match=re.fullmatch(r'addr \$([0-9a-fA-F]+) file (\d+) line (\d+) column (\d+)(?: stmt)?',text)
            if not match or segment is None:raise ViewError('unsupported Rosetta line row')
            offset,index,line,column=int(match[1],16),int(match[2]),int(match[3]),int(match[4])
            if index not in files or line<1:raise ViewError('invalid source file/line')
            lines[segment].append({'offset':offset,'file':files[index],'line':line,'column':column})
    transform,provenance=compiler_locations(compiler_assemblies)
    if compiler_assemblies:
        lines={segment:[transform(row) for row in rows] for segment,rows in lines.items()}
        if not any('origin_assembly' in row for rows in lines.values() for row in rows):raise ViewError('compiler markers matched no assembler source rows')
    regions=[]
    for segment,rows in lines.items():
        if not rows:continue
        spec=placement['segments'].get(segment)
        if not spec:raise ViewError('missing placement for debug segment '+segment)
        start=number(spec['start'],'segment start');length=number(spec['length'],'segment length',0x1000000)
        digest=spec['sha256']
        if not length or start+length>0x1000000 or not re.fullmatch('[0-9a-f]{64}',digest):raise ViewError('invalid segment binding')
        if any(row['offset']>=length for row in rows):raise ViewError('source row exceeds segment')
        regions.append({'segment':segment,'start':start,'length':length,'sha256':digest,'lines':sorted(rows,key=lambda r:r['offset'])})
    if not regions:raise ViewError('no source line records')
    return {'schema':'a2source-map-v1','model':'explicit-segment-placement','regions':regions,
            'compiler_assemblies':provenance,'source_text_verified':False,
            'ignored_sections':sorted(ignored),'debug_sha256':hashlib.sha256(raw_debug).hexdigest(),
            'debug_hash_model':'provided UTF8 bytes; str input reencoded as UTF8'}


def source_lookup(document,pc,memory=None):
    if document.get('schema')!='a2source-map-v1':raise ViewError('unknown source map schema')
    number(pc,'PC');candidates=[];verified=False
    for region in document['regions']:
        start=number(region['start'],'segment start');length=number(region['length'],'segment length',0x1000000)
        if not length or start+length>0x1000000 or not re.fullmatch('[0-9a-f]{64}',region['sha256']):raise ViewError('invalid source binding')
        for row in region['lines']:
            number(row['offset'],'source offset',length-1)
            if not number(row['line'],'source line',0x7fffffff) or not isinstance(row['file'],str):raise ViewError('invalid source location')
        if not start<=pc<start+length:continue
        if memory is not None and hashlib.sha256(memory.read(start,length)).hexdigest()!=region['sha256']:
            raise ViewError('source binding does not match live/captured bytes')
        offset=pc-start;eligible=[r for r in region['lines'] if r['offset']<=offset]
        if not eligible:continue
        nearest=max(r['offset'] for r in eligible)
        verified=memory is not None
        for row in eligible:
            if row['offset']==nearest:candidates.append(dict(row,segment=region['segment'],instruction_offset=offset-nearest))
    return {'schema':'a2source-location-v1','pc':pc,'locations':candidates,'ambiguous':len(candidates)>1,
            'binding_verified':verified,'binding_reason':None if verified else 'PC not mapped' if not candidates else 'no memory artifact supplied'}


def contract():
    return {
        'schema':'a2guestview-contract-v1',
        'models':{'log':'monotonic unsigned head/tail counters over a byte ring; not a decoder for every Linux printk version',
                  'tasks':'circular intrusive list; explicit offsets and pointer width',
                  'stack':'linked frame chain; explicit saved-PC width and adjustment',
                  'source':'Rosetta assembler line-sidecar subset; optional rosetta-source-v1 C markers; explicit segment placements and byte hashes'},
        'bounds':{'address_bits':24,'ring_capacity_max':65536,'tasks_max':4096,'frames_max':1024,'task_name_max':256},
        'address':'integer or {symbol: name, offset: nonnegative integer}; --symbols accepts 24-bit System.map rows',
        'kernel_layout':{'schema':'a2kernel-layout-v1','endian':'little','pointer_bytes':'2|3|4',
            'binding':{'regions':[{'start':'immutable kernel code address','length':'positive byte count','sha256':'lowercase SHA256; must match observed memory'}]},
            'ring':{'buffer':'address','capacity':'positive byte count less than counter modulus','head':'address of head counter','tail':'address of tail counter','index_bytes':'1|2|4; default 4','encoding':'utf-8|ascii; default utf-8'},
            'tasks':{'head':'address of sentinel list node','list_offset':'list node offset within task; default 0','next_offset':'next pointer offset in node; default 0','include_head':'bool; default false',
                'pid_offset':'offset in task','pid_bytes':'integer width; default 2','state_offset':'offset in task','state_bytes':'integer width; default 2','comm_offset':'offset in task','comm_length':'bounded NUL-terminated UTF-8 field length','max_tasks':'default 1024'},
            'stack':{'frame_pointer':'address of first-frame pointer, unless --frame overrides','next_offset':'offset in frame; default 0','return_offset':'offset in frame','return_bytes':'saved PC width; default 3','return_adjust':'-3..3; default 0','max_frames':'default 64'},
            'panic':{'entries':'array of entry addresses, e.g. symbols panic/oops_begin','frame_register':'optional native CPU register containing the first frame pointer'}},
        'memory_image':{'schema':'a2memory-image-v1','regions':[{'start':'24-bit address','bytes':'array of u8','readable':'parallel array of bool'}]},
        'source_placement':{'schema':'a2source-placement-v1','segments':{'SEGMENT_NUMBER':{'start':'24-bit runtime address','length':'byte count','sha256':'lowercase SHA256 of the segment bytes'}}},
        'live':'--ctrl uses the Python CtrlClient lease and coherent, paused CTRL memory reads. Co-pilot is refused. Artifacts are retained in the session directory.',
        'limitations':['layout comes from the selected kernel/ABI; examples are not a kernel profile','no implicit modern-Linux printk structure assumptions','DWARF not yet supported','compiler C-line metadata must be provided by its producer']}


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    sub.add_parser('schema')
    for kind in ('dmesg','tasks','stack','panic'):
        p=sub.add_parser(kind);memory=p.add_mutually_exclusive_group(required=True)
        memory.add_argument('--memory',type=Path);memory.add_argument('--ctrl',type=Path)
        p.add_argument('--owner');p.add_argument('--layout',required=True,type=Path);p.add_argument('--symbols',type=Path)
        if kind in ('stack','panic'):p.add_argument('--map',type=Path)
        if kind=='stack':p.add_argument('--frame',type=lambda s:int(s,16))
        if kind=='panic':p.add_argument('--cpu',type=Path)
    p=sub.add_parser('normalize-rosetta');p.add_argument('--debug',required=True,type=Path);p.add_argument('--placement',required=True,type=Path)
    p.add_argument('--compiler-assembly',action='append',default=[],type=Path)
    p=sub.add_parser('source');p.add_argument('--map',required=True,type=Path);p.add_argument('--pc',required=True,type=lambda s:int(s,16));p.add_argument('--memory',type=Path)
    for child in sub.choices.values():child.add_argument('--out',type=Path)
    args=parser.parse_args()
    try:
        with ExitStack() as resources:
            memory=None
            if getattr(args,'ctrl',None):
                sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
                from gs2debug import CtrlClient
                client=resources.enter_context(CtrlClient(args.ctrl,owner=args.owner))
                memory=LiveMemory(client)
            elif getattr(args,'memory',None):memory=Memory(read_json(args.memory))
            if args.command=='schema':result=contract()
            elif args.command=='normalize-rosetta':result=normalize_rosetta(args.debug.read_bytes(),read_json(args.placement),[(path,path.read_bytes()) for path in args.compiler_assembly])
            elif args.command=='source':result=source_lookup(read_json(args.map),args.pc,memory)
            else:
                layout=read_json(args.layout);table=symbols(args.symbols) if args.symbols else {}
                source=read_json(args.map) if getattr(args,'map',None) else None
                if args.command=='stack':result=stack(memory,layout,table,args.frame,source)
                elif args.command=='panic':
                    cpu=read_json(args.cpu) if args.cpu else client.command('cpu') if getattr(args,'ctrl',None) else None
                    if cpu is None:raise ViewError('offline panic view requires --cpu')
                    result=panic(memory,layout,table,cpu,source)
                else:result=globals()[args.command](memory,layout,table)
            if isinstance(memory,LiveMemory):result['observation']=memory.finish()
        output=json.dumps(result,ensure_ascii=True,indent=2)+'\n'
        if args.out:
            for key in ('memory','layout','symbols','debug','placement','map','cpu'):
                path=getattr(args,key,None)
                if path and args.out.resolve()==path.resolve():raise ViewError('output must not overwrite an input')
            args.out.write_text(output,encoding='utf-8')
        else:print(output,end='')
        return 0
    except (OSError,ValueError,KeyError,TypeError,RuntimeError) as exc:print('guestview: FAIL '+str(exc),file=sys.stderr);return 2


if __name__=='__main__':sys.exit(main())
