#include "../../src/gps_power.h"

using GpsPower::BackupTest;
using Action = BackupTest::Action;
using Result = BackupTest::Result;
using Phase = BackupTest::Phase;

static_assert(TACTICPOD_GPS_BACKUP_TEST == 0 && TACTICPOD_GPS_BACKUP_STANDBY == 0,
              "both GPS backup features default OFF");

// Fake receiver + glue: executes actions the way gps_receiver.cpp does and feeds observations back.
struct Sim {
  BackupTest t;
  uint32_t now = 1000, bytes = 0, wakeGoodAt = 0;
  bool parked = false, gpsAsleep = false, receivedValid = false, profileOk = false, profileFail = false, fix = false;
  bool wakeWorks = true, nmeaStillFlows = false, strayBytesOnWake = false, acceptRequest = true;
  unsigned requests = 0, parks = 0, resumes = 0, wakes = 0;
  uint32_t resumedAt = 0, firstWakeAt = 0;
  unsigned order[8] = {}; unsigned orderN = 0;
  bool violate = false;
  uint32_t profileDelayMs = 2000, fixDelayMs = 3000, nmeaDelayMs = 500;
  uint32_t validSince = 0;

  void act(Action a) {
    switch (a) {
      case Action::SendRequest:
        ++requests; order[orderN++ & 7] = 1;
        gpsAsleep = !nmeaStillFlows && acceptRequest;
        t.requestSent(acceptRequest, now);
        break;
      case Action::ParkUart: ++parks; parked = true; order[orderN++ & 7] = 2; break;
      case Action::ResumeUart:
        ++resumes; parked = false; receivedValid = false; resumedAt = now; order[orderN++ & 7] = 3; break;
      case Action::SendWake:
        ++wakes; if (wakes == 1) firstWakeAt = now;
        if (gpsAsleep && wakeWorks && wakes >= 1 && !wakeGoodAt) wakeGoodAt = now + nmeaDelayMs;
        if (gpsAsleep && !wakeWorks && strayBytesOnWake) bytes += 3;
        break;
      default: break;
    }
  }
  void step() {
    now += 10;
    if (nmeaStillFlows && !parked) { bytes += 2; receivedValid = true; }
    if (wakeGoodAt && now >= wakeGoodAt && !parked) {
      gpsAsleep = false; if (!validSince) { validSince = now; } bytes += 2; receivedValid = true;
    }
    if (validSince) { profileOk = !profileFail && now - validSince >= profileDelayMs; fix = now - validSince >= fixDelayMs; }
    BackupTest::Obs o; o.now = now; o.bytes = bytes; o.txReady = true; o.validNmea = receivedValid;
    o.profileVerified = profileOk; o.profileFailed = profileFail && validSince; o.fix = fix; o.guardViolated = violate;
    act(t.step(o));
  }
  bool run(uint32_t limitMs) {
    for (uint32_t i = 0; i < limitMs / 10 && !t.done(); ++i) step();
    return t.done();
  }
};

static bool startOk(Sim& s) { BackupTest::Guards g; return s.t.start(g, s.now); }

int main() {
  // Guard refusals: every blocker alone refuses; clear guards start.
  {
    bool BackupTest::Guards::*blockers[] = {&BackupTest::Guards::recording, &BackupTest::Guards::sessionBusy,
      &BackupTest::Guards::flashBusy, &BackupTest::Guards::faulted, &BackupTest::Guards::gpsBusy,
      &BackupTest::Guards::suspended, &BackupTest::Guards::uartTxBusy};
    int n = 0;
    for (auto f : blockers) { BackupTest::Guards g; g.*f = true; if (!BackupTest::refusal(g)) return 10 + n; Sim s; if (s.t.start(g, 0)) return 20 + n; ++n; }
    BackupTest::Guards g; g.profileVerified = false; if (!BackupTest::refusal(g)) return 30;
    BackupTest::Guards h; h.nmeaFresh = false; if (!BackupTest::refusal(h)) return 31;
    BackupTest::Guards ok; if (BackupTest::refusal(ok)) return 32;
    Sim s; if (!startOk(s) || !s.t.running() || s.t.current() != Phase::Drain) return 33;
    BackupTest::Guards again; if (s.t.start(again, s.now)) return 34;  // No restart while running.
  }
  // Happy path: request, park, silent listen, wake, valid NMEA, 10 Hz verified, fix; ends in PASS.
  {
    Sim s; if (!startOk(s) || !s.run(60000)) return 40;
    if (s.t.result() != Result::Pass) return 41;
    if (s.requests != 1 || s.parks != 1 || s.resumes != 1 || s.wakes < 1) return 42;
    if (s.order[0] != 1 || s.order[1] != 2 || s.order[2] != 3) return 43;  // request -> park -> resume
    if (!s.t.requestOk() || !s.t.standbySilent() || !s.t.wakeSucceeded() || !s.t.baudConfirmed()) return 44;
    if (!s.t.profileVerified() || !s.t.fixSeen() || s.t.blocksRecording()) return 45;
    if (s.t.validAfter() < 400 || s.t.validAfter() > 700) return 46;  // Measured from the first wake attempt.
    if (s.resumedAt - 1000 < BackupTest::kDwellMs) return 47;  // Parked for the whole dwell.
    if (s.firstWakeAt - s.resumedAt < BackupTest::kListenMs) return 48;  // Silence window before any wake.
    if (s.parked || s.t.parked()) return 49;
  }
  // Pass does not depend on a fix: fix status is reported separately.
  {
    Sim s; s.fixDelayMs = 999999; startOk(s); if (!s.run(90000) || s.t.result() != Result::Pass || s.t.fixSeen()) return 50;
  }
  // GPS never wakes: bounded retries, clear NoWake, recording blocked, UART already restored.
  {
    Sim s; s.wakeWorks = false; startOk(s);
    if (!s.run(60000) || s.t.result() != Result::NoWake) return 60;
    if (s.wakes != GpsPower::kWakeAttempts || s.t.wakeAttempts() != GpsPower::kWakeAttempts) return 61;
    if (s.t.wakeSucceeded() || s.t.baudConfirmed() || !s.t.blocksRecording() || s.parked) return 62;
    if (s.now - 1000 > 20000) return 63;  // Bounded total time.
  }
  // Bytes return but never valid NMEA: baud not confirmed, recording blocked.
  {
    Sim s; s.wakeWorks = false; s.strayBytesOnWake = true; startOk(s);
    if (!s.run(60000) || s.t.result() != Result::NoValidNmea || s.t.baudConfirmed() || !s.t.blocksRecording()) return 70;
    if (!s.t.strayBytes()) return 71;
  }
  // Receiver kept streaming: standby not confirmed, no wake attempted, recording not latched off.
  {
    Sim s; s.nmeaStillFlows = true; startOk(s);
    if (!s.run(60000) || s.t.result() != Result::EntryNotConfirmed || s.wakes != 0 || s.t.blocksRecording()) return 80;
    if (s.t.standbySilent() || s.parked) return 81;
  }
  // 10 Hz audit fails or times out after wake: ProfileFailed, recording blocked.
  {
    Sim s; s.profileFail = true; startOk(s);
    if (!s.run(90000) || s.t.result() != Result::ProfileFailed || !s.t.blocksRecording() || s.t.profileVerified()) return 90;
    Sim u; u.profileDelayMs = 999999; startOk(u);
    if (!u.run(120000) || u.t.result() != Result::ProfileFailed) return 91;
  }
  // Request rejected by a busy UART: nothing parked, nothing to restore.
  {
    Sim s; s.acceptRequest = false; startOk(s);
    if (!s.run(1000) || s.t.result() != Result::RequestNotSent || s.parks || s.resumes || s.t.blocksRecording()) return 100;
  }
  // Recording/flash activity mid-test aborts and restores the UART if it was parked.
  {
    Sim s; startOk(s);
    for (unsigned i = 0; i < 100 && s.t.current() != Phase::Dwell; ++i) s.step();
    if (!s.parked) return 110;
    s.violate = true; s.step();
    if (s.t.result() != Result::Aborted || s.parked || s.resumes != 1 || s.t.blocksRecording()) return 111;
    Sim q; startOk(q); q.violate = true; q.step();  // Before the request: nothing to restore.
    if (q.t.result() != Result::Aborted || q.resumes != 0) return 112;
  }
  // Retry cadence is unchanged: first wake at listen end, then every 1 s.
  {
    Sim s; s.wakeWorks = false; startOk(s); s.run(60000);
    if (s.wakes != 8) return 120;
  }
  // Losing NMEA or the audited profile during the optional fix wait must fail,
  // including the exact step where the fix/timeout could otherwise report PASS.
  for (unsigned loss = 0; loss < 3; ++loss) {
    Sim s; s.fixDelayMs = 999999; startOk(s);
    while (s.t.running() && s.t.current() != Phase::Fix) s.step();
    if (s.t.current() != Phase::Fix || !s.t.profileVerified()) return 130;
    BackupTest::Obs o; o.now = s.now + BackupTest::kFixMs;
    o.validNmea = loss != 0; o.profileVerified = loss != 1;
    o.profileFailed = loss == 2; o.fix = true;
    s.t.step(o);
    if (s.t.result() != Result::ProfileFailed || !s.t.blocksRecording() || s.t.profileVerified()) return 131 + loss;
  }
  // A stale "verified" observation cannot override an explicit failed audit or
  // the absence of valid NMEA, and a stalled profile phase has a bounded timeout.
  for (unsigned failure = 0; failure < 3; ++failure) {
    Sim s; s.profileDelayMs = 999999; startOk(s);
    while (s.t.running() && s.t.current() != Phase::Profile) s.step();
    BackupTest::Obs o; o.now = s.now + BackupTest::kProfileMs;
    o.validNmea = failure != 1; o.profileVerified = failure != 2;
    o.profileFailed = failure == 0;
    s.t.step(o);
    if (s.t.result() != Result::ProfileFailed || !s.t.blocksRecording()) return 140 + failure;
  }
  // Starting subsequent cycles resets previous success/failure observations.
  {
    Sim s;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
      s.wakeGoodAt = s.validSince = 0; s.receivedValid = s.profileOk = s.fix = false;
      s.profileFail = cycle == 1;
      if (!startOk(s) || s.t.wakeSucceeded() || s.t.profileVerified() || s.t.fixSeen()) return 150;
      if (!s.run(90000)) return 151;
      if (s.t.result() != (cycle == 1 ? Result::ProfileFailed : Result::Pass)) return 152;
      if (s.t.blocksRecording() != (cycle == 1)) return 153;
    }
  }
  return 0;
}
