// Executes production clock/parser/queues through the actual UART and main loop.
uint8_t observationWire[140] = {};
uint64_t observationTiming[6] = {};
namespace ObsTests {
uint64_t value(const uint8_t* p, unsigned n = 4) {
  uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint64_t(p[i]) << (8 * i); return v;
}
void validRmc() { nmea("GNRMC,161420.000,A,5046.51536,N,00005.40018,W,022.4,084.4,301026,,,A"); }
void validGga() { nmea("GNGGA,161420.000,5046.51536,N,00005.40018,W,1,07,1.28,-1.200,M,45.000,M,,"); }
bool safe() { return !SPI.erases && !SPI.programs && !SPI.violations && !Serial1.writeCalls; }
}

int observationScenario(unsigned scenario) {
  using namespace RamObservations;
  using namespace ObsTests;
  startupPending = false;
  MonotonicClock::begin(); begin();
  SerialConsole::handleCommand("OBS CLEAR");
  Serial.connected = false;
  if (scenario >= 190 && scenario <= 193) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    setup(); startupPending=false; Serial.connected=false;
    for(unsigned n=0;n<4100;++n) {loop();++mockTime;}
    SerialConsole::handleCommand("OBS STOP");
    SerialConsole::handleCommand("COMP START");
    if(scenario==193) {
      stage=Stage::ExportHeader;Serial.connected=true;Serial.freeSpace=256;
      const size_t before=Serial.used;
      SerialConsole::handleCommand("COMP STOP");
      for(unsigned n=0;n<20;++n) CompactRecorder::update();
      if(!CompactRecorder::capturing() || Serial.used!=before) return 350;
      stage=Stage::Idle;return safe()?0:351;
    }
    if(scenario!=190) SerialConsole::handleCommand("RECORD START");
    uint64_t previous=0,low=UINT64_MAX,high=0;unsigned ni=0,nr=0,ng=0,blocks=0;
    for(unsigned n=0;n<15000;++n) {
      if(n%100==0) rateFrame(false,n,true);
      if(n%1000==0) rateFrame(true,n,true);
      unsigned before=mockImuAxisReads;loop();
      if(mockImuAxisReads!=before) {
        uint64_t t=MonotonicClock::now();if(ni) {uint64_t gap=t-previous;if(gap<low)low=gap;if(gap>high)high=gap;}
        previous=t;++ni;
      }
      if(scenario!=192) {
        size_t length;while(CompactRecorder::output(length)) {
          const uint8_t* frame=CompactRecorder::output(length);
          if(memcmp(frame,"TCB3",4) || length>8228) return 352;
          nr+=value(frame+18,2);ng+=value(frame+20,2);++blocks;CompactRecorder::release();
        }
      }
      ++mockTime;
    }
    SerialConsole::handleCommand("COMP STOP");SerialConsole::handleCommand("RECORD STOP");
    for(unsigned n=0;n<1200;++n) {
      loop();++mockTime;
      if(scenario!=192) {size_t length;while(CompactRecorder::output(length)) {
        const uint8_t* frame=CompactRecorder::output(length);nr+=value(frame+18,2);ng+=value(frame+20,2);
        ++blocks;CompactRecorder::release();}}
    }
    auto s=CompactRecorder::stats();
    if(ni!=1500 || low!=10000 || high!=10000 || SPI.erases || SPI.violations) return 353;
    if(scenario!=190 && (committed!=1500 || missed || lost)) return 354;
    if(scenario==192) return s.fault && s.rejected==1 && s.outputReady==2 ? 0:355;
    return s.accepted==1665 && !s.rejected && !s.fault && s.completed==8 && blocks==8 && nr==150 && ng==15 &&
      (scenario!=190 || safe()) ? 0:356;
  }
  if (scenario == 160) {
    mockTime = 100; mockSubUs = 123; validRmc();
    Rmc r; if (!pop(r) || value(r.bytes + 12) != 127 || value(r.bytes + 32) != 20261030 ||
        value(r.bytes + 24) != 11524 || value(r.bytes + 28) != 8440000 ||
        int32_t(value(r.bytes + 16)) != 507752560 || int32_t(value(r.bytes + 20)) != -900030) return 300;
    mockSubUs = 223; validGga(); Gga g; if (!pop(g)) return 301;
    mockSubUs = 323; const int16_t axes[] = {-32768,32767,0,1,-1,123}; addImu(MonotonicClock::now(), axes);
    Imu i; if (!pop(i)) return 302;
    memcpy(observationWire, i.bytes, 24); memcpy(observationWire + 24, r.bytes, 44); memcpy(observationWire + 68, g.bytes, 40);
    return value(r.bytes + 4, 8) == 100123 && value(i.bytes + 4, 8) == 100323 && safe() ? 0 : 303;
  }
  if (scenario == 161 || scenario == 164 || scenario == 166) {
    validRmc(); Rmc old; pop(old);
    if (scenario == 161) nmea("GNRMC,,V,,,,,,,,,,");
    if (scenario == 164) nmea("GNRMC,,A,5046.51536,N,00005.40018,W,022.4,084.4,301026,,,A");
    if (scenario == 166) nmea("GNRMC,246001,A,9001.00,N,18100.00,E,-1,360,310226,,,?");
    Rmc r; if (!pop(r)) return 304;
    const uint32_t mask = value(r.bytes + 12);
    if (scenario == 164) return mask == 95 && !value(r.bytes + 36) && value(r.bytes + 32) == 20261030 ? 0 : 305;
    if (mask || value(r.bytes + 16) || value(r.bytes + 24) || value(r.bytes + 32) || value(r.bytes + 36)) return 306;
    return scenario != 166 || parserStats().malformedFields >= 6 ? 0 : 307;
  }
  if (scenario == 162) {
    nmea("GPRMC,161420.000,A,5046.51536,N,00005.40018,W,0,0,301026,,,N");
    Rmc r; return pop(r) && value(r.bytes + 12) == 125 && r.bytes[41] == 'N' ? 0 : 308;
  }
  if (scenario == 163) {
    validRmc(); validGga(); Gga g;
    if (!pop(g) || value(g.bytes + 12) != 63 || value(g.bytes + 16) != 20261030 ||
        value(g.bytes + 32, 2) != 128 || value(g.bytes + 34, 2) != 7 || g.bytes[36] != 1 ||
        int32_t(value(g.bytes + 24)) != -1200 || value(g.bytes + 28) != 45000) return 309;
    nmea("GNGGA,,,,,,0,00,,,,,,,");
    return pop(g) && value(g.bytes + 12) == 32 && g.bytes[36] == 0 &&
      !value(g.bytes + 24) && stats(2).observed == 1 && stats(3).observed == 2 ? 0 : 310;
  }
  if (scenario == 165) {
    nmea("GNRMC,161420.000,A,5046.51536,N,00005.40018,W,0,0,301026,,,A", false);
    if (stats(2).observed || parserStats().checksumFailures != 1) return 311;
    validRmc(); return stats(2).observed == 1 && safe() ? 0 : 312;
  }
  if (scenario == 167) {
    const uint8_t incomplete[] = {'$','G','N','R','M','C',','}; binaryData(incomplete, sizeof(incomplete));
    uint8_t ubxPayload[] = {'$','G','N','R','M','C',',',0}; ubx(10, 99, ubxPayload, sizeof(ubxPayload));
    char longBody[230]; memset(longBody, 'X', 220); longBody[220] = 0; nmea(longBody);
    validRmc();
    return stats(2).observed == 1 && parserStats().partial >= 1 && parserStats().overlong == 1 ? 0 : 313;
  }
  if (scenario == 168) {
    prepareNmea("GNRMC,161420.000,A,0000.000,N,00000.000,E,0,0,301026,,,A");
    char copy[256]; strcpy(copy, sentence); Serial1.rx = "";
    for (unsigned i = 0; copy[i]; ++i) {
      mockSubUs = i; const uint8_t byte = copy[i];
      if (byte == '\n' && stats(2).observed) return 314;
      binaryData(&byte, 1);
    }
    Rmc r; return pop(r) && value(r.bytes + 4, 8) == strlen(copy) - 1 && value(r.bytes + 12) == 127 &&
      !value(r.bytes + 16) && !value(r.bytes + 24) ? 0 : 315;
  }
  if (scenario == 169) {
    const int16_t axes[] = {1,2,3,4,5,6};
    for (unsigned n = 0; n < 300; ++n) { mockTime = n * 10; addImu(MonotonicClock::now(), axes); }
    for (unsigned n = 0; n < 40; ++n) { mockTime += 100; validRmc(); }
    for (unsigned n = 0; n < 10; ++n) { mockTime += 1000; validGga(); }
    if (stats(1).used != 256 || stats(1).dropped != 44 || stats(2).dropped != 8 || stats(3).dropped != 2) return 316;
    Imu a, b; if (!pop(a) || !pop(b) || value(a.bytes + 4, 8) != 0 || value(b.bytes + 4, 8) != 10000) return 317;
    memset(a.bytes, 0, sizeof(a.bytes));
    if (value(b.bytes + 12, 2) != 1) return 318;
    SerialConsole::handleCommand("OBS CLEAR");
    return !stats(1).observed && !stats(2).used && !stats(3).dropped && safe() ? 0 : 319;
  }
  if (scenario >= 170 && scenario <= 173) {
    if (scenario == 173) {
      mockTime = 123; mockSubUs = 456; const uint64_t a = MonotonicClock::now();
      __disable_irq(); MonotonicClock::begin(); const uint64_t b = MonotonicClock::now();
      return a == b && mockPrimask == 1 ? 0 : 320;
    }
    mockTime = 4294967; mockSubUs = 290;
    const uint64_t a = MonotonicClock::now();
    mockTime = 4294967; mockSubUs = 300;
    if (scenario == 171) { mockClockTimer.EVENTS_COMPARE[0] = 1; mockTimerLastLow = 4; TIMER4_IRQHandler(); }
    if (scenario == 172) { mockTimerLastLow = 4; mockWrapOnCapture = true; }
    const uint64_t b = MonotonicClock::now(); TIMER4_IRQHandler();
    const uint64_t c = MonotonicClock::now();
    if (a != 4294967290ULL || b != 4294967300ULL || c != b) return 321;
    mockClockTimer.EVENTS_COMPARE[0] = 1; TIMER4_IRQHandler(); // Another complete timer revolution without polling.
    return MonotonicClock::now() == 8589934596ULL ? 0 : 322;
  }
  if (scenario == 174 || scenario == 175 || scenario == 176) {
    mockTime = 100;
    if (scenario == 175) nmea("GNRMC,000000.100,A,0000,N,00000,E,0,0,010127,,,A");
    else nmea("GNRMC,235959.900,A,0000,N,00000,E,0,0,311226,,,A");
    mockTime += scenario == 176 ? 1501 : 100;
    nmea(scenario == 175 ? "GNGGA,235959.900,0000,N,00000,E,1,07,1.0,0,M,0,M,," :
                         "GNGGA,000000.000,0000,N,00000,E,1,07,1.0,0,M,0,M,,");
    Gga g; if (!pop(g)) return 323;
    if (scenario == 176) {
      if (value(g.bytes + 12) & 2) return 324;
      validRmc(); nmea("GNRMC,,V,,,,,,,,,,"); validGga(); pop(g);
      return !(value(g.bytes + 12) & 2) ? 0 : 325;
    }
    return value(g.bytes + 16) == (scenario == 175 ? 20261231UL : 20270101UL) ? 0 : 326;
  }
  if (scenario == 177 || scenario == 178 || scenario == 181 || scenario == 184) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false;
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    if (scenario == 177 || scenario == 181) SerialConsole::handleCommand("RECORD START");
    SerialConsole::handleCommand("OBS START");
    uint64_t prevI = 0, minI = UINT64_MAX, maxI = 0, prevR = 0, minR = UINT64_MAX, maxR = 0;
    unsigned ni = 0, nr = 0, ng = 0;
    for (unsigned n = 0; n < 15000; ++n) {
      if (n % 100 == 0 && scenario != 184) rateFrame(false, n, true);
      if (n % 1000 == 0 && scenario != 184) rateFrame(true, n, true);
      loop();
      Imu i; while (pop(i)) {
        uint64_t t = value(i.bytes + 4, 8); if (ni) { if (t - prevI < minI) minI = t - prevI; if (t - prevI > maxI) maxI = t - prevI; }
        prevI = t; ++ni;
      }
      Rmc r; while (pop(r)) {
        uint64_t t = value(r.bytes + 4, 8); if (nr) { if (t - prevR < minR) minR = t - prevR; if (t - prevR > maxR) maxR = t - prevR; }
        prevR = t; ++nr;
      }
      Gga g; while (pop(g)) ++ng;
      ++mockTime;
    }
    observationTiming[0] = ni; observationTiming[1] = nr; observationTiming[2] = minI;
    observationTiming[3] = maxI; observationTiming[4] = minR; observationTiming[5] = maxR;
    if (ni != 1500 || minI != 10000 || maxI != 10000 || stats(1).dropped ||
        nr != (scenario == 184 ? 0 : 150) || ng != (scenario == 184 ? 0 : 15)) return 327;
    if (scenario == 177 || scenario == 181) {
      SerialConsole::handleCommand("OBS STOP");
      const uint64_t seen = stats(1).observed;
      if (scenario == 181) for (unsigned n = 0; n < 1000; ++n) { loop(); ++mockTime; }
      SerialConsole::handleCommand("RECORD STOP");
      for (unsigned n = 0; n < 600; ++n) { loop(); ++mockTime; }
      if (committed != (scenario == 181 ? 1600 : 1500) || missed || lost || stats(1).observed != seen || SPI.erases || SPI.violations) return 328;
      return 0;
    }
    return safe() ? 0 : 329;
  }
  if (scenario == 179 || scenario == 180) {
    Serial.connected = true; Serial.freeSpace = 0;
    SerialConsole::handleCommand("OBS START"); validRmc();
    SerialConsole::handleCommand("OBS STATUS");
    for (unsigned i = 0; i < 20; ++i) { mockTime += 100; validRmc(); update(); }
    if (stats(2).observed != 21 || Serial.overfilledWrites) return 330;
    if (scenario == 180) {
      stage = Stage::ExportHeader; const size_t before = Serial.used;
      SerialConsole::handleCommand("OBS STOP"); Serial.freeSpace = 256;
      for (unsigned i = 0; i < 10; ++i) update();
      if (Serial.used != before || !fastImu()) return 331;
      stage = Stage::Idle;
    }
    Serial.freeSpace = 256; for (unsigned i = 0; i < 15; ++i) update();
    return contains(scenario == 180 ? "REFUSED during CSV export" : "OBS END") && !Serial.overfilledWrites && safe() ? 0 : 332;
  }
  if (scenario == 182) {
    nmea("GNRMC,000000.000,A,9000.000,S,18000.000,W,0,359.99999,290224,,,A");
    Rmc r; if (!pop(r) || int32_t(value(r.bytes + 16)) != -900000000 ||
      int32_t(value(r.bytes + 20)) != -1800000000 || value(r.bytes + 28) != 35999999) return 333;
    nmea("GNGGA,,,,,,254,65535,655.35,-2147483.648,M,2147483.647,M,,");
    Gga g; return pop(g) && int32_t(value(g.bytes + 24)) == INT32_MIN && value(g.bytes + 28) == INT32_MAX &&
      value(g.bytes + 32, 2) == 65535 && value(g.bytes + 34, 2) == 65535 && g.bytes[36] == 254 ? 0 : 334;
  }
  if (scenario == 183) {
    nmea("GNRMC,235960.000,A,0000,N,00000,E,0,0,311226,,,A");
    Rmc r; Event e;
    if (!pop(r) || (value(r.bytes + 12) & 32) || value(r.bytes + 36) ||
        !pop(e) || value(e.bytes + 12, 2) != 5 || value(e.bytes + 16) != 2 || parserStats().leapSeconds != 1) return 335;
    memcpy(observationWire, e.bytes, 32); return 0;
  }
  if (scenario == 185) {
    rateFrame(false, 1000, true); rateFrame(false, 1000, false); rateFrame(false, 900, true);
    Rmc a, b, c; return pop(a) && pop(b) && pop(c) && stats(2).observed == 3 &&
      value(a.bytes + 36) == value(b.bytes + 36) && value(c.bytes + 36) == 900 && !(value(b.bytes + 12) & 2) ? 0 : 336;
  }
  if (scenario == 186) {
    const uint8_t partial[] = {'$','G','N','R','M','C',','}; binaryData(partial, sizeof(partial));
    mockTime += 1000; update();
    validRmc(); return stats(2).observed == 1 && parserStats().partial == 1 ? 0 : 337;
  }
  if (scenario == 187) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    setup(); startupPending = false; Serial.connected = false;
    for (unsigned n = 0; n < 4100; ++n) { loop(); ++mockTime; }
    SerialConsole::handleCommand("RECORD START"); SerialConsole::handleCommand("OBS START");
    for (unsigned n = 0; n < 15000; ++n) {
      if (n % 100 == 0) rateFrame(false, n, true);
      if (n % 1000 == 0) rateFrame(true, n, true);
      loop(); ++mockTime;
    }
    SerialConsole::handleCommand("RECORD STOP"); SerialConsole::handleCommand("OBS STOP");
    for (unsigned n = 0; n < 600; ++n) { loop(); ++mockTime; }
    return committed == 1500 && !missed && !lost && stats(1).dropped == 1244 &&
      stats(2).dropped == 118 && stats(3).dropped == 7 && !SPI.erases && !SPI.violations ? 0 : 339;
  }
  if (scenario == 188) {
    for (unsigned n = 0; n < 20; ++n) nmea("GNRMC,235960.000,V,,,,,,,311226,,,N");
    return stats(4).observed == 20 && stats(4).used == 16 && stats(4).dropped == 4 && safe() ? 0 : 340;
  }
  if (scenario == 189) {
    imuReady = true; flashReportPrinted = true;
    SerialConsole::handleCommand("OBS START"); mockTime = 100; mockSubUs = 123;
    mockImuReadCostUs = 7;
    const uint64_t before = MonotonicClock::now(); loop(); const uint64_t after = MonotonicClock::now();
    Imu i; return pop(i) && value(i.bytes + 4, 8) == before && after - before == 42 && safe() ? 0 : 341;
  }
  return 338;
}
