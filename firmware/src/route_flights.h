#pragma once
#include <cstdint>

namespace route_flights {
constexpr uint32_t duration = 850;
constexpr uint32_t hubPause = 500;

template <typename Kind> uint32_t lifetime(const Kind &kind) {
  return duration + ((kind == "request" || kind == "response") ? hubPause : 0);
}

inline int32_t elapsed(uint32_t now, uint32_t start) {
  return static_cast<int32_t>(now - start);
}

inline bool active(uint32_t now, uint32_t start, uint32_t span = duration) {
  const auto age = elapsed(now, start);
  return age >= 0 && age < static_cast<int32_t>(span);
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
template <typename Flights, typename Request>
uint32_t nextStart(const Flights &flights, const Request &request, uint32_t now) {
  uint32_t start = now;
  for (const auto &f : flights) {
    const uint32_t end = f.start + lifetime(f.kind);
    if (f.request == request && elapsed(end, start) > 0)
      start = end;
  }
  return start;
}
} // namespace route_flights
