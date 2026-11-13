#include "ble_field_integration.cpp"
#define CACHE_CHECK(condition) do {if(!(condition))return __LINE__;} while(0)
using namespace TcbDetail;
uint32_t cacheExpected[4]{};

static bool cacheBoot(){
  Serial.connected=true;setup();startupPending=false;waitIdle();
  for(unsigned n=0;n<200000 && GpsReceiver::busy();++n)cycle();
  Tcb3Session::invalidateBleManifest();
  return !Recorder::exclusive() && !GpsReceiver::busy();
}
static bool cacheConnect(){
  if(!enterStandby())return false;
  BleMode::Inputs in;in.doubleTap=true;fieldBleRuntime.update(millis(),in,fieldBle);
  in.doubleTap=false;fieldBle.link=true;fieldBleRuntime.update(millis(),in,fieldBle);
  return BleRuntime::active() && fieldBle.link;
}

extern "C" unsigned cacheReadyCase(unsigned){
  CACHE_CHECK(cacheBoot() && cacheConnect());
  const unsigned before=SPI.bulkReads,releases=SPI.releases;
  bool conflict=true;dumping=true;
  CACHE_CHECK(!Tcb3Session::bleBeginList(&conflict) && !conflict);dumping=false;
  bool notReady=false;
  CACHE_CHECK(!Tcb3Session::bleBeginList(&notReady) && notReady && !Tcb3Session::bleListDone());
  CACHE_CHECK(!Tcb3Session::bleSessionCount() && SPI.bulkReads==before && SPI.releases==releases);
  bool partialChecked=false;
  for(unsigned n=0;n<20000 && manifestCache!=ManifestCache::Ready;++n){
    const unsigned reads=SPI.bulkReads;
    CACHE_CHECK(!Tcb3Session::bleService(16));
    CACHE_CHECK(SPI.bulkReads-reads<=16 && SPI.deepPower && !bleReading && !SPI.transactionActive);
    CACHE_CHECK(manifestCache!=ManifestCache::Failed);
    if(!partialChecked && bleManifest.count() && manifestCache==ManifestCache::Scanning){
      notReady=false;
      CACHE_CHECK(!Tcb3Session::bleBeginList(&notReady) && notReady && !Tcb3Session::bleListDone());
      CACHE_CHECK(!Tcb3Session::bleSessionCount());partialChecked=true;
    }
  }
  CACHE_CHECK(partialChecked && manifestCache==ManifestCache::Ready && SPI.bulkReads>before);
  const unsigned reads=SPI.bulkReads,identities=SPI.jedecReads,wakes=SPI.releases;
  for(unsigned n=0;n<3;++n){
    CACHE_CHECK(Tcb3Session::bleBeginList() && Tcb3Session::bleListDone());
    CACHE_CHECK(Tcb3Session::bleSessionCount()==3 && !Tcb3Session::bleListFailed());
    CACHE_CHECK(Tcb3Session::bleSession(0)->sessionId==1);
    Tcb3Session::bleFinishList();Tcb3Session::bleCancel();
    CACHE_CHECK(manifestCache==ManifestCache::Ready);
  }
  CACHE_CHECK(SPI.bulkReads==reads && SPI.releases==wakes && SPI.jedecReads==identities);
  dumping=true;notReady=true;
  CACHE_CHECK(!Tcb3Session::bleBeginList(&notReady) && !notReady);dumping=false;
  CACHE_CHECK(Tcb3Session::bleBeginList());
  Tcb3Session::invalidateBleManifest();
  CACHE_CHECK(!Tcb3Session::bleBeginList() && !Tcb3Session::bleBeginSession(1,1));
  CACHE_CHECK(!Tcb3Session::bleListDone() && !Tcb3Session::bleSessionCount() && SPI.bulkReads==reads);
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations || SPI.wakeDelayViolations;
}

extern "C" unsigned cacheContentionCase(unsigned){
  CACHE_CHECK(cacheBoot());
  uint64_t before=MonotonicClock::now();
  Tcb3Session::bleIdleService();
  CACHE_CHECK(manifestCache==ManifestCache::Scanning && MonotonicClock::now()-before<2000);
  unsigned idleReads=SPI.bulkReads;
  CACHE_CHECK(!Tcb3Session::bleService(16) && SPI.bulkReads==idleReads && !SPI.deepPower);
  for(unsigned mode=0;mode<7;++mode){
    const unsigned reads=SPI.bulkReads;
    if(mode==0)pending=true;
    if(mode==1)owned=true;
    if(mode==2)dumping=true;
    if(mode==3)resetting=true;
    if(mode==4)pageOpen=true;
    if(mode==5)locked=true;
    if(mode==6)gpsTestDiagnosticAvailable=false;
    Tcb3Session::bleIdleService();
    CACHE_CHECK(SPI.bulkReads==reads && manifestCache==ManifestCache::Scanning);
    pending=owned=dumping=resetting=pageOpen=locked=false;gpsTestDiagnosticAvailable=true;
  }
  RamObservations::handleCommand("OBS START");
  CACHE_CHECK(RamObservations::capturing());
  idleReads=SPI.bulkReads;Tcb3Session::bleIdleService();
  CACHE_CHECK(SPI.bulkReads==idleReads);
  RamObservations::handleCommand("OBS STOP");
  Recorder::begin(true);
  const unsigned reads=SPI.bulkReads;
  Tcb3Session::bleIdleService();CACHE_CHECK(SPI.bulkReads==reads);
  waitIdle();CACHE_CHECK(warmBleCache());
  const unsigned readyReads=SPI.bulkReads;
  for(unsigned n=0;n<100;++n)Tcb3Session::bleIdleService();
  CACHE_CHECK(SPI.bulkReads==readyReads);
  Tcb3Session::handleCommand("TCB READ");
  CACHE_CHECK(dumping);
  Tcb3Session::bleIdleService();CACHE_CHECK(SPI.bulkReads==readyReads);
  Serial.connected=false;Tcb3Session::update();Serial.connected=true;
  CACHE_CHECK(!dumping && manifestCache==ManifestCache::Ready);
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
}

extern "C" unsigned cacheStandbyCase(unsigned){
  CACHE_CHECK(cacheBoot());
  Tcb3Session::bleIdleService();CACHE_CHECK(manifestCache==ManifestCache::Scanning);
  Serial.connected=false;standbyPolicy.wake(millis());advanceUs(2100000);cycle();
  CACHE_CHECK(standbyPolicy.inStandby() && SPI.deepPower);
  const unsigned reads=SPI.bulkReads,wakes=SPI.releases;
  runMs(5000);
  CACHE_CHECK(SPI.bulkReads==reads && SPI.releases==wakes && manifestCache==ManifestCache::Scanning);
  Serial.connected=true;cycle();
  CACHE_CHECK(!standbyPolicy.inStandby() && !SPI.deepPower);
  CACHE_CHECK(warmBleCache());
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations || SPI.asleepAccesses;
}

extern "C" unsigned cacheAppendCase(unsigned){
  CACHE_CHECK(cacheBoot() && warmBleCache());
  BatteryRuntime::inject(4000,true);SPI.programDelayMs=10;
  Tcb3Session::handleCommand("TCB DURATION 1");
  Tcb3Session::handleCommand("TCB START");
  CACHE_CHECK(pending && manifestCache==ManifestCache::Cold && !Tcb3Session::bleSessionCount());
  for(unsigned n=0;n<400000 && (pending || owned);++n){
    cycle();CACHE_CHECK(manifestCache==ManifestCache::Cold);
  }
  CACHE_CHECK(!pending && !owned && !locked && store.stats.finished);
  waitIdle();CACHE_CHECK(warmBleCache() && bleManifest.count()==4);
  CACHE_CHECK(bleManifest.entry(3)->sessionId==store.stats.session && (bleManifest.entry(3)->flags&3)==3);
  CACHE_CHECK(bleManifest.entry(0)->sessionId==1 && bleManifest.entry(1)->sessionId==3);
  return SPI.erases || SPI.violations;
}

extern "C" unsigned cacheResetCase(unsigned){
  CACHE_CHECK(cacheBoot() && warmBleCache());
  Tcb3Session::handleCommand("RECORD RESET");
  CACHE_CHECK(manifestCache==ManifestCache::Ready && !SPI.erases);
  Tcb3Session::handleCommand("RECORD RESET CONFIRM");
  CACHE_CHECK(resetting && manifestCache==ManifestCache::Cold && !Tcb3Session::bleSessionCount());
  for(unsigned n=0;n<400000 && resetting;++n)cycle();
  CACHE_CHECK(!resetting && !resetFailed && SPI.erases==1);
  waitIdle();CACHE_CHECK(warmBleCache() && !bleManifest.count());
  CACHE_CHECK(cacheConnect() && Tcb3Session::bleBeginList() && Tcb3Session::bleListDone());
  CACHE_CHECK(!Tcb3Session::bleSessionCount() && !Tcb3Session::bleListFailed());
  return SPI.violations;
}

extern "C" unsigned cacheEraseGuardCase(unsigned){
  CACHE_CHECK(cacheBoot() && warmBleCache());
  Recorder::handleCommand("RECORD ERASE");Recorder::handleCommand("RECORD ERASE CONFIRM");
  CACHE_CHECK(!SPI.erases && !SPI.enables && manifestCache==ManifestCache::Ready);
  // Model an externally erased image followed by explicit invalidation.
  memset(SPI.memory,255,Tcb3Test::WriteEnd);Tcb3Session::invalidateBleManifest();
  CACHE_CHECK(!Tcb3Session::bleListDone() && !Tcb3Session::bleSessionCount());
  CACHE_CHECK(warmBleCache() && !bleManifest.count());
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
}

extern "C" unsigned cacheValidationCase(unsigned){
  CACHE_CHECK(cacheBoot());
  const bool ready=warmBleCache();
  CACHE_CHECK(ready==!cacheExpected[0]);
  CACHE_CHECK(cacheConnect());
  if(!ready){
    bool notReady=true;
    CACHE_CHECK(manifestCache==ManifestCache::Failed && !Tcb3Session::bleBeginList(&notReady) && !notReady);
    CACHE_CHECK(!Tcb3Session::bleListDone() && !Tcb3Session::bleSessionCount());
    const unsigned reads=SPI.bulkReads;
    for(unsigned n=0;n<10;++n)Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads==reads);
  }else{
    CACHE_CHECK(Tcb3Session::bleBeginList() && Tcb3Session::bleListDone());
    CACHE_CHECK(Tcb3Session::bleSessionCount()==cacheExpected[1]);
    CACHE_CHECK(Tcb3Session::bleIncompleteCount()==cacheExpected[2]);
    CACHE_CHECK(Tcb3Session::bleListOverflow()==bool(cacheExpected[3]));
  }
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
}

extern "C" unsigned cachePerformanceCase(unsigned){
  CACHE_CHECK(cacheBoot() && cacheConnect());
  BlePerformance::counters()=BlePerformance::Counters{};
  const unsigned before=SPI.bulkReads;
  Tcb3Session::bleService(16);
  auto& s=BlePerformance::counters();
  CACHE_CHECK(s.cacheState==1 && s.cacheSteps==16);
  CACHE_CHECK(s.readCalls==SPI.bulkReads-before);
  CACHE_CHECK(s.readTotalUs==(s.readBytes+uint64_t(s.readCalls)*4)*8);
  CACHE_CHECK(s.cacheServiceUs>=s.cacheReadUs && s.cacheReadUs==s.readTotalUs);
  mockTime+=50;
  for(unsigned n=0;n<20000 && manifestCache!=ManifestCache::Ready;++n){
    ++mockTime;Tcb3Session::bleService(16);
  }
  CACHE_CHECK(manifestCache==ManifestCache::Ready && s.cacheState==2 && s.cacheSteps>16);
  CACHE_CHECK(s.cacheElapsedMs>=50 && uint32_t(millis()-s.cacheStartedAt)-s.cacheElapsedMs<=1);
  const uint32_t steps=s.cacheSteps,reads=s.readCalls;
  CACHE_CHECK(Tcb3Session::bleBeginList());Tcb3Session::bleFinishList();Tcb3Session::bleCancel();
  CACHE_CHECK(s.cacheSteps==steps && s.readCalls==reads);
  // USB exports keep their fast path and do not enter BLE read-time totals.
  BleFlashIo flash;FlashPower::release(flash);
  uint8_t bytes[64];dumping=true;
  CACHE_CHECK(read(nullptr,Tcb3::Sector,bytes,sizeof(bytes)) && SPI.hz==16000000);
  dumping=false;
  CACHE_CHECK(s.readCalls==reads);
  Tcb3Session::invalidateBleManifest();
  CACHE_CHECK(!s.cacheState && !s.cacheSteps && !s.cacheElapsedMs && !s.cacheServiceUs && !s.cacheReadUs);
  CACHE_CHECK(s.readCalls==reads);
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations || SPI.wakeDelayViolations;
}
