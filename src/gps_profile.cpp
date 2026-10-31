#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "gps_profile.h"
#include "gps_config.h"
#include "gps_uart_tx.h"
#include "recorder.h"
#include "flash_write_test.h"

namespace {
namespace Profile {
// M10 SPG 5.10, UBX-21035062 R03. No CPU/OTP keys or persistent layers.
constexpr uint32_t kKeep = 0xffffffffUL;
struct Key { uint32_t id; const char* name; uint32_t target; };
const Key keys[] = {
  {0x1031001f, "GPS_ENA", 1}, {0x10310001, "GPS_L1CA_ENA", 1},
  {0x10310021, "GAL_ENA", 1}, {0x10310007, "GAL_E1_ENA", 1},
  {0x10310022, "BDS_ENA", 0}, {0x1031000d, "BDS_B1_ENA", 0},
  {0x10310025, "GLO_ENA", 0}, {0x10310024, "QZSS_ENA", 1},
  {0x10310012, "QZSS_L1CA_ENA", 1},
  {0x10310020, "SBAS_ENA", kKeep}, {0x10310005, "SBAS_L1CA_ENA", kKeep},
  {0x1031000f, "BDS_B1C_ENA", kKeep}, {0x10310014, "QZSS_L1S_ENA", kKeep},
  {0x10310018, "GLO_L1_ENA", kKeep},
  {0x30210001, "RATE_MEAS_ms", 100}, {0x30210002, "RATE_NAV", 1},
  {0x209100ac, "RMC_UART1", 1}, {0x209100bb, "GGA_UART1", 10},
  {0x209100c0, "GSA_UART1", 0}, {0x209100c5, "GSV_UART1", 0},
  {0x209100ca, "GLL_UART1", 0}, {0x209100b1, "VTG_UART1", 0},
  // Read-only guards: never written by either command. Snapshot and verify
  // these as well, so success cannot hide a changed UART/time reference.
  {0x40520001, "UART1_BAUDRATE", 38400}, {0x10520005, "UART1_ENABLED", 1},
  {0x10730001, "UART1INPROT_UBX", 1}, {0x10740001, "UART1OUTPROT_UBX", 1},
  {0x10740002, "UART1OUTPROT_NMEA", 1}, {0x20210003, "RATE_TIMEREF", kKeep},
  {0x20d00001, "PM_OPERATEMODE", 0},
};
constexpr unsigned kWritten = 22, kKeys = sizeof(keys) / sizeof(keys[0]);
// Every packet fits one BSP DMA batch. 57, 37, 54, 12-byte writes also have
// different lengths, preventing stale AMOUNT from masquerading as completion.
const unsigned chunkStart[] = {0, 9, 14, kWritten};
constexpr unsigned kDataChunks = 3, kStages = 4;
uint32_t saved[kKeys];
bool snapshotValid = false, restoring = false, automatic = false, touched = false;
bool verified = false, verificationFailed = false, testApplied = false;
bool noticePending = false;
bool startup = false;
unsigned readRetries = 0;
const char* configuration = "UNKNOWN (not verified)";
char configurationValues[220];
enum class Job { Version, Snapshot, Write, Verify };
enum class Phase { Idle, Send, Wait, Gap, Report };
Job job = Job::Version;
Phase phase = Phase::Idle;
unsigned keyIndex = 0, chunk = 0, copies = 1, reportLine = 0;
uint32_t phaseAt = 0, attemptAt = 0, gapMs = 0;
const char* reason = "";
const char* outcome = "";
uint32_t failedKey = 0, mismatches = 0;
char output[240]; size_t outputLength = 0;
enum class ReadState : uint8_t { None, Value, Timeout, Nak };
struct Readback { uint32_t value, at; ReadState state; } testRead[kKeys], restoreRead[kKeys];
enum class TxState : uint8_t { None, Sent, Ack, Nak, Timeout, Short, Busy };
struct TxRecord { uint32_t sentAt, replyMs; uint16_t bytes; uint8_t action, pairs; TxState state; } tx[2][kStages];
struct Failure { uint32_t key, wanted, actual, at, commitAge; bool valid, restore; } firstMismatch;
enum class EventKind : uint8_t { Tx, Ack, Nak, Timeout, Short, Busy, Mismatch, Rollback, IgnoredAck };
struct Event { uint32_t at, key, wanted, actual; EventKind kind; uint8_t stage; bool restore; } events[64];
unsigned eventHead = 0, eventCount = 0;
uint32_t droppedEvents = 0, ignoredAcks = 0, commitAckAt[2] = {};
bool outputIsTrace = false;
constexpr unsigned kFirstValueLine = 3 + 2 * kStages;

unsigned sizeOf(uint32_t key) { return key >> 28 == 3 ? 2 : key >> 28 == 4 ? 4 : 1; }
uint32_t readLe(const uint8_t* p, unsigned size) {
  uint32_t value = 0;
  for (unsigned i = 0; i < size; ++i) value |= uint32_t(p[i]) << (8 * i);
  return value;
}
void writeLe(uint8_t* p, uint32_t value, unsigned size) {
  for (unsigned i = 0; i < size; ++i) p[i] = value >> (8 * i);
}
uint32_t target(unsigned i) { return restoring || keys[i].target == kKeep ? saved[i] : keys[i].target; }
uint32_t testTarget(unsigned i) { return keys[i].target == kKeep ? saved[i] : keys[i].target; }
void text(const char* p) {
  while (*p && outputLength + 1 < sizeof(output)) output[outputLength++] = *p++;
  output[outputLength] = 0;
}
void number(uint32_t value) {
  char digits[11]; char* p = digits + 10; *p = 0;
  do { *--p = '0' + value % 10; value /= 10; } while (value);
  text(p);
}
void hex(uint32_t value) {
  text("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    char c[2] = {"0123456789ABCDEF"[(value >> shift) & 15], 0}; text(c);
  }
}
const char* actionName(unsigned action) { return action == 1 ? "START" : action == 2 ? "CONTINUE" : "APPLY_EMPTY"; }
const char* txName(TxState state) {
  switch (state) {
    case TxState::Sent: return "SENT/NO_REPLY";
    case TxState::Ack: return "ACK";
    case TxState::Nak: return "NAK";
    case TxState::Timeout: return "TIMEOUT";
    case TxState::Short: return "SHORT_TX";
    case TxState::Busy: return "TX_INCOMPLETE";
    default: return "NOT_SENT";
  }
}
void event(EventKind kind, uint32_t key = 0, uint32_t wanted = 0, uint32_t actual = 0) {
  if (eventCount == sizeof(events) / sizeof(events[0])) { ++droppedEvents; return; }
  events[(eventHead + eventCount++) % 64] = {millis(), key, wanted, actual, kind, uint8_t(chunk), restoring};
}
void readText(const Readback& value) {
  if (value.state == ReadState::Value) number(value.value);
  else text(value.state == ReadState::Timeout ? "TIMEOUT" : value.state == ReadState::Nak ? "NAK" : "NOT_READ");
}
void flushTrace() {
  if (!eventCount || !Serial || Recorder::exporting() || (outputLength && !outputIsTrace)) return;
  const Event& e = events[eventHead];
  if (!outputLength) {
    outputIsTrace = true;
    text("GPS TRACE "); text(e.restore ? "RESTORE " : "TEST ");
    text("t_ms="); number(e.at); text(" ");
    if (e.kind == EventKind::Tx) {
      text("TX stage="); number(e.stage + 1); text("/4 action="); text(actionName(e.key));
      text(" RAM=1 bytes="); number(e.actual); text(" key_values="); number(e.wanted);
    } else if (e.kind == EventKind::Mismatch) {
      text("READBACK MISMATCH key="); hex(e.key); text(" requested="); number(e.wanted);
      text(" actual="); number(e.actual); text(" commit_age_ms=");
      if (tx[e.restore ? 1 : 0][kDataChunks].state == TxState::Ack) number(e.at - commitAckAt[e.restore ? 1 : 0]);
      else text("UNKNOWN (commit ACK missing)");
      text(e.restore ? " (restore verification)" : " (captured before rollback)");
    } else if (e.kind == EventKind::Rollback) text("ROLLBACK BEGIN; original snapshot retained");
    else {
      text(e.kind == EventKind::Ack ? "ACK" : e.kind == EventKind::Nak ? "NAK" :
           e.kind == EventKind::Timeout ? "TIMEOUT" : e.kind == EventKind::Short ? "SHORT_TX" :
           e.kind == EventKind::Busy ? "TX_INCOMPLETE" : e.actual == 0 ? "IGNORED_NAK" : "IGNORED_ACK");
      text(" stage=");
      if (e.stage < kStages) { number(e.stage + 1); text("/4"); } else text("POST_COMMIT");
      text(" key_or_message="); hex(e.key);
      text(" elapsed_ms="); number(e.wanted);
    }
    text("\n");
  }
  if (Serial.availableForWrite() < int(outputLength)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(output), outputLength) != outputLength) return;
  outputLength = 0; outputIsTrace = false; eventHead = (eventHead + 1) % 64; --eventCount;
}
void gap(uint32_t duration) { phase = Phase::Gap; gapMs = duration; phaseAt = millis(); }
void report(const char* result) {
  if (!verified && touched) configuration = "UNKNOWN (configuration failed; GPS RESTORE may be required)";
  outcome = result; phase = Phase::Report; reportLine = 0; outputLength = 0; outputIsTrace = false; noticePending = false;
}
void beginRollback(bool autoRestore) {
  readRetries = 0;
  restoring = true; automatic = autoRestore; verified = false;
  verificationFailed = false; mismatches = 0; chunk = keyIndex = 0;
  job = Job::Write; attemptAt = millis(); gap(600);
  memset(restoreRead, 0, sizeof(restoreRead)); memset(tx[1], 0, sizeof(tx[1])); commitAckAt[1] = 0;
  event(EventKind::Rollback);
}
void fail(const char* why) {
  if (!*reason) reason = why;
  if (!failedKey && (job == Job::Snapshot || job == Job::Verify)) failedKey = keys[keyIndex].id;
  if (!restoring) {
    if (snapshotValid && touched) { beginRollback(true); return; }
    snapshotValid = false; report("ABORTED: no configuration writes sent"); return;
  }
  // A VALGET cancels an unfinished VALSET transaction. Even after a lost ACK
  // or rejected restore, inspect every field before declaring rollback failed.
  if (job == Job::Write) { job = Job::Verify; keyIndex = 0; gap(600); return; }
  verificationFailed = true; ++mismatches;
  if (++keyIndex == kKeys) report("RESTORE FAILED/UNVERIFIED: snapshot retained; retry GPS RESTORE");
  else gap(200);
}
bool preflight() {
  if (!saved[14] || !saved[15] || saved[15] > 127) { failedKey = keys[14].id; return false; }
  for (unsigned i = kWritten; i < kKeys; ++i)
    if (keys[i].target != kKeep && saved[i] != keys[i].target) { failedKey = keys[i].id; return false; }
  return true;
}
void advanceValue(uint32_t value) {
  readRetries = 0;
  if (job == Job::Snapshot) saved[keyIndex] = value;
  else {
    (restoring ? restoreRead : testRead)[keyIndex] = {value, millis(), ReadState::Value};
  }
  if (job != Job::Snapshot && value != target(keyIndex)) {
    event(EventKind::Mismatch, keys[keyIndex].id, target(keyIndex), value);
    if (!firstMismatch.valid) firstMismatch = {keys[keyIndex].id, target(keyIndex), value, millis(),
      tx[restoring ? 1 : 0][kDataChunks].state == TxState::Ack ? uint32_t(millis() - commitAckAt[restoring ? 1 : 0]) : 0xffffffffUL, true, restoring};
    if (!restoring) { fail("test readback mismatch"); return; }
    verificationFailed = true; ++mismatches;
    if (!failedKey) failedKey = keys[keyIndex].id;
  }
  if (++keyIndex < kKeys) { gap(200); return; }
  keyIndex = 0;
  if (job == Job::Snapshot) {
    if (!preflight()) { fail("UART/protocol/power preflight incompatible; nothing changed"); return; }
    bool alreadyMatches = true;
    for (unsigned i = 0; i < kWritten; ++i)
      if (keys[i].target != kKeep && saved[i] != keys[i].target) alreadyMatches = false;
    if (alreadyMatches) {
      if (!startup) { fail("receiver already matches test profile; original snapshot may have been lost"); return; }
      // A board-only reset loses the old snapshot. Never invent a baseline.
      verified = testApplied = true;
      configuration = "10HZ VERIFIED RAM MEAS=100 ms NAV=1 GPS=1 GAL=1 BDS=0 RMC=1 GGA=10 GSA/GSV/GLL/VTG=0 UART1=38400 (snapshot unavailable)";
      report("STARTUP 10HZ ALREADY VERIFIED (no writes; original snapshot unavailable)"); return;
    }
    snapshotValid = true; job = Job::Write; chunk = 0; gap(200); return;
  }
  if (verificationFailed) { report("RESTORE FAILED/UNVERIFIED: snapshot retained; retry GPS RESTORE"); return; }
  verified = true;
  if (restoring) {
    testApplied = false; snapshotValid = false;
    outputLength = 0;
    text("RESTORED VERIFIED RAM MEAS="); number(saved[14]); text(" ms NAV="); number(saved[15]);
    text(" RMC="); number(saved[16]); text(" GGA="); number(saved[17]);
    text(" GPS="); number(saved[0]); text(" GAL="); number(saved[2]); text(" BDS="); number(saved[4]);
    strncpy(configurationValues, output, sizeof(configurationValues) - 1);
    configurationValues[sizeof(configurationValues) - 1] = 0;
    configuration = configurationValues;
    report(automatic ? "TEST FAILED; automatic RESTORE VERIFIED" : "RESTORE VERIFIED");
  } else {
    testApplied = true;
    configuration = "10HZ VERIFIED RAM MEAS=100 ms NAV=1 GPS=1 GAL=1 BDS=0 RMC=1 GGA=10 GSA/GSV/GLL/VTG=0 UART1=38400";
    report(startup ? "STARTUP 10HZ VERIFIED (RAM only)" : "10HZ TEST VERIFIED (RAM only; use GPS RATE to measure cadence)");
  }
}
bool matchingVersion(const uint8_t* p, size_t length) {
  const char expected[] = "FWVER=SPG 5.10";
  for (unsigned base = 40; base + 30 <= length; base += 30) {
    if (memcmp(p + base, expected, sizeof(expected) - 1) == 0 &&
        (p[base + sizeof(expected) - 1] == 0 || p[base + sizeof(expected) - 1] == ' ')) return true;
  }
  return false;
}
void render() {
  outputLength = 0;
  if (!reportLine) {
    text("GPS PROFILE: "); text(outcome); text("\n");
  } else if (reportLine == 1) {
    text("Reason: "); text(*reason ? reason : "explicit command completed");
    text(" | first_failed_key="); hex(failedKey); text(" | restore_mismatches/unavailable="); number(mismatches); text("\n");
  } else if (reportLine == 2) {
    text("First mismatch before recovery: ");
    if (!firstMismatch.valid) text("NONE");
    else {
      text(firstMismatch.restore ? "RESTORE " : "TEST "); hex(firstMismatch.key);
      text(" requested="); number(firstMismatch.wanted); text(" actual="); number(firstMismatch.actual);
      text(" t_ms="); number(firstMismatch.at); text(" commit_age_ms=");
      if (firstMismatch.commitAge == 0xffffffffUL) text("UNKNOWN"); else number(firstMismatch.commitAge);
    }
    text("\n");
  } else if (reportLine >= 3 && reportLine < kFirstValueLine) {
    const unsigned which = (reportLine - 3) / kStages, stage = (reportLine - 3) % kStages;
    const TxRecord& t = tx[which][stage];
    text(which ? "RESTORE" : "TEST"); text(" transaction stage="); number(stage + 1); text("/4 action=");
    text(actionName(stage == 0 ? 1 : stage == kDataChunks ? 3 : 2));
    text(" status="); text(txName(t.state)); text(" bytes="); number(t.bytes);
    text(" key_values="); number(t.pairs); text(" reply_ms="); number(t.replyMs); text("\n");
  } else if (reportLine >= kFirstValueLine && reportLine < kFirstValueLine + kKeys && touched) {
    const unsigned i = reportLine - kFirstValueLine;
    text(keys[i].name); text(" ["); hex(keys[i].id); text("] saved="); number(saved[i]);
    text(" test_requested="); number(testTarget(i)); text(" test_actual="); readText(testRead[i]);
    text(" restore_requested="); number(saved[i]); text(" restore_actual="); readText(restoreRead[i]); text("\n");
  } else {
    text("GPS PROFILE END | UART=38400 | snapshot="); text(snapshotValid ? "retained in XIAO RAM" : "none");
    text(" | trace_dropped="); number(droppedEvents); text(" ignored_ack_nak="); number(ignoredAcks);
    text(" | no BBR/flash/OTP writes; recordings unchanged.\n");
  }
}
}
}

namespace GpsProfile {
bool active() { return Profile::phase != Profile::Phase::Idle && !(Profile::startup && Profile::phase == Profile::Phase::Report); }
const char* status() { return active() ? "CONFIGURING (RAM only)" : Profile::configuration; }
const char* error() { return Profile::reason; }
bool startTest() {
  using namespace Profile;
  if (active() || GpsConfig::active() || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed() || snapshotValid) return false;
  restoring = automatic = touched = verified = verificationFailed = testApplied = false;
  startup = false; readRetries = 0;
  keyIndex = chunk = 0; reason = outcome = ""; failedKey = mismatches = 0;
  memset(testRead, 0, sizeof(testRead)); memset(restoreRead, 0, sizeof(restoreRead)); memset(tx, 0, sizeof(tx));
  firstMismatch = Failure{}; commitAckAt[0] = commitAckAt[1] = 0;
  eventHead = eventCount = 0; droppedEvents = ignoredAcks = 0; outputLength = 0; outputIsTrace = false;
  job = Job::Version; phase = Phase::Send; phaseAt = attemptAt = millis();
  noticePending = true;
  return true;
}
bool startStartup() {
  if (!startTest()) return false;
  Profile::startup = true;
  return true;
}
bool startRestore() {
  using namespace Profile;
  if (active() || GpsConfig::active() || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed() || !snapshotValid) return false;
  reason = outcome = ""; failedKey = 0;
  beginRollback(false); noticePending = true; return true;
}
void onFrame(uint8_t cls, uint8_t id, const uint8_t* p, size_t length) {
  using namespace Profile;
  if (active() && cls == 5 && (id == 0 || id == 1) && length == 2 && p[0] == 6 && p[1] == 0x8a &&
      (phase != Phase::Wait || job != Job::Write || uint32_t(millis() - phaseAt) >= 1200)) {
    ++ignoredAcks; event(EventKind::IgnoredAck, 0x068a, uint32_t(millis() - phaseAt), id);
  }
  if (phase != Phase::Wait || uint32_t(millis() - phaseAt) >= 1200) return;
  if (job == Job::Version) {
    if (cls == 5 && id == 0 && length == 2 && p[0] == 10 && p[1] == 4) {
      event(EventKind::Nak, 0x0a04, uint32_t(millis() - phaseAt)); fail("MON-VER rejected"); return;
    }
    if (cls != 10 || id != 4 || length < 40 || (length - 40) % 30) return;
    if (!matchingVersion(p, length)) { fail("receiver must advertise FWVER=SPG 5.10"); return; }
    readRetries = 0; job = Job::Snapshot; keyIndex = 0; gap(200); return;
  }
  const uint8_t expectedId = job == Job::Write ? 0x8a : 0x8b;
  if (cls == 5 && length == 2 && p[0] == 6 && p[1] == expectedId) {
    if (id == 0) {
      event(EventKind::Nak, 0x0600 | expectedId, uint32_t(millis() - phaseAt));
      if (job == Job::Write) { tx[restoring ? 1 : 0][chunk].state = TxState::Nak; tx[restoring ? 1 : 0][chunk].replyMs = millis() - phaseAt; }
      else if (job == Job::Verify) (restoring ? restoreRead : testRead)[keyIndex].state = ReadState::Nak;
      fail("receiver rejected UBX request"); return;
    }
    if (id != 1 || job != Job::Write) return;
    tx[restoring ? 1 : 0][chunk].state = TxState::Ack;
    tx[restoring ? 1 : 0][chunk].replyMs = millis() - phaseAt;
    event(EventKind::Ack, 0x068a, uint32_t(millis() - phaseAt));
    if (chunk == kDataChunks) commitAckAt[restoring ? 1 : 0] = millis();
    if (++chunk == kStages) { job = Job::Verify; keyIndex = 0; gap(600); }
    else gap(200);
    return;
  }
  if (job == Job::Write || cls != 6 || id != 0x8b) return;
  const unsigned size = sizeOf(keys[keyIndex].id);
  if (length != 4 + copies * (4 + size) || p[0] != 1 || p[1] || p[2] || p[3]) return;
  for (unsigned copy = 0; copy < copies; ++copy) {
    const unsigned base = 4 + copy * (4 + size);
    if (readLe(p + base, 4) != keys[keyIndex].id ||
        (copy && readLe(p + base + 4, size) != readLe(p + 8, size))) return;
  }
  const uint32_t value = readLe(p + 8, size);
  if ((keys[keyIndex].id >> 28) == 1 && value > 1) return;
  advanceValue(value);
}
void update() {
  using namespace Profile;
  if (phase == Phase::Idle) return;
  const uint32_t now = millis();
  flushTrace(); // Never wait for USB: capture/rollback progress independently.
  if (noticePending && Serial) {
    const char* message = restoring ? "GPS RESTORE: restoring saved RAM snapshot; wait for GPS PROFILE END.\n" :
      startup ? "GPS STARTUP: capturing rollback snapshot before RAM-only 10Hz configuration.\n" :
      "GPS 10HZ TEST: capturing rollback snapshot before RAM-only changes; wait for GPS PROFILE END.\n";
    const size_t length = strlen(message);
    if (Serial.availableForWrite() >= int(length) && Serial.write(reinterpret_cast<const uint8_t*>(message), length) == length)
      noticePending = false;
  }
  if (phase != Phase::Report && uint32_t(now - attemptAt) >= 60000) {
    if (!restoring && touched) { if (!*reason) reason = "test operation deadline"; beginRollback(true); }
    else { if (!*reason) reason = "operation deadline"; report(touched ? "RESTORE FAILED/UNVERIFIED: snapshot retained; retry GPS RESTORE" : "ABORTED: no configuration writes sent"); }
  }
  if (phase == Phase::Gap && uint32_t(now - phaseAt) >= gapMs) { phase = Phase::Send; phaseAt = now; }
  if (phase == Phase::Send) {
    if (!GpsUartTx::ready()) {
      if (uint32_t(now - phaseAt) >= 1000) {
        event(EventKind::Busy, job == Job::Write ? 0x068a : 0x068b, uint32_t(now - phaseAt));
        if (job == Job::Write) tx[restoring ? 1 : 0][chunk].state = TxState::Busy;
        if (restoring) { if (!*reason) reason = "UART TX incomplete; reset/recovery may be required"; report("RESTORE FAILED/UNVERIFIED: snapshot retained; retry GPS RESTORE"); }
        else fail("UART TX incomplete");
      }
      return;
    }
    uint8_t packet[64] = {0xb5, 0x62, 6, uint8_t(job == Job::Write ? 0x8a : 0x8b)};
    unsigned length = 10;
    if (job == Job::Version) { packet[2] = 10; packet[3] = 4; length = 6; }
    else if (job == Job::Write) {
      packet[6] = 1; packet[7] = 1; // version 1, RAM bit only; BBR/flash bits clear
      // SPG 5.10 section 4.6: APPLY ignores supplied key/value pairs.
      // Stage ALL data with START/ONGOING, then send an empty APPLY.
      packet[8] = chunk == 0 ? 1 : chunk == kDataChunks ? 3 : 2;
      for (unsigned i = chunk < kDataChunks ? chunkStart[chunk] : kWritten;
           i < (chunk < kDataChunks ? chunkStart[chunk + 1] : kWritten); ++i) {
        writeLe(packet + length, keys[i].id, 4); length += 4;
        const unsigned size = sizeOf(keys[i].id); writeLe(packet + length, target(i), size); length += size;
      }
      if (chunk < kDataChunks && length + 2 == GpsUartTx::previousSize()) {
        // Official VALSET semantics allow duplicate keys (last value wins).
        // A retry must not reuse the preceding DMA length: its AMOUNT could
        // otherwise be stale if this new transmission stalls.
        const unsigned i = chunkStart[chunk + 1] - 1;
        writeLe(packet + length, keys[i].id, 4); length += 4;
        const unsigned size = sizeOf(keys[i].id); writeLe(packet + length, target(i), size); length += size;
      }
      touched = true; // A short/uncertain write may still have reached receiver.
    } else {
      copies = GpsUartTx::previousSize() == 16 ? 2 : 1;
      for (unsigned copy = 0; copy < copies; ++copy) { writeLe(packet + length, keys[keyIndex].id, 4); length += 4; }
    }
    packet[4] = length - 6; // Payload length; all packets are <256 bytes.
    uint8_t a = 0, b = 0;
    for (unsigned i = 2; i < length; ++i) { a += packet[i]; b += a; }
    packet[length++] = a; packet[length++] = b;
    if (job == Job::Write) {
      unsigned pairs = chunk < kDataChunks ? chunkStart[chunk + 1] - chunkStart[chunk] : 0;
      if (chunk < kDataChunks && length != (chunk == 0 ? 57 : chunk == 1 ? 37 : 54)) ++pairs;
      tx[restoring ? 1 : 0][chunk] = {now, 0, uint16_t(length), packet[8], uint8_t(pairs), TxState::Sent};
      event(EventKind::Tx, packet[8], pairs, length);
    }
    if (GpsUartTx::write(packet, length) != length) {
      if (job == Job::Write) tx[restoring ? 1 : 0][chunk].state = TxState::Short;
      event(EventKind::Short, job == Job::Write ? 0x068a : 0x068b);
      fail("short/uncertain UART write"); return;
    }
    phase = Phase::Wait; phaseAt = now;
  } else if (phase == Phase::Wait && uint32_t(now - phaseAt) >= 1200) {
    event(EventKind::Timeout, job == Job::Write ? 0x068a : job == Job::Version ? 0x0a04 : keys[keyIndex].id, uint32_t(now - phaseAt));
    if (job == Job::Write) { tx[restoring ? 1 : 0][chunk].state = TxState::Timeout; tx[restoring ? 1 : 0][chunk].replyMs = now - phaseAt; }
    else if (job == Job::Verify) (restoring ? restoreRead : testRead)[keyIndex].state = ReadState::Timeout;
    // Never retry a VALSET stage: ACKs carry no transaction sequence number.
    // Read-only requests can safely be retried twice within the job deadline.
    if (startup && job != Job::Write && readRetries < 2) { ++readRetries; gap(200); }
    else fail("UBX response timeout");
  }
  if (phase != Phase::Report || !Serial || Recorder::exporting()) return;
  if (eventCount && (!outputLength || outputIsTrace)) { flushTrace(); return; }
  if (!touched && reportLine == 3) reportLine = kFirstValueLine + kKeys;
  if (!outputLength) render();
  if (Serial.availableForWrite() < int(outputLength)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(output), outputLength) != outputLength) return;
  outputLength = 0;
  if (++reportLine == kFirstValueLine + kKeys + 1) phase = Phase::Idle;
}
}
