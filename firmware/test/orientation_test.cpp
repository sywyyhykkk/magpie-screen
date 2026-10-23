#include "../src/orientation.h"
#include <cassert>
#include <cmath>

int main() {
  OrientationTransition t;
  assert(!t.active(0));
  t.turnTo(1, 100);
  assert(t.active(100));
  assert(std::fabs(t.sample(100)) < .002f);
  assert(t.sample(200) > 0 && t.sample(200) < t.to);
  assert(!t.active(520));
  assert(t.sample(520) == t.to);
  t.snap(0);
  t.turnTo(3, 1000);
  assert(t.to < 0); // 0 -> 270 takes the short counterclockwise path.
  float mid = t.sample(1150);
  t.turnTo(1, 1150);
  assert(std::fabs(t.sample(1150) - mid) < .002f);
  t.snap(2);
  assert(!t.active(1200));
  t.turnTo(3, UINT32_MAX - 100);
  assert(t.active(50)); // millis() rollover during the animation.
  assert(!t.active(400));
}
