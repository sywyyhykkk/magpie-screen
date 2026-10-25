#pragma once
#include <algorithm>
#include <cstdint>
namespace power {
struct Settings {
  int usbBrightness = 179, batteryBrightness = 102;
  uint32_t usbSeconds = 300, batterySeconds = 120;
  bool alwaysOn = false, wakeOnData = true;
};
struct Decision {
  bool off, dim;
  int brightness;
  uint32_t pollMs;
};
inline Decision evaluate(const Settings &s, bool usb, bool low, uint32_t idle,
                         bool chargingMode = false) {
  if (chargingMode && usb)
    return {false, false, 51, 10000u}; // 20% of the 0–255 brightness range.
  uint32_t timeout = (usb ? s.usbSeconds : s.batterySeconds) * 1000;
  bool off = !(usb && s.alwaysOn) && idle >= timeout;
  uint32_t dimAt = usb ? (timeout > 30000 ? timeout - 30000 : timeout / 2)
                       : std::min(uint32_t(30000), timeout / 2);
  bool dim = !off && !(usb && s.alwaysOn) && idle >= dimAt;
  int b = usb ? s.usbBrightness : s.batteryBrightness;
  if (dim || (!usb && low))
    b = std::min(b, 38);
  return {off, dim, b, usb ? 10000u : off ? 60000u : 30000u};
}
inline bool dataWakes(const Settings &s, bool usb) {
  return usb && s.wakeOnData;
}
} // namespace power
