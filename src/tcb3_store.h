#pragma once
#include <stdint.h>
#include <stddef.h>
#include "tcb3_test_limits.h"

// Portable foreground-only engine. No erase API; wire slices <=64 bytes.
namespace Tcb3 {
constexpr uint32_t End=0xFFF000, Sector=4096, Page=256;
constexpr uint32_t Header=0x33485054, Data=0x33445054, Footer=0x33455054;
constexpr unsigned Fragment=216, MetadataBytes=160;
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
  bool releasePending=false,scanned=false,transferring=false;
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
  void update();
  const uint8_t* output(unsigned& n) const {n=ready?packetSize:0;return ready?packet:nullptr;}
  void release(){ready=false;}
  bool done() const {return ended && !ready;}
  bool failed() const {return fault;}
private:
  Port port{};uint8_t packet[280]{},page[256]{};
  uint32_t at=0,sequence=0;unsigned pageIndex=0,offset=0,packetSize=0;
  bool ready=false,opening=true,ended=false,selected=false,fault=false;
  void make(unsigned kind,uint32_t address,const uint8_t*,unsigned length);
};
}
