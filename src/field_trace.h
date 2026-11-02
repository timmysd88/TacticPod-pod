#pragma once
#include <stdint.h>

// Foreground-only RAM flight recorder. No flash writes, allocation or serial IO
// when noting an event. Oldest entries are overwritten with an explicit counter.
namespace FieldTrace {
enum class Kind : uint8_t { StandbyEnter, Wake, Press, Release, StartAccepted,
  SessionCreated, StopArmed, StopAccepted, Finalising, Complete, Fault,
  GpsInvalidated, GpsVerified, GpsFailed };
struct Event { uint32_t ms, session, detail; Kind kind; };
struct Buffer {
  Event events[64]{};
  unsigned head=0, count=0, remaining=0;
  uint32_t overwritten=0;
};
inline Buffer& buffer() { static Buffer value; return value; }
inline void note(Kind kind, uint32_t ms, uint32_t session=0, uint32_t detail=0) {
#if TACTICPOD_REUSABLE
  auto& b=buffer();
  if(b.count==64){b.head=(b.head+1)%64;--b.count;++b.overwritten;}
  b.events[(b.head+b.count++)%64]={ms,session,detail,kind};
#else
  (void)kind;(void)ms;(void)session;(void)detail;
#endif
}
inline const char* name(Kind k) {
  static const char* const names[]={"standby_enter","wake","button_press","button_release",
    "start_accepted","session_created","stop_armed","stop_accepted","finalising",
    "finalised_clean","fault","gps_invalidated","gps_verified","gps_failed"};
  return names[unsigned(k)];
}
inline unsigned format(const Event& e, uint32_t overwritten, char* line, unsigned capacity) {
  unsigned used=0;
  auto text=[&](const char* p){while(*p && used+1<capacity)line[used++]=*p++;};
  auto number=[&](uint32_t n){char b[11];unsigned i=10;b[i]=0;do{b[--i]='0'+n%10;n/=10;}while(n);text(b+i);};
  text("FIELD t_ms=");number(e.ms);text(" event=");text(name(e.kind));
  text(" session=");number(e.session);text(" detail=");number(e.detail);
  text(" overwritten=");number(overwritten);text("\n");line[used]=0;return used;
}
}
