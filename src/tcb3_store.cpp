#include "tcb3_store.h"
#include <string.h>
#include "imu_config.h"
namespace Tcb3 {
uint32_t get(const uint8_t* p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(p[i])<<(8*i);return v;}
void put(uint8_t* p,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=v>>(8*i);}
uint32_t crc32(const uint8_t* p,unsigned n,uint32_t c){
  static const uint32_t table[]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
    0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
  while(n--){c^=*p++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return c;
}
bool validPage(const uint8_t* p){return get(p+252)==0xc0dec0deU && get(p+248)==~crc32(p,248);}
static bool blank(const uint8_t* p,unsigned n){while(n--)if(*p++!=255)return false;return true;}
void metadata(uint8_t* p,uint64_t us){
  memset(p,255,MetadataBytes);put(p,0x00030200);put(p+4,0);put(p+8,us,8);put(p+16,0,8);
  const uint32_t values[]={1000000,100,ImuConfig::SensorRateHz,
    ImuConfig::AccelMicroGPerCount,ImuConfig::GyroMicroDpsPerCount,
    10,1,38400,2000000,8228,1,0};
  for(unsigned i=0;i<12;++i)put(p+24+i*4,values[i]);
}
void Store::fail(const char* why,bool incomplete){
  if(transferring){port.abortPage(port.context);transferring=false;incomplete=true;}
  error=why;stage=State::Fault;if(incomplete)++stats.incompleteWrites;io=0;
}
bool Store::read(uint32_t a,uint8_t* p,unsigned n){
  if(!n || n>64 || a>=End || n>End-a || !port.read(port.context,a,p,n)){fail("read/address failure",io!=0);return false;}return true;
}
void Store::begin(Port p,uint32_t now){
  if(transferring)port.abortPage(port.context);
  port=p;stats=Stats{};error=nullptr;stage=State::Scan;scan=0;started=now;block=nullptr;io=0;releasePending=false;scanned=false;transferring=false;
  epoch=0;scanStep=0;sector0Blank=false;
  if(Tcb3Test::Append || Tcb3Test::Reusable)blankAt=0;
}
bool Store::cancelScan(){if(stage!=State::Scan && stage!=State::Probe)return false;stage=State::Idle;scanned=false;return true;}
void Store::endEpochScan(){stage=State::Probe;blankAt=0;}
void Store::halt(const char* why){fail(why,io!=0 || block!=nullptr);}
bool Store::start(const uint8_t* m){
  if(Tcb3Test::Reusable && scanned && stage==State::Idle && stats.next!=recoveredStart){fail("recovered address changed");return false;}
  if(Tcb3Test::Append && scanned && stage==State::Idle && stats.next!=Tcb3Test::WriteStart){fail("initial append address mismatch");return false;}
  if(!scanned || stage!=State::Idle || stats.next>=Tcb3Test::WriteEnd || get(m)!=0x00030200 || get(m+64)!=1)return false;
  if(Tcb3Test::Enabled && !Tcb3Test::Append && stats.next!=0)return false;
  if(stats.next<floor())return false;
  uint32_t next=stats.next,skipped=stats.probeSkipped,erases=stats.eraseOperations,eraseMs=stats.maxEraseMs;
  stats=Stats{};stats.next=next;stats.probeSkipped=skipped;stats.eraseOperations=erases;stats.maxEraseMs=eraseMs;sector=next;stats.session=sector/Sector+1;
  memcpy(meta,m,MetadataBytes);if(Tcb3Test::Reusable && epoch)put(meta+EpochMetadataOffset,epoch);
  blankAt=0;pageIndex=1;blockExpected=0;block=nullptr;stage=State::Blank;return true;
}
bool Store::offer(const uint8_t* p,unsigned n){
  if(!accepting())return false;
  if(n<36 || n>8228 || memcmp(p,"TCB3",4) || p[4] || (p[5]!=0 && p[5]!=8) || p[6] || p[7] ||
     get(p+14,2)+36!=n || get(p+8)!=blockExpected){fail("invalid block envelope");return false;}
  unsigned count=0,raw=0;const unsigned sizes[]={24,44,40,32};
  for(unsigned i=0;i<4;++i){unsigned c=get(p+16+2*i,2);count+=c;raw+=c*sizes[i];}
  if(!count || count>256 || raw>8192 || raw!=get(p+12,2) || get(p+14,2)>raw){fail("invalid block counts");return false;}
  block=p;length=n;offset=checkAt=0;blockCrc=0xffffffffU;stage=State::Validate;return true;
}
bool Store::released(){bool v=releasePending;releasePending=false;return v;}
void Store::finish(uint64_t us,const uint64_t* counts,Reason why,uint32_t rejected,uint32_t missed){
  if(stage==State::Idle || stage==State::Fault || stage==State::Scan)return;
  stats.closing=true;stopUs=us;reason=why;stats.rejected=rejected;stats.missed=missed;
  for(unsigned i=0;i<4;++i)stats.offered[i]=counts[i];
}
void Store::envelope(uint32_t magic,unsigned used){
  memset(page,255,256);put(page,magic);put(page+4,stats.session);put(page+8,stats.pageSequence);
  put(page+12,used,2);put(page+14,0,2);
}
void Store::writePage(uint32_t address){
  if(address<floor() || address>=Tcb3Test::WriteEnd || Page>Tcb3Test::WriteEnd-address || (address&255)){fail("write boundary");return;}
  at=address;io=1;ioOffset=0;verifyAt=0;verifyCrc=0xffffffffU;
  // CRC calculation and blank verification are sliced, before any programming.
}
void Store::service(uint32_t now){
  if(io==10){
    // Lazy erase of a stale (pre-reset) sector, only ever above the live prefix and never sector 0.
    int s=port.status(port.context);
    if(s!=0){fail("status before erase");return;}
    if(!Tcb3Test::Reusable || !epoch || !port.erase || at<Sector || at>=Tcb3Test::WriteEnd || (at&(Sector-1)) ||
       !port.erase(port.context,at)){fail("erase rejected");return;}
    ++stats.eraseOperations;started=lastPoll=now;io=11;return;
  }
  if(io==11){
    if(uint32_t(now-lastPoll)<2)return;
    lastPoll=now;
    int s=port.status(port.context);
    if(s<0 || s==255){fail("erase status failure");return;}
    if(s&1){++stats.busyPolls;if(uint32_t(now-started)>=3000)fail("erase timeout");return;}
    if(s&2){fail("WEL retained after erase");return;}
    if(uint32_t(now-started)>stats.maxEraseMs)stats.maxEraseMs=now-started;
    io=12;verifyAt=0;return;
  }
  if(io==12){
    if(!read(at+verifyAt,scratch,64))return;
    if(!blank(scratch,64)){fail("erase verify failed");return;}
    verifyAt+=64;if(verifyAt==Sector)io=0;return;
  }
  if(io==1){
    if(!read(at+verifyAt,scratch,64))return;
    if(!blank(scratch,64)){fail("nonblank destination");return;}
    unsigned n=verifyAt==192?56:64;verifyCrc=crc32(page+verifyAt,n,verifyCrc);verifyAt+=64;
    if(verifyAt==256){put(page+248,~verifyCrc);put(page+252,0xc0dec0deU);io=2;}return;
  }
  if(io==2){
    if(Tcb3Test::Reusable && !stats.programOperations &&
       (at!=recoveredStart || sector!=recoveredStart || stats.session!=recoveredStart/Sector+1 ||
        stats.next!=recoveredStart+Sector || stats.sectorSequence)){fail("reusable first program mismatch");return;}
    if(Tcb3Test::Append && !stats.programOperations &&
       (at!=Tcb3Test::WriteStart || sector!=Tcb3Test::WriteStart || stats.session!=Tcb3Test::ExpectedSession ||
        stats.sectorSequence!=0 || stats.next!=Tcb3Test::WriteStart+Sector)){
      fail("first program address mismatch");return;
    }
    int s=port.status(port.context);
    if(s<0 || s==255 || (s&3)){fail("status before program",true);return;}
    if(at<floor() || at>=Tcb3Test::WriteEnd || Page>Tcb3Test::WriteEnd-at || !port.beginPage(port.context,at)){fail("page begin rejected",true);return;}
    transferring=true;ioOffset=0;io=8;return;
  }
  if(io==8){
    if(!port.pageBytes(port.context,page+ioOffset,64)){fail("page transfer rejected",true);return;}
    ioOffset+=64;if(ioOffset==Page)io=9;return;
  }
  if(io==9){
    bool ok=port.endPage(port.context);transferring=false;
    if(!ok){fail("page program rejected",true);return;}
    ++stats.programOperations;started=lastPoll=now;io=3;return;
  }
  if(io==3){
    if(uint32_t(now-lastPoll)<2)return;
    lastPoll=now;
    int s=port.status(port.context);
    if(s<0 || s==255){fail("status failure",true);return;}
    if(s&1){++stats.busyPolls;if(uint32_t(now-started)>=1000)fail("program timeout",true);return;}
    if(s&2){fail("WEL retained",true);return;}
    io=7;verifyAt=0;verifyCrc=0xffffffffU;return;
  }
  if(io==7){
    if(!read(at+verifyAt,scratch,64))return;
    if(memcmp(page+verifyAt,scratch,64)){++stats.crcFailures;fail("post-commit readback mismatch",true);return;}
    verifyCrc=crc32(scratch,verifyAt==192?56:64,verifyCrc);verifyAt+=64;
    if(verifyAt==256){
      if(get(scratch+60)!=0xc0dec0deU || get(scratch+56)!=~verifyCrc){++stats.crcFailures;fail("post-commit CRC",true);return;}
      io=0;committed(now);
    }return;
  }
}
void Store::committed(uint32_t now){
  if(stage==State::Header){++stats.headerPages;pageIndex=1;stage=State::Ready;return;}
  if(stage==State::Footer){stage=State::Idle;scanned=false;stats.finished=true;return;}
  if(stage==State::Data){
    ++pageIndex;++stats.pageSequence;++stats.pages;offset+=fragment;
    if(offset==length){
      for(unsigned i=0;i<4;++i)stats.committed[i]+=get(block+16+2*i,2);
      ++stats.blocks;++blockExpected;block=nullptr;releasePending=true;
      if(uint32_t(now-blockAt)>stats.maxBlockMs)stats.maxBlockMs=now-blockAt;
    }
    stage=State::Ready;
  }
}
void Store::update(uint32_t now){
  if(stage==State::Fault || stage==State::Idle)return;
  if(io){service(now);return;}
  if(stage==State::Scan){
    int s=port.status(port.context);
    if(s<0 || s==255){fail("scan status");return;}
    if(s&1){if(uint32_t(now-started)>=5000)fail("boot busy timeout");return;}
    if(s&2){fail("unexpected WEL at boot");return;}
    if(Tcb3Test::Append){
      if(s!=0){fail("unexpected append scan status");return;}
      // Engineering profiles permit only their known predecessor sector map.
      // Validate its complete committed headers; an ambiguous recovery is fatal.
      if(scan<Tcb3Test::WriteStart){
        if(!read(scan+blankAt,page+blankAt,64))return;
        blankAt+=64;if(blankAt<Page)return;
        const uint32_t priorStart=(Tcb3Test::Movement && scan>=0x2000)?0x2000:0;
        if(!validPage(page) || get(page)!=Header || get(page+4)!=priorStart/Sector+1 ||
           get(page+12,2)!=MetadataBytes || get(page+14,2)!=0 ||
           get(page+16)!=0x00030200 || get(page+20)!=(scan-priorStart)/Sector){
          fail("preserved header invalid");return;
        }
        if(scan==priorStart)memcpy(meta,page+16,MetadataBytes);
        else if(memcmp(meta+8,page+24,MetadataBytes-8)){
          fail("preserved metadata mismatch");return;
        }
        blankAt=0;
      }else{
        if(!read(scan,scratch,16))return;
        if(!blank(scratch,16)){fail("unexpected occupied append sector");return;}
      }
      scan+=Sector;
      if(scan==End){stats.next=Tcb3Test::WriteStart;stage=State::Probe;blankAt=0;}
      return;
    }
    if(Tcb3Test::Reusable){
      if(scanStep==0){
        // Sector 0 decides whether a reset marker exists before any other sector is trusted.
        if(!read(blankAt,page+blankAt,64))return;
        blankAt+=64;if(blankAt<Page)return;
        blankAt=0;const uint32_t m=get(page);
        if(m==ResetMagic){
          if(!validPage(page) || !get(page+4) || get(page+4)==0xffffffffU){fail("reset marker invalid; repeat RECORD RESET");return;}
          epoch=get(page+4);stats.next=Sector;scan=Sector;scanStep=1;
        }else if(!blank(page,16) && m!=Header && m!=0x31485054 && m!=0x32485054){
          fail("sector 0 invalid; repeat RECORD RESET");return;
        }else if(blank(page,16) && !blank(page,Page)){
          fail("sector 0 partly programmed; repeat RECORD RESET");return; // Torn marker: never treat as fresh flash.
        }else{sector0Blank=blank(page,16);scanStep=2;}
        return;
      }
      if(scanStep==1){
        // Only a contiguous run of headers carrying the current epoch is live; the first other sector is the head.
        if(!blankAt){
          if(!read(scan,scratch,16))return;
          if(get(scratch)!=Header){endEpochScan();return;}
          blankAt=1;return;
        }
        if(blankAt==1){
          if(!read(scan+EpochHeaderOffset,scratch,4))return;
          if(get(scratch)!=epoch){blankAt=0;endEpochScan();return;}
          blankAt=2;return;
        }
        if(blankAt<6){
          // A matching epoch word alone is not trusted: a torn erase can raise old bits. Require a sealed header page.
          const unsigned part=(blankAt-2)*64;
          if(!read(scan+part,page+part,64))return;
          ++blankAt;return;
        }
        if(blankAt==6){
          if(validPage(page)){blankAt=0;stats.next=scan+Sector;scan+=Sector;if(scan>=End)endEpochScan();return;}
          blankAt=7;return;
        }
        // Unsealed header: a torn current-epoch header has no data behind it and is reusable; anything else is corruption.
        if(!read(scan+Page,scratch,16))return;
        if(!blank(scratch,16)){fail("live sector header corrupt; repeat RECORD RESET");return;}
        blankAt=0;endEpochScan();return;
      }
    }
    if(!read(scan,scratch,16))return;
    if(!blank(scratch,16)){
      if(Tcb3Test::Reusable && sector0Blank && scan<Tcb3Test::WriteStart){fail("sector 0 blank with earlier data; repeat RECORD RESET");return;}
      stats.next=scan+Sector;
    }
    scan+=Sector;if(scan==End){
      if(Tcb3Test::Append || Tcb3Test::Reusable){stage=State::Probe;blankAt=0;}
      else {stage=State::Idle;scanned=true;}
    }return;
  }
  if(stage==State::Probe){
    // Read-only allocation preflight. Never backfill holes below occupied headers.
    if(Tcb3Test::Reusable){
      // Fully erased storage has no history: begin at the protected lower boundary, never below it.
      if(!epoch && !stats.next && !blankAt)stats.next=Tcb3Test::WriteStart;
      if(stats.next>=Tcb3Test::WriteEnd){++stats.full;fail("recording partition full");return;}
      if(stats.next<floor() || stats.next%Sector){fail("reusable append boundary");return;}
    }else if(stats.next!=Tcb3Test::WriteStart){fail("initial append address mismatch");return;}
    int s=port.status(port.context);
    if(s!=0){fail("append probe status");return;}
    if(!read(stats.next+blankAt,scratch,64))return;
    if(!blank(scratch,64)){
      // Stale pre-reset sector at the head: erase it, then probe again from its start.
      if(Tcb3Test::Reusable && epoch){at=stats.next;blankAt=0;io=10;return;}
      fail("occupied initial append sector");return;
    }
    blankAt+=64;
    if(blankAt==Sector){if(Tcb3Test::Reusable)recoveredStart=stats.next;stage=State::Idle;scanned=true;}return;
  }
  if(stage==State::Blank){
    if(!read(sector+blankAt,scratch,64))return;
    if(!blank(scratch,64)){
      if(Tcb3Test::Reusable && epoch && sector>=Sector){at=sector;blankAt=0;io=10;return;}
      fail("occupied candidate sector");return;
    }
    blankAt+=64;if(blankAt<Sector)return;
    stats.next=sector+Sector;envelope(Header,MetadataBytes);memcpy(page+16,meta,MetadataBytes);
    put(page+20,stats.sectorSequence);stage=State::Header;writePage(sector);return;
  }
  if(stage==State::Validate){
    if(!checkAt)blockAt=now;
    unsigned n=length-4-checkAt;if(n>64)n=64;blockCrc=crc32(block+checkAt,n,blockCrc);checkAt+=n;stats.crcBytes+=n;
    if(checkAt==length-4){if(~blockCrc!=get(block+length-4)){++stats.crcFailures;fail("block CRC");return;}stage=State::Ready;}return;
  }
  if(stage==State::Ready){
    if(block){
      if(pageIndex==15){
        if(stats.next>=Tcb3Test::WriteEnd){++stats.full;block=nullptr;releasePending=true;return;}
        sector=stats.next;++stats.sectorSequence;blankAt=0;stage=State::Blank;return;
      }
      fragment=length-offset;if(fragment>Fragment)fragment=Fragment;
      envelope(Data,16+fragment);put(page+16,blockExpected);put(page+20,offset,2);put(page+22,length,2);
      put(page+24,fragment,2);put(page+26,(offset?0:1)|(offset+fragment==length?2:0),2);
      put(page+28,get(block+length-4));memcpy(page+32,block+offset,fragment);
      stage=State::Data;writePage(sector+pageIndex*Page);return;
    }
    if(stats.closing){
      for(unsigned i=0;i<4;++i)if(stats.offered[i]<stats.committed[i]){fail("footer count underflow");return;}
      envelope(Footer,160);put(page+16,uint32_t(reason));put(page+20,1);put(page+24,stopUs,8);
      for(unsigned i=0;i<4;++i){put(page+32+8*i,stats.offered[i],8);put(page+64+8*i,stats.committed[i],8);
        put(page+96+8*i,stats.offered[i]>=stats.committed[i]?stats.offered[i]-stats.committed[i]:0,8);}
      const uint32_t values[]={stats.rejected,stats.crcFailures,stats.incompleteWrites,stats.full,stats.missed,stats.blocks,stats.pages,0};
      for(unsigned i=0;i<8;++i)put(page+128+4*i,values[i]);
      stage=State::Footer;writePage(sector+15*Page);
    }
  }
}

void Export::begin(Port p){port=p;at=sequence=pageIndex=offset=phase=0;epoch=probeAt=0;ready=ended=selected=fault=false;opening=true;}
void Export::make(unsigned kind,uint32_t address,const uint8_t* data,unsigned n){
  memset(packet,0,20);memcpy(packet,"TPX3",4);packet[4]=1;packet[5]=kind;put(packet+8,sequence++);
  put(packet+12,address);put(packet+16,n,2);if(n)memcpy(packet+20,data,n);
  put(packet+20+n,~crc32(packet,20+n));packetSize=n+24;ready=true;
}
void Export::update(){
  if(ready || ended)return;
  int status=port.status(port.context);
  if(status<0 || status==255 || (status&3)){
    make(3,at<End?at:End-Page,nullptr,0);fault=ended=true;return;
  }
  if(opening){uint8_t p[16]{};put(p,End);put(p+4,Page);put(p+8,Sector);put(p+12,0x00030200);make(1,0,p,16);opening=false;return;}
  if(Tcb3Test::Reusable && phase==0){
    // Classify sector 0 first: a reset marker hides every older session; an interrupted reset exports nothing.
    if(!port.read(port.context,offset,page+offset,64)){make(3,0,nullptr,0);fault=ended=true;return;}
    offset+=64;if(offset<Page)return;
    offset=0;const uint32_t m=get(page);
    if(m==ResetMagic){
      if(!validPage(page) || !get(page+4) || get(page+4)==0xffffffffU){make(3,0,page,16);fault=ended=true;return;}
      epoch=get(page+4);at=Sector;phase=2;
    }else if(!blank(page,16) && m!=Header && m!=0x31485054 && m!=0x32485054){
      make(3,0,page,16);fault=ended=true;
    }else if(blank(page,16)){
      if(!blank(page,Page)){make(3,0,page,16);fault=ended=true;return;} // Torn marker: export nothing.
      probeAt=Sector;phase=1;
    }
    else phase=2;
    return;
  }
  if(Tcb3Test::Reusable && phase==1){
    // Blank sector 0 with data below the protected boundary is an interrupted reset: fail closed.
    if(probeAt>=Tcb3Test::WriteStart){phase=2;return;}
    if(!port.read(port.context,probeAt,page,16)){make(3,probeAt,nullptr,0);fault=ended=true;return;}
    if(!blank(page,16)){make(3,probeAt,page,16);fault=ended=true;return;}
    probeAt+=Sector;
    return;
  }
  if(at>=End){make(4,End,nullptr,0);ended=true;return;}
  if(!selected){
    if(!port.read(port.context,at,page,16)){make(3,at,nullptr,0);fault=ended=true;return;}
    if(get(page)==Header){
      if(epoch){
        uint8_t e[4];
        if(!port.read(port.context,at+EpochHeaderOffset,e,4)){make(3,at,nullptr,0);fault=ended=true;return;}
        if(get(e)!=epoch){make(4,End,nullptr,0);ended=true;return;}
      }
      selected=true;pageIndex=offset=0;return;
    }
    if(epoch){make(4,End,nullptr,0);ended=true;return;}
    if(!blank(page,16) && get(page)!=0x31485054 && get(page)!=0x32485054)make(3,at,page,16);
    at+=Sector;return;
  }
  uint32_t address=at+pageIndex*Page;
  if(port.readPage){
    if(!port.readPage(port.context,address,page)){make(3,address,nullptr,0);fault=ended=true;return;}
  }else{
    if(!port.read(port.context,address+offset,page+offset,64)){make(3,address,nullptr,0);fault=ended=true;return;}
    offset+=64;if(offset<256)return;
  }
  // Epoch mode: an unsealed header (torn or never completed) marks the end of live data, as in the store scan.
  if(Tcb3Test::Reusable && epoch && pageIndex==0 && !validPage(page)){make(4,End,nullptr,0);ended=true;return;}
  make(2,address,page,256);offset=0;if(++pageIndex==16){at+=Sector;selected=false;}
}

void Reset::fail(const char* why){error=why;phase=Phase::Fault;}
void Reset::begin(Port p,uint32_t now){
  port=p;error=nullptr;phase=Phase::Read;offset=0;highest=0;at=Sector;newEpoch=0;started=lastPoll=now;
}
// Busy poll shared by the program/erase waits. True once the chip is idle with WEL clear.
bool Reset::step(uint32_t now,unsigned timeoutMs){
  if(uint32_t(now-lastPoll)<2)return false;
  lastPoll=now;
  int s=port.status(port.context);
  if(s<0 || s==255){fail("reset status failure");return false;}
  if(s&1){if(uint32_t(now-started)>=timeoutMs)fail("reset flash busy timeout");return false;}
  if(s&2){fail("reset WEL retained");return false;}
  return true;
}
void Reset::update(uint32_t now){
  if(!active())return;
  if(!port.program || !port.erase){fail("reset unsupported by port");return;}
  switch(phase){
  case Phase::Read:{
    // The highest epoch ever written is found by header, so a torn marker can never let an old epoch recur.
    if(!port.read(port.context,offset,page+offset,64)){fail("reset read failure");return;}
    offset+=64;if(offset<Page)return;
    sector0WasBlank=blank(page,Page);
    if(get(page)==ResetMagic && validPage(page) && get(page+4)!=0xffffffffU)highest=get(page+4);
    phase=Phase::Epochs;return;
  }
  case Phase::Epochs:{
    if(at>=End){
      if(highest>=0xfffffffeU){fail("reset epoch exhausted; use RECORD ERASE");return;}
      newEpoch=highest+1;
      // Blank sector 0 needs no revoke or erase, so no intermediate state can look like fresh flash.
      if(sector0WasBlank){phase=Phase::Marker;return;}
      phase=Phase::Revoke;return;
    }
    if(!port.read(port.context,at,scratch,16)){fail("reset read failure");return;}
    if(get(scratch)==Header){
      if(!port.read(port.context,at+EpochHeaderOffset,scratch,4)){fail("reset read failure");return;}
      const uint32_t e=get(scratch);if(e!=0xffffffffU && e>highest)highest=e;
    }
    at+=Sector;return;
  }
  case Phase::Revoke:{
    // Zeroing the old magic is always a legal NOR program and makes a half-finished reset fail closed.
    int s=port.status(port.context);
    if(s!=0){fail("reset status before revoke");return;}
    static const uint8_t zero[4]={0,0,0,0};
    if(!port.program(port.context,0,zero,4)){fail("reset revoke rejected");return;}
    started=lastPoll=now;phase=Phase::RevokeWait;return;
  }
  case Phase::RevokeWait:
    if(step(now,1000)){offset=0;phase=Phase::RevokeVerify;}return;
  case Phase::RevokeVerify:
    if(!port.read(port.context,0,scratch,4) || get(scratch)){fail("reset revoke verify failed");return;}
    phase=Phase::Erase;return;
  case Phase::Erase:{
    int s=port.status(port.context);
    if(s!=0){fail("reset status before erase");return;}
    if(!port.erase(port.context,0)){fail("reset erase rejected");return;}
    started=lastPoll=now;phase=Phase::EraseWait;return;
  }
  case Phase::EraseWait:
    if(step(now,3000)){offset=0;phase=Phase::EraseVerify;}return;
  case Phase::EraseVerify:{
    if(!port.read(port.context,offset,scratch,64)){fail("reset read failure");return;}
    if(!blank(scratch,64)){fail("reset erase verify failed");return;}
    offset+=64;if(offset==Sector)phase=Phase::Marker;return;
  }
  case Phase::Marker:{
    int s=port.status(port.context);
    if(s!=0){fail("reset status before marker");return;}
    memset(page,255,Page);put(page,ResetMagic);put(page+4,newEpoch);
    put(page+248,~crc32(page,248));put(page+252,0xc0dec0deU);
    if(!port.program(port.context,0,page,Page)){fail("reset marker rejected");return;}
    started=lastPoll=now;phase=Phase::MarkerWait;return;
  }
  case Phase::MarkerWait:
    if(step(now,1000)){offset=0;phase=Phase::MarkerVerify;}return;
  case Phase::MarkerVerify:{
    if(!port.read(port.context,offset,scratch,64)){fail("reset read failure");return;}
    for(unsigned i=0;i<64;++i)if(scratch[i]!=page[offset+i]){fail("reset marker verify failed");return;}
    offset+=64;if(offset==Page)phase=Phase::Done;return;
  }
  default:return;
  }
}
}
