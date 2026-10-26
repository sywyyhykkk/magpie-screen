#include "../src/wifi_diagnostics.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  WifiDiagnostics wifi;
  assert(std::strcmp(wifi.state(), "not configured") == 0);
  wifi.begin(100);
  assert(!wifi.poll(30099, false));
  assert(std::strcmp(wifi.state(), "connecting") == 0);
  assert(wifi.poll(30100, false));
  assert(wifi.timedOut && wifi.reason == 0);
  assert(std::strcmp(wifi.state(), "failed") == 0);
  assert(!wifi.poll(60000, false)); // Log timeout once; stay failed.

  wifi.begin(70000);
  wifi.associated = true;
  assert(wifi.poll(100000, false));
  assert(wifi.associated && wifi.timedOut); // DHCP timeout, not AP discovery.
  wifi.disconnected(201);
  assert(wifi.reason == 201 && !wifi.timedOut && !wifi.associated);
  wifi.associated = true; // An automatic retry must not hide the last failure.
  assert(!wifi.poll(200000, false));
  assert(std::strcmp(wifi.state(), "failed") == 0 && wifi.reason == 201);
  wifi.poll(200001, true); // A successful automatic retry clears the failure.
  assert(std::strcmp(wifi.state(), "connected") == 0);
  assert(wifi.reason == 0 && !wifi.timedOut);

  wifi.poll(200002, false); // IP loss has its own bounded recovery period.
  assert(std::strcmp(wifi.state(), "connecting") == 0);
  assert(wifi.poll(230002, false));
  wifi.begin(240000); // Explicit retry clears the old failure.
  assert(wifi.reason == 0 && !wifi.timedOut && !wifi.associated);

  wifi.begin(UINT32_MAX - 10000);
  assert(!wifi.poll(19998, false));
  assert(wifi.poll(19999, false)); // Correct through millis() rollover.
  std::cout << "PASS Wi-Fi failure, timeout, retry, recovery and rollover\n";
}
