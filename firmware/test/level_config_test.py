"""Check calibration validation and NVS JSON round trips using the real parser."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
arduino = r'''
#pragma once
#include <string>
#include <cctype>
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  bool startsWith(const char *s) const { return rfind(s, 0) == 0; }
  bool endsWith(const char *s) const {
    std::string suffix(s);
    return size() >= suffix.size() && compare(size()-suffix.size(), suffix.size(), suffix) == 0;
  }
  void remove(size_t pos) { erase(pos); }
  int indexOf(char c) const { auto p = find(c); return p == npos ? -1 : int(p); }
  String substring(size_t pos) const { return substr(pos); }
  String substring(size_t a, size_t b) const { return substr(a, b-a); }
  long toInt() const { return std::strtol(c_str(), nullptr, 10); }
};
#include <ArduinoJson.h>
namespace ArduinoJson { template<> struct Converter<String> {
  static String fromJson(JsonVariantConst v) { return v.as<std::string>(); }
  static void toJson(const String &s, JsonVariant v) { v.set(std::string(s)); }
}; }
'''
code = r'''
#include "standalone_config.h"
#include <cassert>
int main() {
  String error;
  DeviceConfig defaults;
  DynamicJsonDocument input(4096), saved(4096), loaded(4096);
  deserializeJson(input, R"({"provider":"codex","usbBrightness":179})");
  assert(parseConfig(input.as<JsonObject>(), defaults, error));
  assert(defaults.levelOffsetDegrees == 0); // Older saved settings still load.
  assert(defaults.accelBiasXG == 0 && defaults.accelBiasYG == 0);
  // Synthetic signed six-decimal inputs exercise serialization precision.
  constexpr float kSyntheticBiasXG = .012345f, kSyntheticBiasYG = -.023456f;
  input.clear();
  input["accelBiasXG"] = kSyntheticBiasXG;
  input["accelBiasYG"] = kSyntheticBiasYG;
  DeviceConfig axis;
  assert(parseConfig(input.as<JsonObject>(), axis, error));
  saved.clear(); configJSON(saved.to<JsonObject>(), axis, true);
  std::string axisRaw; serializeJson(saved, axisRaw);
  assert(!deserializeJson(loaded, axisRaw));
  DeviceConfig axisRebooted;
  assert(parseConfig(loaded.as<JsonObject>(), axisRebooted, error));
  assert(fabsf(axisRebooted.accelBiasXG - kSyntheticBiasXG) < .000001f);
  assert(fabsf(axisRebooted.accelBiasYG - kSyntheticBiasYG) < .000001f);
  deserializeJson(input, R"({"levelOffsetDegrees":0})");
  assert(parseConfig(input.as<JsonObject>(), axisRebooted, error));
  assert(axisRebooted.accelBiasXG == axis.accelBiasXG && axisRebooted.accelBiasYG == axis.accelBiasYG);
  for (const char *field : {"accelBiasXG", "accelBiasYG"}) {
    for (const char *bad : {"null", "true", "\"0.1\"", "[]", "{}", "-0.500001", "0.500001", "1e100"}) {
      assert(!deserializeJson(input, std::string("{\"") + field + "\":" + bad + "}"));
      DeviceConfig next;
      assert(!parseConfig(input.as<JsonObject>(), next, error));
    }
    for (float good : {-.5f, 0.f, .5f}) {
      input.clear(); input[field] = good;
      DeviceConfig next;
      assert(parseConfig(input.as<JsonObject>(), next, error));
    }
  }
  for (int i = 0; i < 4; ++i) assert(defaults.levelOffset(i) == 0);
  for (float offset : {-180.f, -8.3f, 0.f, 12.5f, 180.f}) {
    input.clear(); input["levelOffsetDegrees"] = offset;
    DeviceConfig next = defaults;
    assert(parseConfig(input.as<JsonObject>(), next, error));
    assert(next.levelOffsetDegrees == offset);
    for (int i = 0; i < 4; ++i) assert(next.levelOffset(i) == offset);
    saved.clear(); configJSON(saved.to<JsonObject>(), next, true);
    std::string raw; serializeJson(saved, raw);
    assert(!deserializeJson(loaded, raw));
    DeviceConfig rebooted;
    assert(parseConfig(loaded.as<JsonObject>(), rebooted, error));
    assert(fabsf(rebooted.levelOffsetDegrees - offset) < .0001f);
    for (int i = 0; i < 4; ++i) assert(fabsf(rebooted.levelOffset(i) - offset) < .0001f);
    input.to<JsonObject>().clear(); input["background"] = true;
    assert(parseConfig(input.as<JsonObject>(), rebooted, error));
    assert(fabsf(rebooted.levelOffsetDegrees - offset) < .0001f);
  }
  constexpr float kDirectionalOffsetsDegrees[4] = {-2.5f, 1.25f, 4.f, -1.5f};
  const char *directionKeys[] = {"levelOffsetDegrees", "levelOffsetRightDegrees",
                                 "levelOffsetDownDegrees", "levelOffsetLeftDegrees"};
  input.clear();
  for (int i = 0; i < 4; ++i) input[directionKeys[i]] = kDirectionalOffsetsDegrees[i];
  DeviceConfig fourWay;
  assert(parseConfig(input.as<JsonObject>(), fourWay, error));
  saved.clear(); configJSON(saved.to<JsonObject>(), fourWay, true);
  std::string raw; serializeJson(saved, raw);
  assert(!deserializeJson(loaded, raw));
  DeviceConfig rebooted;
  assert(parseConfig(loaded.as<JsonObject>(), rebooted, error));
  for (int i = 0; i < 4; ++i) assert(rebooted.levelOffset(i) == kDirectionalOffsetsDegrees[i]);
  for (const char *field : {"levelOffsetRightDegrees", "levelOffsetDownDegrees", "levelOffsetLeftDegrees"}) {
    for (const char *bad : {"null", "true", "\"3\"", "180.1", "1e100"}) {
      assert(!deserializeJson(input, std::string("{\"") + field + "\":" + bad + "}"));
      DeviceConfig next;
      assert(!parseConfig(input.as<JsonObject>(), next, error));
    }
  }
  for (const char *bad : {"null", "true", "\"2.5\"", "[]", "{}", "-180.1", "180.1", "1e100"}) {
    assert(!deserializeJson(input, std::string("{\"levelOffsetDegrees\":") + bad + "}"));
    DeviceConfig next;
    assert(!parseConfig(input.as<JsonObject>(), next, error));
    assert(next.levelOffsetDegrees == 0);
  }
  for (float bad : {NAN, INFINITY, -INFINITY}) {
    input.clear(); input["levelOffsetDegrees"] = bad;
    DeviceConfig next;
    assert(!parseConfig(input.as<JsonObject>(), next, error));
  }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'Arduino.h').write_text(arduino)
    (path / 'test.cpp').write_text(code)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined']
    if os.environ.get('SDKROOT'):
        command += ['-isysroot', os.environ['SDKROOT']]
    command += [str(path / 'test.cpp'), '-I' + tmp,
                '-I' + str(root / 'firmware/src'),
                '-I' + str(root / 'firmware/.pio/libdeps/liquid/ArduinoJson/src'),
                '-o', str(path / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: level calibration defaults, signed decimals, persistence and invalid input')
