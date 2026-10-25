#pragma once
#include <cstdint>

struct ChargingMode {
  enum class BootAction { None, Wake, ShortPress, Enter, Exit };
  static constexpr uint32_t kHoldMs = 3000, kDebounceMs = 30;
  bool active = false;

  BootAction boot(bool pressed, uint32_t now, bool usb, bool screenOff) {
    if (pressed != rawDown) {
      rawDown = pressed;
      changedAt = now;
    }
    if (pressed != down && now - changedAt >= kDebounceMs) {
      down = pressed;
      if (down) {
        pressedAt = now;
        consumed = active;
        wakeOnly = screenOff;
        if (exit()) return BootAction::Exit;
        if (screenOff) return BootAction::Wake;
      } else if (!consumed && !wakeOnly) {
        return BootAction::ShortPress;
      }
    }
    if (pressed && down && !consumed && usb && now - pressedAt >= kHoldMs) {
      consumed = active = true;
      return BootAction::Enter;
    }
    return BootAction::None;
  }

  bool exit() {
    if (!active) return false;
    active = false;
    if (down) consumed = true;
    return true;
  }

private:
  bool rawDown = false, down = false, consumed = false, wakeOnly = false;
  uint32_t changedAt = 0, pressedAt = 0;
};
