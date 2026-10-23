#pragma once
#include <cmath>
#include <cstdint>

// Match the preview's 420 ms cubic-bezier(.24, 1.35, .38, 1).
struct OrientationTransition {
  static constexpr float pi = 3.14159265358979323846f;
  float from = 0, to = 0;
  uint32_t started = 0;
  bool wasActive = false;
  bool active(uint32_t now) const { return from != to && now - started < 420; }
  float sample(uint32_t now) const {
    if (!active(now)) return to;
    float progress = (now - started) / 420.f;
    float low = 0, high = 1;
    for (int i = 0; i < 12; ++i) {
      float t = (low + high) * .5f, u = 1 - t;
      float x = 3 * u * u * t * .24f + 3 * u * t * t * .38f + t * t * t;
      if (x < progress) low = t; else high = t;
    }
    float t = (low + high) * .5f, u = 1 - t;
    float eased = 3 * u * u * t * 1.35f + 3 * u * t * t + t * t * t;
    return from + (to - from) * eased;
  }
  void turnTo(int quarter, uint32_t now) {
    float current = sample(now);
    from = current;
    to = current + std::remainder(quarter * pi / 2 - current, 2 * pi);
    started = now;
  }
  void snap(int quarter) { from = to = quarter * pi / 2; wasActive = false; }
};
