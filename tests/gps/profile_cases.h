// Stateful receiver model: staged VALSET transactions, atomic commit and a
// configuration database independent of the firmware's snapshot/target arrays.
namespace ProfileModel {
const uint32_t ids[] = {
  0x1031001f,0x10310001,0x10310021,0x10310007,0x10310022,0x1031000d,
  0x10310025,0x10310024,0x10310012,0x10310020,0x10310005,0x1031000f,
  0x10310014,0x10310018,0x30210001,0x30210002,0x209100ac,0x209100bb,
  0x209100c0,0x209100c5,0x209100ca,0x209100b1,0x40520001,0x10520005,
  0x10730001,0x10740001,0x10740002,0x20210003,0x20d00001
};
const uint32_t initial[] = {1,1,1,1,1,1,0,1,1,1,1,0,1,1,1000,1,1,1,1,1,1,1,38400,1,1,1,1,1,0};
const uint32_t desired[] = {1,1,1,1,0,0,0,1,1,1,1,0,1,1,100,1,1,10,0,0,0,0,38400,1,1,1,1,1,0};
struct Receiver {
  uint32_t db[29], original[29], staged[29];
  unsigned scenario, handled = 0, sets = 0, commits = 0, invalid = 0, gets = 0;
  bool pending = false, injected = false, secondInjected = false;
  explicit Receiver(unsigned s) : scenario(s) { memcpy(db, initial, sizeof(db)); memcpy(original, db, sizeof(db)); }
  int find(uint32_t key) { for (unsigned i = 0; i < 29; ++i) if (ids[i] == key) return i; return -1; }
  static uint32_t read(const uint8_t* p, unsigned size) {
    uint32_t value = 0; for (unsigned i = 0; i < size; ++i) value |= uint32_t(p[i]) << (8 * i); return value;
  }
  static unsigned width(uint32_t key) { return key >> 28 == 3 ? 2 : key >> 28 == 4 ? 4 : 1; }
  void ack(bool yes, uint8_t id = 0x8a) { const uint8_t p[] = {6, id}; ubx(5, yes ? 1 : 0, p, sizeof(p)); }
  bool originalRestored() { return memcmp(db, original, sizeof(db)) == 0; }
  bool testConfigured() { return memcmp(db, desired, sizeof(db)) == 0; }
  void service() {
    if (handled >= Serial1.writeCalls) return;
    if (handled >= 256) { ++invalid; ++handled; return; }
    const uint8_t* packet = Serial1.writes[handled]; const size_t count = Serial1.writeSizes[handled++];
    if (count < 8 || count > 64 || packet[0] != 0xb5 || packet[1] != 0x62 || read(packet + 4, 2) != count - 8) { ++invalid; return; }
    uint8_t a = 0, b = 0;
    for (unsigned i = 2; i < count - 2; ++i) { a += packet[i]; b += a; }
    if (a != packet[count - 2] || b != packet[count - 1]) { ++invalid; return; }
    if (packet[2] == 10 && packet[3] == 4 && count == 8) {
      uint8_t version[100] = {};
      memcpy(version, "ROM SPG 5.10 (7b202e)", 21);
      memcpy(version + 40, scenario == 104 ? "FWVER=SPG 5.20" : "FWVER=SPG 5.10", 14);
      memcpy(version + 70, "PROTVER=34.10", 13);
      ubx(10, 4, version, sizeof(version)); return;
    }
    if (packet[2] != 6 || (packet[3] != 0x8a && packet[3] != 0x8b)) { ++invalid; return; }
    if (packet[3] == 0x8b) {
      pending = false; ++gets; // Per protocol, VALGET cancels a staged transaction.
      if (packet[6] || packet[7] || packet[8] || packet[9] || (count != 16 && count != 20)) { ++invalid; return; }
      const int key = find(read(packet + 10, 4));
      if (key < 0) { ++invalid; return; }
      if (!injected && Profile::job == Profile::Job::Snapshot && key == 5 && (scenario == 105 || scenario == 106)) {
        injected = true; if (scenario == 105) ack(false, 0x8b); return;
      }
      if (!injected && Profile::job == Profile::Job::Verify && !Profile::restoring && key == 15 && scenario == 111) { injected = true; return; }
      if (!secondInjected && Profile::job == Profile::Job::Verify && Profile::restoring && key == 14 && scenario == 135) { secondInjected = true; return; }
      uint8_t response[24] = {1,0,0,0}; unsigned length = 4;
      for (unsigned offset = 10; offset < count - 2; offset += 4) {
        const uint32_t id = read(packet + offset, 4); const int entry = find(id);
        if (entry < 0) { ++invalid; return; }
        const unsigned bytes = width(id);
        memcpy(response + length, packet + offset, 4); length += 4;
        for (unsigned i = 0; i < bytes; ++i) response[length++] = db[entry] >> (8 * i);
      }
      if (scenario == 119 && !injected) {
        injected = true;
        ubx(6,0x8b,response,length,false);
        response[1]=1; ubx(6,0x8b,response,length); response[1]=0;
        response[4]^=1; ubx(6,0x8b,response,length); response[4]^=1;
      }
      if (scenario == 128) {
        const size_t bytes = makeUbx(6,0x8b,response,length);
        for (unsigned i = 0; i < bytes; ++i) binaryData(configFrame + i, 1);
      } else ubx(6,0x8b,response,length);
      return;
    }
    ++sets;
    if (packet[6] != 1 || packet[7] != 1 || packet[9] || (packet[8] < 1 || packet[8] > 3)) { ++invalid; return; }
    const unsigned action = packet[8];
    if (scenario == 123 && !injected) {
      injected = true; Serial1.shortTx = false; return; // Truncated frame never accepted.
    }
    if (scenario == 129 && !Profile::restoring && Profile::chunk == 2 && !injected) { injected = true; return; }
    if (action == 1) { memcpy(staged, db, sizeof(staged)); pending = true; }
    if (!pending) { ack(false); return; }
    // SPG 5.10 section 4.6: APPLY ignores any key/value payload. Only
    // START/ONGOING stage values. The old model incorrectly accepted it.
    for (unsigned offset = 10; action != 3 && offset < count - 2;) {
      const uint32_t id = read(packet + offset, 4); offset += 4;
      const int key = find(id); const unsigned bytes = width(id);
      // UART/protocol/clock settings are never allowed in a VALSET packet.
      if (key < 0 || key >= 22 || offset + bytes > count - 2) { ++invalid; return; }
      staged[key] = read(packet + offset, bytes); offset += bytes;
    }
    const bool restore = Profile::restoring;
    if (!restore && !injected && ((scenario == 107 && action == 3) || (scenario == 124 && action == 2))) {
      injected = true; pending = false; ack(false); return;
    }
    if (!restore && !injected && scenario == 108 && action == 1) { injected = true; return; }
    if (!restore && !injected && scenario == 122 && action == 2) { injected = true; return; }
    if (restore && !secondInjected && (scenario == 112 || scenario == 127) && action == 3) {
      secondInjected = true; pending = false; ack(false); return;
    }
    if (action == 3) {
      memcpy(db, staged, sizeof(db)); pending = false; ++commits;
      if (!restore && (scenario == 110 || scenario == 112) && !injected) { injected = true; db[17] = 1; }
      if (!restore && scenario == 138 && !injected) { injected = true; db[14] = 1000; }
      if (!restore && scenario == 109 && !injected) { injected = true; return; } // ACK lost after actual commit.
      if (restore && scenario == 113 && !secondInjected) { secondInjected = true; return; }
    }
    ack(true);
    if (scenario == 142 && !restore && action == 1) ack(true); // Duplicate must not advance the next stage.
  }
  void run(unsigned limit = 70000) {
    for (unsigned i = 0; i < limit && GpsProfile::active(); ++i) {
      if (scenario == 133 && !injected && Profile::job == Profile::Job::Write && Profile::chunk == 1 && !Profile::restoring) {
        injected = true; mockTime += 60001;
      }
      if (scenario == 123 && !injected && Profile::job == Profile::Job::Write && !Profile::restoring)
        Serial1.shortTx = true;
      if (scenario == 129 && !injected && Profile::job == Profile::Job::Write && Profile::chunk == 2 && !Profile::restoring)
        Serial1.stuckTx = true;
      ++mockTime; GpsReceiver::update(); service();
      if (i % 250 == 0) validFix();
      if (Profile::phase == Profile::Phase::Report && (!Serial || Serial.freeSpace == 0)) break;
    }
  }
  bool safe() { return !invalid && !SPI.erases && !SPI.programs && !SPI.enables && !SPI.violations && Serial1.baud == 38400; }
};
}

int startupScenario(unsigned scenario) {
  using namespace ProfileModel;
  Receiver receiver(scenario == 149 ? 109 : scenario == 150 ? 106 : 0);
  stage = Stage::Idle;
  imuReady = true; flashReportPrinted = true;
  if (scenario == 146) {
    mockTime = 30000; drain(); statusGps();
    return !Serial1.writeCalls && !startupPending && contains("no checksum-valid NMEA within 30s") ? 0 : 250;
  }
  if (scenario == 147) memcpy(receiver.db, desired, sizeof(receiver.db));
  if (scenario == 148) Serial.connected = false;
  if (scenario == 151) {
    stage = Stage::Record; validFix();
    if (Serial1.writeCalls || !startupPending) return 251;
    stage = Stage::Idle;
  }
  validFix();
  if (scenario == 152) {
    for (unsigned i = 0; i < 5000; ++i) { ++mockTime; loop(); }
    return !GpsProfile::active() && Serial1.writeCalls == 3 && !receiver.sets &&
      strstr(GpsProfile::error(), "timeout") && receiver.safe() ? 0 : 262;
  }
  // Service even when USB is absent; do not rely on the model's report helper.
  for (unsigned i = 0; i < 45000 && (startupPending || GpsProfile::active()); ++i) {
    ++mockTime; loop(); receiver.service();
  }
  if (GpsProfile::active() || GpsReceiver::uartScanBusy() || startupPending || !receiver.safe()) return 252;
  if (scenario == 149) {
    if (!receiver.originalRestored() || !Profile::verified || !*GpsProfile::error()) return 253;
  } else if (!receiver.testConfigured() || !Profile::verified) return 254;
  if (scenario == 147) {
    if (receiver.sets || Profile::snapshotValid) return 255;
  } else if (scenario != 149 && !Profile::snapshotValid) return 256;
  if (scenario == 150 && !receiver.injected) return 257;
  if (!mockImuAxisReads || !mockRedWrites) return 258;
  Serial.connected = true;
  for (unsigned i = 0; i < 200; ++i) GpsReceiver::update();
  clear(); statusGps();
  if (!contains(scenario == 149 ? "RESTORED VERIFIED RAM" : "10HZ VERIFIED RAM")) return 259;
  if (scenario == 145 || scenario == 148) {
    SerialConsole::handleCommand("GPS RESTORE"); receiver.run();
    if (!receiver.originalRestored() || Profile::snapshotValid) return 260;
    validFix(); drain();
    if (GpsProfile::active() || !receiver.originalRestored()) return 261;
  }
  return 0;
}

int profileScenario(unsigned scenario) {
  using namespace ProfileModel;
  Receiver receiver(scenario);
  if (scenario == 136) {
    // Golden reproduction of the ORIGINAL three-packet implementation.
    // Rate bytes are correct, but SPG 5.10 ignores their APPLY payload.
    const unsigned starts[] = {0,9,14,22};
    for (unsigned stage=0; stage<3; ++stage) {
      uint8_t packet[64] = {0xb5,0x62,6,0x8a,0,0,1,1,uint8_t(stage+1),0};
      unsigned length=10;
      for (unsigned key=starts[stage];key<starts[stage+1];++key) {
        for (unsigned byte=0;byte<4;++byte) packet[length++]=ids[key]>>(8*byte);
        for (unsigned byte=0;byte<Receiver::width(ids[key]);++byte) packet[length++]=desired[key]>>(8*byte);
      }
      packet[4]=length-6; uint8_t a=0,b=0;
      for (unsigned i=2;i<length;++i) { a+=packet[i]; b+=a; }
      packet[length++]=a; packet[length++]=b;
      GpsUartTx::write(packet,length); receiver.service();
    }
    return receiver.db[4]==0 && receiver.db[14]==1000 && receiver.db[17]==1 && receiver.commits==1 && receiver.safe() ? 0 : 226;
  }
  if (scenario == 139 || scenario == 144) { receiver.scenario = 138; Serial.freeSpace = 0; }
  if (scenario == 140) receiver.scenario = 107;
  if (scenario == 141) receiver.scenario = 109;
  if (scenario == 132) {
    const char* const commands[] = {"GPS CONFIG","GPS RATE","GPS RAW","UART LOOPBACK TEST","GPS SCAN"};
    for (const char* cmd : commands) {
      SerialConsole::handleCommand(cmd); SerialConsole::handleCommand("GPS 10HZ TEST");
      if (GpsProfile::active()) return 222;
      Config::phase = Config::Phase::Idle; Rate::phase = Rate::Phase::Idle;
      rawPhase = RawPhase::Idle; loopPhase = LoopPhase::Idle; Scan::phase = Scan::Phase::Idle;
    }
    return !Serial1.writeCalls && receiver.safe() ? 0 : 223;
  }
  if (scenario == 103) {
    SerialConsole::handleCommand("GPS RESTORE"); drain();
    return !GpsProfile::active() && !Serial1.writeCalls && contains("refused") ? 0 : 200;
  }
  if (scenario == 102) {
    const Stage stages[] = {Stage::Scan,Stage::Record,Stage::Footer,Stage::Erase,Stage::ExportPage,Stage::Fault};
    for (Stage s : stages) { stage = s; SerialConsole::handleCommand("GPS 10HZ TEST"); if (GpsProfile::active()) return 201; }
    stage = Stage::Idle; gpsTestDiagnosticAvailable = false;
    SerialConsole::handleCommand("GPS 10HZ TEST");
    return !GpsProfile::active() && !Serial1.writeCalls ? 0 : 202;
  }
  if (scenario == 118) mockTime = 0xffffff00UL;
  if (scenario == 120) receiver.db[22] = 9600;
  if (scenario == 130) memcpy(receiver.db, desired, sizeof(receiver.db));
  if (scenario == 131) receiver.db[28] = 1;
  if (scenario == 125) { receiver.db[9] = 0; receiver.db[10] = 0; receiver.db[7] = 0; receiver.db[8] = 0; memcpy(receiver.original,receiver.db,sizeof(receiver.db)); }
  SerialConsole::handleCommand("GPS 10HZ TEST");
  if (!GpsProfile::active() || !GpsReceiver::uartScanBusy()) return 203;
  if (scenario == 144) {
    const uint8_t stray[] = {6,0x8a};
    for (unsigned i=0;i<80;++i) ubx(5,1,stray,sizeof(stray));
  }
  if (scenario == 115) {
    const char* const commands[] = {"GPS 10HZ TEST","GPS RESTORE","GPS CONFIG","GPS RAW","GPS SCAN","GPS RATE","UART LOOPBACK TEST",
      "RECORD START","RECORD ERASE","RECORD ERASE CONFIRM","RECORD READ","ERASE FFF000","DIAG"};
    for (const char* cmd : commands) SerialConsole::handleCommand(cmd);
    if (GpsConfig::active() || GpsUartScan::active() || GpsRate::active() || rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || Recorder::recording()) return 204;
  }
  if (scenario == 134) { receiver.scenario = 110; Serial.connected = false; }
  if (scenario == 116 || scenario == 117) { Serial.freeSpace = scenario == 116 ? 0 : 256; Serial.connected = scenario != 117; }
  receiver.run();
  if (scenario == 138 || scenario == 139 || scenario == 144) {
    if (!receiver.originalRestored() || !Profile::firstMismatch.valid || Profile::firstMismatch.key != 0x30210001 ||
        Profile::firstMismatch.actual != 1000 || Profile::firstMismatch.wanted != 100 || Profile::firstMismatch.restore ||
        Profile::testRead[14].value != 1000 || Profile::restoreRead[14].value != 1000) return 227;
    if (scenario == 139 || scenario == 144) { Serial.freeSpace=256; receiver.run(); }
    if (scenario == 144) return Profile::droppedEvents && contains("First mismatch before recovery: TEST 0x30210001 requested=100 actual=1000") && receiver.safe() ? 0 : 228;
    const char* failed = strstr(Serial.output,"READBACK MISMATCH key=0x30210001 requested=100 actual=1000");
    const char* rollback = strstr(Serial.output,"ROLLBACK BEGIN;");
    return failed && rollback && failed < rollback && Profile::firstMismatch.at < Profile::tx[1][0].sentAt &&
      contains("test_requested=100 test_actual=1000 restore_requested=1000 restore_actual=1000") &&
      contains("automatic RESTORE VERIFIED") && receiver.safe() ? 0 : 229;
  }
  if (scenario == 140 || scenario == 141) {
    const auto state = scenario==140 ? Profile::TxState::Nak : Profile::TxState::Timeout;
    return Profile::tx[0][3].state==state && Profile::tx[0][3].bytes==12 && Profile::tx[0][3].pairs==0 &&
      contains(scenario==140 ? "action=APPLY_EMPTY status=NAK" : "action=APPLY_EMPTY status=TIMEOUT") &&
      receiver.originalRestored() && receiver.safe() ? 0 : 230;
  }
  if (scenario == 142 && (Profile::ignoredAcks!=1 || !contains("IGNORED_ACK") || Profile::tx[0][3].state!=Profile::TxState::Ack)) return 231;
  if (scenario == 143 && (Profile::testRead[0].at-Profile::commitAckAt[0]<600 || Profile::testRead[14].at-Profile::commitAckAt[0]<3400)) return 232;
  if (scenario == 134) {
    if (!receiver.originalRestored() || Profile::snapshotValid || Profile::phase != Profile::Phase::Report) return 224;
    Serial.connected = true; receiver.run();
    return contains("automatic RESTORE VERIFIED") && receiver.safe() && !GpsProfile::active() ? 0 : 225;
  }
  if (scenario == 116 || scenario == 117) {
    if (Profile::phase != Profile::Phase::Report || !Profile::testApplied || Serial.overfilledWrites) return 205;
    Serial.freeSpace = 256; Serial.connected = true; receiver.run();
  }
  if (scenario == 104 || scenario == 105 || scenario == 106 || scenario == 120 || scenario == 130 || scenario == 131) {
    return !GpsProfile::active() && receiver.sets == 0 && !Profile::snapshotValid && contains("ABORTED") && receiver.safe() ? 0 : 206;
  }
  if (scenario == 107 || scenario == 108 || scenario == 109 || scenario == 110 || scenario == 111 || scenario == 122 || scenario == 123 || scenario == 124 || scenario == 133) {
    return !GpsProfile::active() && receiver.originalRestored() && !Profile::snapshotValid && !receiver.pending &&
      contains("automatic RESTORE VERIFIED") && receiver.safe() ? 0 : 207;
  }
  if (scenario == 112) {
    if (receiver.originalRestored() || !Profile::snapshotValid || !contains("RESTORE FAILED/UNVERIFIED")) return 208;
    SerialConsole::handleCommand("GPS RESTORE"); receiver.run();
    return receiver.originalRestored() && !Profile::snapshotValid && contains("RESTORE VERIFIED") && receiver.safe() ? 0 : 209;
  }
  if (scenario == 129) {
    if (!Profile::snapshotValid || !contains("RESTORE FAILED/UNVERIFIED") || receiver.sets != 3) return 220;
    Serial1.stuckTx = false; mockUarte.TXD.AMOUNT = GpsUartTx::previousSize();
    SerialConsole::handleCommand("GPS RESTORE"); receiver.run();
    return !Profile::snapshotValid && receiver.originalRestored() && receiver.safe() ? 0 : 221;
  }
  if (scenario != 125 && !receiver.testConfigured()) return 210;
  if (scenario == 125 && (receiver.db[9] || receiver.db[10] || receiver.db[7] != 1 || receiver.db[8] != 1)) return 211;
  if (!Profile::snapshotValid || !Profile::verified || GpsProfile::active() || receiver.commits != 1 || !receiver.safe()) return 212;
  for (unsigned i = 0; i < 29; ++i) if (Profile::saved[i] != receiver.original[i]) return 213;
  if (scenario == 114) {
    const unsigned before = Serial1.writeCalls;
    SerialConsole::handleCommand("GPS 10HZ TEST"); drain();
    if (GpsProfile::active() || before != Serial1.writeCalls || Profile::saved[14] != 1000) return 214;
  }
  if (scenario == 121) {
    stage = Stage::Record; SerialConsole::handleCommand("GPS RESTORE");
    if (GpsProfile::active() || !Profile::snapshotValid) return 215;
    stage = Stage::Idle;
  }
  if (scenario == 126) {
    memset(SPI.memory,0xff,sizeof(SPI.memory)); memset(SPI.finalSector,0xff,sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
    for (unsigned i=0;i<4100;++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD START");
    for (unsigned i=0;i<1000;++i) {
      if (i%100==0) prepareRateNmea(false,i,true);
      ++mockTime; loop();
    }
    SerialConsole::handleCommand("GPS RESTORE");
    if (GpsProfile::active()) return 216;
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i=0;i<600;++i) { ++mockTime; loop(); }
    return committed==100 && missed==0 && lost==0 && sizeof(RecordFormat::Sample)==16 && receiver.testConfigured() && !SPI.violations ? 0 : 217;
  }
  SerialConsole::handleCommand("GPS RESTORE"); receiver.run();
  if (scenario == 127 || scenario == 135) {
    if (!Profile::snapshotValid || !contains("RESTORE FAILED/UNVERIFIED")) return 218;
    SerialConsole::handleCommand("GPS RESTORE"); receiver.run();
  }
  if (scenario == 137) {
    unsigned stage=0;
    for (unsigned n=0;n<Serial1.writeCalls;++n) {
      const uint8_t* p=Serial1.writes[n]; if (p[2]!=6 || p[3]!=0x8a) continue;
      const unsigned expectedAction=stage%4==0 ? 1 : stage%4==3 ? 3 : 2;
      if (p[6]!=1 || p[7]!=1 || p[8]!=expectedAction || p[9]!=0) return 233;
      if (stage%4==3) {
        const uint8_t emptyApply[] = {0xb5,0x62,6,0x8a,4,0,1,1,3,0,0x99,0x1b};
        if (Serial1.writeSizes[n]!=sizeof(emptyApply) || memcmp(p,emptyApply,sizeof(emptyApply))) return 234;
      }
      if (stage%4==2) {
        const uint8_t expected[] = {1,0,0x21,0x30,uint8_t(stage<4 ? 100 : 0xe8),uint8_t(stage<4 ? 0 : 3),2,0,0x21,0x30,1,0};
        if (memcmp(p+10,expected,sizeof(expected))) return 235;
      }
      ++stage;
    }
    if (stage!=8) return 236;
  }
  return !GpsProfile::active() && receiver.originalRestored() && !Profile::snapshotValid && !Profile::testApplied &&
    contains("RESTORE VERIFIED") && receiver.safe() && !Serial.overfilledWrites ? 0 : 219;
}
