#pragma once
#include <cstdint>

namespace route_flights {
constexpr uint32_t duration = 850;

inline int32_t elapsed(uint32_t now, uint32_t start) {
  return static_cast<int32_t>(now - start);
}

inline bool active(uint32_t now, uint32_t start) {
  const auto age = elapsed(now, start);
  return age >= 0 && age < static_cast<int32_t>(duration);
}

inline bool finished(uint32_t now, uint32_t start) {
  return elapsed(now, start) >= static_cast<int32_t>(duration);
}

// Keep each request's legs in order, including legs queued by earlier polls.
// Other requests can fly independently. Signed deltas also handle millis wrap.
template <typename Flights, typename Request>
uint32_t nextStart(const Flights &flights, const Request &request, uint32_t now) {
  uint32_t start = now;
  for (const auto &f : flights) {
    const uint32_t end = f.start + duration;
    if (f.request == request && elapsed(end, start) > 0)
      start = end;
  }
  return start;
}
} // namespace route_flights
