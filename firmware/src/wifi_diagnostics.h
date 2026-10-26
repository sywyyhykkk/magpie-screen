#pragma once
#include <cstdint>

// Updated on the main task only; Wi-Fi callbacks deliver numeric events.
struct WifiDiagnostics {
  enum class Phase { Unconfigured, Connecting, Connected, Failed };
  static constexpr uint32_t timeoutMs = 30000;
  Phase phase = Phase::Unconfigured;
  uint32_t startedAt = 0;
  int reason = 0;
  bool associated = false, timedOut = false;

  void begin(uint32_t now) {
    phase = Phase::Connecting;
    startedAt = now;
    reason = 0;
    associated = timedOut = false;
  }
  void connected() {
    phase = Phase::Connected;
    reason = 0;
    associated = true;
    timedOut = false;
  }
  void disconnected(int code) {
    phase = Phase::Failed;
    reason = code;
    associated = timedOut = false;
  }
  bool poll(uint32_t now, bool online) {
    if (online) {
      connected();
    } else if (phase == Phase::Connected) {
      begin(now); // Lost IP: allow DHCP a bounded time to recover.
    } else if (phase == Phase::Connecting && now - startedAt >= timeoutMs) {
      phase = Phase::Failed;
      timedOut = true;
      return true;
    }
    return false;
  }
  const char *state() const {
    switch (phase) {
    case Phase::Connecting: return "connecting";
    case Phase::Connected: return "connected";
    case Phase::Failed: return "failed";
    default: return "not configured";
    }
  }
};
