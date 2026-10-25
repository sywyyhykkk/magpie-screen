"""Exercise the firmware's actual button dispatch with host GPIO/PMU stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'firmware/src/main.cpp').read_text()
buttons = source.split('void io() {', 1)[1]
buttons = buttons[buttons.index('  uint32_t now = millis();'):]
buttons = buttons[:buttons.index('  static uint32_t lastTouch = 0;')]

prefix = r'''
#include "charging_mode.h"
#include <cassert>
#include <iostream>
constexpr int LOW = 0;
ChargingMode chargingMode;
bool prevKey = false, keyLongHandled = false, usbPower = true;
bool screenOff = false, setupMode = false, dirty = false, pmuOK = true;
bool bootDown = false, keyDown = false;
uint32_t keyTime = 0, lastPmu = 0, clockNow = 0;
int screen = 2, selections = 0, setupCalls = 0;
uint32_t millis() { return clockNow; }
int digitalRead(int pin) { return !(pin == 0 ? bootDown : keyDown); }
void userActivity() { screenOff = false; }
void selectScreen(int page) { screen = page; ++selections; userActivity(); }
void startSetup() { setupMode = true; ++setupCalls; }
struct Pmu {
  bool shortPress = false;
  void getIrqStatus() {}
  bool isPekeyShortPressIrq() { return shortPress; }
  void clearIrqStatus() { shortPress = false; }
} pmu;
void buttons() {
'''
suffix = r'''
}
void tick(uint32_t at, bool boot = false, bool key = false, bool power = false) {
  clockNow = at; bootDown = boot; keyDown = key; pmu.shortPress = power;
  buttons();
}
int main() {
  uint32_t time = 10000;
  for (int page : {1, 2, 3}) for (int exitKey : {0, 1, 2}) {
    screen = page; selections = setupCalls = 0;
    tick(time, true); tick(time+30, true);
    tick(time+3030, true);
    assert(chargingMode.active && screen == page && selections == 0);
    tick(time+3100); tick(time+3130);
    assert(chargingMode.active); // Releasing entry hold does not exit.
    if (exitKey == 0) {
      tick(time+4000, true); tick(time+4030, true);
      tick(time+8000, true); tick(time+8100); tick(time+8130);
    } else if (exitKey == 1) {
      tick(time+4000, false, true);
      tick(time+8000, false, true); // No setup on a held exit press.
      tick(time+8100);
    } else tick(time+4000, false, false, true);
    assert(!chargingMode.active && screen == page);
    assert(selections == 0 && setupCalls == 0);
    time += 10000;
  }
  tick(time, true); tick(time+30, true);
  tick(time+200); tick(time+230);
  assert(screen == 1 && selections == 1); // Normal short press still selects.
  tick(time+1000, false, true); tick(time+4000, false, true);
  assert(screen == 3 && setupCalls == 1); // Existing GPIO18 hold preserved.
  tick(time+4100);
  tick(time+5000, false, false, true);
  assert(screen == 2);
  std::cout << "Charging button dispatch: all three exit keys preserve all "
               "three pages; normal short/long actions passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'buttons.cpp').write_text(prefix + buttons + suffix)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined']
    if os.environ.get('SDKROOT'):
        command += ['-isysroot', os.environ['SDKROOT']]
    command += [str(path / 'buttons.cpp'), '-I' + str(root / 'firmware/src'),
                '-o', str(path / 'buttons')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'buttons')], check=True)
