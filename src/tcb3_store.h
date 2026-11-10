#pragma once
#include <stdint.h>
#include <stddef.h>
#include "tcb3_test_limits.h"

// Portable foreground-only engine; wire slices <=64 bytes. Sector erase exists
// only for the reusable RECORD RESET epoch mechanism (see docs/fast-record-reset.md).
namespace Tcb3 {
constexpr uint32_t End=0xFFF000, Sector=4096, Page=256;
constexpr uint32_t Header=0x33485054, Data=0x33445054, Footer=0x33455054;
constexpr uint32_t ResetMagic=0x33525054; // "TPR3": reset marker page at address 0.
constexpr unsigned Fragment=216, MetadataBytes=160;
constexpr unsigned MaxManifestSessions=64;
// Reserved header word (page offset 120, metadata offset 104) carrying the reset epoch.
constexpr unsigned EpochMetadataOffset=104, EpochHeaderOffset=16+EpochMetadataOffset;
struct Port {
  void* context;
  bool (*read)(void*,uint32_t,uint8_t*,unsigned);
  // One CS-low page transaction, streamed in <=64-byte wire slices. No other
  // flash command is permitted between beginPage and endPage/abortPage.
  bool (*beginPage)(void*,uint32_t);
  bool (*pageBytes)(void*,const uint8_t*,unsigned);
  bool (*endPage)(void*);
  void (*abortPage)(void*); // Release CS; a partial program may result: fault locks.
  int (*status)(void*); // NOR status: WIP bit0, WEL bit1; -1/0xff invalid.
  // Optional (reusable reset only). program: one WREN+page program of <=256 bytes inside one page.
  // erase: start one WREN+4 KB sector erase; completion is observed through status().
  bool (*program)(void*,uint32_t,const uint8_t*,unsigned);
  bool (*erase)(void*,uint32_t);
};
enum class State : uint8_t { Scan, Idle, Blank, Header, Ready, Validate, Data, Footer, Fault, Probe };
enum class Reason : uint32_t { Clean=0, Full=1, Overflow=2, Deadline=3 };
struct Stats {
  uint64_t committed[4],offered[4];
  uint32_t blocks,pages,crcFailures,incompleteWrites,full,missed,rejected;
  uint32_t next,session,sectorSequence,pageSequence;
  uint32_t programOperations,headerPages,crcBytes,busyPolls;
  uint32_t maxBlockMs;
  uint32_t probeSkipped;
  uint32_t eraseOperations,maxEraseMs;
  bool closing,finished;
};
uint32_t crc32(const uint8_t*,unsigned,uint32_t seed=0xffffffffU);
uint32_t get(const uint8_t*,unsigned bytes=4);
void put(uint8_t*,uint64_t,unsigned bytes=4);
bool validPage(const uint8_t*);
void metadata(uint8_t*,uint64_t startUs); // Declared target rates; availability flags=0.
class Store {
public:
  void begin(Port,uint32_t nowMs);
  bool cancelScan();
  void halt(const char* why); // Fault-lock; releases any held CS, never retries.
  void update(uint32_t nowMs);
  bool start(const uint8_t metadata[MetadataBytes]);
  bool offer(const uint8_t*,unsigned); // Immutable lease until released()/fault.
  void finish(uint64_t stopUs,const uint64_t offered[4],Reason,uint32_t rejected,uint32_t missed);
  bool released();
  State state() const {return stage;}
  bool accepting() const {return stage==State::Ready && !block && !stats.closing && !stats.full;}
  bool active() const {return stage!=State::Idle && stage!=State::Scan && stage!=State::Probe && stage!=State::Fault;}
  uint32_t initialAddress() const {return recoveredStart;}
  // Lowest address a session may use. A valid reset marker owns sector 0, so sessions begin at sector 1.
  uint32_t floor() const {return Tcb3Test::Reusable && epoch?Sector:Tcb3Test::WriteStart;}
  uint32_t resetEpoch() const {return epoch;}
  Stats stats{};
  const char* error=nullptr;
private:
  Port port{};State stage=State::Idle;
  uint8_t meta[MetadataBytes]{},page[Page]{},scratch[64]{};
  const uint8_t* block=nullptr;
  uint32_t scan=0,sector=0,at=0,started=0,lastPoll=0,blockCrc=0,blockExpected=0,blockAt=0;
  uint64_t stopUs=0;Reason reason=Reason::Clean;
  unsigned blankAt=0,pageIndex=1,length=0,offset=0,fragment=0,checkAt=0;
  unsigned io=0,ioOffset=0,recoveredStart=0,verifyAt=0;
  uint32_t verifyCrc=0xffffffffU;
  bool releasePending=false,scanned=false,transferring=false,sector0Blank=false;
  uint32_t epoch=0;unsigned scanStep=0;
  void endEpochScan();
  void fail(const char*,bool incomplete=false);
  bool read(uint32_t,uint8_t*,unsigned);
  void envelope(uint32_t,unsigned);
  void writePage(uint32_t);
  void service(uint32_t);
  void committed(uint32_t now);
};

// Read-only packet export. All TPH3-prefix sectors, including torn headers,
// yield their complete 16 pages. Other occupied headers yield ISSUE packets.
class Export {
public:
  void begin(Port);
  bool beginSession(Port,uint32_t session,uint32_t firstAddress,uint32_t endAddress);
  void update();
  const uint8_t* output(unsigned& n) const {n=ready?packetSize:0;return ready?packet:nullptr;}
  void release(){ready=false;}
  bool done() const {return ended && !ready;}
  bool failed() const {return fault;}
private:
  Port port{};uint8_t packet[280]{},page[256]{};
  uint32_t at=0,sequence=0,epoch=0,probeAt=0,sessionId=0,sessionEnd=End;
  unsigned pageIndex=0,offset=0,packetSize=0,phase=0;
  bool ready=false,opening=true,ended=false,selected=false,fault=false,sessionOnly=false;
  void make(unsigned kind,uint32_t address,const uint8_t*,unsigned length);
};

struct ManifestEntry {
  uint32_t sessionId=0,resetEpoch=0,firstAddress=0,sectorCount=0,formatVersion=0,footerReason=0;
  uint32_t flags=0; // bit 0: footer sealed; bit 1: stored pages/blocks validated; bit 2: clean completion.
  uint64_t startUs=0,stopUs=0,deviceId=0;
  uint64_t committed[4]{},lost[4]{};
};

class Manifest {
public:
  void begin(Port,uint32_t firstAddress,uint32_t endAddress);
  void update();
  bool done() const {return phase==Phase::Done || phase==Phase::Fault;}
  bool failed() const {return phase==Phase::Fault;}
  bool overflow() const {return tooMany;}
  bool incomplete() const {return incompleteCount!=0;}
  uint32_t incompleteCountValue() const {return incompleteCount;}
  unsigned count() const {return entryCount;}
  const ManifestEntry* entry(unsigned index) const {return index<entryCount?entries+index:nullptr;}
  const char* errorMessage() const {return error;}
private:
  enum class Phase:uint8_t {Marker,MarkerTail,CleanPrefix,EpochProbe,Probe,Header,Data,Footer,Done,Fault};
  Port port{};
  ManifestEntry entries[MaxManifestSessions]{},current{};
  uint8_t page[Page]{},metadata[MetadataBytes]{},blockHeader[24]{},blockTail[4]{};
  uint32_t address=0,limit=0,resetEpoch=0,sectorSequence=0,nextPage=0,expectedBlock=0,blockSequence=0;
  uint32_t blockLength=0,blockCrcExpected=0,blockCrc=0xffffffffU,blockReceived=0,blockCount=0;
  uint64_t observed[4]{};
  uint32_t incompleteCount=0;
  unsigned entryCount=0,offset=0,pageIndex=0;
  Phase phase=Phase::Done;
  bool sessionOpen=false,blockOpen=false,tooMany=false;
  const char* error=nullptr;
  bool readPageSlice();
  void fail(const char*);
  void processHeader();
  void processDataPage();
  void processFooter();
  void advanceSector();
};

// Fast logical reset: erase sector 0, then program a CRC-sealed marker carrying a new epoch.
// Sessions written under an older epoch become invisible and are erased lazily before reuse.
// Order: scan for the highest epoch ever written, revoke the old sector-0 magic, erase, program marker.
class Reset {
public:
  void begin(Port,uint32_t nowMs);
  void update(uint32_t nowMs);
  bool active() const {return phase!=Phase::Idle && phase!=Phase::Done && phase!=Phase::Fault;}
  bool done() const {return phase==Phase::Done;}
  bool failed() const {return phase==Phase::Fault;}
  uint32_t epoch() const {return newEpoch;}
  const char* error=nullptr;
private:
  enum class Phase : uint8_t { Idle, Read, Epochs, Revoke, RevokeWait, RevokeVerify, Erase, EraseWait, EraseVerify, Marker, MarkerWait, MarkerVerify, Done, Fault };
  Port port{};Phase phase=Phase::Idle;uint8_t page[Page]{},scratch[64]{};
  uint32_t started=0,lastPoll=0,newEpoch=0,highest=0,at=0;unsigned offset=0;bool sector0WasBlank=false;
  bool step(uint32_t now,unsigned timeoutMs);
  void fail(const char*);
};
}
