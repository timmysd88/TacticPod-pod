#include "ble_field_integration.cpp"
#define CACHE_CHECK(condition) do {if(!(condition))return __LINE__;} while(0)
using namespace TcbDetail;
uint32_t cacheExpected[4]{};

static bool cacheBoot(){
  Serial.connected=true;setup();waitIdle(); // Exercise actual GPS identity/profile startup.
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
    CACHE_CHECK(SPI.bulkReads-reads<=32 && SPI.deepPower && !bleReading && !SPI.transactionActive);
    CACHE_CHECK(manifestCache!=ManifestCache::Failed);
    if(!partialChecked && bleCache.manifest.count() && manifestCache==ManifestCache::Scanning){
      notReady=false;
      CACHE_CHECK(!Tcb3Session::bleBeginList(&notReady) && notReady && !Tcb3Session::bleListDone());
      CACHE_CHECK(!Tcb3Session::bleSessionCount());
      const unsigned steps=BlePerformance::counters().cacheSteps;
      Tcb3Session::bleCancel();
      CACHE_CHECK(manifestCache==ManifestCache::Scanning && BlePerformance::counters().cacheSteps==steps);
      partialChecked=true;
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
  waitIdle();CACHE_CHECK(warmBleCache() && bleCache.manifest.count()==4);
  CACHE_CHECK(bleCache.manifest.entry(3)->sessionId==store.stats.session && (bleCache.manifest.entry(3)->flags&3)==3);
  CACHE_CHECK(bleCache.manifest.entry(0)->sessionId==1 && bleCache.manifest.entry(1)->sessionId==3);
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
  waitIdle();CACHE_CHECK(warmBleCache() && !bleCache.manifest.count());
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
  CACHE_CHECK(warmBleCache() && !bleCache.manifest.count());
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
}

extern "C" unsigned cacheValidationCase(unsigned){
  CACHE_CHECK(cacheBoot() && cacheConnect());
  for(unsigned n=0;n<20000 &&
      (manifestCache==ManifestCache::Cold || manifestCache==ManifestCache::Scanning);++n){
    const unsigned before=SPI.bulkReads;
    Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads-before<=32 && !bleReading && !SPI.transactionActive);
  }
  const bool ready=manifestCache==ManifestCache::Ready;
  CACHE_CHECK(ready==!cacheExpected[0]);
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
  Tcb3Session::bleService(0);
  CACHE_CHECK(manifestCache==ManifestCache::Cold && SPI.bulkReads==before);
  Tcb3Session::bleService(16);
  auto& s=BlePerformance::counters();
  CACHE_CHECK(s.cacheState==1 && s.cacheSteps==32);
  CACHE_CHECK(s.readCalls==SPI.bulkReads-before);
  CACHE_CHECK(s.readTotalUs==(s.readBytes+uint64_t(s.readCalls)*4)*8);
  CACHE_CHECK(s.cacheServiceUs>=s.cacheReadUs && s.cacheReadUs==s.readTotalUs);
  mockTime+=50;
  for(unsigned n=0;n<20000 && manifestCache!=ManifestCache::Ready;++n){
    ++mockTime;Tcb3Session::bleService(16);
  }
  CACHE_CHECK(manifestCache==ManifestCache::Ready && s.cacheState==2 && s.cacheSteps>32);
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
  for(unsigned fault=0;fault<2;++fault){
    Tcb3Session::bleService(16);
    CACHE_CHECK(manifestCache==ManifestCache::Scanning);
    if(fault==0)SPI.wrongId=true;else SPI.stuckBusy=true;
    Tcb3Session::bleService(16);
    CACHE_CHECK(manifestCache==ManifestCache::Failed && s.cacheState==3);
    const unsigned failedReads=SPI.bulkReads;
    SPI.wrongId=SPI.stuckBusy=false;
    Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads==failedReads && manifestCache==ManifestCache::Failed);
    Tcb3Session::invalidateBleManifest();
  }
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations || SPI.wakeDelayViolations;
}

extern "C" unsigned provisionalCacheCase(unsigned scenario){
  CACHE_CHECK(cacheBoot() && cacheConnect());
  CACHE_CHECK(Tcb3Session::bleEnableProvisional() && scannerActive && provisionalMode);
  CACHE_CHECK(!Tcb3Session::bleBeginSession(1,0xffffffffU));
  if(scenario==12){
    SPI.wrongId=true;Tcb3Session::bleService(16);SPI.wrongId=false;
    CACHE_CHECK(manifestCache==ManifestCache::Failed && Tcb3Session::bleDiscoveryError()==11);
    CACHE_CHECK(!Tcb3Session::bleBeginList());
    return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
  }
  if(scenario==9){
    const unsigned before=SPI.bulkReads;
    gpsTestDiagnosticAvailable=false;Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads==before && manifestCache==ManifestCache::Cold);
    gpsTestDiagnosticAvailable=true;
  }
  for(unsigned n=0;n<20000 &&
      (manifestCache==ManifestCache::Cold || manifestCache==ManifestCache::Scanning);++n){
    const unsigned before=SPI.bulkReads;
    Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads-before<=32 && !bleReading && !SPI.transactionActive);
  }
  if(scenario==6 || scenario==7 || scenario==8){
    CACHE_CHECK(manifestCache==ManifestCache::Failed && !Tcb3Session::bleBeginList());
    CACHE_CHECK(!Tcb3Session::bleSessionCount());
    CACHE_CHECK(Tcb3Session::bleDiscoveryError()==(scenario==6?8:scenario==7?9:10));
    return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
  }
  CACHE_CHECK(manifestCache==ManifestCache::Ready && Tcb3Session::bleBeginList());
  CACHE_CHECK(Tcb3Session::bleSessionCount()==(scenario==2?2U:3U));
  CACHE_CHECK(Tcb3Session::bleIncompleteCount()==(scenario==2?1U:0U));
  const auto* entry=Tcb3Session::bleSession((scenario==2 || scenario==17)?0:2);
  CACHE_CHECK(entry && entry->flags==9 && !(entry->flags&2));
  const uint32_t id=entry->sessionId,epoch=entry->resetEpoch;
  CACHE_CHECK(!Tcb3Session::bleBeginSession(id,epoch));
  if(scenario==2)CACHE_CHECK(!Tcb3Session::bleBeginValidation(12,epoch));
  if(scenario==3){
    CACHE_CHECK(Tcb3::get(SPI.memory)==Tcb3::ResetMagic && epoch==7);
    Tcb3::put(SPI.memory+4,8);Tcb3::put(SPI.memory+248,~Tcb3::crc32(SPI.memory,248));
  }
  if(scenario==10)SPI.memory[45056+256+32]^=1;
  if(scenario==13)SPI.memory[45056+45*4096+15*256+32]^=1;
  if(scenario==14)SPI.memory[45056+4096+20]^=1;
  if(scenario==17){
    uint8_t* footer=SPI.memory+4096+15*256;
    const uint64_t stop=uint64_t(Tcb3::get(footer+24))|(uint64_t(Tcb3::get(footer+28))<<32);
    Tcb3::put(footer+24,stop+1,8);
    Tcb3::put(footer+248,~Tcb3::crc32(footer,248));
  }
  CACHE_CHECK(Tcb3Session::bleBeginValidation(id,epoch));
  CACHE_CHECK(!scannerActive && validating && !Tcb3Session::bleBeginSession(id,epoch));
  if(scenario==18){
    for(unsigned mode=0;mode<7;++mode){
      if(mode==0)pending=true;
      if(mode==1)owned=true;
      if(mode==2)dumping=true;
      if(mode==3)resetting=true;
      if(mode==4)pageOpen=true;
      if(mode==5)locked=true;
      if(mode==6)gpsTestDiagnosticAvailable=false;
      const unsigned before=SPI.bulkReads;
      CACHE_CHECK(!Tcb3Session::bleService(16) && SPI.bulkReads==before && validating && !bleReading);
      pending=owned=dumping=resetting=pageOpen=locked=false;gpsTestDiagnosticAvailable=true;
    }
  }
  if(scenario==4 || scenario==11){
    Tcb3Session::bleService(16);
    if(scenario==4){
      Tcb3Session::bleCancel();
      CACHE_CHECK(scannerActive && provisionalMode && manifestCache==ManifestCache::Cold);
    }else{
      Tcb3Session::invalidateBleManifest();
      CACHE_CHECK(!scannerActive && !provisionalMode && manifestCache==ManifestCache::Cold);
    }
    CACHE_CHECK(!validating && !Tcb3Session::bleBeginSession(id,epoch));
    return SPI.programs || SPI.enables || SPI.erases || SPI.violations;
  }
  if(scenario==5)SPI.stuckBusy=true;
  for(unsigned n=0;n<30000 && !Tcb3Session::bleValidationDone();++n){
    const unsigned before=SPI.bulkReads;
    Tcb3Session::bleService(16);
    CACHE_CHECK(SPI.bulkReads-before<=16 && !bleReading && !SPI.transactionActive);
  }
  SPI.stuckBusy=false;
  CACHE_CHECK(Tcb3Session::bleValidationDone());
  const bool bad=scenario==1 || scenario==3 || scenario==5 || scenario==10 ||
    (scenario>=13 && scenario<=17);
  CACHE_CHECK(Tcb3Session::bleValidationFailed()==bad);
  if(bad){
    CACHE_CHECK(!Tcb3Session::bleBeginSession(id,epoch));
  }else{
    CACHE_CHECK(Tcb3Session::bleBeginSession(id,epoch));
    Tcb3Session::bleFinishSession();
    CACHE_CHECK(!Tcb3Session::bleBeginSession(id==1?3:1,epoch));
    bool notReady=false;
    CACHE_CHECK(!Tcb3Session::bleBeginList(&notReady) && notReady);
    CACHE_CHECK(scannerActive && manifestCache==ManifestCache::Cold);
    CACHE_CHECK(!Tcb3Session::bleBeginSession(id,epoch));
  }
  return SPI.programs || SPI.enables || SPI.erases || SPI.violations || SPI.wakeDelayViolations;
}
