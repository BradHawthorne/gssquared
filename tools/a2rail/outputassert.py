"""Content assertions for PNG pixels, generated audio, and configured bitmap text.

Exit 0 means the declared assertion passed, 1 a measured mismatch, 2 invalid or
unsupported evidence. All thresholds and source hashes appear in the report.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib

class EvidenceError(ValueError):pass

def sha(data):return hashlib.sha256(data).hexdigest()

def png(path):
    blob=Path(path).read_bytes()
    if len(blob)>128*1024*1024 or blob[:8]!=b'\x89PNG\r\n\x1a\n':raise EvidenceError('invalid/oversized PNG')
    pos=8;header=None;packed=bytearray();ended=False;seen_data=False;data_closed=False
    while pos<len(blob):
        if pos+12>len(blob):raise EvidenceError('truncated PNG chunk')
        size,=struct.unpack_from('>I',blob,pos);kind=blob[pos+4:pos+8];end=pos+12+size
        if end>len(blob):raise EvidenceError('truncated PNG payload')
        data=blob[pos+8:pos+8+size];crc,=struct.unpack_from('>I',blob,pos+8+size)
        if zlib.crc32(kind+data)!=crc:raise EvidenceError('PNG CRC mismatch')
        if header is None and kind!=b'IHDR':raise EvidenceError('PNG header must be first')
        if kind==b'IHDR':
            if header is not None or size!=13:raise EvidenceError('duplicate/invalid PNG header')
            w,h,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',data)
            if not w or not h or w*h>16000000 or depth!=8 or color not in (0,2,4,6) or compression or filtering or interlace:
                raise EvidenceError('requires bounded noninterlaced 8-bit gray/RGB/gray-alpha/RGBA PNG')
            header=(w,h,color)
        elif kind==b'IDAT':
            if data_closed:raise EvidenceError('noncontiguous PNG data')
            packed.extend(data);seen_data=True
        elif kind==b'IEND':
            if size or not seen_data or end!=len(blob):raise EvidenceError('invalid PNG end')
            ended=True;break
        else:
            if seen_data:data_closed=True
            if not (kind[0]&32) and kind!=b'PLTE':raise EvidenceError('unsupported critical PNG chunk')
        pos=end
    if not ended:raise EvidenceError('PNG end absent')
    w,h,color=header;bpp={0:1,2:3,4:2,6:4}[color];stride=w*bpp;need=(stride+1)*h
    inflater=zlib.decompressobj();raw=inflater.decompress(packed,need+1)
    if len(raw)!=need or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:raise EvidenceError('PNG decompression length/end mismatch')
    pixels=bytearray();previous=bytearray(stride)
    for y in range(h):
        mode=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        if mode>4:raise EvidenceError('unsupported PNG row filter')
        for i in range(stride):
            a=row[i-bpp] if i>=bpp else 0;b=previous[i];c=previous[i-bpp] if i>=bpp else 0
            if mode==1:v=a
            elif mode==2:v=b
            elif mode==3:v=(a+b)//2
            elif mode==4:
                p=a+b-c;da,db,dc=abs(p-a),abs(p-b),abs(p-c);v=a if da<=db and da<=dc else b if db<=dc else c
            else:v=0
            row[i]=(row[i]+v)&255
        for i in range(0,stride,bpp):
            if color in (0,4):pixels.extend((row[i],row[i],row[i],row[i+1] if color==4 else 255))
            else:pixels.extend((*row[i:i+3],row[i+3] if color==6 else 255))
        previous=row
    return {'width':w,'height':h,'rgba':bytes(pixels),'sha256':sha(blob)}

def image(left,right,delta=0,bad_fraction=0):
    if type(delta) is not int or not 0<=delta<=255 or not math.isfinite(bad_fraction) or not 0<=bad_fraction<=1:raise EvidenceError('invalid image thresholds')
    a,b=png(left),png(right)
    if (a['width'],a['height'])!=(b['width'],b['height']):raise EvidenceError('image dimensions differ')
    maximum=bad=total=0;box=None
    for i in range(0,len(a['rgba']),4):
        differences=[abs(x-y) for x,y in zip(a['rgba'][i:i+4],b['rgba'][i:i+4])];m=max(differences);maximum=max(maximum,m);total+=sum(differences)
        if m>delta:
            bad+=1;x=(i//4)%a['width'];y=(i//4)//a['width']
            box=[x,y,x,y] if box is None else [min(box[0],x),min(box[1],y),max(box[2],x),max(box[3],y)]
    count=a['width']*a['height'];fraction=bad/count
    return {'schema':'a2image-assert-v1','model':'decoded-RGBA-channel-distance','pass':fraction<=bad_fraction,'width':a['width'],'height':a['height'],
            'expected_sha256':a['sha256'],'actual_sha256':b['sha256'],'thresholds':{'channel_delta':delta,'bad_pixel_fraction':bad_fraction},
            'bad_pixels':bad,'bad_pixel_fraction':fraction,'maximum_channel_delta':maximum,'mean_channel_delta':total/(4*count),'bad_pixel_bounds_inclusive':box}

def audio(path):
    blob=Path(path).read_bytes()
    if len(blob)>128*1024*1024:raise EvidenceError('oversized audio capture')
    rows=[json.loads(line) for line in blob.splitlines()]
    for row in rows:
        if not isinstance(row,dict):raise EvidenceError('audio record is not an object')
        event=row.get('event')
        if isinstance(event,str) and event.startswith('audio.'):row['event']=event[6:]
    if len(rows)<2 or any(r.get('schema')!='a2audio-v1' for r in rows) or rows[0].get('event')!='started' or rows[-1].get('event')!='stopped':raise EvidenceError('incomplete audio lifecycle')
    status=rows[-1]['status']
    if status.get('complete') is not True or status.get('active') is not False or status.get('error') or int(status.get('dropped',-1))!=0:raise EvidenceError('audio capture reports loss/error')
    source=rows[0].get('source')
    if source not in ('doc','sfx') or status.get('source')!=source:raise EvidenceError('audio source mismatch')
    samples=[];timing=[];channels=None;count=0
    for sequence,row in enumerate(rows[1:-1]):
        ch=row.get('channels');n=row.get('frames');data=row.get('samples')
        if row.get('event')!='samples' or row.get('sequence')!=sequence or int(row.get('frame_offset',-1))!=count or type(ch) is not int or ch not in (1,2) or type(n) is not int or not 0<n<=1000000:
            raise EvidenceError('invalid audio batch shape/sequence')
        if channels is not None and ch!=channels:raise EvidenceError('audio channel shape changed')
        channels=ch
        if len(data)!=n*ch or any(type(v) is not int or not -32768<=v<=32767 for v in data):raise EvidenceError('invalid int16 samples')
        period=row.get('ticks_per_sample');rate=row.get('sample_rate_hz');end=row.get('end_tick')
        if source=='doc':
            if type(period) is not int or period<=0 or row.get('tick_domain')!='c14m' or end is None:raise EvidenceError('DOC timing absent')
            end=int(end);start=end-n*period
            if start<0:raise EvidenceError('negative DOC sample epoch')
            ticks=[(start+(i+1)*period,period) for i in range(n)]
            if timing and ticks[0][0]<=timing[-1][0]:raise EvidenceError('DOC clock moved backwards')
            timing.extend(ticks)
        else:
            if type(rate) is not int or not 1<=rate<=384000:raise EvidenceError('SFX rate absent')
            timing.extend([(None,rate)]*n)
        samples.extend(data);count+=n
        if count>1000000:raise EvidenceError('audio reader frame cap exceeded')
    if not count or int(status.get('frames',-1))!=count:raise EvidenceError('empty audio/frame count mismatch')
    return {'source':source,'channels':channels,'samples':samples,'timing':timing,'sha256':sha(blob),'frames':count}

def sound(left,right,sample_tolerance=0,rms_tolerance=0):
    if type(sample_tolerance) is not int or not 0<=sample_tolerance<=65535 or not math.isfinite(rms_tolerance) or rms_tolerance<0:raise EvidenceError('invalid audio thresholds')
    a,b=audio(left),audio(right)
    if (a['source'],a['channels'],a['frames'])!=(b['source'],b['channels'],b['frames']):raise EvidenceError('audio source/channel/frame shape differs')
    # Time comparisons use relative DOC origin; capture can start at any epoch.
    normalize=lambda v:[(tick-v[0][0],period) if tick is not None else (None,period) for tick,period in v]
    same_time=normalize(a['timing'])==normalize(b['timing']);ch=a['channels'];channels=[]
    for c in range(ch):
        expected=a['samples'][c::ch];actual=b['samples'][c::ch];errors=[x-y for x,y in zip(expected,actual)]
        channels.append({'channel':c,'expected_rms':math.sqrt(sum(x*x for x in expected)/len(expected)),
                         'actual_rms':math.sqrt(sum(x*x for x in actual)/len(actual)),
                         'error_rms':math.sqrt(sum(x*x for x in errors)/len(errors)),'maximum_sample_error':max(map(abs,errors))})
    return {'schema':'a2audio-assert-v1','model':'aligned-generated-source-samples','pass':same_time and all(c['maximum_sample_error']<=sample_tolerance and c['error_rms']<=rms_tolerance for c in channels),
            'expected_sha256':a['sha256'],'actual_sha256':b['sha256'],'source':a['source'],'frames':a['frames'],'relative_timing_equal':same_time,
            'thresholds':{'sample_error':sample_tolerance,'error_rms':rms_tolerance},'channels':channels}

def text(image_path,font_path,columns,rows,x=0,y=0,threshold=128):
    image_data=png(image_path);blob=Path(font_path).read_bytes();font=json.loads(blob)
    if font.get('schema')!='a2bitmap-font-v1':raise EvidenceError('unsupported bitmap-font schema')
    w,h=font['width'],font['height'];glyphs=font['glyphs']
    if any(type(n) is not int or n<=0 for n in (w,h,columns,rows)) or not 0<=threshold<=255 or x<0 or y<0 or x+columns*w>image_data['width'] or y+rows*h>image_data['height']:raise EvidenceError('invalid font/grid bounds')
    templates={}
    for char,glyph in glyphs.items():
        if len(char)!=1 or len(glyph)!=h or any(len(line)!=w or set(line)-{'.','#'} for line in glyph):raise EvidenceError('invalid glyph bitmap')
        key=''.join(glyph)
        if key in templates:raise EvidenceError('ambiguous duplicate font bitmap')
        templates[key]=char
    result=[];unknown=[];data=image_data['rgba']
    for row in range(rows):
        line=''
        for col in range(columns):
            key=''
            for gy in range(h):
                for gx in range(w):
                    i=4*((y+row*h+gy)*image_data['width']+x+col*w+gx);r,g,b,a=data[i:i+4]
                    if a!=255:raise EvidenceError('text extraction requires opaque pixels')
                    key+='#' if (299*r+587*g+114*b)//1000>=threshold else '.'
            if key not in templates:unknown.append({'column':col,'row':row,'bitmap':key})
            line+=templates.get(key,'\ufffd')
        result.append(line)
    return {'schema':'a2bitmap-text-v1','model':'configured-grid-glyph-template; no universal OCR claim','pass':not unknown,
            'image_sha256':image_data['sha256'],'font_sha256':sha(blob),'text':'\n'.join(result),'unknown':unknown,
            'grid':{'x':x,'y':y,'columns':columns,'rows':rows,'width':w,'height':h,'luma_threshold':threshold}}

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    for name in ('image','audio'):
        s=sub.add_parser(name);s.add_argument('expected',type=Path);s.add_argument('actual',type=Path);s.add_argument('--out',type=Path)
        if name=='image':s.add_argument('--channel-delta',type=int,default=0);s.add_argument('--bad-pixel-fraction',type=float,default=0)
        else:s.add_argument('--sample-tolerance',type=int,default=0);s.add_argument('--rms-tolerance',type=float,default=0)
    t=sub.add_parser('text');t.add_argument('image',type=Path);t.add_argument('font',type=Path);t.add_argument('--columns',type=int,required=True);t.add_argument('--rows',type=int,required=True)
    t.add_argument('--x',type=int,default=0);t.add_argument('--y',type=int,default=0);t.add_argument('--threshold',type=int,default=128);t.add_argument('--out',type=Path)
    args=p.parse_args()
    try:
        if args.cmd=='image':report=image(args.expected,args.actual,args.channel_delta,args.bad_pixel_fraction)
        elif args.cmd=='audio':report=sound(args.expected,args.actual,args.sample_tolerance,args.rms_tolerance)
        else:report=text(args.image,args.font,args.columns,args.rows,args.x,args.y,args.threshold)
        encoded=json.dumps(report,indent=2)+'\n'
        if args.out:
            with args.out.open('x',encoding='utf-8') as out:out.write(encoded)
        else:print(encoded,end='')
        return 0 if report['pass'] else 1
    except (ValueError,OSError,KeyError,TypeError,struct.error,zlib.error) as exc:print('output assertion: FAIL '+str(exc),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
