#pragma once
#include <stdint.h>

// Portable, fixed-memory, voltage-only charge ESTIMATE for a single-cell LiPo.
// This is not a fuel gauge: it knows nothing about current, temperature or age.
namespace Battery {

enum class Category : uint8_t { Unknown, Critical, Low, Half, Full };

inline const char* categoryName(Category c) {
  switch (c) {
    case Category::Critical: return "CRITICAL";
    case Category::Low: return "LOW";
    case Category::Half: return "HALF";
    case Category::Full: return "FULL";
    default: return "UNKNOWN";
  }
}

// Median-ish burst filter: sort, discard the lowest and highest, average the rest.
// With fewer than three values the plain mean is used. Sorts the caller's array.
inline uint32_t trimmedMean(uint32_t* v, unsigned n) {
  if (!n) return 0;
  for (unsigned i = 1; i < n; ++i) {
    const uint32_t x = v[i];
    unsigned j = i;
    while (j && v[j - 1] > x) { v[j] = v[j - 1]; --j; }
    v[j] = x;
  }
  const unsigned lo = n >= 3 ? 1 : 0, hi = n >= 3 ? n - 1 : n;
  uint32_t sum = 0;
  for (unsigned i = lo; i < hi; ++i) sum += v[i];
  return (sum + (hi - lo) / 2) / (hi - lo);
}

// Generic 1S LiPo (4.20 V full) open-circuit/light-load discharge table, roughly
// C/5 or lighter. It is a widely used typical curve, NOT measured for this cell or
// load. Real cells differ with age, temperature and current; validate on hardware.
// Bottom end follows the existing 3.30 V hard limit rather than the cell's 3.0 V.
struct CurvePoint { uint16_t mv; uint8_t percent; };
constexpr CurvePoint kCurve[] = {
  {3300, 0}, {3500, 2}, {3610, 5}, {3690, 10}, {3710, 15}, {3730, 20}, {3750, 25},
  {3770, 30}, {3790, 35}, {3800, 40}, {3820, 45}, {3840, 50}, {3850, 55}, {3870, 60},
  {3910, 65}, {3950, 70}, {3980, 75}, {4020, 80}, {4080, 85}, {4110, 90}, {4150, 95},
  {4200, 100},
};
constexpr unsigned kCurvePoints = sizeof(kCurve) / sizeof(kCurve[0]);

// Linear interpolation between table points, rounded to the nearest percent, 0..100.
inline uint8_t percentFromMv(uint32_t mv) {
  if (mv <= kCurve[0].mv) return 0;
  if (mv >= kCurve[kCurvePoints - 1].mv) return 100;
  unsigned i = 1;
  while (mv > kCurve[i].mv) ++i;
  const CurvePoint& a = kCurve[i - 1];
  const CurvePoint& b = kCurve[i];
  const uint32_t span = b.mv - a.mv;
  return uint8_t(a.percent + ((mv - a.mv) * (b.percent - a.percent) + span / 2) / span);
}

// Exact category boundaries on the integer percentage: 80-100, 45-79, 10-44, 0-9.
inline Category categoryForPercent(unsigned percent) {
  if (percent >= 80) return Category::Full;
  if (percent >= 45) return Category::Half;
  if (percent >= 10) return Category::Low;
  return Category::Critical;
}

// Category with hysteresis: moving up needs `margin` points past the boundary,
// moving down needs to fall `margin` points below it. The first value is exact.
class CategoryTracker {
 public:
  static const unsigned kMargin = 2;
  Category update(unsigned percent) {
    if (current == Category::Unknown) return current = categoryForPercent(percent);
    for (;;) {
      if (current != Category::Full && percent >= bound(current) + kMargin) current = Category(uint8_t(current) + 1);
      else if (current != Category::Critical && percent + kMargin < bound(Category(uint8_t(current) - 1))) current = Category(uint8_t(current) - 1);
      else return current;
    }
  }
  Category value() const { return current; }
  void reset() { current = Category::Unknown; }

 private:
  // Lowest percentage of the next category up.
  static unsigned bound(Category from) { return from == Category::Critical ? 10 : from == Category::Low ? 45 : 80; }
  Category current = Category::Unknown;
};

// Smoothed estimate. Voltage sags under load and while charging inflates it, so the
// estimate may always fall (rate limited) but may only rise when the cell has been
// unloaded and USB-free for a while. Nothing here ever enables a protective action.
class Gauge {
 public:
  static const uint32_t kSlewUs = 3000000;       // At most one percentage point per 3 s.
  static const uint32_t kRestUs = 10000000;      // Unloaded settle time before rises are allowed.
  static const uint32_t kUntrustedResetUs = 60000000;

  // `trusted` must be the Monitor's own validation result; `filteredMv` the filtered, calibrated value.
  void update(bool trusted, uint32_t filteredMv, uint64_t now, bool loaded, bool usbPresent) {
    if (loaded || usbPresent || !restStarted) {restStarted = true; disturbedAt = now;}
    if (!trusted) {
      if (valid && now - lastTrustedUs > kUntrustedResetUs) reset();
      return;
    }
    lastTrustedUs = now;
    chargeInfluenced = usbPresent;
    unsigned target = percentFromMv(filteredMv);
    if (usbPresent && target > 99) target = 99; // A charger holding 4.2 V is not proof of 100%.
    if (!valid) {
      percentValue = uint8_t(target);
      valid = true;
      stepAt = now;
      tracker.reset();
      tracker.update(percentValue);
      return;
    }
    const bool mayRise = now - disturbedAt >= kRestUs;
    if (target == percentValue || (target > percentValue && !mayRise)) {
      stepAt = now; // No banked steps: a later move still starts at one point per interval.
    } else {
      const uint32_t steps = uint32_t((now - stepAt) / kSlewUs);
      if (steps) {
        const unsigned gap = target > percentValue ? target - percentValue : percentValue - target;
        const unsigned move = steps < gap ? steps : gap;
        percentValue = uint8_t(target > percentValue ? percentValue + move : percentValue - move);
        stepAt += uint64_t(move) * kSlewUs;
        if (move == gap) stepAt = now;
      }
    }
    tracker.update(percentValue);
  }

  // Estimate and category are available only while the measurement is trusted.
  bool available(bool trusted) const { return valid && trusted; }
  unsigned percent() const { return percentValue; }
  Category category(bool trusted) const { return available(trusted) ? tracker.value() : Category::Unknown; }
  bool chargeInfluencedEstimate() const { return chargeInfluenced; }
  void reset() { valid = false; chargeInfluenced = false; tracker.reset(); restStarted = false; }

 private:
  bool valid = false, chargeInfluenced = false, restStarted = false;
  uint8_t percentValue = 0;
  uint64_t stepAt = 0, disturbedAt = 0, lastTrustedUs = 0;
  CategoryTracker tracker{};
};

}
