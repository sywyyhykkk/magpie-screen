#include "labels.h"
#include "liquid.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "FreeSansBold10pt7b.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SensorQMI8658.hpp>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPowersLib.h>

constexpr int W = 480, H = 24;
constexpr char VERSION[] = "0.2.0";
Arduino_ESP32QSPI bus(12, 38, 4, 5, 6, 7, false);
Arduino_CO5300 display(&bus, 39, 0, false, 480, 480, 0, 0, 0, 0);
class StripCanvas : public Arduino_Canvas {
public:
  StripCanvas() : Arduino_Canvas(W, H, nullptr) {}
  // Arduino GFX 1.5.9 custom fonts omit the lower clipping bound.
  // Strip rendering deliberately uses negative glyph Y coordinates.
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && x < W && y >= 0 && y < H)
      _framebuffer[y * W + x] = color;
  }
  bool begin(int32_t = GFX_NOT_DEFINED) override {
    _framebuffer = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_INTERNAL |
                                                               MALLOC_CAP_DMA);
    return _framebuffer;
  }
};
StripCanvas canvases[2];
struct Transfer {
  int index, y;
};
QueueHandle_t freeBuffers, transfers;
TaskHandle_t drawingTask;
volatile int requestedBrightness = 128;
volatile uint32_t transferUs = 0;
void transferTask(void *) {
  if (!display.begin(80000000)) {
    while (true)
      delay(1000);
  }
  bus.writeC8D8(0x36, 0xA0);
  xTaskNotifyGive(drawingTask);
  int brightness = -1;
  for (;;) {
    Transfer t;
    xQueueReceive(transfers, &t, portMAX_DELAY);
    if (brightness != requestedBrightness) {
      brightness = requestedBrightness;
      display.setBrightness(brightness);
    }
    uint32_t start = micros();
    display.draw16bitRGBBitmap(0, t.y, canvases[t.index].getFramebuffer(), W,
                               H);
    transferUs += micros() - start;
    xQueueSend(freeBuffers, &t.index, portMAX_DELAY);
    if (t.y == 480 - H)
      xTaskNotifyGive(drawingTask);
  }
}
SensorQMI8658 imu;
XPowersAXP2101 pmu;
Preferences prefs;
WebServer web(80);
liquid::Slosh fluid;
DynamicJsonDocument data(24576);
bool imuOK = false, pmuOK = false, orange = false, otaActive = false,
     otaFailed = false;
float target = 0, fill = 0, viscosity = .45f, fps = 0;
int screen = 1, rotation = 0;
uint32_t nextFrame = 0, lastFrame = 0, lastReport = 0, frames = 0,
         lastState = 0, lastPmu = 0, lastMotion = 0;
String key, ssid, password, line;
uint16_t palette[1800];
bool prevBoot = false, prevKey = false;
uint32_t keyTime = 0, bootTime = 0;
struct Flight {
  String key, kind, target;
  bool failed;
  uint32_t start;
};
std::vector<Flight> flights;
std::vector<String> seen;
void makePalette() {
  for (int i = 0; i < 1800; i++) {
    float d = i - 900, r = 12, g = 14, b = 18;
    if (d < 0) {
      float glow = liquid::clamp(1 + d / 4, 0, 1);
      r += glow * 55;
      g += glow * 52;
      b += glow * 45;
    } else if (orange) {
      float t = liquid::clamp(d / 430, 0, 1);
      r = 249 - 121 * t;
      g = 172 - 112 * t;
      b = 112 - 75 * t;
    } else {
      float t = liquid::clamp(d / 150, 0, 1);
      r = g = b = 105 - 90 * sqrtf(t);
    }
    if (d >= 0 && d < 2) {
      r = 245;
      g = 240;
      b = orange ? 205 : 245;
    } else if (d >= 2 && d < 12) {
      r += 18;
      g += 18;
      b += 18;
    }
    palette[i] = display.color565(std::min(255, (int)r), std::min(255, (int)g),
                                  std::min(255, (int)b));
  }
}
void selectScreen(int n) {
  if (n < 1 || n > 3)
    return;
  screen = n;
  prefs.putUChar("screen", n);
}
void status(JsonObject s) {
  s["type"] = "status";
  s["version"] = VERSION;
  s["screen"] = screen;
  s["fps"] = fps;
  s["ip"] =
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0";
  s["wifi"] = WiFi.status() == WL_CONNECTED ? "connected"
              : ssid.length()               ? "connecting"
                                            : "not configured";
  s["paired"] = key.length() > 0;
  s["imu"] = imuOK;
  s["pmu"] = pmuOK;
  s["heap"] = ESP.getFreeHeap();
  s["remaining"] = data["quota"]["remaining"];
  s["provider"] = data["quota"]["provider"];
  s["brightness"] = requestedBrightness;
  s["viscosity"] = viscosity;
}
void serialStatus() {
  StaticJsonDocument<768> d;
  status(d.to<JsonObject>());
  serializeJson(d, Serial);
  Serial.println();
}
bool command(JsonDocument &d, bool usb, String &error) {
  String op = d["op"] | "";
  if (op == "pair") {
    String v = d["token"] | "";
    if (!usb || v.length() != 48) {
      error = "Invalid pairing";
      return false;
    }
    if (v != key) {
      key = v;
      prefs.putString("key", key);
    }
    return true;
  }
  if (op == "ping")
    return true;
  if (op == "config") {
    JsonObject c = d["config"];
    if (!c["screen"].is<int>() || !c["brightness"].is<int>() ||
        !c["viscosity"].is<float>()) {
      error = "Invalid config";
      return false;
    }
    int n = c["screen"], b = c["brightness"];
    float v = c["viscosity"];
    if (n < 1 || n > 3 || b < 10 || b > 255 || !isfinite(v) || v < 0 || v > 1) {
      error = "Config range";
      return false;
    }
    selectScreen(n);
    requestedBrightness = b;
    viscosity = v;
    prefs.putUChar("brightness", b);
    prefs.putFloat("viscosity", v);
    return true;
  }
  if (op == "wifi") {
    String s = d["ssid"] | "", p = d["password"] | "";
    if (!s.length() || s.length() > 32 || p.length() > 63 ||
        (p.length() && p.length() < 8)) {
      error = "Invalid Wi-Fi";
      return false;
    }
    ssid = s;
    password = p;
    prefs.putString("ssid", s);
    prefs.putString("password", p);
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    return true;
  }
  if (op == "state") {
    if (!d["state"].is<JsonObject>()) {
      error = "Invalid state";
      return false;
    }
    auto r = d["state"]["quota"]["remaining"];
    if (!r.isNull() && (!r.is<float>() || !isfinite(r.as<float>()) ||
                        r.as<float>() < 0 || r.as<float>() > 100)) {
      error = "Invalid quota";
      return false;
    }
    data.set(d["state"]);
    lastState = millis();
    String provider = data["quota"]["provider"] | "";
    bool next = provider == "claude" || provider == "anthropic";
    if (next != orange) {
      orange = next;
      makePalette();
    }
    for (JsonObject e : data["route"]["events"].as<JsonArray>()) {
      String id = e["key"] | "";
      if (!id.length() || std::find(seen.begin(), seen.end(), id) != seen.end())
        continue;
      seen.push_back(id);
      if (seen.size() > 128)
        seen.erase(seen.begin());
      if (flights.size() < 12)
        flights.push_back({id, e["kind"] | "", e["target"] | "",
                           e["failed"] | false, millis()});
    }
    return true;
  }
  error = "Unknown operation";
  return false;
}
bool authorized() { return key.length() && web.header("X-Magpie-Key") == key; }
void setupWeb() {
  const char *headers[] = {"X-Magpie-Key"};
  web.collectHeaders(headers, 1);
  web.on("/command", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DynamicJsonDocument d(28672), reply(1024);
    String error;
    bool ok = !deserializeJson(d, web.arg("plain")) && command(d, false, error);
    reply["ok"] = ok;
    reply["error"] = error;
    status(reply.createNestedObject("status"));
    String body;
    serializeJson(reply, body);
    web.send(ok ? 200 : 400, "application/json", body);
  });
  web.on(
      "/update", HTTP_POST,
      []() {
        if (!authorized()) {
          web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
          return;
        }
        bool ok = otaActive && !otaFailed && !Update.hasError();
        web.send(ok ? 200 : 400, "application/json",
                 ok ? "{\"ok\":true}"
                    : "{\"error\":\"Firmware validation/write failed\"}");
        otaActive = false;
        if (ok) {
          delay(300);
          ESP.restart();
        }
      },
      []() {
        if (!authorized())
          return;
        HTTPUpload &u = web.upload();
        if (u.status == UPLOAD_FILE_START) {
          otaActive = true;
          otaFailed = !Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
        } else if (u.status == UPLOAD_FILE_WRITE && !otaFailed) {
          if (Update.write(u.buf, u.currentSize) != u.currentSize)
            otaFailed = true;
        } else if (u.status == UPLOAD_FILE_END) {
          if (!otaFailed && !Update.end(true))
            otaFailed = true;
          if (otaFailed)
            Update.abort();
        } else if (u.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          otaFailed = true;
          otaActive = false;
        }
      });
  web.begin();
}
void io() {
  web.handleClient();
  int budget = 32768;
  while (Serial.available() && budget-- > 0) {
    char c = Serial.read();
    if (c == '\n') {
      DynamicJsonDocument d(28672);
      String error;
      bool parsed = !deserializeJson(d, line);
      line = "";
      bool ok = parsed && command(d, true, error);
      StaticJsonDocument<256> reply;
      reply["id"] = d["id"];
      reply["ok"] = ok;
      reply["error"] = parsed ? error : "Invalid JSON";
      serializeJson(reply, Serial);
      Serial.println();
    } else if (line.length() < 32768)
      line += c;
    else
      line = "";
  }
  uint32_t now = millis();
  bool b = digitalRead(0) == LOW, k = digitalRead(18) == LOW;
  if (b && !prevBoot && now - bootTime > 150) {
    selectScreen(1);
    bootTime = now;
  }
  if (k && !prevKey && now - keyTime > 150) {
    selectScreen(3);
    keyTime = now;
  }
  prevBoot = b;
  prevKey = k;
  if (pmuOK && now - lastPmu > 50) {
    lastPmu = now;
    pmu.getIrqStatus();
    if (pmu.isPekeyShortPressIrq())
      selectScreen(2);
    pmu.clearIrqStatus();
  }
  if (lastState && now - lastState > 45000) {
    data.clear();
    flights.clear();
    lastState = 0;
  }
}
void text(StripCanvas &c, int x, int y, const String &s, int size = 2,
          uint16_t color = 0xffff) {
  c.setFont(nullptr);
  c.setTextSize(size);
  c.setTextColor(color);
  c.setCursor(x, y);
  c.print(s);
}
void large(StripCanvas &c, int x, int y, const String &s, bool big = false) {
  c.setFont(&FreeSansBold10pt7b);
  c.setTextSize(big ? 2 : 1);
  c.setTextColor(0xffff);
  c.setCursor(x, y);
  c.print(s);
  c.setFont(nullptr);
}
void label(StripCanvas &c, int x, int y, int id) {
  if (y >= H || y + 24 <= 0)
    return;
  const auto &l = labels[id];
  c.drawBitmap(x, y, l.bits, l.width, 24, 0xad75);
}
String value(JsonVariant v) { return v.isNull() ? "--" : v.as<String>(); }
void bird(StripCanvas &c, int x, int y, uint16_t color) {
  c.fillEllipse(x, y, 10, 5, color);
  c.fillTriangle(x - 5, y, x - 18, y - 13, x + 2, y - 3, color);
  c.fillTriangle(x + 4, y, x + 12, y - 12, x + 9, y + 3, color);
  c.drawLine(x - 8, y + 3, x - 20, y + 11, color);
  c.fillTriangle(x + 9, y - 1, x + 16, y + 1, x + 9, y + 3, color);
}
void drawUI(StripCanvas &c, int sy) {
  if (screen == 1) {
    String p = data["quota"]["provider"] | "MAGPIE";
    p.toUpperCase();
    large(c, std::max(36, 240 - (int)p.length() * 8), 122 - sy, p);
    bool known = !data["quota"]["remaining"].isNull();
    String amount =
        known ? String(data["quota"]["remaining"].as<float>(), 1) + "%" : "--";
    large(c, 240 - amount.length() * 14, 236 - sy, amount, true);
    label(c, 180, 270 - sy, known ? 0 : 1);
    text(c, 160, 310 - sy,
         String(data["quota"]["window"] | "").substring(0, 16), 1, 0x9cd3);
    for (int i = 1; i < 10; i++)
      c.drawFastHLine(448, 48 * i - sy, i == 5 ? 18 : 10, 0x630c);
    text(c, 26, 446 - sy, lastState ? "LIVE / MAGPIE" : "CONNECT MAC /config",
         1, 0x8c71);
  } else if (screen == 2) {
    label(c, 26, 28 - sy, 2);
    text(c, 389, 34 - sy, "LIVE", 2, 0x9cd3);
    auto r = data["route"];
    auto targets = r["targets"].as<JsonArray>();
    c.drawRoundRect(22, 192 - sy, 100, 90, 12, 0x52aa);
    text(c, 34, 225 - sy, "CLIENT", 2);
    c.fillRoundRect(176, 200 - sy, 118, 74, 14, 0x29a5);
    bird(c, 235, 224 - sy, 0xc6b8);
    text(c, 200, 247 - sy, "MAGPIE", 2);
    c.drawLine(122, 236 - sy, 176, 236 - sy, 0x52aa);
    int i = 0;
    for (JsonObject t : targets) {
      int y = 105 + i * 78;
      c.drawLine(294, 236 - sy, 337, y + 26 - sy, 0x52aa);
      c.drawRoundRect(337, y - sy, 130, 56, 8,
                      t["failed"] | false ? 0xd208 : 0x52aa);
      text(c, 346, y + 8 - sy, value(t["provider"]).substring(0, 10), 2);
      text(c, 346, y + 34 - sy, value(t["model"]).substring(0, 18), 1, 0xad75);
      i++;
    }
    if (!i) {
      label(c, 328, 218 - sy, 3);
    }
    uint32_t now = millis();
    for (auto &f : flights) {
      float t = std::min(1.f, (now - f.start) / 850.f);
      int x1 = 122, y1 = 236, x2 = 176, y2 = 236;
      if (f.kind == "attempt" || f.kind == "response") {
        x1 = 294;
        int index = 0;
        for (JsonObject a : targets) {
          if (f.target == (a["key"] | ""))
            break;
          index++;
        }
        x2 = 337;
        y2 = 131 + std::min(index, 3) * 78;
      }
      if (f.kind == "response" || f.kind == "result")
        t = 1 - t;
      bird(c, x1 + (x2 - x1) * t,
           y1 + (y2 - y1) * t - 18 * sinf(t * liquid::kPi) - sy,
           f.failed ? 0xfaa8 : 0xcff2);
    }
    text(c, 26, 444 - sy, r.isNull() ? "OFFLINE" : value(r["state"]), 2,
         0xad75);
  } else {
    auto u = data["usage"];
    label(c, 26, 22 - sy, 4);
    text(c, 389, 28 - sy, "LIVE", 2, 0xad75);
    large(c, 26, 112 - sy, value(u["total"]), true);
    text(c, 265, 96 - sy, "TOKENS", 2, 0xad75);
    text(c, 26, 136 - sy,
         "IN " + value(u["input"]) + "  OUT " + value(u["output"]) +
             "  CACHE " + value(u["cache"]),
         1, 0xad75);
    const char *keys[] = {"calls", "cost", "cacheRate", "speed"};
    for (int i = 0; i < 4; i++) {
      int x = 26 + (i % 2) * 222, y = 170 + (i / 2) * 87;
      c.drawRoundRect(x, y - sy, 210, 78, 9, 0x3186);
      label(c, x + 12, y + 7 - sy, 5 + i);
      large(c, x + 12, y + 62 - sy, value(u[keys[i]]));
    }
    text(c, 38, 333 - sy, "SUCCESS " + value(u["success"]), 1, 0xad75);
    label(c, 26, 352 - sy, 9);
    int i = 0;
    for (JsonObject p : u["providers"].as<JsonArray>()) {
      int y = 383 + i * 25;
      text(c, 26, y - sy, value(p["name"]).substring(0, 10), 1, 0xad75);
      c.fillRect(126, y + 2 - sy, 220, 7, 0x2104);
      c.fillRect(126, y + 2 - sy, 220 * (p["fraction"] | 0.f), 7, 0x9e51);
      text(c, 366, y - sy, value(p["tokens"]), 1, 0xad75);
      i++;
    }
    if (u.isNull())
      label(c, 174, 400 - sy, 1);
    text(c, 26, 462 - sy,
         u.isNull()                ? "OFFLINE"
         : (u["unpriced"] | 0) > 0 ? "USD ESTIMATE / UNPRICED CALLS"
                                   : "TODAY / ALL SOURCES",
         1, 0x8c71);
  }
  // Small screen indicators, matching the three physical keys.
  for (int i = 0; i < 3; i++)
    c.fillCircle(218 + i * 22, 8 - sy, 3, screen == i + 1 ? 0xbef2 : 0x4208);
}
void setup() {
  Serial.setRxBufferSize(32768);
  Serial.begin(115200);
  delay(350);
  Wire.begin(15, 14);
  pinMode(0, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  prefs.begin("magpie", false);
  key = prefs.getString("key", "");
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("password", "");
  screen = prefs.getUChar("screen", 1);
  requestedBrightness = prefs.getUChar("brightness", 128);
  viscosity = prefs.getFloat("viscosity", .45f);
  for (auto &c : canvases)
    if (!c.begin()) {
      while (true)
        delay(1000);
    }
  imuOK = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, 15, 14);
  if (imuOK) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_1000Hz,
                            SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
  }
  pmuOK = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14);
  if (pmuOK) {
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  if (ssid.length())
    WiFi.begin(ssid.c_str(), password.c_str());
  MDNS.begin("magpie-screen");
  setupWeb();
  makePalette();
  freeBuffers = xQueueCreate(2, sizeof(int));
  transfers = xQueueCreate(2, sizeof(Transfer));
  for (int i = 0; i < 2; i++)
    xQueueSend(freeBuffers, &i, 0);
  drawingTask = xTaskGetCurrentTaskHandle();
  xTaskCreatePinnedToCore(transferTask, "display", 8192, nullptr, 2, nullptr,
                          0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  lastFrame = micros();
  nextFrame = lastFrame + 33333;
  lastReport = millis();
  serialStatus();
}
void loop() {
  io();
  if (otaActive) {
    delay(1);
    return;
  }
  uint32_t now = micros();
  if ((int32_t)(now - nextFrame) < 0) {
    delay(1);
    return;
  }
  nextFrame += 33333;
  if ((int32_t)(now - nextFrame) > 33333)
    nextFrame = now + 33333;
  float dt = (now - lastFrame) / 1000000.f;
  lastFrame = now;
  if (imuOK && imu.getDataReady()) {
    float x, y, z;
    if (imu.getAccelerometer(x, y, z) && x * x + y * y > .035f) {
      float delta = remainderf(atan2f(x, y) - target, 2 * liquid::kPi);
      target += delta * (1 - expf(-12 * dt));
    }
  }
  float remaining = std::min(dt, .1f);
  while (remaining > 0) {
    float h = std::min(remaining, 1.f / 60);
    fluid.step(target, viscosity, h);
    remaining -= h;
  }
  bool known = !data["quota"]["remaining"].isNull();
  float wanted = known ? data["quota"]["remaining"].as<float>() / 100 : 0;
  fill += (wanted - fill) * (1 - expf(-3 * dt));
  if (!known)
    fill = 0;
  uint32_t start = micros();
  liquid::Shape shape(fluid, fill);
  static int32_t surface[2801];
  if (screen == 1)
    for (int i = 0; i <= 2800; i++)
      surface[i] = (int32_t)(shape.surface(i * .25f - 350) * 65536);
  int32_t dx = (int32_t)(shape.c * 65536), dy = (int32_t)(-shape.s * 65536);
  transferUs = 0;
  for (int sy = 0; sy < 480; sy += H) {
    int index;
    xQueueReceive(freeBuffers, &index, portMAX_DELAY);
    auto &c = canvases[index];
    uint16_t *pixels = c.getFramebuffer();
    if (screen == 1 && known && fill > .001f) {
      for (int row = 0; row < H; row++) {
        float yy = sy + row - 239.5f;
        int32_t lx =
                    (int32_t)((-239.5f * shape.c + yy * shape.s + 350) * 65536),
                ly = (int32_t)((239.5f * shape.s + yy * shape.c) * 65536);
        uint16_t *dest = pixels + row * 480;
        for (int x = 0; x < 480; x++) {
          int u = lx >> 14;
          int v = ((ly - surface[u]) >> 16) + 900;
          *dest++ = palette[std::max(0, std::min(1799, v))];
          lx += dx;
          ly += dy;
        }
      }
    } else
      c.fillScreen(0x0861);
    drawUI(c, sy);
    Transfer t{index, sy};
    xQueueSend(transfers, &t, portMAX_DELAY);
  }
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  flights.erase(
      std::remove_if(flights.begin(), flights.end(),
                     [](const Flight &f) { return millis() - f.start > 850; }),
      flights.end());
  frames++;
  uint32_t ms = millis();
  if (ms - lastReport >= 2000) {
    fps = frames * 1000.f / (ms - lastReport);
    serialStatus();
    Serial.printf("timing frame=%u transfer=%u us\n", micros() - start,
                  transferUs);
    frames = 0;
    lastReport = ms;
  }
}
