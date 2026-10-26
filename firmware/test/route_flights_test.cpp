#include "../src/route_flights.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

struct Flight {
  std::string request;
  uint32_t start;
  std::string kind, target;
};

int main() {
  using namespace route_flights;
  std::vector<Flight> flights;
  auto enqueue = [&](const std::string &request, uint32_t now,
                     const std::string &kind = "attempt",
                     const std::string &target = "seat") {
    auto start = nextStart(flights, request, kind, target, now);
    flights.push_back({request, start, kind, target});
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
  assert(enqueue("1:101", 1100, "attempt", "other") == 1100);
  assert(enqueue("2:100", 1100, "attempt", "third") == 1100);
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

  // Both directions disappear at the hub for 500ms before the next leg starts.
  for (const std::string arrival : {"request", "response"}) {
    flights.clear();
    const uint32_t start = UINT32_MAX - 400;
    assert(enqueue("1:100", start, arrival) == start);
    const uint32_t departure = start + duration + hubPause;
    assert(enqueue("1:100", start, arrival == "request" ? "attempt" : "result") == departure);
    assert(active(start + duration - 1, start));
    assert(hubPause == 500);
    for (uint32_t ms = duration; ms < duration + hubPause; ++ms) {
      const uint32_t now = start + ms;
      assert(!active(now, start));
      assert(!finished(now, start, lifetime(arrival)));
      assert(progress(now, start) == 1.f);
      assert(!active(now, departure));
    }
    assert(finished(departure, start, lifetime(arrival)));
    assert(active(departure, departure));
    assert(progress(departure, departure) == 0.f);
    assert(enqueue("1:101", start + duration) == start + duration);
  }
  // A departure received during the hold still waits for its remaining time.
  flights.clear();
  enqueue("1:100", 1000, "request");
  assert(enqueue("1:100", 1000 + duration + 50) == 1000 + duration + hubPause);

  // Batch arrivals on one line must be distinguishable and still concurrent.
  flights.clear();
  for(int i=0;i<4;i++)
    assert(enqueue(std::to_string(i),1000,"request","") == 1000+i*launchGap);
  int visible=0;
  for(const auto &f:flights) visible+=active(1000+3*launchGap,f.start);
  assert(visible==4);
  // Separate destinations and opposite directions do not share launch slots.
  assert(enqueue("a",1000,"attempt","upper") == 1000);
  assert(enqueue("b",1000,"attempt","lower") == 1000);
  assert(enqueue("c",1000,"response","upper") == 1000);
  assert(enqueue("d",1000,"attempt","upper") == 1000+launchGap);

  // Full four-leg batches retain all requests and preserve each leg's order.
  flights.clear();
  for(int i=0;i<8;i++) {
    auto request=std::to_string(i), target=std::to_string(i%4);
    const auto first=enqueue(request,1000,"request","");
    const auto out=enqueue(request,1000,"attempt",target);
    const auto back=enqueue(request,1000,"response",target);
    const auto last=enqueue(request,1000,"result","");
    assert(out>=first+duration+hubPause);
    assert(back>=out+duration);
    assert(last>=back+duration+hubPause);
  }
  assert(flights.size()==32 && flights.size()<=capacity);
  // Launch spacing also survives millis rollover.
  flights.clear();
  enqueue("a",UINT32_MAX-100,"request","");
  const auto wrapped=enqueue("b",UINT32_MAX-100,"request","");
  assert(wrapped==uint32_t(UINT32_MAX-100+launchGap));
  assert(active(wrapped,flights[0].start) && active(wrapped,flights[1].start));

  // Every frame follows the actual wire, for all layouts and both directions.
  for(int count=1;count<=4;count++) for(int row=0;row<count;row++) {
    const int y=rowY(row,count);
    for(bool returning:{false,true}) for(int ms=0;ms<=int(duration);ms++) {
      const float t=progress(1000+ms,1000);
      const auto left=position(t,false,y,returning);
      assert(left.y==240.f && left.x>=84.f && left.x<=146.f);
      const auto right=position(t,true,y,returning);
      assert(std::abs((right.x-222.f)*(y-240.f)-(right.y-240.f)*48.f)<.01f);
      if(ms==0 || ms==int(duration)) {
        const bool atEnd=returning ? ms==0 : ms==int(duration);
        assert(right.x==(atEnd?270.f:222.f));
        assert(right.y==(atEnd?float(y):240.f));
      }
    }
  }
}
