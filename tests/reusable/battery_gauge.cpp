// Host-only checks of the portable battery maths: ADC conversion, burst filtering,
// calibration, charge-estimate interpolation, categories, hysteresis and trust gating.
#include "../../src/battery_monitor.h"
#include "../../src/battery_gauge.h"

using Battery::Category;

namespace {
// Feeds a trusted, calibrated, cell-armed monitor with a steady nominal voltage.
void arm(Battery::Monitor& m, uint64_t& now, uint32_t nominalMv, unsigned samples = 6) {
  m.cell(true);
  if (!m.calibrate(1000000)) return;
  for (unsigned i = 0; i < samples; ++i) { now += 1000000; m.feed(nominalMv, now); }
}
}

int main() {
  // 1. ADC conversion: 12-bit, 0.6 V reference, gain 1/6 (3.6 V full scale), 1M/510k divider.
  if (Battery::nominalMillivolts(0) != 0 || Battery::nominalMillivolts(-5) != 0) return 1;
  if (Battery::nominalMillivolts(4095) != 0) return 2;  // Rail: never a reading.
  if (Battery::nominalMillivolts(1500) != 3904) return 3;
  for (int raw = 1; raw < 4095; raw += 37) {
    const double want = raw * 3600.0 / 4095.0 * 1510.0 / 510.0;
    const double got = Battery::nominalMillivolts(raw);
    if (got - want > 0.51 || want - got > 0.51) return 4;
  }
  // The pin sees at most 1.42 V from a 4.2 V cell: far inside the 3.6 V full scale.
  if (4200.0 * 510.0 / 1510.0 >= 3600.0) return 5;

  // 2. Burst filtering: sorted trimmed mean rejects a single high and low outlier.
  {
    uint32_t v[5] = {3900, 3905, 4300, 3895, 3902};
    if (Battery::trimmedMean(v, 5) != 3902) return 10;
    uint32_t w[5] = {3500, 3900, 3901, 3902, 3903};
    if (Battery::trimmedMean(w, 5) != 3901) return 11;
    uint32_t two[2] = {3900, 3910};
    if (Battery::trimmedMean(two, 2) != 3905) return 12;
    uint32_t one[1] = {3333};
    if (Battery::trimmedMean(one, 1) != 3333 || Battery::trimmedMean(one, 0) != 0) return 13;
  }

  // 3. Calibration: operator gain scales the nominal value; invalid gains are refused.
  {
    Battery::Monitor m;
    if (m.calibrate(700000) || m.calibrate(1300000) || m.calibrate(1000000, 500) || m.s.calibrated) return 20;
    if (!m.calibrate(1010000)) return 21;
    m.feed(3904, 1000000);
    if (m.s.mv != 3943 || m.s.nominalMv != 3904) return 22;  // Raw nominal and calibrated stay separate.
    if (m.gainPpm() != 1010000) return 23;
    Battery::Monitor o;
    if (!o.calibrate(1000000, -20)) return 24;
    o.feed(3904, 1000000);
    if (o.s.mv != 3884) return 25;
  }

  // 4. Charge estimate interpolation against the documented table.
  if (Battery::percentFromMv(4200) != 100 || Battery::percentFromMv(4350) != 100) return 30;
  if (Battery::percentFromMv(3300) != 0 || Battery::percentFromMv(3000) != 0 || Battery::percentFromMv(0) != 0) return 31;
  for (unsigned i = 0; i < Battery::kCurvePoints; ++i)
    if (Battery::percentFromMv(Battery::kCurve[i].mv) != Battery::kCurve[i].percent) return 32;
  if (Battery::percentFromMv(3830) != 48) return 33;  // Halfway between 3820 mV/45% and 3840 mV/50%.
  if (Battery::percentFromMv(3400) != 1) return 34;   // 3300 mV/0% to 3500 mV/2%.
  for (unsigned i = 1; i < Battery::kCurvePoints; ++i)
    if (Battery::kCurve[i].mv <= Battery::kCurve[i - 1].mv || Battery::kCurve[i].percent < Battery::kCurve[i - 1].percent) return 35;
  {
    unsigned last = 0;
    for (uint32_t mv = 3000; mv <= 4300; ++mv) {
      const unsigned p = Battery::percentFromMv(mv);
      if (p < last || p > 100) return 36;
      last = p;
    }
  }
  // Not linear: a linear 3.0-4.2 V map would call 3.8 V 67%.
  if (Battery::percentFromMv(3800) > 45) return 37;

  // 5. Category boundaries: exact, exhaustive, no overlap, no gap.
  for (unsigned p = 0; p <= 100; ++p) {
    const Category c = Battery::categoryForPercent(p);
    const Category want = p >= 80 ? Category::Full : p >= 45 ? Category::Half : p >= 10 ? Category::Low : Category::Critical;
    if (c != want) return 40;
  }
  if (Battery::categoryForPercent(100) != Category::Full || Battery::categoryForPercent(80) != Category::Full) return 41;
  if (Battery::categoryForPercent(79) != Category::Half || Battery::categoryForPercent(45) != Category::Half) return 42;
  if (Battery::categoryForPercent(44) != Category::Low || Battery::categoryForPercent(10) != Category::Low) return 43;
  if (Battery::categoryForPercent(9) != Category::Critical || Battery::categoryForPercent(0) != Category::Critical) return 44;
  if (Battery::categoryName(Category::Unknown)[0] != 'U' || Battery::categoryName(Category::Full)[0] != 'F' ||
      Battery::categoryName(Category::Half)[0] != 'H' || Battery::categoryName(Category::Low)[0] != 'L' ||
      Battery::categoryName(Category::Critical)[0] != 'C') return 45;

  // 6. Hysteresis: the first category is exact; later changes need to clear the boundary by 2 points.
  {
    Battery::CategoryTracker t;
    if (t.value() != Category::Unknown) return 50;
    if (t.update(80) != Category::Full) return 51;
    for (unsigned p = 78; p <= 100; ++p) if (t.update(p) != Category::Full) return 52;  // Down needs <=77.
    if (t.update(77) != Category::Half) return 53;
    for (unsigned i = 0; i < 20; ++i) {                                                  // Boundary chatter.
      if (t.update(79) != Category::Half || t.update(80) != Category::Half || t.update(81) != Category::Half) return 54;
    }
    if (t.update(82) != Category::Full) return 55;
    t.reset();
    if (t.update(79) != Category::Half) return 56;
    if (t.update(46) != Category::Half || t.update(43) != Category::Half) return 57;
    if (t.update(42) != Category::Low) return 58;
    if (t.update(45) != Category::Low || t.update(46) != Category::Low) return 59;
    if (t.update(47) != Category::Half) return 60;
    if (t.update(7) != Category::Critical) return 61;  // Falls through Low in one call.
    if (t.update(11) != Category::Critical || t.update(9) != Category::Critical) return 62;
    if (t.update(12) != Category::Low) return 63;
    if (t.update(100) != Category::Full) return 64;    // Rises through every boundary.
  }

  // 7. Invalid or implausible readings are rejected and never produce a trusted state.
  {
    Battery::Monitor m;
    uint64_t now = 0;
    m.feed(1999, now += 1000000);
    if (m.s.valid || m.s.invalid != 1) return 70;
    m.feed(4401, now += 1000000);
    if (m.s.valid || m.s.invalid != 2) return 71;
    m.feed(3900, now += 1000000);
    if (!m.s.valid) return 72;
    m.feed(3900, now);  // Duplicate timestamp.
    if (m.s.valid) return 73;
    // A plausible voltage proves nothing: no cell armed, no calibration => never trusted.
    Battery::Monitor usb;
    now = 0;
    for (unsigned i = 0; i < 10; ++i) usb.feed(4115, now += 1000000);
    if (usb.trusted(now) || !usb.s.valid) return 74;
    Battery::Gauge g;
    g.update(usb.trusted(now), usb.s.filteredMv, now, false, false);
    if (g.available(usb.trusted(now)) || g.category(usb.trusted(now)) != Category::Unknown) return 75;
    // Cell armed but not calibrated: still untrusted.
    Battery::Monitor uncal;
    now = 0;
    uncal.cell(true);
    for (unsigned i = 0; i < 10; ++i) uncal.feed(4115, now += 1000000);
    if (uncal.trusted(now)) return 76;
    // Stale readings lose trust.
    Battery::Monitor stale;
    now = 0;
    arm(stale, now, 3900);
    if (!stale.trusted(now) || stale.trusted(now + 3000001)) return 77;
  }

  // 8. Gauge: trusted-only, smooth, conservative under load / with USB, no 100% from a charger.
  {
    Battery::Monitor m;
    uint64_t now = 0;
    arm(m, now, 3900);
    Battery::Gauge g;
    g.update(m.trusted(now), m.s.filteredMv, now, false, false);
    const unsigned start = Battery::percentFromMv(3900);
    if (!g.available(true) || g.percent() != start || g.category(true) != Category::Half) return 80;
    if (g.category(false) != Category::Unknown || g.available(false)) return 81;
    // Voltage steps down 400 mV: the estimate walks down 1 point per 3 s, never jumps.
    uint64_t t = now;
    g.update(true, 3500, t += 1000000, false, false);
    if (g.percent() != start) return 82;
    g.update(true, 3500, t += 2000000, false, false);
    if (g.percent() != start - 1) return 83;
    g.update(true, 3500, t += 3000000, false, false);
    if (g.percent() != start - 2) return 84;
    // Rising voltage under load (or right after load/USB) is ignored until the cell rests.
    const unsigned held = g.percent();
    g.update(true, 3900, t += 1000000, true, false);
    g.update(true, 3900, t += 5000000, true, false);
    if (g.percent() != held) return 85;
    g.update(true, 3900, t += 9000000, false, false);
    if (g.percent() != held) return 86;  // Only 9 s unloaded so far.
    g.update(true, 3900, t += 2000000, false, false);
    g.update(true, 3900, t += 3000000, false, false);
    if (g.percent() != held + 1) return 87;  // Rises are slew-limited too.
    // Untrusted: no estimate, and a long loss of trust discards the stale value.
    if (g.available(false)) return 88;
    g.update(false, 3900, t += 1000000, false, false);
    if (!g.available(true)) return 89;
    g.update(false, 3900, t += 61000000, false, false);
    if (g.available(true)) return 90;
    // USB present at 4.2 V: a charger can hold the cell there, so 100% is never reported.
    Battery::Gauge c;
    c.update(true, 4200, 1000000, false, true);
    if (c.percent() != 99 || !c.chargeInfluencedEstimate()) return 91;
    // Off USB, a rested 4.2 V reading is allowed to be 100%.
    Battery::Gauge d;
    d.update(true, 4200, 1000000, false, false);
    if (d.percent() != 100 || d.chargeInfluencedEstimate()) return 92;
    // Falling is always allowed, even with USB present.
    Battery::Gauge e;
    e.update(true, 3800, 1000000, false, false);
    const unsigned before = e.percent();
    e.update(true, 3700, 4000000, false, true);
    if (e.percent() != before - 1) return 93;
  }
  return 0;
}
