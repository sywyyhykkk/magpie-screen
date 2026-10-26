#pragma once
#include <Arduino.h>
#include "power_policy.h"
#include "provider_appearance.h"
#include <ArduinoJson.h>
#include <cmath>
struct DeviceConfig {
  String url, apiKey, provider = "codex", accountId, windowId;
  String liquidTheme = "auto", quotaIcon = "auto";
  bool background = false, showIdentity = false;
  bool wifiCompatibility = false;
  float accelBiasXG = 0, accelBiasYG = 0;
  float levelOffsetDegrees = 0;
  // Missing direction settings inherit the old single offset on upgrade.
  float directionalLevelOffsets[3] = {NAN, NAN, NAN};
  float levelOffset(int direction) const {
    if (direction == 0 || !std::isfinite(directionalLevelOffsets[direction - 1]))
      return levelOffsetDegrees;
    return directionalLevelOffsets[direction - 1];
  }
  power::Settings power;
};
inline bool validURL(const String &s) {
  // LAN HTTP only: no userinfo, path, query, fragment or loopback address.
  if (s.isEmpty())
    return true;
  if (!s.startsWith("http://") || s.length() > 200)
    return false;
  String host = s.substring(7);
  if (host.endsWith("/"))
    host.remove(host.length() - 1);
  int colon = host.indexOf(':');
  if (colon >= 0) {
    String port = host.substring(colon + 1);
    if (!port.length() || port.toInt() < 1 || port.toInt() > 65535)
      return false;
    for (char c : port)
      if (c < '0' || c > '9')
        return false;
    host = host.substring(0, colon);
  }
  if (host.isEmpty() || host == "localhost" || host.startsWith("127.") ||
      host == "0.0.0.0")
    return false;
  for (char c : host)
    if (!isalnum((unsigned char)c) && c != '.' && c != '-')
      return false;
  return true;
}
inline void configJSON(JsonObject c, const DeviceConfig &v,
                       bool secrets = false) {
  c["url"] = v.url;
  c["provider"] = v.provider;
  c["accountId"] = v.accountId;
  c["windowId"] = v.windowId;
  c["liquidTheme"] = v.liquidTheme;
  c["quotaIcon"] = v.quotaIcon;
  c["background"] = v.background;
  c["showIdentity"] = v.showIdentity;
  c["wifiCompatibility"] = v.wifiCompatibility;
  c["accelBiasXG"] = v.accelBiasXG;
  c["accelBiasYG"] = v.accelBiasYG;
  c["levelOffsetDegrees"] = v.levelOffsetDegrees;
  c["levelOffsetRightDegrees"] = v.levelOffset(1);
  c["levelOffsetDownDegrees"] = v.levelOffset(2);
  c["levelOffsetLeftDegrees"] = v.levelOffset(3);
  c["usbBrightness"] = v.power.usbBrightness;
  c["batteryBrightness"] = v.power.batteryBrightness;
  c["usbSeconds"] = v.power.usbSeconds;
  c["batterySeconds"] = v.power.batterySeconds;
  c["alwaysOn"] = v.power.alwaysOn;
  c["wakeOnData"] = v.power.wakeOnData;
  if (secrets)
    c["apiKey"] = v.apiKey;
  else
    c["hasApiKey"] = !v.apiKey.isEmpty();
}
inline bool parseConfig(JsonObject c, DeviceConfig &v, String &error) {
  auto fail = [&](const char *s) {
    error = s;
    return false;
  };
  for (JsonPair p : c) {
    String k = p.key().c_str();
    if (k == "url" || k == "apiKey" || k == "provider" || k == "accountId" ||
        k == "windowId") {
      if (!p.value().is<const char *>())
        return fail("Invalid text setting");
      String val = p.value().as<String>();
      if (val.length() > (k == "apiKey" ? 512 : 200))
        return fail("Setting too long");
      for (char ch : val)
        if ((unsigned char)ch < 32 || ch == 127)
          return fail("Invalid control character");
      if (k == "url") {
        if (!validURL(val))
          return fail("Use http://LAN-host:port without a path");
        if (val.endsWith("/"))
          val.remove(val.length() - 1);
        v.url = val;
      }
      if (k == "apiKey" && val.length())
        v.apiKey = val; // blank retains saved secret
      if (k == "provider") {
        if (!val.length())
          return fail("Provider required");
        v.provider = val;
      }
      if (k == "accountId")
        v.accountId = val;
      if (k == "windowId")
        v.windowId = val;
    } else if (k == "liquidTheme" || k == "quotaIcon") {
      if (!p.value().is<const char *>() ||
          !appearance::validChoice(p.value().as<const char *>()))
        return fail("Invalid appearance choice");
      if (k == "liquidTheme") v.liquidTheme = p.value().as<String>();
      else v.quotaIcon = p.value().as<String>();
    } else if (k == "background" || k == "showIdentity" || k == "alwaysOn" || k == "wakeOnData" ||
               k == "clearApiKey" || k == "wifiCompatibility") {
      if (!p.value().is<bool>())
        return fail("Invalid switch");
      bool b = p.value();
      if (k == "background")
        v.background = b;
      if (k == "showIdentity")
        v.showIdentity = b;
      if (k == "wifiCompatibility")
        v.wifiCompatibility = b;
      if (k == "alwaysOn")
        v.power.alwaysOn = b;
      if (k == "wakeOnData")
        v.power.wakeOnData = b;
      if (k == "clearApiKey" && b)
        v.apiKey = "";
    } else if (k == "accelBiasXG" || k == "accelBiasYG") {
      if (!p.value().is<float>())
        return fail("Invalid accelerometer bias");
      float n = p.value().as<float>();
      if (!std::isfinite(n) || n < -.5f || n > .5f)
        return fail("Accelerometer bias must be between -0.5 and 0.5 g");
      if (k == "accelBiasXG") v.accelBiasXG = n;
      else v.accelBiasYG = n;
    } else if (k == "levelOffsetDegrees" || k == "levelOffsetRightDegrees" ||
               k == "levelOffsetDownDegrees" || k == "levelOffsetLeftDegrees") {
      if (!p.value().is<float>())
        return fail("Invalid level calibration angle");
      float n = p.value().as<float>();
      if (!std::isfinite(n) || n < -180 || n > 180)
        return fail("Level calibration must be between -180 and 180 degrees");
      if (k == "levelOffsetDegrees") v.levelOffsetDegrees = n;
      else v.directionalLevelOffsets[k == "levelOffsetRightDegrees" ? 0 :
                                    k == "levelOffsetDownDegrees" ? 1 : 2] = n;
    } else if (k == "usbBrightness" || k == "batteryBrightness" ||
               k == "usbSeconds" || k == "batterySeconds") {
      if (!p.value().is<int>())
        return fail("Invalid number");
      int n = p.value();
      bool brightness = k.endsWith("Brightness");
      if (n < (brightness ? 10 : 30) || n > (brightness ? 255 : 3600))
        return fail("Setting out of range");
      if (k == "usbBrightness")
        v.power.usbBrightness = n;
      if (k == "batteryBrightness")
        v.power.batteryBrightness = n;
      if (k == "usbSeconds")
        v.power.usbSeconds = n;
      if (k == "batterySeconds")
        v.power.batterySeconds = n;
    } else
      return fail("Unknown setting");
  }
  return true;
}
