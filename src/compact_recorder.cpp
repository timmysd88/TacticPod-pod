#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "compact_recorder.h"
#include "monotonic_clock.h"
#include "recorder.h"

namespace {
namespace CompactDetail {
using namespace CompactRecorder;
enum class SourceState : uint8_t { Free, Fill, Sealed, Encode };
struct Source {
  uint8_t bytes[kInputBytes];
  unsigned size = 0, count = 0;
  uint32_t sequence = 0;
  uint64_t first = 0;
  SourceState state = SourceState::Free;
} sources[2];
struct Destination { uint8_t bytes[kFrameBytes]; unsigned size = 0; uint32_t sequence = 0; bool busy = false, ready = false; } destinations[2];
Stats totals = {};
int filling = -1;
uint32_t nextSequence = 0;
uint64_t lastAccepted = 0;
bool haveAccepted = false;
const uint8_t sizes[] = {0,24,44,40,32}, columns[] = {0,6,10,10,6};
struct Field { uint8_t offset, bytes; bool sign; };
const Field fields[4][10] = {
  {{12,2,true},{14,2,true},{16,2,true},{18,2,true},{20,2,true},{22,2,true}},
  {{12,4,false},{16,4,true},{20,4,true},{24,4,false},{28,4,false},{32,4,false},{36,4,false},{40,1,false},{41,1,false},{42,2,false}},
  {{12,4,false},{16,4,false},{20,4,false},{24,4,true},{28,4,true},{32,2,false},{34,2,false},{36,1,false},{37,2,false},{39,1,false}},
  {{12,2,false},{14,1,false},{15,1,false},{16,4,false},{20,4,false},{24,8,false}}
};
uint64_t load(const uint8_t* p, unsigned n) { uint64_t v = 0; for (unsigned i=0;i<n;++i) v |= uint64_t(p[i]) << (8*i); return v; }
void store(uint8_t* p, uint64_t v, unsigned n) { for (unsigned i=0;i<n;++i) p[i] = v >> (8*i); }
unsigned uvSize(uint64_t n) { unsigned bytes=1; while (n>=128) { ++bytes; n>>=7; } return bytes; }
uint64_t zz(int64_t n) { return n>=0 ? uint64_t(n)*2 : uint64_t(-(n+1))*2+1; }
unsigned width(uint64_t n) { unsigned bits=0; while(n) {++bits;n>>=1;} return bits; }
uint64_t gcd(uint64_t a,uint64_t b) {
  // Admission bounds every offset/gap below 2,000,000us; use native UDIV.
  uint32_t x=uint32_t(a),y=uint32_t(b);
  while(y) {uint32_t r=x%y;x=y;y=r;} return x;
}
unsigned sourceUsed() { unsigned n=0; for(const auto& s:sources) if(s.state!=SourceState::Free) ++n; return n; }
unsigned readyUsed() { unsigned n=0; for(const auto& d:destinations) if(d.ready) ++n; return n; }
void acquire() {
  if(filling>=0 || !totals.capturing) return;
  for(unsigned i=0;i<2;++i) if(sources[i].state==SourceState::Free) {
    filling=i; auto& s=sources[i]; s.size=s.count=0; s.state=SourceState::Fill; return;
  }
}
void seal() {
  if(filling<0) return;
  auto& s=sources[filling];
  if(!s.count) {s.state=SourceState::Free;filling=-1;return;}
  s.sequence=nextSequence++; s.state=SourceState::Sealed; filling=-1; ++totals.sealed;
  acquire();
}
enum class Phase : uint8_t { Idle, Index, TimeGather, TimeCost, TimeEmit, FieldGather, FieldSelect, FieldEmit, Header, RawCopy, Crc, Finish };
struct Workspace {
  uint16_t indices[4][kRecords];
  int64_t values[kRecords]; // time offsets or one numeric column, never 128-bit math
  unsigned counts[4], at, row, kind, column, mode, seed, emitStage;
  unsigned cost[7], regular[2][17], escapes[2][17], riceK[2];
  uint64_t maximum[2], quantum, timeCost[4], zeroRun;
  uint64_t bits, cpu; unsigned pending, length, copyAt, crcAt;
  uint32_t crc;
  int source = -1, destination = -1;
  Phase phase = Phase::Idle;
} work;
uint8_t* target() { return destinations[work.destination].bytes; }
Source& source() { return sources[work.source]; }
void byte(uint8_t n) { if(work.length<kFrameBytes) target()[work.length++]=n; }
void uv(uint64_t n) { while(n>=128) {byte(uint8_t(n)|128);n>>=7;} byte(n); }
void rawNumber(uint64_t n,unsigned bytes) {for(unsigned i=0;i<bytes;++i) byte(n>>(8*i));}
void bits(uint64_t n,unsigned count) {
  work.bits |= n<<work.pending; work.pending+=count;
  while(work.pending>=8) {byte(work.bits);work.bits>>=8;work.pending-=8;}
}
void endBits() {if(work.pending) byte(work.bits);work.bits=0;work.pending=0;}
const uint8_t* indexed(unsigned row) {return source().bytes+work.indices[work.kind-1][row];}
int64_t fieldValue(unsigned row) {
  const Field& f=fields[work.kind-1][work.column];
  uint64_t n=load(indexed(row)+f.offset,f.bytes);
  if(f.sign && (n & (uint64_t(1)<<(f.bytes*8-1)))) n |= UINT64_MAX << (f.bytes*8);
  return int64_t(n);
}
void beginKind() {
  while(work.kind<=4 && !work.counts[work.kind-1]) ++work.kind;
  if(work.kind>4) {work.phase=Phase::Header;return;}
  work.row=0;work.quantum=0;work.phase=Phase::TimeGather;
}
void beginField() {
  if(work.column==columns[work.kind]) {++work.kind;beginKind();return;}
  work.row=0;work.maximum[0]=work.maximum[1]=0;
  memset(work.regular,0,sizeof(work.regular)); memset(work.escapes,0,sizeof(work.escapes));
  memset(work.cost,0,sizeof(work.cost));
  work.phase=Phase::FieldGather;
}
void addResidual(unsigned predictor,uint64_t residual) {
  if(residual>work.maximum[predictor]) work.maximum[predictor]=residual;
  for(unsigned k=0;k<=16;++k) {
    uint64_t q=residual>>k;
    if(q>=31) ++work.escapes[predictor][k];
    else work.regular[predictor][k]+=unsigned(q)+1+k;
  }
}
void fallback() {work.phase=Phase::RawCopy;work.copyAt=0;work.length=32;++totals.rawFallbacks;}
void step() {
  if(work.phase==Phase::Idle) {
    int input=-1,output=-1;
    for(unsigned i=0;i<2;++i) {
      if(sources[i].state==SourceState::Sealed && (input<0 || sources[i].sequence<sources[input].sequence)) input=i;
      if(!destinations[i].busy && !destinations[i].ready) output=i;
    }
    if(input<0) return;
    if(output<0) {++totals.backpressureSlices;return;}
    work.source=input;work.destination=output;work.cpu=0;
    source().state=SourceState::Encode; destinations[output].busy=true;
    memset(work.counts,0,sizeof(work.counts));
    work.at=work.row=work.copyAt=0;work.length=32+(source().count*2+7)/8;
    memset(target(),0,work.length>=96 ? 96 : work.length); // header + max order map
    work.phase=Phase::Index;return;
  }
  // Any expansion switches immediately to bounded byte-for-byte raw fallback.
  if(work.length>source().size+32 && work.phase!=Phase::RawCopy && work.phase!=Phase::Crc && work.phase!=Phase::Finish) {fallback();return;}
  const unsigned count=work.kind>=1 && work.kind<=4 ? work.counts[work.kind-1] : 0;
  if(work.phase==Phase::Index) {
    if(work.row<source().count) {
      unsigned kind=source().bytes[work.at];
      work.indices[kind-1][work.counts[kind-1]++]=work.at;
      target()[32+work.row/4] |= (kind-1)<<((work.row%4)*2);
      work.at+=sizes[kind];++work.row;return;
    }
    work.kind=1;beginKind();return;
  }
  if(work.phase==Phase::TimeGather) {
    if(work.row<count) {
      work.values[work.row]=int64_t(load(indexed(work.row)+4,8)-source().first);
      if(work.row) work.quantum=gcd(work.quantum,uint64_t(work.values[work.row]-work.values[work.row-1]));
      ++work.row;return;
    }
    if(!work.quantum) work.quantum=1;
    unsigned first=uvSize(uint64_t(work.values[0]));
    work.timeCost[0]=work.timeCost[1]=work.timeCost[3]=first;
    work.timeCost[2]=first+uvSize(work.quantum);work.row=1;work.zeroRun=0;work.phase=Phase::TimeCost;return;
  }
  if(work.phase==Phase::TimeCost) {
    if(work.row<count) {
      int64_t gap=work.values[work.row]-work.values[work.row-1];
      int64_t prev=work.row>1 ? work.values[work.row-1]-work.values[work.row-2] : 0;
      work.timeCost[0]+=uvSize(gap);work.timeCost[1]+=uvSize(zz(gap-prev));
      work.timeCost[2]+=uvSize(zz(gap/int64_t(work.quantum)-prev/int64_t(work.quantum)));
      if(work.row==1) work.timeCost[3]+=uvSize(gap);
      else if(gap==prev) ++work.zeroRun;
      else {
        if(work.zeroRun) {work.timeCost[3]+=1+uvSize(work.zeroRun);work.zeroRun=0;}
        work.timeCost[3]+=uvSize(zz(gap-prev)+1);
      }
      ++work.row;return;
    }
    if(work.zeroRun) work.timeCost[3]+=1+uvSize(work.zeroRun);
    work.mode=0;for(unsigned m=1;m<4;++m) if(work.timeCost[m]<work.timeCost[work.mode]) work.mode=m;
    byte(work.mode);uv(work.values[0]);if(work.mode==2) uv(work.quantum);
    work.row=1;work.zeroRun=0;work.phase=Phase::TimeEmit;return;
  }
  if(work.phase==Phase::TimeEmit) {
    if(work.row<count) {
      int64_t gap=work.values[work.row]-work.values[work.row-1];
      int64_t prev=work.row>1 ? work.values[work.row-1]-work.values[work.row-2] : 0;
      if(work.mode==0) uv(gap);
      else if(work.mode==1) uv(zz(gap-prev));
      else if(work.mode==2) uv(zz(gap/int64_t(work.quantum)-prev/int64_t(work.quantum)));
      else if(work.row==1) uv(gap);
      else if(gap==prev) ++work.zeroRun;
      else {
        if(work.zeroRun) {uv(0);uv(work.zeroRun);work.zeroRun=0;return;}
        uv(zz(gap-prev)+1);
      }
      ++work.row;return;
    }
    if(work.zeroRun) {uv(0);uv(work.zeroRun);work.zeroRun=0;return;}
    work.column=0;beginField();return;
  }
  const Field f=work.kind>=1 && work.kind<=4 ? fields[work.kind-1][work.column] : Field{};
  if(work.phase==Phase::FieldGather) {
    if(work.row<count) {
      if(f.bytes<8) {
        work.values[work.row]=fieldValue(work.row);
        if(work.row) {uint64_t r=zz(work.values[work.row]-work.values[work.row-1]);work.cost[1]+=uvSize(r);addResidual(0,r);}
        if(work.row>1) {uint64_t r=zz(work.values[work.row]-2*work.values[work.row-1]+work.values[work.row-2]);work.cost[2]+=uvSize(r);addResidual(1,r);}
      }
      ++work.row;return;
    }
    work.phase=Phase::FieldSelect;return;
  }
  if(work.phase==Phase::FieldSelect) {
    work.cost[0]=count*f.bytes;
    work.cost[1]+=f.bytes; work.cost[2]+=(count<2?count:2)*f.bytes;
    unsigned seed1=f.bytes,seed2=(count<2?count:2)*f.bytes;
    work.cost[3]=seed1+1+((count-1)*width(work.maximum[0])+7)/8;
    work.cost[4]=seed2+1+((count-(count<2?count:2))*width(work.maximum[1])+7)/8;
    for(unsigned p=0;p<2;++p) {
      unsigned best=0xffffffffUL;
      const unsigned residualWidth=width(work.maximum[p]);
      for(unsigned k=0;k<=16;++k) {
        unsigned n=work.regular[p][k]+work.escapes[p][k]*(31+residualWidth);
        if(n<best) {best=n;work.riceK[p]=k;}
      }
      work.cost[5+p]=(p?seed2:seed1)+2+(best+7)/8;
    }
    work.mode=0;
    if(f.bytes<8) for(unsigned m=1;m<7;++m) if(work.cost[m]<work.cost[work.mode]) work.mode=m;
    byte(work.mode);work.seed=(work.mode==2 || work.mode==4 || work.mode==6) ? (count<2?count:2) : 1;
    work.row=0;work.emitStage=0;work.bits=0;work.pending=0;work.phase=Phase::FieldEmit;return;
  }
  if(work.phase==Phase::FieldEmit) {
    if(work.mode==0) {
      if(work.row<count) {rawNumber(load(indexed(work.row)+f.offset,f.bytes),f.bytes);++work.row;return;}
    } else {
      if(work.row<work.seed) {rawNumber(load(indexed(work.row)+f.offset,f.bytes),f.bytes);++work.row;return;}
      unsigned p=(work.mode==2 || work.mode==4 || work.mode==6) ? 1 : 0;
      if(!work.emitStage) {
        if(work.mode==3 || work.mode==4) byte(width(work.maximum[p]));
        if(work.mode==5 || work.mode==6) {byte(work.riceK[p]);byte(width(work.maximum[p]));}
        work.emitStage=1;return;
      }
      if(work.row<count) {
        int64_t r=work.values[work.row]-work.values[work.row-1];
        if(p) r-=work.values[work.row-1]-work.values[work.row-2];
        uint64_t residual=zz(r);
        if(work.mode==1 || work.mode==2) uv(residual);
        else if(work.mode==3 || work.mode==4) bits(residual,width(work.maximum[p]));
        else {
          unsigned k=work.riceK[p];uint64_t q=residual>>k;
          if(q>=31) {bits(0x7fffffffUL,31);bits(residual,width(work.maximum[p]));}
          else {bits((uint64_t(1)<<q)-1,unsigned(q));bits(0,1);bits(residual&((uint64_t(1)<<k)-1),k);}
        }
        ++work.row;return;
      }
      endBits();
    }
    ++work.column;beginField();return;
  }
  if(work.phase==Phase::RawCopy) {
    for(unsigned n=0;n<16 && work.copyAt<source().size;++n) byte(source().bytes[work.copyAt++]);
    if(work.copyAt==source().size) {work.mode=0;work.phase=Phase::Header;}
    return;
  }
  if(work.phase==Phase::Header) {
    memcpy(target(),"TCB3",4);target()[4]=0;
    target()[5]=work.copyAt==source().size && work.copyAt ? 0 : 8;
    target()[6]=target()[7]=0;
    store(target()+8,source().sequence,4);store(target()+12,source().size,2);store(target()+14,work.length-32,2);
    for(unsigned k=0;k<4;++k) store(target()+16+k*2,work.counts[k],2);
    store(target()+24,source().first,8);
    work.crc=0xffffffffUL;work.crcAt=0;work.phase=Phase::Crc;return;
  }
  if(work.phase==Phase::Crc) {
    for(unsigned n=0;n<16 && work.crcAt<work.length;++n) {
      work.crc^=target()[work.crcAt++];
      for(unsigned bit=0;bit<8;++bit) work.crc=(work.crc>>1)^((work.crc&1)?0xedb88320UL:0);
    }
    if(work.crcAt==work.length) {rawNumber(~work.crc,4);work.phase=Phase::Finish;}
    return;
  }
  if(work.phase==Phase::Finish) {
    auto& d=destinations[work.destination];d.size=work.length;d.sequence=source().sequence;d.busy=false;d.ready=true;
    source().state=SourceState::Free;work.source=-1;work.phase=Phase::Idle;++totals.completed;
    unsigned n=readyUsed();if(n>totals.outputHigh) totals.outputHigh=n;
    acquire();
  }
}
char reportLine[256];unsigned reportIndex=0,reportLength=0;bool reporting=false;
Stats reportStats;const char* notice="RAM only";
void text(const char* p) {while(*p && reportLength+1<sizeof(reportLine)) reportLine[reportLength++]=*p++;}
void number(uint64_t n) {char b[21];char* p=b+20;*p=0;do{*--p='0'+n%10;n/=10;}while(n);text(p);}
void request(const char* message) {notice=message;reportIndex=reportLength=0;reporting=true;reportStats=CompactRecorder::stats();}
void report() {
  if(!reporting || !Serial || Recorder::exporting()) return;
  if(!reportLength) {
    text("COMP ");
    if(reportIndex==0) {text(notice);text(" | capturing=");number(reportStats.capturing);text(" fault=");number(reportStats.fault);}
    else if(reportIndex==1) {text("records accepted=");number(reportStats.accepted);text(" rejected=");number(reportStats.rejected);text(" blocks sealed=");number(reportStats.sealed);text(" completed=");number(reportStats.completed);}
    else if(reportIndex==2) {text("RAM input=");number(reportStats.inputUsed);text("/2 output=");number(reportStats.outputReady);text("/2 input_high=");number(reportStats.inputHigh);text(" output_high=");number(reportStats.outputHigh);text(" backpressure_slices=");number(reportStats.backpressureSlices);text(" raw_fallbacks=");number(reportStats.rawFallbacks);}
    else if(reportIndex==3) {text("TIMING work_us=");number(reportStats.workUs);text(" max_block_step_us=");number(reportStats.maxBlockWorkUs);text(" max_slice_us=");number(reportStats.maxSliceUs);text(" max_units=");number(reportStats.maxUnits);}
    else text("END | RAM only; no flash sink; timing requires hardware validation");
    text("\n");
  }
  if(Serial.availableForWrite()<int(reportLength)) return;
  if(Serial.write(reinterpret_cast<const uint8_t*>(reportLine),reportLength)!=reportLength) return;
  reportLength=0;if(++reportIndex==5) reporting=false;
}
}
}

namespace CompactRecorder {
using namespace CompactDetail;
bool capturing() {return totals.capturing;}
Stats stats() {Stats s=totals;s.inputUsed=sourceUsed();s.outputReady=readyUsed();return s;}
bool clear() {
  if(totals.capturing) return false;
  for(auto& s:sources) {s.state=SourceState::Free;s.size=s.count=0;}
  for(auto& d:destinations) {d.busy=d.ready=false;d.size=0;}
  totals=Stats{};filling=-1;nextSequence=0;haveAccepted=false;work.source=-1;work.phase=Phase::Idle;return true;
}
bool start() {
  if(totals.capturing || sourceUsed() || readyUsed()) return false;
  clear();totals.capturing=true;acquire();return true;
}
void stop() {totals.capturing=false;seal();}
void observe(const uint8_t* value,size_t length) {
  if(!totals.capturing) return;
  unsigned kind=length?value[0]:0;
  if(kind<1 || kind>4 || length!=sizes[kind] || value[1]!=1 || load(value+2,2)!=length) {
    ++totals.rejected;totals.fault=true;stop();return;
  }
  uint64_t timestamp=load(value+4,8);
  if(timestamp==UINT64_MAX || (haveAccepted && timestamp<lastAccepted)) {++totals.rejected;totals.fault=true;stop();return;}
  acquire();
  if(filling>=0 && sources[filling].count && (timestamp-sources[filling].first>=2000000 ||
      sources[filling].count==kRecords || sources[filling].size+length>kInputBytes)) seal();
  acquire();
  if(filling<0) {++totals.rejected;totals.fault=true;totals.capturing=false;return;}
  auto& s=sources[filling];if(!s.count) s.first=timestamp;
  memcpy(s.bytes+s.size,value,length);s.size+=length;++s.count;++totals.accepted;
  lastAccepted=timestamp;haveAccepted=true;
  unsigned n=sourceUsed();if(n>totals.inputHigh) totals.inputHigh=n;
}
const uint8_t* output(size_t& length) {
  int best=-1;for(unsigned i=0;i<2;++i) if(destinations[i].ready && (best<0 || destinations[i].sequence<destinations[best].sequence)) best=i;
  length=best<0?0:destinations[best].size;return best<0?nullptr:destinations[best].bytes;
}
void release() {
  size_t length;const uint8_t* p=output(length);
  for(auto& d:destinations) if(p==d.bytes) {d.ready=false;d.size=0;++totals.consumed;break;}
}
bool handleCommand(const char* command) {
  bool begin=strcmp(command,"COMP START")==0,end=strcmp(command,"COMP STOP")==0;
  bool reset=strcmp(command,"COMP CLEAR")==0,status=strcmp(command,"COMP STATUS")==0;
  if(!begin && !end && !reset && !status) return false;
  if(Recorder::exporting()) {request("REFUSED during CSV export");return true;}
  if(begin) request(start()?"START: opt-in RAM compression":"START REFUSED: clear retained buffers while stopped");
  else if(end) {stop();request("STOP: draining retained RAM blocks");}
  else if(reset) request(clear()?"CLEAR: RAM buffers cleared":"CLEAR REFUSED while capturing");
  else request("STATUS");
  return true;
}
void update(bool allowWork) {
  if(totals.capturing && filling>=0 && sources[filling].count) {
    uint64_t now=MonotonicClock::now();
    if(now>=sources[filling].first && now-sources[filling].first>=2000000) seal();
  }
  if(allowWork) {
    uint64_t startUs=MonotonicClock::now();unsigned units=0;
    while(units<kUnitsPerSlice && MonotonicClock::now()-startUs<kSliceUs) {
      int old=work.source;uint64_t at=MonotonicClock::now();step();
#ifdef TCB_SIM_COMP_UNIT
      if(old>=0 || work.source>=0)TCB_SIM_COMP_UNIT();
#endif
      uint64_t used=MonotonicClock::now()-at;
      ++units;totals.workUs+=used;
      if(old>=0 || work.source>=0) work.cpu+=used;
      if(old>=0 && work.source<0) {if(work.cpu>totals.maxBlockWorkUs) totals.maxBlockWorkUs=work.cpu;work.cpu=0;}
      if(work.phase==Phase::Idle) break;
    }
    uint64_t elapsed=MonotonicClock::now()-startUs;
    if(elapsed>totals.maxSliceUs) totals.maxSliceUs=elapsed;
    if(units>totals.maxUnits) totals.maxUnits=units;
  }
  report();
}
}
