#include "../src/charging_mode.h"
#include "../src/power_policy.h"
#include <cassert>
#include <iostream>

using Action = ChargingMode::BootAction;

Action press(ChargingMode &mode, uint32_t at, bool usb = true,
             bool off = false) {
  assert(mode.boot(true, at, usb, off) == Action::None);
  return mode.boot(true, at + ChargingMode::kDebounceMs, usb, off);
}
Action release(ChargingMode &mode, uint32_t at, bool usb = true) {
  assert(mode.boot(false, at, usb, false) == Action::None);
  return mode.boot(false, at + ChargingMode::kDebounceMs, usb, false);
}

int main() {
  ChargingMode mode;
  assert(press(mode, 100) == Action::None);
  assert(release(mode, 300) == Action::ShortPress);
  assert(!mode.active);

  assert(press(mode, 1000) == Action::None);
  assert(mode.boot(true, 4029, true, false) == Action::None);
  assert(mode.boot(true, 4030, true, false) == Action::Enter);
  assert(mode.active);
  assert(mode.boot(true, 8000, true, false) == Action::None);
  assert(release(mode, 8100) == Action::None); // Entry release stays in mode.
  assert(mode.active);
  assert(press(mode, 8500) == Action::Exit);
  assert(!mode.active);
  assert(mode.boot(true, 12000, true, false) == Action::None);
  assert(release(mode, 12100) == Action::None); // Exit press cannot select page 1.

  ChargingMode unplugged;
  press(unplugged, 100);
  assert(unplugged.boot(true, 3130, true, false) == Action::Enter);
  assert(unplugged.exit());
  assert(!unplugged.active && !unplugged.exit());
  assert(unplugged.boot(true, 7000, false, false) == Action::None);
  assert(release(unplugged, 7100, false) == Action::None);

  ChargingMode battery;
  press(battery, 100, false);
  assert(battery.boot(true, 8000, false, false) == Action::None);
  assert(!battery.active);
  assert(release(battery, 8100, false) == Action::ShortPress);

  ChargingMode sleeping;
  assert(press(sleeping, 100, true, true) == Action::Wake);
  assert(release(sleeping, 300) == Action::None);
  assert(press(sleeping, 1000, true, true) == Action::Wake);
  assert(sleeping.boot(true, 4030, true, false) == Action::Enter);

  ChargingMode bounce;
  bounce.boot(true, 100, true, false);
  bounce.boot(false, 110, true, false);
  assert(bounce.boot(false, 200, true, false) == Action::None);
  assert(!bounce.active);

  ChargingMode rollover;
  uint32_t start = UINT32_MAX - 1000;
  press(rollover, start);
  assert(rollover.boot(true, start + 3030, true, false) == Action::Enter);

  power::Settings settings;
  settings.usbBrightness = 255;
  settings.usbSeconds = 30;
  settings.alwaysOn = true;
  auto decision = power::evaluate(settings, true, false, 29999, true);
  assert(!decision.off && !decision.dim && decision.brightness == 51);
  decision = power::evaluate(settings, true, false, 30000, true);
  assert(decision.off && !decision.dim);
  assert(power::evaluate(settings, true, false, 3600000, true).off);
  decision = power::evaluate(settings, true, false, 0, false);
  assert(!decision.off && decision.brightness == 255);
  decision = power::evaluate(settings, false, false, 0, true);
  assert(!decision.off && decision.brightness == settings.batteryBrightness);
  assert(power::evaluate(settings, false, false, 3600000, true).off);
  std::cout << "Charging mode: entry/exit, held keys, USB, wake, debounce, "
               "rollover and brightness passed\n";
}
