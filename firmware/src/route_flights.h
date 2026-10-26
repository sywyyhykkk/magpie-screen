#pragma once
#include <cstddef>
#include <cstdint>

namespace route_flights {
constexpr uint32_t duration = 850;
constexpr uint32_t hubPause = 500;
constexpr uint32_t launchGap = 240;
constexpr size_t capacity = 64;

inline int rowY(int index, int count) {
  return count <= 1 ? 240 : count == 2 ? 176 + index * 128
                          : count == 3 ? 132 + index * 108 : 100 + index * 92;
}

struct Point { float x, y; };
inline Point position(float t, bool destination, int targetY, bool returning) {
  if (returning) t = 1.f - t;
  const float x = destination ? 222.f : 84.f;
  const float endX = destination ? 270.f : 146.f;
  const float endY = destination ? float(targetY) : 240.f;
  return {x + (endX - x) * t, 240.f + (endY - 240.f) * t};
}

template <typename Kind> uint32_t lifetime(const Kind &kind) {
  return duration + ((kind == "request" || kind == "response") ? hubPause : 0);
}

inline int32_t elapsed(uint32_t now, uint32_t start) {
  return static_cast<int32_t>(now - start);
}

// The bird disappears on arrival; the queue retains the hidden hub pause.
inline bool active(uint32_t now, uint32_t start) {
  const auto age = elapsed(now, start);
  return age >= 0 && age < static_cast<int32_t>(duration);
}

inline bool finished(uint32_t now, uint32_t start, uint32_t span = duration) {
  return elapsed(now, start) >= static_cast<int32_t>(span);
}

inline float progress(uint32_t now, uint32_t start) {
  const auto age = elapsed(now, start);
  return age <= 0 ? 0.f : age >= static_cast<int32_t>(duration)
                             ? 1.f : age / float(duration);
}

// Keep each request's legs in order, including legs queued by earlier polls.
// Other requests can fly independently. Signed deltas also handle millis wrap.
template <typename Flights, typename Request, typename Kind, typename Target>
uint32_t nextStart(const Flights &flights, const Request &request,
                   const Kind &kind, const Target &target, uint32_t now) {
  uint32_t start = now;
  for (const auto &f : flights) {
    const uint32_t end = f.start + lifetime(f.kind);
    if (f.request == request && elapsed(end, start) > 0)
      start = end;
  }
  // Polling batches often give independent birds identical starts. Space birds
  // on the same directed line so they remain visible, while allowing overlap
  // in flight time and immediate departures on different lines.
  bool collision;
  do {
    collision = false;
    for (const auto &f : flights) {
      if (f.kind != kind || f.target != target) continue;
      const auto delta = elapsed(start, f.start);
      if (delta > -static_cast<int32_t>(launchGap) &&
          delta < static_cast<int32_t>(launchGap)) {
        start = f.start + launchGap;
        collision = true;
      }
    }
  } while (collision);
  return start;
}
} // namespace route_flights
