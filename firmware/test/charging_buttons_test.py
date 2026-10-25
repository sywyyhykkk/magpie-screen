"""Exercise actual button, USB event and power dispatch with host PMU stubs."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'firmware/src/main.cpp').read_text()
buttons = source.split('void io() {', 1)[1]
buttons = buttons[buttons.index('  uint32_t now = millis();'):]
buttons = buttons[:buttons.index('  if (wifiPending && !networkBusy) {')]
activity = source[source.index('void userActivity(bool chargingInteraction) {'):]
activity = activity[:activity.index('void startSetup() {')]
data_activity = source[source.index('        if (changed && power::dataWakes'):]
data_activity = data_activity[:data_activity.index('        if (i == 2 && !screenOff)')]
power = source[source.index('void updateUsbPower(bool usb) {'):]
power = power[:power.index('void networkIO() {')]

prefix = r'''
#include "charging_mode.h"
#include "power_policy.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
constexpr int LOW = 0;
constexpr int XPOWERS_AXP2101_CHG_DONE_STATE = 4;
ChargingMode chargingMode;
bool prevKey = false, keyLongHandled = false, usbPower = true;
bool screenOff = false, setupMode = false, dirty = false, pmuOK = true;
bool bootDown = false, keyDown = false, touchDown = false, touchOK = true;
uint32_t keyTime = 0, lastPmu = 0, clockNow = 0;
uint32_t lastActivity = 0, lastPowerRead = 0, lowSince = 0, due[3] = {};
uint32_t pollInterval = 0, otaNoticeUntil = 0, previewUntil = 0, noticeUntil = 0;
bool batteryPresent = true, charging = false, chargeDone = false;
bool otaActive = false, requestedOff = false, dimmed = false;
int batteryPercent = 80, batteryMv = 3800, requestedBrightness = 179;
int previewBrightness = -1;
std::string previewSource;
std::vector<int> flights;
struct Config { power::Settings power; } config;
int screen = 2, selections = 0, setupCalls = 0;
uint32_t millis() { return clockNow; }
int digitalRead(int pin) {
  return !(pin == 0 ? bootDown : pin == 18 ? keyDown : touchDown);
}
struct Touch {
  int getPoint(int16_t *, int16_t *, int) { return touchDown ? 1 : 0; }
} touch;
float target = 0, fill = 0;
struct Fluid { void reset(float) {} } fluid;
struct Value {
  Value operator[](const char *) { return {}; }
  bool isNull() { return false; }
  template<class T> T as() { return T(80); }
} data;
void userActivity(bool chargingInteraction = false);
void selectScreen(int page) { screen = page; ++selections; userActivity(); }
void startSetup() { setupMode = true; ++setupCalls; }
struct Pmu {
  bool shortPress = false, insert = false, remove = false, vbus = true;
  int batteryReads = 0;
  void getIrqStatus() {}
  bool isPekeyShortPressIrq() { return shortPress; }
  bool isVbusInsertIrq() { return insert; }
  bool isVbusRemoveIrq() { return remove; }
  bool isVbusIn() { return vbus; }
  bool isBatteryConnect() { return true; }
  bool isCharging() { return vbus; }
  int getBatteryPercent() { ++batteryReads; return 80; }
  int getBattVoltage() { return 3800; }
  int getChargerStatus() { return 0; }
  void shutdown() { assert(false); }
  void clearIrqStatus() { shortPress = insert = remove = false; }
} pmu;
'''
suffix = r'''
}
void tick(uint32_t at, bool boot = false, bool key = false, bool power = false,
          bool tap = false) {
  clockNow = at; bootDown = boot; keyDown = key; pmu.shortPress = power;
  touchDown = tap;
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

  // USB removal must exit in the 50 ms event pass, without a battery read.
  time += 10000;
  screen = 3; chargingMode.active = true; dirty = false;
  requestedBrightness = 51;
  due[0] = due[1] = due[2] = 1234;
  lastPmu = lastPowerRead = time;
  pmu.vbus = false; pmu.remove = true;
  tick(time+49);
  assert(chargingMode.active && usbPower && pmu.remove);
  tick(time+50); powerIO();
  assert(!chargingMode.active && !usbPower && screen == 3 && dirty);
  assert(requestedBrightness == config.power.batteryBrightness);
  assert(lastActivity == time+50 && pmu.batteryReads == 0);
  assert(due[0] == 0 && due[1] == 0 && due[2] == 0 && !pmu.remove);

  // Insertion updates power and brightness without entering charging mode.
  pmu.vbus = true; pmu.insert = true;
  tick(time+100); powerIO();
  assert(usbPower && !chargingMode.active && screen == 3);
  assert(requestedBrightness == config.power.usbBrightness);
  assert(pmu.batteryReads == 0 && !pmu.insert);

  // Both latched flags use actual VBUS state after a quick unplug/replug.
  chargingMode.active = true;
  pmu.insert = pmu.remove = true;
  tick(time+150); powerIO();
  assert(usbPower && chargingMode.active && requestedBrightness == 51);
  assert(!pmu.insert && !pmu.remove && pmu.batteryReads == 0);
  pmu.vbus = false; pmu.insert = pmu.remove = true;
  tick(time+200); powerIO();
  assert(!usbPower && !chargingMode.active);
  assert(requestedBrightness == config.power.batteryBrightness);

  // Battery telemetry keeps its one-second cadence and reconciles VBUS.
  pmu.vbus = true;
  tick(time+999); powerIO();
  assert(!usbPower && pmu.batteryReads == 0);
  tick(time+1000); powerIO();
  assert(usbPower && pmu.batteryReads == 1 && batteryPercent == 80);
  tick(time+1050); powerIO();
  assert(pmu.batteryReads == 1);

  // Entry and touch get 30 seconds at 20%, regardless of USB always-on.
  time += 20000;
  config.power.alwaysOn = true;
  config.power.wakeOnData = true;
  tick(time, true); tick(time+30, true); tick(time+3030, true); powerIO();
  assert(chargingMode.active && !screenOff && requestedBrightness == 51);
  assert(lastActivity == time+3030);
  tick(time+3100); tick(time+3130);
  tick(time+33029); powerIO();
  assert(!screenOff);
  tick(time+33030); powerIO();
  assert(chargingMode.active && screenOff && requestedOff);
  // Motion and incoming data use default activity, which must leave it asleep.
  userActivity(); dataActivity(true); powerIO();
  assert(screenOff && requestedOff && lastActivity == time+3030);
  tick(time+33100, false, false, false, true); powerIO();
  assert(chargingMode.active && !screenOff && !requestedOff && dirty);
  assert(lastActivity == time+33100 && requestedBrightness == 51);
  // Touch while already lit also restarts the timeout.
  tick(time+34000, false, false, false, true); powerIO();
  config.power.wakeOnData = false;
  tick(time+63999); userActivity(); dataActivity(true); powerIO();
  assert(!screenOff && lastActivity == time+34000);
  tick(time+64000); powerIO();
  assert(chargingMode.active && screenOff);

  // Any button still exits directly to the remembered page while asleep.
  time += 70000;
  for (int exitKey : {0, 1, 2}) {
    chargingMode.active = true;
    lastActivity = time-30000; clockNow = time; screen = 3;
    powerIO(); assert(screenOff);
    if (exitKey == 0) {
      tick(time+100, true); tick(time+130, true);
      tick(time+200); tick(time+230);
    } else if (exitKey == 1) {
      tick(time+100, false, true); tick(time+200);
    } else tick(time+100, false, false, true);
    powerIO();
    assert(!chargingMode.active && !screenOff && screen == 3);
    assert(requestedBrightness == config.power.usbBrightness);
    time += 10000;
  }
  // USB removal also wakes the normal page from charging sleep.
  chargingMode.active = true;
  lastActivity = time-30000; clockNow = time;
  powerIO(); assert(screenOff);
  pmu.vbus = false; pmu.remove = true;
  tick(time+100); powerIO();
  assert(!chargingMode.active && !screenOff && !requestedOff && screen == 3);
  assert(requestedBrightness == config.power.batteryBrightness);
  std::cout << "Charging button dispatch: all three exit keys preserve all "
               "three pages; short/long actions, USB events, brightness and "
               "battery cadence; 30s sleep, touch wake and asleep exits passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'buttons.cpp').write_text(
        prefix + activity + power + '\nvoid dataActivity(bool changed) {\n' +
        data_activity + '}\nvoid buttons() {\n' + buttons + suffix)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined']
    if os.environ.get('SDKROOT'):
        command += ['-isysroot', os.environ['SDKROOT']]
    command += [str(path / 'buttons.cpp'), '-I' + str(root / 'firmware/src'),
                '-o', str(path / 'buttons')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'buttons')], check=True)
