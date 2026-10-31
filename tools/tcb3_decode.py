"""TCB3 containers 3.1/3.2 and TPX3 export, host files only."""
from pathlib import Path
import argparse,json,struct,zlib
try:
    from .compact_decode import decode_frame
    from .tacticpod_decode import decode_image as legacy_decode, json_document as legacy_json
except ImportError:
    from compact_decode import decode_frame
    from tacticpod_decode import decode_image as legacy_decode, json_document as legacy_json

END=0xfff000
def json_document(value):
    value=legacy_json(value)
    def convert(item):
        if isinstance(item,dict):return {k:([str(n) for n in v] if k in ('offered','committed','lost') else
            str(v) if k in ('start_us','device_id') and v is not None else convert(v)) for k,v in item.items()}
        if isinstance(item,list):return [convert(v) for v in item]
        return item
    return convert(value)
def u(data,at=0,fmt='I'):return struct.unpack_from('<'+fmt,data,at)[0]
def require(ok,text):
    if not ok:raise ValueError(text)
def page_valid(p):
    require(len(p)==256 and u(p,252)==0xc0dec0de,'missing/torn page commit')
    require(u(p,248)==zlib.crc32(p[:248]),'page CRC mismatch')
    require(u(p,14,'H')==0,'unknown page flags')
    used=u(p,12,'H');require(used<=232,'page payload bound')
    require(p[16+used:248]==b'\xff'*(232-used),'page padding')
    return used

def decode_packets(data):
    at=0;seq=0;last=-1;image=bytearray();issues=[];ended=False
    while at<len(data):
        require(not ended and len(data)-at>=24,'truncated/trailing export packet')
        h=data[at:at+20];size=u(h,16,'H');require(size<=256 and at+24+size<=len(data),'packet size')
        packet=data[at:at+24+size];payload=packet[20:-4];kind=h[5];address=u(h,12)
        require(h[:4]==b'TPX3' and h[4]==1 and u(h,6,'H')==0 and u(h,18,'H')==0,'packet version/flags')
        require(u(h,8)==seq,'packet sequence gap')
        require(u(packet,len(packet)-4)==zlib.crc32(packet[:-4]),'packet CRC')
        if seq==0:
            require(kind==1 and address==0 and size==16 and tuple(struct.unpack('<4I',payload)) in
                    ((END,256,4096,0x30100),(END,256,4096,0x30200)),'export BEGIN metadata')
        elif kind==2:
            require(size==256 and address%256==0 and address<END and address>last,'export page address/order')
            if len(image)<address+256:image.extend(b'\xff'*(address+256-len(image)))
            image[address:address+256]=payload;last=address
        elif kind==3:
            require(address<END and size in (0,16),'ISSUE payload')
            issues.append({'address':address,'error':'unrecognised sector or transport failure'})
        elif kind==4:
            require(size==0 and address==END,'export END');ended=True
        else:raise ValueError('unexpected export packet')
        seq+=1;at+=size+24
    require(ended and seq>=2,'interrupted export: no END')
    if len(image)%4096:image.extend(b'\xff'*(4096-len(image)%4096))
    return bytes(image),issues

def decode_image(image):
    require(len(image)<=0x1000000 and len(image)%4096==0,'flash image size/alignment')
    image=image[:END];legacy=bytearray(image);sessions=[];issues=[];by_id={};versions=set()
    def issue(address,text,session=None):
        item={'address':address,'error':text};issues.append(item)
        if session is not None:session['issues'].append(item)
    for address in range(0,len(image),4096):
        sector=image[address:address+4096];h=sector[:256]
        if h[:4]!=b'TPH3':continue
        legacy[address:address+4096]=b'\xff'*4096
        try:
            require(page_valid(h)==160,'header payload size')
            sid=u(h,4);sseq=u(h,20);sequence=u(h,8)
            version=u(h,16)
            require(sid>0 and version in (0x30100,0x30200) and u(h,80)==1,'header version/schema')
            versions.add(version)
            require(u(h,84)&~3==0,'unknown metadata availability flags')
            require(u(h,84)&1 or u(h,32,'Q')==0,'unavailable device ID')
            require(u(h,84)&2 or h[88:120]==b'\xff'*32,'unavailable build ID')
            require(h[120:176]==b'\xff'*56 and u(h,24,'Q')<(1<<64)-1,'header reserved/start timestamp')
            require(sseq!=0 or (sid==address//4096+1 and sequence==0),'session identity/start')
            metadata=h[16:176];canonical=metadata[:4]+b'\0'*4+metadata[8:]
            if sid in by_id:
                s=by_id[sid];require(sseq>s['_sector'] and canonical==s['_metadata'] and s['footer'] is None,'continuation identity/metadata')
                if sseq!=s['_sector']+1:
                    issue(address,'missing/corrupt continuation sector',s);s['_partial']=None
                if sequence!=s['_next']:
                    issue(address,'continuation page sequence gap',s);s['_partial']=None
            else:
                s={'version':3,'session_id':sid,'sectors':[],'records':[],'frames':[],
                   'footer':None,'issues':[],'raw':b'','status':'interrupted',
                   'metadata':dict(start_us=u(h,24,'Q'),device_id=u(h,32,'Q') if u(h,84)&1 else None,availability_flags=u(h,84),
                                  clock_hz=u(h,40),imu_hz=u(h,44),imu_odr_hz=u(h,48),
                                  accel_ug_per_lsb=u(h,52),gyro_udps_per_lsb=u(h,56),rmc_hz=u(h,60),
                                  gga_hz=u(h,64),uart_baud=u(h,68),block_us=u(h,72),max_frame=u(h,76),
                                  build_id_hex=h[88:120].hex() if u(h,84)&2 else None),
                   '_metadata':canonical,'_partial':None,'_last_block':-1,'_last_us':-1}
                sessions.append(s);by_id[sid]=s
                if version==0x30200:s['container_version']='3.2.0'
                if sseq or sequence:issue(address,'missing session prefix',s)
            s['_sector']=sseq;s['_next']=sequence;s['sectors'].append(address)
        except ValueError as exc:issue(address,str(exc));continue
        for page_index in range(1,15):
            p=sector[page_index*256:(page_index+1)*256];where=address+page_index*256
            if p==b'\xff'*256:continue
            try:
                used=page_valid(p)
                require(p[:4]==b'TPD3' and u(p,4)==sid and used>=17,'data identity/length')
                q=u(p,8)
                if q!=s['_next']:issue(where,'data page sequence gap',s);s['_partial']=None
                s['_next']=q+1
                block_seq=u(p,16);offset,length,count,flags=struct.unpack_from('<4H',p,20);crc=u(p,28)
                require(36<=length<=8228 and 0<count<=216 and used==16+count and offset+count<=length,'fragment bounds')
                require(flags==((1 if offset==0 else 0)|(2 if offset+count==length else 0)),'fragment flags')
                if flags&1:
                    if s['_partial'] is not None:issue(where,'incomplete previous block',s)
                    s['_partial']={'sequence':block_seq,'length':length,'crc':crc,'bytes':bytearray()}
                b=s['_partial'];require(b is not None,'orphan fragment')
                require((b['sequence'],b['length'],b['crc'],len(b['bytes']))==(block_seq,length,crc,offset),'fragment continuity')
                b['bytes'].extend(p[32:32+count])
                if flags&2:
                    frame=bytes(b['bytes']);require(u(frame,len(frame)-4)==crc,'fragment/block CRC field')
                    decoded=decode_frame(frame)
                    require(decoded['sequence']==block_seq and block_seq>s['_last_block'],'block sequence')
                    require(decoded['records'][0]['timestamp_us']>=s['_last_us'],'cross-block monotonicity')
                    if block_seq!=s['_last_block']+1:issue(where,'block sequence gap',s)
                    s['_last_block']=block_seq;s['_last_us']=decoded['records'][-1]['timestamp_us']
                    s['frames'].append({'sequence':block_seq,'bytes':len(frame)})
                    s['records'].extend(decoded['records']);s['raw']+=decoded['raw'];s['_partial']=None
            except (ValueError,struct.error) as exc:issue(where,str(exc),s);s['_partial']=None
        p=sector[15*256:16*256]
        if p!=b'\xff'*256:
            try:
                require(page_valid(p)==160 and p[:4]==b'TPE3' and u(p,4)==sid and u(p,8)==s['_next'],'footer envelope')
                why=u(p,16);require(why<=3 and u(p,20)==1,'footer reason/flags')
                offered=list(struct.unpack_from('<4Q',p,32));committed=list(struct.unpack_from('<4Q',p,64));lost=list(struct.unpack_from('<4Q',p,96))
                require(all(o>=c and o-c==l for o,c,l in zip(offered,committed,lost)),'footer accounting')
                actual=[sum(r['type_id']==k for r in s['records']) for k in range(1,5)]
                require(actual==committed and u(p,148)==len(s['frames']),'footer recovered count mismatch')
                require(u(p,152)==s['_next'] and u(p,156)==0 and p[160:176]==b'\xff'*16,'footer page count/reserved')
                require(why!=0 or (not any(lost) and not any(u(p,o) for o in range(128,148,4))),'clean footer contains loss/errors')
                require(s['_last_us']<=u(p,24,'Q')<(1<<64)-1,'footer stop timestamp')
                s['footer']=dict(reason=why,stop_us=u(p,24,'Q'),offered=offered,committed=committed,lost=lost,
                                 rejected=u(p,128),crc_failures=u(p,132),incomplete_writes=u(p,136),full=u(p,140),missed=u(p,144))
                s['status']='complete' if not s['issues'] and why==0 and not any(lost) else 'closed_with_loss'
            except (ValueError,struct.error) as exc:issue(address+15*256,str(exc),s)
    for s in sessions:
        if s['_partial'] is not None:issue(s['sectors'][-1],'interrupted final block',s)
        for key in list(s):
            if key.startswith('_'):del s[key]
    old=legacy_decode(bytes(legacy))
    label='mixed' if len(versions)>1 else '3.2.0' if 0x30200 in versions else '3.1.0'
    return dict(schema='tacticpod.tcb3.decode/1',container_version=label,sessions=sessions,legacy=old,issues=issues)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path)
    parser.add_argument('--raw-output',type=Path);args=parser.parse_args()
    try:
        data=args.input.read_bytes();packet_issues=[]
        if data[:4]==b'TPX3':data,packet_issues=decode_packets(data)
        result=decode_image(data);result['export_issues']=packet_issues
        if args.raw_output:
            with args.raw_output.open('xb') as f:f.write(b''.join(s['raw'] for s in result['sessions']))
        for s in result['sessions']:del s['raw']
        print(json.dumps(json_document(result),indent=2,sort_keys=True))
    except (ValueError,OSError) as exc:parser.exit(2,str(exc)+'\n')
if __name__=='__main__':main()
