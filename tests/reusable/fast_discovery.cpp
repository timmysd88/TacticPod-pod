#include "fast_discovery.h"
#include <string.h>

namespace FastDiscovery {
using namespace Tcb3;
namespace {
bool allFF(const uint8_t* p, unsigned n){while(n--)if(*p++!=0xff)return false;return true;}
uint64_t get64(const uint8_t* p){return uint64_t(get(p))|(uint64_t(get(p+4))<<32);}
bool envelope(const uint8_t* p, uint32_t magic){
  return validPage(p) && get(p)==magic && get(p+12,2)==MetadataBytes &&
    get(p+14,2)==0 && allFF(p+16+MetadataBytes,232-MetadataBytes);
}
}

void Scanner::begin(Port p, uint32_t end){
  port=p; limit=end; address=epoch=sectorSequence=pageSequence=incompleteCount=0;
  entryCount=offset=0; sessionOpen=tooMany=false; phase=Phase::Marker;
  outcome=Result::Scanning; problem=nullptr;
  memset(entries,0,sizeof(entries)); memset(&current,0,sizeof(current));
  if(!port.read || !port.status || !end || end>End || end%Sector)
    fail(Result::Ambiguous,"invalid discovery range");
}
bool Scanner::read(uint32_t at, uint8_t* output, unsigned size){
  const int status=port.status(port.context);
  if(status<0 || status==255 || (status&3) || !port.read(port.context,at,output,size)){
    fail(Result::IoError,"flash status or read failed");return false;
  }
  return true;
}
bool Scanner::readPage(uint32_t at){
  const unsigned n=(Page-offset<64)?Page-offset:64;
  if(!read(at+offset,page+offset,n))return false;
  offset+=n;return offset==Page;
}
void Scanner::fail(Result result, const char* message){outcome=result;problem=message;}
void Scanner::finish(){
  if(sessionOpen){++incompleteCount;sessionOpen=false;}
  outcome=tooMany?Result::Overflow:incompleteCount?Result::Incomplete:Result::Complete;
}
void Scanner::next(){
  address+=Sector;offset=0;
  if(address>=limit)finish();else phase=Phase::Probe;
}
void Scanner::header(){
  if(allFF(page,Page)){
    if(sessionOpen){++incompleteCount;sessionOpen=false;}
    if(epoch)finish();else next();
    return;
  }
  const uint32_t magic=get(page);
  if(magic==0x31485054 || magic==0x32485054){
    if(sessionOpen){++incompleteCount;sessionOpen=false;}
    if(epoch)fail(Result::Ambiguous,"legacy header at epoch boundary");else next();
    return;
  }
  if(magic!=Header){fail(Result::Ambiguous,"unrecognised or torn sector header");return;}
  if(!envelope(page,Header)){fail(Result::Corrupt,"unsealed or corrupt header");return;}
  const uint32_t id=get(page+4), sequence=get(page+20), pages=get(page+8);
  const uint32_t version=get(page+16), pageEpoch=get(page+120), flags=get(page+84);
  if(!id || (version!=0x00030100 && version!=0x00030200) || get(page+80)!=1 ||
     (flags&~3U) || (!(flags&1U) && get64(page+32)) ||
     (!(flags&2U) && !allFF(page+88,32)) || !pageEpoch ||
     get64(page+24)==~uint64_t(0) || !allFF(page+124,52)){
    fail(Result::Corrupt,"invalid header metadata");return;
  }
  if(epoch && pageEpoch!=epoch){finish();return;}
  if(sessionOpen && sequence==0){
    ++incompleteCount;sessionOpen=false;
  }
  if(!sessionOpen){
    if(sequence || pages || id!=address/Sector+1){
      fail(Result::Ambiguous,"missing session prefix");return;
    }
    memset(&current,0,sizeof(current));
    current.provisional=true;
    auto& e=current.metadata;
    e.sessionId=id;e.resetEpoch=pageEpoch;e.firstAddress=address;e.sectorCount=1;
    e.formatVersion=version;e.startUs=get64(page+24);e.deviceId=(flags&1U)?get64(page+32):0;
    memcpy(metadata,page+16,MetadataBytes);memset(metadata+4,0,4);
    sectorSequence=pageSequence=0;sessionOpen=true;
  }else{
    bool same=true;
    for(unsigned i=0;i<MetadataBytes;++i)
      if(((i>=4 && i<8)?0:page[16+i])!=metadata[i])same=false;
    if(id!=current.metadata.sessionId || sequence!=sectorSequence+1 || !same ||
       pages<pageSequence || pages-pageSequence>14){
      fail(Result::Corrupt,"continuation identity or sequence mismatch");return;
    }
    ++current.metadata.sectorCount;sectorSequence=sequence;pageSequence=pages;
  }
  offset=0;phase=Phase::Footer;
}
void Scanner::footer(){
  if(allFF(page,Page)){next();return;}
  auto& e=current.metadata;
  const uint32_t pages=get(page+8), reason=get(page+16);
  if(!sessionOpen || !envelope(page,Footer) || get(page+4)!=e.sessionId ||
     pages<pageSequence || pages-pageSequence>14 || reason>3 || get(page+20)!=1 ||
     get(page+148)>pages || get(page+152)!=pages || get(page+156)!=0 ||
     !allFF(page+160,16) || get64(page+24)<e.startUs || get64(page+24)==~uint64_t(0)){
    fail(Result::Corrupt,"invalid footer metadata");return;
  }
  bool clean=reason==0;
  for(unsigned i=0;i<4;++i){
    const uint64_t offered=get64(page+32+8*i), committed=get64(page+64+8*i), lost=get64(page+96+8*i);
    if(offered<committed || offered-committed!=lost){
      fail(Result::Corrupt,"invalid footer accounting");return;
    }
    e.committed[i]=committed;e.lost[i]=lost;clean=clean && !lost;
  }
  for(unsigned i=128;i<148;i+=4)clean=clean && get(page+i)==0;
  if(reason==0 && !clean){fail(Result::Corrupt,"clean footer reports loss");return;}
  e.stopUs=get64(page+24);e.footerReason=reason;
  // No ManifestEntry validation flags are asserted for provisional candidates.
  if(entryCount<MaxManifestSessions)entries[entryCount++]=current;else tooMany=true;
  sessionOpen=false;next();
}
void Scanner::update(){
  if(done())return;
  if(phase==Phase::Marker){
    if(!readPage(0))return;
    offset=0;
    const uint32_t magic=get(page);
    if(magic==ResetMagic){
      if(!validPage(page) || !get(page+4) || get(page+4)==0xffffffffU || !allFF(page+8,240)){
        fail(Result::Corrupt,"invalid reset marker");return;
      }
      epoch=get(page+4);address=Sector;phase=Phase::Probe;
    }else if(allFF(page,Page)){
      address=Sector;phase=Phase::CleanPrefix;
    }else if(magic==Header || magic==0x31485054 || magic==0x32485054){
      address=0;header();
    }else fail(Result::Ambiguous,"unrecognised or torn reset marker");
    if(!done() && address>=limit)finish();
    return;
  }
  if(phase==Phase::CleanPrefix){
    if(address>=limit){finish();return;}
    if(address>=Tcb3Test::WriteStart){phase=Phase::Probe;return;}
    uint8_t prefix[16];
    if(!read(address,prefix,sizeof(prefix)))return;
    if(!allFF(prefix,sizeof(prefix))){fail(Result::Ambiguous,"data below protected boundary");return;}
    address+=Sector;return;
  }
  if(phase==Phase::Probe){
    if(!read(address,page,16))return;
    if(allFF(page,16) && !epoch){
      if(sessionOpen){++incompleteCount;sessionOpen=false;}
      next();return;
    }
    if((get(page)==0x31485054 || get(page)==0x32485054) && !epoch){
      if(sessionOpen){++incompleteCount;sessionOpen=false;}
      next();return;
    }
    offset=16;phase=Phase::Header;return;
  }
  if(phase==Phase::Header){
    if(readPage(address))header();
    return;
  }
  if(phase==Phase::Footer && readPage(address+15*Page))footer();
}
}
