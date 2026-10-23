#include "../src/power_policy.h"
#include <cassert>
#include <iostream>
int main() {
  power::Settings s;
  auto usb = power::evaluate(s, true, false, 0);
  assert(!usb.off && !usb.dim && usb.brightness == 179 && usb.pollMs == 10000);
  assert(power::evaluate(s, true, false, 270000).dim);
  assert(power::evaluate(s, true, false, 300000).off);
  assert(power::evaluate(s, false, false, 30000).dim);
  assert(power::evaluate(s, false, false, 120000).off);
  assert(power::evaluate(s, false, false, 120000).pollMs == 60000);
  assert(power::evaluate(s, false, false, 0).pollMs == 30000);
  s.alwaysOn = true;
  assert(!power::evaluate(s, true, false, 3600000).off);
  assert(!power::evaluate(s, true, false, 3600000).dim);
  assert(power::evaluate(s, false, false, 3600000).off);
  assert(power::evaluate(s, false, true, 0).brightness <= 38);
  assert(power::dataWakes(s, true));
  assert(!power::dataWakes(s, false));
  s.wakeOnData = false;
  assert(!power::dataWakes(s, true));
  s.usbSeconds = 30;
  s.alwaysOn = false;
  assert(power::evaluate(s, true, false, 15000).dim);
  // Unsigned elapsed time still works across millis() rollover.
  uint32_t last = UINT32_MAX - 10000, now = 250000;
  assert(power::evaluate(s, true, false, now - last).off);
  std::cout << "Power policy: USB/battery, dim/off, always-on, data wake, low "
               "battery and rollover passed\n";
}
