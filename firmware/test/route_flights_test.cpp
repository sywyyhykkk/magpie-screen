#include "../src/route_flights.h"
#include <cassert>
#include <string>
#include <vector>

struct Flight {
  std::string request;
  uint32_t start;
};

int main() {
  using namespace route_flights;
  std::vector<Flight> flights;
  auto enqueue = [&](const std::string &request, uint32_t now) {
    auto start = nextStart(flights, request, now);
    flights.push_back({request, start});
    return start;
  };
  // A complete request arriving in one poll: left -> middle -> right,
  // then right -> middle -> left. Never show two legs of it together.
  for (uint32_t leg = 0; leg < 4; ++leg)
    assert(enqueue("1:100", 1000) == 1000 + leg * duration);
  for (uint32_t now = 1000; now <= 1000 + 4 * duration; ++now) {
    int visible = 0;
    for (const auto &f : flights)
      visible += active(now, f.start);
    assert(visible == (now < 1000 + 4 * duration ? 1 : 0));
  }
  // A later poll must append to the same request, not restart its queue.
  assert(enqueue("1:100", 1100) == 1000 + 4 * duration);
  assert(!finished(1100, flights.back().start));
  // Request IDs and upstream process IDs each define independent flights.
  assert(enqueue("1:101", 1100) == 1100);
  assert(enqueue("2:100", 1100) == 1100);
  assert(enqueue("1:100", 10000) == 10000);
  assert(finished(10000, 1000));
  // Queued starts spanning the uint32_t millis rollover still wait their turn.
  flights.clear();
  constexpr uint32_t now = UINT32_MAX - 400;
  assert(enqueue("1:100", now) == now);
  const uint32_t next = now + duration;
  assert(enqueue("1:100", now) == next);
  assert(!active(now, next));
  assert(!finished(now, next));
  assert(finished(next, now));
  assert(active(next, next));
  assert(enqueue("1:100", next) == next + duration);
}
