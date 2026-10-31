#pragma once
#include <stdint.h>
#ifndef TACTICPOD_TCB3_FLASH_ENABLED
#define TACTICPOD_TCB3_FLASH_ENABLED 0
#endif
#ifndef TACTICPOD_REUSABLE
#define TACTICPOD_REUSABLE 0
#endif
#ifndef TACTICPOD_TCB3_TEST_BUILD
#define TACTICPOD_TCB3_TEST_BUILD 0
#endif
#if TACTICPOD_TCB3_TEST_BUILD && !TACTICPOD_TCB3_FLASH_ENABLED
#error TCB3 test build requires explicit TCB3 enable
#endif
#if TACTICPOD_TCB3_TEST_BUILD < 0 || TACTICPOD_TCB3_TEST_BUILD > 3
#error Unknown TCB3 engineering test profile
#endif
namespace Tcb3Test {
constexpr bool Reusable=TACTICPOD_REUSABLE!=0;
constexpr bool Enabled=TACTICPOD_TCB3_TEST_BUILD!=0;
constexpr bool Guarded=Enabled || Reusable;
constexpr bool Movement=TACTICPOD_TCB3_TEST_BUILD==3;
constexpr bool Append=TACTICPOD_TCB3_TEST_BUILD>=2;
constexpr uint32_t WriteStart=Reusable?0x39000UL:Movement?0xB000UL:Append?0x2000UL:0;
constexpr uint32_t ExpectedSession=WriteStart/4096+1;
constexpr uint32_t WriteEnd=Movement?0x100000UL:Append?0x40000UL:Enabled?0x10000UL:0xFFF000UL;
constexpr uint64_t CaptureUs=Movement?120000000ULL:Append?30000000ULL:5000000ULL;
constexpr uint64_t FinaliseUs=Movement?180000000ULL:Append?60000000ULL:15000000ULL; // Total since capture start.
constexpr const char* Marker=Movement?" test=120s,1MiB,180s profile=3B10 exact=B000":Append?" test=30s,256KiB,60s profile=3B9 exact=2000":" test=5s,64KiB,15s";
static_assert(WriteEnd<=0xFFF000UL && WriteEnd%4096==0,"test fence must contain whole recording sectors");
static_assert(WriteStart%4096==0 && WriteStart<WriteEnd,"test write window must contain whole sectors");
static_assert(FinaliseUs>CaptureUs,"finalisation deadline must follow capture cutoff");
static_assert(!Reusable || (!Enabled && TACTICPOD_TCB3_FLASH_ENABLED),"reusable must be a separate enabled profile");
// Nominal 100/10/1 Hz streams: allow an extra boundary block, each at the hard
// 8228-byte frame maximum. Independent hard address checks cover excess traffic.
constexpr uint32_t WorstBlocks=uint32_t((CaptureUs+1999999)/2000000)+1;
constexpr uint32_t WorstDataPages=WorstBlocks*((8228+215)/216);
constexpr uint32_t WorstSectors=(WorstDataPages+13)/14;
static_assert(!Movement || WorstSectors*4096<=WriteEnd-WriteStart,"movement window must fit raw frame budget");
}
