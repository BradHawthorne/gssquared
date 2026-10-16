"""Content-bound artifact transport for an explicitly selected output root."""
import base64
import hashlib
import os
from pathlib import Path,PureWindowsPath
import stat
import struct
import uuid
import zlib
from .observations import integer,number,schema

MAX_READ=1024*1024
MAX_WRITE=512*1024
MAX_DIRECTORY=4096
PATH={'type':'string','minLength':1,'maxLength':4096,'description':'Relative to artifact root, or an absolute path inside it. No parent traversal.'}
HASH={'type':'string','pattern':'^[0-9a-fA-F]{64}$','description':'SHA256 of the exact supplied/returned bytes, including only the requested range for a ranged read.'}
TOOLS=[
    {'name':'gs2_artifact_list','description':'List one directory of the selected artifact root, without following links. Bounded stable-name ordering and offset pagination; file metadata is discovery, not proof the producer has finished. Available after guest quit. Managed sessions default to their own output directory; attach requires explicit --artifact-root.',
     'inputSchema':schema({'path':dict(PATH,default='.'),'offset':number(0,MAX_DIRECTORY,0),'limit':number(1,256,128)}),'annotations':{'readOnlyHint':True}},
    {'name':'gs2_artifact_read','description':'Read bounded artifact bytes with SHA256 and optional expected digest. Complete validated native PNGs become MCP image content; UTF-8 becomes text; binary/ranged data can use base64. Complete means the whole file at this read, not finalized producer output. Close captures before final assertions. No shell or arbitrary paths outside the selected root.',
     'inputSchema':schema({'path':PATH,'offset':number(0,2**53-1,0),'length':number(1,MAX_READ),'mode':{'type':'string','enum':['auto','text','base64'],'default':'auto'},'sha256':HASH},('path',)),'annotations':{'readOnlyHint':True}},
    {'name':'gs2_artifact_put','description':'Upload a test input into the selected artifact root, with required SHA256. Supply exactly one UTF-8 text or strict base64 value, at most 512 KiB decoded. Atomically creates a new file and never overwrites; a repeated upload fails. Return path can be passed explicitly to native load/mount/verify tools; uploading does not touch guest memory.',
     'inputSchema':schema({'path':PATH,'text':{'type':'string','maxLength':MAX_WRITE},'base64':{'type':'string','maxLength':4*((MAX_WRITE+2)//3)},'sha256':HASH},('path','sha256')),'annotations':{'readOnlyHint':False,'destructiveHint':False,'idempotentHint':False}}
]
TOOL_NAMES={row['name'] for row in TOOLS}

def digest(data):return hashlib.sha256(data).hexdigest()

def expected(params,actual):
    value=params.get('sha256')
    if 'sha256' in params:
        if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdefABCDEF' for c in value):raise ValueError('sha256 must contain 64 hex digits')
        if value.lower()!=actual:raise ValueError('artifact SHA256 mismatch; no content accepted')

def png_info(blob):
    """Validate bounded native 8-bit PNG structure, CRC and deflate framing."""
    pos=8;header=None;packed=bytearray();seen=False;closed=False;ended=False
    while pos<len(blob):
        if pos+12>len(blob):raise ValueError('truncated PNG chunk')
        size,=struct.unpack_from('>I',blob,pos);kind=blob[pos+4:pos+8];end=pos+size+12
        if end>len(blob):raise ValueError('truncated PNG data')
        data=blob[pos+8:pos+8+size];crc,=struct.unpack_from('>I',blob,end-4)
        if zlib.crc32(kind+data)!=crc:raise ValueError('PNG CRC mismatch')
        if header is None and kind!=b'IHDR':raise ValueError('PNG header must be first')
        if kind==b'IHDR':
            if header is not None or size!=13:raise ValueError('invalid PNG header')
            w,h,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',data)
            if not w or not h or w*h>4000000 or depth!=8 or color not in (0,2,4,6) or compression or filtering or interlace:raise ValueError('image preview supports bounded noninterlaced native 8-bit PNG; use base64 for other formats')
            header=(w,h,color)
        elif kind==b'IDAT':
            if closed:raise ValueError('noncontiguous PNG data')
            packed.extend(data);seen=True
        elif kind==b'IEND':
            if size or not seen or end!=len(blob):raise ValueError('invalid PNG end')
            ended=True;break
        else:
            if seen:closed=True
            if not kind[0]&32 and kind!=b'PLTE':raise ValueError('unsupported critical PNG chunk')
        pos=end
    if not ended:raise ValueError('PNG end absent')
    w,h,color=header;stride=w*{0:1,2:3,4:2,6:4}[color];length=(stride+1)*h
    inflater=zlib.decompressobj()
    try:raw=inflater.decompress(packed,length+1)
    except zlib.error as error:raise ValueError('invalid PNG compressed data') from error
    if len(raw)!=length or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail or any(raw[y*(stride+1)]>4 for y in range(h)):raise ValueError('PNG pixels/filter/decompression framing mismatch')
    return {'width':w,'height':h,'mime_type':'image/png','validation':'native PNG chunks/CRC/deflate length/filter tags; no color-model equivalence claim'}

class Artifacts:
    def __init__(self,root):
        self.root=Path(root).resolve(strict=True)
        if not self.root.is_dir():raise ValueError('artifact root must be an existing directory')

    def path(self,value):
        if not isinstance(value,str) or not value or len(value)>4096 or any(ord(c)<32 or ord(c)==127 for c in value):raise ValueError('invalid artifact path')
        windows=PureWindowsPath(value)
        if '..' in windows.parts:raise ValueError('parent traversal is not an artifact path')
        candidate=Path(value)
        if not candidate.is_absolute():
            if windows.drive:raise ValueError('foreign or drive-relative artifact path')
            candidate=self.root/candidate
        path=candidate.resolve()
        if not path.is_relative_to(self.root):raise ValueError('artifact path is outside selected root')
        # Reject alternate data streams, including on a non-Windows test host.
        if any(':' in part for part in path.relative_to(self.root).parts):raise ValueError('alternate data streams are not artifact paths')
        return path

    def call(self,name,params):
        if name not in TOOL_NAMES:raise ValueError('unknown artifact tool')
        spec=next(row['inputSchema'] for row in TOOLS if row['name']==name)
        if not isinstance(params,dict) or set(params)-set(spec['properties']) or set(spec['required'])-set(params):raise ValueError('unsupported or missing artifact parameters')
        path=self.path(params.get('path','.'))
        if name=='gs2_artifact_list':return self.list(path,params),[]
        if name=='gs2_artifact_put':return self.put(path,params),[]
        return self.read(path,params)

    def list(self,path,params):
        offset=integer(params,'offset',0,0,MAX_DIRECTORY);limit=integer(params,'limit',128,1,256)
        with os.scandir(path) as scan:
            entries=[]
            for entry in scan:
                if len(entries)==MAX_DIRECTORY:raise ValueError('directory exceeds 4096-entry discovery bound; select a smaller output directory')
                entries.append(entry.name)
        entries.sort();rows=[]
        for name in entries[offset:offset+limit]:
            child=path/name;info=child.lstat()
            kind='symlink' if stat.S_ISLNK(info.st_mode) else 'file' if stat.S_ISREG(info.st_mode) else 'directory' if stat.S_ISDIR(info.st_mode) else 'special'
            rows.append({'path':child.relative_to(self.root).as_posix(),'kind':kind,'bytes':info.st_size if kind=='file' else None})
        truncated=offset+len(rows)<len(entries)
        return {'schema':'a2artifact-list-v1','ok':True,'root':str(self.root),'directory':path.relative_to(self.root).as_posix(),'entries':rows,'truncated':truncated,'next_offset':offset+len(rows) if truncated else None,'consistency':'one directory-name snapshot; metadata may change; no links followed or recursive scan'}

    def put(self,path,params):
        if 'sha256' not in params:raise ValueError('upload requires SHA256 before publication')
        if ('text' in params)==('base64' in params):raise ValueError('supply exactly one text or base64 input')
        if 'text' in params:
            if not isinstance(params['text'],str):raise ValueError('text must be a string')
            data=params['text'].encode('utf-8')
        else:
            value=params['base64']
            if not isinstance(value,str) or len(value)>4*((MAX_WRITE+2)//3):raise ValueError('base64 input exceeds upload bound')
            try:data=base64.b64decode(value,validate=True)
            except (ValueError,UnicodeError) as error:raise ValueError('invalid strict base64') from error
        if len(data)>MAX_WRITE:raise ValueError('upload exceeds 512 KiB')
        checksum=digest(data);expected(params,checksum)
        if not path.parent.is_dir():raise ValueError('upload parent directory must already exist')
        temporary=path.parent/('.artifact-'+uuid.uuid4().hex)
        try:
            with temporary.open('xb') as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
            # Atomic publication without replace() or an overwrite fallback.
            os.link(temporary,path)
        finally:
            if temporary.exists():temporary.unlink()
        return {'schema':'a2artifact-put-v1','ok':True,'created':True,'path':str(path),'relative_path':path.relative_to(self.root).as_posix(),'bytes':len(data),'sha256':checksum,'effect':'host input file created; no guest load; repeated creation refuses'}

    def read(self,path,params):
        mode=params.get('mode','auto')
        if mode not in ('auto','text','base64'):raise ValueError('unsupported artifact mode')
        flags=os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NONBLOCK',0)|getattr(os,'O_NOFOLLOW',0)
        descriptor=os.open(path,flags)
        with os.fdopen(descriptor,'rb') as stream:
            before=os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):raise ValueError('only regular artifact files can be read')
            offset=integer(params,'offset',0,0,2**53-1)
            length=integer(params,'length',None,1,MAX_READ) if 'length' in params else before.st_size-offset
            if length<0 or length>MAX_READ:raise ValueError('artifact exceeds 1 MiB read bound; select an explicit offset/length range')
            if offset>before.st_size or offset+length>before.st_size:raise ValueError('artifact range is outside file')
            stream.seek(offset);data=stream.read(length);after=os.fstat(stream.fileno())
        signature=lambda info:(info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns)
        if len(data)!=length or signature(before)!=signature(after) or signature(after)!=signature(path.stat()):raise ValueError('artifact changed during read; finish producer or retry observation explicitly')
        checksum=digest(data);expected(params,checksum)
        complete=offset==0 and length==before.st_size
        result={'schema':'a2artifact-read-v1','ok':True,'path':str(path),'relative_path':path.relative_to(self.root).as_posix(),'file_bytes':before.st_size,'offset':offset,'bytes':length,'complete':complete,'sha256':checksum,'hash_scope':'returned bytes','producer_finalized':None,'consistency':'stable file metadata during bounded read; active producers may append later; close captures before final assertions'}
        if mode=='auto' and complete and data.startswith(b'\x89PNG\r\n\x1a\n'):
            result.update(png_info(data));result['encoding']='mcp-image'
            return result,[{'type':'image','data':base64.b64encode(data).decode('ascii'),'mimeType':'image/png'}]
        if mode!='base64':
            try:
                text=data.decode('utf-8')
                if mode=='auto' and any(ord(c)<32 and c not in '\t\r\n' for c in text):raise UnicodeError('binary control characters')
                result.update(encoding='utf-8',text=text);return result,[]
            except UnicodeError:
                if mode=='text':raise ValueError('artifact range is not strict UTF-8; select aligned bytes or base64')
        result.update(encoding='base64',base64=base64.b64encode(data).decode('ascii'));return result,[]
