#include "../../src/tcb3_store.cpp"
#include <string.h>

uint8_t nor[0x1000000];
uint8_t exportPacket[280];
uint32_t entryData[21];
Tcb3::Manifest manifest;
Tcb3::Export exporter;
uint32_t manifestLastReadAddress=0;
int manifestTestStatus=0;
bool manifestTestReadFailure=false;
uint32_t exportSliceReads=0,exportPageReads=0,exportReadViolations=0,exportStatusPolls=0;
bool exportPageFailure=false;

bool readFlash(void*,uint32_t address,uint8_t* output,unsigned length){
  ++exportSliceReads;
  manifestLastReadAddress=address;
  if(manifestTestReadFailure)return false;
  if(!length || length>64 || address>=Tcb3::End || length>Tcb3::End-address)return false;
  memcpy(output,nor+address,length);
  return true;
}
bool readFlashPage(void*,uint32_t address,uint8_t* output){
  ++exportPageReads;
  const unsigned slot=(address%Tcb3::Sector)/Tcb3::Page;
  if(address%Tcb3::Page || address>=Tcb3::End || Tcb3::Page>Tcb3::End-address || !slot || slot==15){
    ++exportReadViolations;return false;
  }
  if(exportPageFailure)return false;
  memcpy(output,nor+address,Tcb3::Page);return true;
}
int flashStatus(void*){++exportStatusPolls;return manifestTestStatus;}
Tcb3::Port flash{nullptr,readFlash,nullptr,nullptr,nullptr,nullptr,flashStatus,nullptr,nullptr};

extern "C" unsigned exportUsePages(unsigned enabled){
  flash.readPage=enabled?readFlashPage:nullptr;
  exportSliceReads=exportPageReads=exportReadViolations=exportStatusPolls=0;
  return 0;
}
extern "C" unsigned exportLastSector(){
  return exporter.beginSession(flash,Tcb3::End/Tcb3::Sector,Tcb3::End-Tcb3::Sector,Tcb3::End);
}
extern "C" unsigned manifestBegin(uint32_t end){
  manifest.begin(flash,0,end);
  return manifest.failed();
}
extern "C" unsigned manifestUpdate(unsigned count){
  while(count-- && !manifest.done())manifest.update();
  return manifest.done();
}
extern "C" unsigned manifestCount(){return manifest.count();}
extern "C" unsigned manifestFailed(){return manifest.failed();}
extern "C" unsigned manifestIncomplete(){return manifest.incompleteCountValue();}
extern "C" unsigned manifestOverflow(){return manifest.overflow();}
extern "C" unsigned manifestError(){return uint32_t(reinterpret_cast<uintptr_t>(manifest.errorMessage()));}
extern "C" unsigned manifestEntry(unsigned index){
  const Tcb3::ManifestEntry* entry=manifest.entry(index);
  if(!entry)return 0;
  memset(entryData,0,sizeof(entryData));
  entryData[0]=entry->sessionId;entryData[1]=entry->resetEpoch;
  entryData[2]=entry->firstAddress;entryData[3]=entry->sectorCount;
  entryData[4]=entry->formatVersion;entryData[5]=entry->flags;
  entryData[6]=entry->footerReason;entryData[7]=uint32_t(entry->startUs);
  entryData[8]=uint32_t(entry->startUs>>32);entryData[9]=uint32_t(entry->stopUs);
  entryData[10]=uint32_t(entry->stopUs>>32);entryData[11]=uint32_t(entry->deviceId);
  entryData[12]=uint32_t(entry->deviceId>>32);
  for(unsigned i=0;i<4;++i){
    entryData[13+i]=uint32_t(entry->committed[i]);
    entryData[17+i]=uint32_t(entry->lost[i]);
  }
  return 1;
}
extern "C" unsigned exportBegin(uint32_t id){
  if(id!=3)return 0;
  return exporter.beginSession(flash,id,2*Tcb3::Sector,11*Tcb3::Sector);
}
extern "C" unsigned exportAllBegin(){
  exporter.begin(flash);
  return 0;
}
extern "C" unsigned exportStep(){
  exporter.update();
  unsigned length=0;const uint8_t* data=exporter.output(length);
  if(!data)return 0;
  memcpy(exportPacket,data,length);
  return length;
}
extern "C" unsigned exportRelease(){exporter.release();return 0;}
extern "C" unsigned exportDone(){return exporter.done();}
extern "C" unsigned exportFailed(){return exporter.failed();}
