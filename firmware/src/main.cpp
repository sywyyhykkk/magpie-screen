#include "config_page.h"

#include "liquid.h"
#include "orientation.h"
#include "route_flights.h"
#include "standalone_network.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "ui_render.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SensorQMI8658.hpp>
#include <TouchDrvCSTXXX.hpp>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPowersLib.h>

constexpr int W = 480, H = 24;
constexpr char VERSION[] = "0.3.11";
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
class OverlayCanvas : public Arduino_Canvas {
public:
  OverlayCanvas():Arduino_Canvas(480,480,nullptr) {}
  bool begin(int32_t = GFX_NOT_DEFINED) override {
    _framebuffer=(uint16_t*)heap_caps_malloc(480*480*2,MALLOC_CAP_SPIRAM);
    uiAlpha=(uint8_t*)heap_caps_malloc(480*480,MALLOC_CAP_SPIRAM);
    if(!_framebuffer||!uiAlpha) return false;
    std::fill(_framebuffer,_framebuffer+480*480,(uint16_t)0x0001);
    memset(uiAlpha,255,480*480);
    for(int r=0;r<480;r++) {uiLo[r]=480; uiHi[r]=0;}
    return true;
  }
  // Record drawn spans; coordinates here are pre-rotation content space.
  void writePixelPreclipped(int16_t x,int16_t y,uint16_t color) override {
    uiMark(x,y,1,1); Arduino_Canvas::writePixelPreclipped(x,y,color);
  }
  void writeFastVLine(int16_t x,int16_t y,int16_t h,uint16_t color) override {
    uiMark(x,h<0?y+h+1:y,1,abs(h)); Arduino_Canvas::writeFastVLine(x,y,h,color);
  }
  void writeFastHLine(int16_t x,int16_t y,int16_t w,uint16_t color) override {
    uiMark(w<0?x+w+1:x,y,abs(w),1); Arduino_Canvas::writeFastHLine(x,y,w,color);
  }
  void writeFillRectPreclipped(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color) override {
    uiMark(x,y,w,h); Arduino_Canvas::writeFillRectPreclipped(x,y,w,h,color);
  }
};
OverlayCanvas overlay;
struct Transfer {
  int index, y;
};
QueueHandle_t freeBuffers, transfers;
TaskHandle_t drawingTask;
volatile int requestedBrightness = 179;
volatile bool requestedOff = false;
volatile uint32_t transferUs = 0;
void transferTask(void *) {
  if (!display.begin(80000000)) {
    while (true)
      delay(1000);
  }
  bus.writeC8D8(0x36, 0xA0);
  xTaskNotifyGive(drawingTask);
  int brightness = -1;
  bool off = false;
  for (;;) {
    Transfer t;
    bool transfer = xQueueReceive(transfers, &t, pdMS_TO_TICKS(20)) == pdTRUE;
    if (off != requestedOff) {
      off = requestedOff;
      if (off)
        display.displayOff();
      else
        display.displayOn();
    }
    if (brightness != requestedBrightness) {
      brightness = requestedBrightness;
      display.setBrightness(brightness);
    }
    if (!transfer)
      continue;
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
TouchDrvCST92xx touch;
XPowersAXP2101 pmu;
Preferences prefs;
WebServer web(80);
liquid::Slosh fluid;
liquid::Motion motion;
DynamicJsonDocument data(24576);
bool imuOK = false, gyroOK = false, pmuOK = false, orange = false, otaActive = false,
     otaFailed = false;
float target = 0, fill = 0, viscosity = .45f, fps = 0;
int screen = 1, rotation = 0;
OrientationTransition orientation;
uint32_t nextFrame = 0, lastStep = 0, lastSample = 0, lastReport = 0,
         frames = 0, lastState = 0, lastPmu = 0, lastMotion = 0;
constexpr uint32_t kFrameUs = 16667; // 60 fps cap
// Palette is indexed by depth below the surface in quarter pixels.
constexpr int kPalette = 1800, kSurfaceIndex = 32;
String key, ssid, password, line;
uint16_t palette[kPalette];
bool prevBoot = false, prevKey = false;
uint32_t keyTime = 0, bootTime = 0;
struct Flight {
  String key, kind, target, request;
  bool failed;
  uint32_t start;
};
std::vector<Flight> flights;
std::vector<String> seen;
DeviceConfig config;
DataAdapter adapter;
String adminCode, setupSSID;
bool touchOK = false, screenOff = false, dimmed = false, dirty = true;
bool usbPower = false, batteryPresent = false, charging = false,
     chargeDone = false;
bool setupMode = false, networkBusy = false, wifiPending = false;
int batteryPercent = -1, batteryMv = 0, previewBrightness = -1;
String previewSource;
uint32_t setupStarted = 0, lastActivity = 0, lastPowerRead = 0, lowSince = 0;
uint32_t configRevision = 1, due[3] = {}, channelTime[3] = {}, previewUntil = 0;
uint32_t noticeUntil = 0;
bool channelOK[3] = {};
String channelError[3], quotaNotice, routeMode;
uint32_t pollInterval = 10000, totalFrames = 0;
int pollCursor = 0;
void userActivity();
void startSetup();
void networkIO();
void powerIO();
void setupStandaloneWeb();
bool saveStandalone(JsonDocument &d, String &error);
void updatePalette() {
  bool next = config.provider == "claude" || config.provider == "anthropic";
  if (orange != next) {
    orange = next;
    dirty = true;
  }
}
// Colour at depth d px below the surface (negative: above it).
static void shade(float d, float rgb[3]) {
  float r = orange?36:65, g = orange?33:65, b = orange?38:65;
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
  rgb[0] = r; rgb[1] = g; rgb[2] = b;
}
void makePalette() {
  // Box-filter each quarter-pixel entry over one pixel: antialiased surface.
  for (int i = 0; i < kPalette; i++) {
    float sum[3] = {}, rgb[3];
    for (int k = 0; k < 8; k++) {
      shade((i - kSurfaceIndex) / 4.f + (k + .5f) / 8 - .5f, rgb);
      for (int j = 0; j < 3; j++) sum[j] += rgb[j];
    }
    palette[i] = display.color565(std::min(255, (int)(sum[0] / 8 + .5f)),
                                  std::min(255, (int)(sum[1] / 8 + .5f)),
                                  std::min(255, (int)(sum[2] / 8 + .5f)));
  }
}
void selectScreen(int n) {
  if (n < 1 || n > 3)
    return;
  userActivity();
  setupMode = false;
  if (screen != n) {
    screen = n;
    prefs.putUChar("screen", n);
  }
  dirty = true;
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
  s["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : ssid;
  s["routeTargets"] = data["route"]["targets"].size();
  s["paired"] = key.length() > 0;
  s["imu"] = imuOK;
  s["pmu"] = pmuOK;
  s["heap"] = ESP.getFreeHeap();
  s["remaining"] = data["quota"]["remaining"];
  s["provider"] = data["quota"]["provider"];
  s["brightness"] = requestedBrightness;
  s["viscosity"] = viscosity;
  s["standalone"] = true;
  s["routeMode"] = routeMode;
  s["touch"] = touchOK;
  s["usb"] = usbPower;
  s["battery"] = batteryPresent;
  s["charging"] = charging;
  s["chargeDone"] = chargeDone;
  s["batteryPercent"] = nullptr;
  if (batteryPresent && batteryPercent >= 0 && batteryPercent <= 100)
    s["batteryPercent"] = batteryPercent;
  s["batteryMv"] = batteryPresent ? batteryMv : 0;
  s["display"] = screenOff ? "off" : dimmed ? "dim" : "on";
  s["idleSeconds"] = (millis() - lastActivity) / 1000;
  s["renderedFrames"] = totalFrames;
  s["pollMs"] = pollInterval;
  s["setup"] = setupMode;
  if (pmuOK) {
    s["chargeVoltageCode"] = pmu.getChargeTargetVoltage();
    s["chargeCurrentCode"] = pmu.getChargerConstantCurr();
  }
  auto channels = s.createNestedArray("channels");
  for (int i = 0; i < 3; i++) {
    auto c = channels.createNestedObject();
    c["ok"] = channelOK[i];
    c["error"] = channelError[i];
    c["age"] = nullptr;
    if (channelTime[i])
      c["age"] = (millis() - channelTime[i]) / 1000;
  }
}
void serialStatus() {
  StaticJsonDocument<2048> d;
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
  if (op == "standalone")
    return saveStandalone(d, error);
  if (op == "wake") {
    userActivity();
    return true;
  }
  if (op == "ping")
    return true;
  if (op == "config") {
    error = "Use the device /config page for standalone settings";
    return false;
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
    wifiPending = true;
    userActivity();
    return true;
  }
  if (op == "state") {
    error = "Standalone firmware reads Magpie directly";
    return false;
  }
  error = "Unknown operation";
  return false;
}
bool authorized() {
  String supplied = web.header("X-Magpie-Key");
  return (key.length() && supplied == key) ||
         (adminCode.length() && supplied == adminCode);
}
void setupWeb() {
  const char *headers[] = {"X-Magpie-Key"};
  web.collectHeaders(headers, 1);
  web.on("/command", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DynamicJsonDocument d(28672), reply(3072);
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
  setupStandaloneWeb();
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
    if (screenOff)
      userActivity();
    else
      selectScreen(1);
    bootTime = now;
  }
  if (k && !prevKey && now - keyTime > 150) {
    if (screenOff)
      userActivity();
    else
      selectScreen(3);
    keyTime = now;
  }
  static bool longHandled = false;
  if (!k)
    longHandled = false;
  if (k && now - keyTime >= 3000 && !longHandled) {
    longHandled = true;
    startSetup();
  }
  prevBoot = b;
  prevKey = k;
  if (pmuOK && now - lastPmu > 50) {
    lastPmu = now;
    pmu.getIrqStatus();
    if (pmu.isPekeyShortPressIrq()) {
      if (screenOff)
        userActivity();
      else
        selectScreen(2);
    }
    pmu.clearIrqStatus();
  }
  static uint32_t lastTouch = 0;
  if (touchOK && now - lastTouch >= 80 && digitalRead(11) == LOW) {
    lastTouch = now;
    int16_t x[5], y[5];
    if (touch.getPoint(x, y, 5) > 0)
      userActivity();
  }
  if (wifiPending && !networkBusy) {
    wifiPending = false;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    adapter.reset();
    for (auto &t : due)
      t = 0;
  }
  if (setupStarted && now - setupStarted >= 600000) {
    setupStarted = 0;
    setupMode = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    dirty = true;
  }
  networkIO();
  powerIO();
  if (now - lastReport >= 2000) {
    fps = frames * 1000.f / (now - lastReport);
    frames = 0;
    lastReport = now;
    serialStatus();
  }
}
void text(Arduino_Canvas &c, int x, int y, const String &s, int size=2, uint16_t color=0xffff) {
  const UiFont &f=size==1?uiFont18:size==2?uiFont22:uiFont28;
  uiText(c,x,y+f.size,s,f,color,448-x);
}
String value(JsonVariant v) { return v.isNull()?"--":v.as<String>(); }
int routeY(int i,int n) { return n<=1?240:n==2?176+i*128:n==3?132+i*108:100+i*92; }
void bird(Arduino_Canvas &c,int x,int y,bool facingLeft) { icon(c,x-16,y-16,icon_magpie32,false,facingLeft); }
void drawUI(Arduino_Canvas &c,int) {
  constexpr uint16_t muted=0x9cd3, green=0xaeea;
  if(setupMode) {
    uiText(c,32,80,"DEVICE SETUP",uiFont28);
    text(c,32,110,"Connect Wi-Fi / open in browser",1);
    text(c,32,160,setupSSID,2);
    text(c,32,205,"Wi-Fi password / access code:",1);
    text(c,32,240,adminCode,3);
    text(c,32,300,"192.168.4.1/config",2);
    text(c,32,340,WiFi.localIP().toString()+"/config",2);
    text(c,32,400,"Press a key to return",2); return;
  }
  if(screen==1) {
    String p=data["quota"]["provider"]|"MAGPIE", name=p; name.toUpperCase();
    int w=textWidth(name,uiFont22), left=240-(w+44)/2;
    icon(c,left,148,providerIcon(p),true); uiText(c,left+44,173,name,uiFont22);
    bool known=!data["quota"]["remaining"].isNull();
    float n=data["quota"]["remaining"]|0.f;
    String amount=known?String(n,n==floorf(n)?0:1):"--";
    int nw=textWidth(amount,uiFont88), total=nw+(known?36:0);
    uiText(c,240-total/2,278,amount,uiFont88);
    if(known) uiText(c,240-total/2+nw+8,278,"%",uiFont28,muted);
    String caption=known?"剩余额度":"数据不可用", window=data["quota"]["window"]|"";
    quotaCaption(c,240,305,caption,known?window:String(""),muted);
    if(noticeUntil&&(int32_t)(noticeUntil-millis())>0) centered(c,240,362,quotaNotice,uiFont18,green);
    return;
  }
  uiText(c,24,42,screen==2?"实时路由":"今日概览",uiFont22,green);
  if(screen==3) {
    bool known=batteryPresent && batteryPercent>=0 && batteryPercent<=100;
    uint16_t color=known && batteryPercent<=10 ? 0xfba8 : muted;
    c.drawRoundRect(306,27,27,15,2,color);
    c.fillRect(333,32,3,5,color);
    if(known) c.fillRect(309,30,(21*batteryPercent+50)/100,9,color);
    String label=known ? String(batteryPercent)+"%" : "--%";
    uiText(c,400-textWidth(label,uiFont18),42,label,uiFont18,color);
  }
  uiText(c,410,42,channelOK[screen==2?2:1]?"实时":"断开",uiFont18,muted);
  c.drawFastHLine(24,57,432,0x3186);
  if(screen==2) {
    auto r=data["route"]; auto targets=r["targets"].as<JsonArray>();
    String source=r["sources"][0]|"codex";
    icon(c,36,208,providerIcon(source),source!="claude");
    String name=source; if(name.length()) name.setCharAt(0,toupper(name[0]));
    centered(c,54,268,name,uiFont22);
    c.drawFastHLine(84,240,62,0x52aa);
    c.fillRoundRect(146,200,76,80,4,0x1924); c.drawRoundRect(146,200,76,80,4,green);
    icon(c,164,208,icon_magpie40); centered(c,184,268,"magpie",uiFont18,green);
    int i=0;
    for(JsonObject t:targets) {
      int y=routeY(i,targets.size()); c.drawLine(222,240,270,y,0x52aa);
      c.fillRoundRect(270,y-42,186,84,3,0x10c3); c.drawRoundRect(270,y-42,186,84,3,0x3186);
      String account=config.showIdentity?String(t["account"]|""):String(""); if(!account.length()) account="账号 0"+String(i+1);
      uiText(c,280,y-17,account,uiFont18,muted,166);
      uiText(c,280,y+7,value(t["model"]),uiFont18,0x5e7c,166);
      uiText(c,280,y+31,t["failed"]==true?"失败":t["done"]==true?"已结束":"正在响应",uiFont18,muted);
      i++;
    }
    if(!i) { c.drawFastHLine(222,240,48,0x52aa); centered(c,360,248,"等待请求",uiFont22,muted); }
    for(auto &f:flights) {
      const uint32_t now = millis();
      if (!route_flights::active(now, f.start)) continue;
      float t=route_flights::progress(now,f.start); int x1=84,x2=146,y2=240;
      if(f.kind=="attempt"||f.kind=="response") {
        x1=222; x2=270; int index=0;
        for(JsonObject a:targets) { if(f.target==(a["key"]|"")) break; index++; }
        y2=routeY(std::min(index,3),(int)targets.size());
      }
      bool returning=f.kind=="response"||f.kind=="result";
      if(returning) t=1-t;
      bird(c,x1+(x2-x1)*t,240+(y2-240)*t-18*sinf(t*liquid::kPi),returning);
    }
  } else {
    auto u=data["usage"];
    uiText(c,24,110,value(u["total"]),uiFont42);
    uiText(c,278,106,"Token · 含缓存",uiFont18,muted);
    uiText(c,24,140,"输入 "+value(u["input"])+"  输出 "+value(u["output"])+"  缓存 "+value(u["cache"]),uiFont18,muted,432);
    const char *keys[]={"calls","cost","cacheRate","speed"};
    const char *names[]={"请求","费用估算","缓存命中率","输出速度"};
    for(int i=0;i<4;i++) {
      int x=24+i%2*220,y=158+i/2*90;
      c.drawRoundRect(x,y,212,82,5,0x3186);
      if(i==1) {
        auto bounds=inkBounds(names[i],uiFont18);
        dottedCaption(c,x+12+bounds.left,y+25+(bounds.top+bounds.bottom)/2,names[i],"USD",uiFont18,muted,8);
      } else uiText(c,x+12,y+25,names[i],uiFont18,muted);
      uiText(c,x+12,y+53,value(u[keys[i]]),uiFont28);
      if(i==0) uiText(c,x+12,y+74,"成功 "+value(u["success"]),uiFont18,muted);
      if(i==1&&(u["unpriced"]|0)>0) uiText(c,x+12,y+74,value(u["unpriced"])+" 次未计价",uiFont18,muted,190);
      if(i==2) { c.fillRect(x+12,y+66,188,4,0x2104); String rate=value(u["cacheRate"]); c.fillRect(x+12,y+66,(int)(188*std::min(100.f,rate.toFloat())/100),4,green); }
      if(i==3) uiText(c,x+116,y+53,"token/秒",uiFont18,muted);
    }
    uiText(c,24,360,"供应商",uiFont22); uiText(c,350,360,"输入＋输出",uiFont18,muted);
    int i=0;
    for(JsonObject p:u["providers"].as<JsonArray>()) {
      int y=386+i*27; String name=value(p["name"]); if(name.length()) name.setCharAt(0,toupper(name[0]));
      uiText(c,24,y,name,uiFont18,muted,105);
      c.fillRect(136,y-10,200,6,0x2104); c.fillRect(136,y-10,200*(p["fraction"]|0.f),6,green);
      uiText(c,350,y,value(p["tokens"]),uiFont18,muted,106); if(++i==3) break;
    }
    if(!i) uiText(c,24,391,"暂无供应商用量",uiFont18,muted);
    uiText(c,24,466,u.isNull()?"数据不可用":"今日全部来源",uiFont18,muted);
  }
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
  adminCode = prefs.getString("admin", "");
  if (adminCode.length() != 12) {
    char code[13];
    snprintf(code, sizeof(code), "%08x%04x", esp_random(),
             unsigned(esp_random() & 0xffff));
    adminCode = code;
    prefs.putString("admin", adminCode);
  }
  DynamicJsonDocument saved(4096);
  String error;
  if (!deserializeJson(saved, prefs.getString("standalone", "{}")))
    parseConfig(saved.as<JsonObject>(), config, error);
  setupSSID =
      "Magpie-Screen-" + String((uint32_t)ESP.getEfuseMac() & 0xffff, HEX);
  requestedBrightness = config.power.usbBrightness;
  viscosity = prefs.getFloat("viscosity", .45f);
  for (auto &c : canvases)
    if (!c.begin()) {
      while (true)
        delay(1000);
    }
  if (!overlay.begin()) { while(true) delay(1000); }
  imuOK = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, 15, 14);
  if (imuOK) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_1000Hz,
                            SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
    // The gyroscope tells real turns apart from shakes; 1024 dps covers flicks.
    gyroOK = imu.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS,
                                 SensorQMI8658::GYR_ODR_896_8Hz,
                                 SensorQMI8658::LPF_MODE_0) == 0;
  }
  touch.setPins(40, 11);
  touchOK = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, 15, 14);
  pinMode(11, INPUT_PULLUP);
  pmuOK = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14);
  if (pmuOK) {
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    // Preserve charge current/voltage: battery capacity alone is not its charge
    // specification.
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  if (ssid.length())
    WiFi.begin(ssid.c_str(), password.c_str());
  MDNS.begin("magpie-screen");
  setupWeb();
  updatePalette();
  makePalette();
  freeBuffers = xQueueCreate(2, sizeof(int));
  transfers = xQueueCreate(2, sizeof(Transfer));
  for (int i = 0; i < 2; i++)
    xQueueSend(freeBuffers, &i, 0);
  drawingTask = xTaskGetCurrentTaskHandle();
  xTaskCreatePinnedToCore(transferTask, "display", 8192, nullptr, 2, nullptr,
                          0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  lastStep = lastSample = micros();
  nextFrame = lastStep + kFrameUs;
  lastReport = millis();
  lastActivity = millis();
  pollJobs = xQueueCreate(1, sizeof(PollJob *));
  pollResults = xQueueCreate(1, sizeof(PollResult *));
  xTaskCreatePinnedToCore(networkTask, "magpie-http", 12288, nullptr, 1,
                          nullptr, 0);
  if (!ssid.length())
    startSetup();
  serialStatus();
}
// Packed RGB565 lanes blend all channels with one multiply (5-bit alpha).
static inline uint16_t blendFast(uint32_t fg, uint32_t bg, uint32_t a) {
  fg = (fg | fg << 16) & 0x07e0f81f;
  bg = (bg | bg << 16) & 0x07e0f81f;
  bg += (fg - bg) * ((a + 4) >> 3) >> 5;
  bg &= 0x07e0f81f;
  return bg | bg >> 16;
}
// Accelerometer runs independently of rendering so the liquid sees shakes.
void sampleMotion(uint32_t now) {
  if (!imuOK || now - lastSample < (screenOff ? 33333u : 10000u))
    return;
  float dt = (now - lastSample) / 1000000.f, x, y, z;
  lastSample = now;
  if (!imu.getAccelerometer(x, y, z))
    return;
  // Gyroscope only runs while the liquid is on screen, to save power.
  if (gyroOK && screenOff == imu.isEnableGyroscope())
    screenOff ? imu.disableGyroscope() : imu.enableGyroscope();
  float gx, gy, gz, rate = NAN;
  if (gyroOK && !screenOff && imu.getGyroscope(gx, gy, gz))
    rate = gz * liquid::kPi / 180;
  static float bx = 0, by = 0, bz = 0;
  static bool have = false;
  float distance =
      (x - bx) * (x - bx) + (y - by) * (y - by) + (z - bz) * (z - bz);
  if (have && distance > .012f) {
    userActivity();
    lastMotion = millis();
  }
  if (!have || distance > .012f) {
    bx = x;
    by = y;
    bz = z;
    have = true;
  }
  motion.update(x, y, z, dt, rate);
  target = motion.target;
}
static int overlayRotation = -1;
static int16_t uiTop = 0, uiBottom = 0, uiLeft = 0, uiRight = 0;
void rebuildOverlay() {
  uiClear(overlay.getFramebuffer());
  uiRotation = rotation;
  overlay.setRotation(rotation);
  drawUI(overlay, 0);
  overlayRotation = rotation;
  uiTop = 480; uiBottom = 0; uiLeft = 480; uiRight = 0;
  for (int r = 0; r < 480; r++)
    if (uiLo[r] < uiHi[r]) {
      uiTop = std::min<int>(uiTop, r); uiBottom = r + 1;
      uiLeft = std::min(uiLeft, uiLo[r]); uiRight = std::max(uiRight, uiHi[r]);
    }
}
void loop() {
  io();
  if(adapter.clearIdleRoute(data.as<JsonObject>(),millis())) {flights.clear();dirty=true;}
  if (otaActive) {
    delay(1);
    return;
  }
  uint32_t now = micros();
  sampleMotion(now);
  static float orientationAngle=0;
  static uint32_t settledAt=0;
  if(fabsf(remainderf(target-orientationAngle,2*liquid::kPi))>.035f) {
    orientationAngle=target; settledAt=millis();
  }
  if(millis()-settledAt>320 && fabsf(remainderf(target-rotation*liquid::kPi/2,2*liquid::kPi))>52*liquid::kPi/180) {
    rotation=((int)lroundf(target/(liquid::kPi/2))%4+4)%4;
    orientation.turnTo(rotation,millis()); dirty=true;
  }
  if (screenOff) {
    orientation.snap(rotation);
    lastStep = now;
    delay(5);
    return;
  }
  bool known = !data["quota"]["remaining"].isNull();
  float wanted = known ? data["quota"]["remaining"].as<float>() / 100 : 0;
  // Physics advances in ~4 ms chunks between frames, not once per frame.
  if (now - lastStep >= 4000) {
    float dt = std::min((now - lastStep) / 1000000.f, .1f);
    lastStep = now;
    fill += (wanted - fill) * (1 - expf(-3 * dt));
    if (!known)
      fill = 0;
    fluid.step(motion, viscosity, fill, dt);
  }
  if ((int32_t)(now - nextFrame) < 0) {
    delay(1);
    return;
  }
  nextFrame += kFrameUs;
  if ((int32_t)(now - nextFrame) > (int32_t)kFrameUs)
    nextFrame = now + kFrameUs;
  bool moving = fabsf(fill - wanted) > .0002f || !fluid.settled(target);
  bool turning=orientation.active(millis());
  float contentAngle=orientation.sample(millis());
  bool flying = screen == 2 && !flights.empty();
  if (!dirty && !turning && !orientation.wasActive && !(screen == 1 && !setupMode && known && moving) &&
      !flying)
    return;
  orientation.wasActive=turning;
  // The UI layer is redrawn only when its content changes, not per liquid frame.
  if (dirty || flying || overlayRotation != rotation)
    rebuildOverlay();
  dirty = false;
  bool liquidVisible = screen == 1 && known && fill > .001f;
  static liquid::Shape shape;
  static int32_t surface[2801];
  int32_t surfaceTop = 0, surfaceBottom = 0;
  if (liquidVisible) {
    shape.update(fluid, fill);
    shape.raster(surface, 2801, surfaceTop, surfaceBottom);
  }
  int32_t dx = (int32_t)(shape.c * 65536), dy = (int32_t)(-shape.s * 65536);
  // During a turn the overlay (drawn at the final quarter) is resampled by the
  // remaining angle, limited to the UI bounding box.
  float delta = contentAngle - rotation * liquid::kPi / 2;
  float deltaCos = cosf(delta), deltaSin = sinf(delta);
  int32_t contentCos=lroundf(deltaCos*65536), contentSin=lroundf(deltaSin*65536);
  const uint16_t air = palette[0], deep = palette[kPalette - 1];
  const int32_t airLimit = -(kSurfaceIndex << 14),
                deepLimit = (kPalette - 1 - kSurfaceIndex) << 14;
  transferUs = 0;
  for (int sy = 0; sy < 480; sy += H) {
    int index;
    xQueueReceive(freeBuffers, &index, portMAX_DELAY);
    auto &c = canvases[index];
    uint16_t *pixels = c.getFramebuffer();
    if (liquidVisible) {
      for (int row = 0; row < H; row++) {
        float yy = sy + row - 239.5f;
        int32_t lx =
                    (int32_t)((-239.5f * shape.c + yy * shape.s + 350) * 65536),
                ly = (int32_t)((239.5f * shape.s + yy * shape.c) * 65536);
        uint16_t *dest = pixels + row * 480;
        int32_t lyEnd = ly + 479 * dy, lyMin = std::min(ly, lyEnd),
                lyMax = std::max(ly, lyEnd);
        if (lyMax - surfaceTop < airLimit || lyMin - surfaceBottom >= deepLimit) {
          uint32_t pair = lyMax - surfaceTop < airLimit ? air * 0x10001u : deep * 0x10001u;
          uint32_t *wide = (uint32_t *)dest;
          for (int x = 0; x < 240; x++) wide[x] = pair;
          continue;
        }
        for (int x = 0; x < 480; x++) {
          int v = ((ly - surface[lx >> 14]) >> 14) + kSurfaceIndex;
          *dest++ = palette[std::max(0, std::min(kPalette - 1, v))];
          lx += dx;
          ly += dy;
        }
      }
    } else
      c.fillScreen(0x0861);
    // Rotate only the content; liquid remains in the physical gravity frame.
    const uint16_t *ui=overlay.getFramebuffer();
    for(int row=0;row<H;row++) {
      int y=sy+row;
      uint16_t *dest=pixels+row*480;
      if(!turning) {
        const uint16_t *src=ui+y*480; const uint8_t *alpha=uiAlpha+y*480;
        for(int x=uiLo[y];x<uiHi[y];x++) {
          uint16_t color=src[x];
          if(color==0x0001) continue;
          uint8_t a=alpha[x];
          dest[x]=a==255?color:blendFast(color,dest[x],a);
        }
        continue;
      }
      if(uiTop>=uiBottom) continue;
      float ux0=239.5f-239.5f*deltaCos+(y-239.5f)*deltaSin;
      float uy0=239.5f+239.5f*deltaSin+(y-239.5f)*deltaCos;
      float from=0,to=479;
      auto limit=[&](float p,float step,float lo,float hi) {
        if(fabsf(step)<1e-6f) { if(p<lo||p>hi) to=-1; return; }
        float a=(lo-p)/step,b=(hi-p)/step;
        from=std::max(from,std::min(a,b)); to=std::min(to,std::max(a,b));
      };
      limit(ux0,deltaCos,uiLeft-1,uiRight);
      limit(uy0,-deltaSin,uiTop-1,uiBottom);
      int x0=(int)ceilf(from), x1=(int)floorf(to);
      int32_t rx=lroundf(ux0*65536)+x0*contentCos, ry=lroundf(uy0*65536)-x0*contentSin;
      for(int x=x0;x<=x1;x++,rx+=contentCos,ry-=contentSin) {
        int ux=(rx+32768)>>16,uy=(ry+32768)>>16;
        if(ux<0||ux>=480||uy<0||uy>=480)continue;
        int source=uy*480+ux;
        if(ui[source]!=0x0001) dest[x]=blendFast(ui[source],dest[x],uiAlpha[source]);
      }
    }
    if(screen==1&&!setupMode) for(int i=1;i<10;i++) c.drawFastHLine(458,48*i-sy,i%2?7:12,0x7bef);
    Transfer t{index, sy};
    xQueueSend(transfers, &t, portMAX_DELAY);
  }
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  size_t flightCount = flights.size();
  flights.erase(
      std::remove_if(flights.begin(), flights.end(),
                     [](const Flight &f) { return route_flights::finished(millis(), f.start, route_flights::lifetime(f.kind)); }),
      flights.end());
  if (flightCount != flights.size())
    dirty = true;
  frames++;
  totalFrames++;
}

void userActivity() {
  lastActivity = millis();
  if (screenOff) {
    screenOff = false;
    requestedOff = false;
    dirty = true;
    flights.clear();
    fluid.reset(target);
    fill = data["quota"]["remaining"].isNull()
               ? 0
               : data["quota"]["remaining"].as<float>() / 100;
  }
}
void startSetup() {
  userActivity();
  setupMode = true;
  setupStarted = millis();
  dirty = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(setupSSID.c_str(), adminCode.c_str());
}
void powerIO() {
  uint32_t now = millis();
  if (pmuOK && now - lastPowerRead >= 1000) {
    lastPowerRead = now;
    bool usb = pmu.isVbusIn(), battery = pmu.isBatteryConnect(),
         charge = pmu.isCharging();
    int pct = battery ? pmu.getBatteryPercent() : -1,
        mv = battery ? pmu.getBattVoltage() : 0;
    bool done =
        battery && pmu.getChargerStatus() == XPOWERS_AXP2101_CHG_DONE_STATE;
    if (usb != usbPower) {
      lastActivity = now;
      for (auto &t : due)
        t = 0;
      userActivity();
    }
    if (usb != usbPower || battery != batteryPresent || charge != charging ||
        pct != batteryPercent || done != chargeDone)
      dirty = true;
    usbPower = usb;
    batteryPresent = battery;
    charging = charge;
    chargeDone = done;
    batteryPercent = pct;
    batteryMv = mv;
    // Debounced low-voltage shutdown; percentage alone is not a calibrated
    // gauge.
    if (!usbPower && batteryPresent && mv > 0 && mv < 3300) {
      if (!lowSince)
        lowSince = now;
      if (now - lowSince >= 30000 && !otaActive)
        pmu.shutdown();
    } else
      lowSince = 0;
  }
  bool low = batteryPresent && batteryPercent >= 0 && batteryPercent <= 10;
  auto decision =
      power::evaluate(config.power, usbPower, low, now - lastActivity);
  pollInterval = decision.pollMs;
  if (decision.off != screenOff) {
    screenOff = decision.off;
    requestedOff = screenOff;
    dirty = true;
    flights.clear();
  }
  dimmed = decision.dim;
  requestedBrightness = decision.brightness;
  if (previewBrightness >= 0) {
    if ((int32_t)(previewUntil - now) > 0 &&
        ((previewSource == "usb") == usbPower))
      requestedBrightness = previewBrightness;
    else if ((int32_t)(previewUntil - now) <= 0)
      previewBrightness = -1;
  }
  if (noticeUntil && (int32_t)(now - noticeUntil) >= 0) {
    noticeUntil = 0;
    dirty = true;
  }
}
void networkIO() {
  PollResult *r = nullptr;
  if (xQueueReceive(pollResults, &r, 0) == pdTRUE) {
    networkBusy = false;
    if (r->revision == configRevision) {
      int i = r->channel;
      bool wasOK = channelOK[i];
      bool ok = r->code == 200 && !r->error.length();
      if(i==2 && r->mode.length())routeMode=r->mode;
      channelOK[i] = ok;
      channelError[i] = r->error;
      if (ok) {
        channelTime[i] = millis();
        bool changed = false;
        auto root = data.as<JsonObject>();
        if (root.isNull())
          root = data.to<JsonObject>();
        const char *section = i==0 ? "quota" : i==1 ? "usage" : "route";
        String before = jsonText(root[section]);
        if (i == 0) {
          float old = number(data["quota"]["remaining"]);
          bool base = adapter.baseline[0];
          changed = adapter.quotas(r->body.as<JsonObjectConst>(), config, root);
          lastState = millis();
          float next = number(data["quota"]["remaining"]);
          if (!base || screenOff)
            fill = isfinite(next) ? next / 100 : 0;
          if (changed && !screenOff && isfinite(next)) {
            if (isfinite(old))
              fluid.splash(liquid::clamp((next - old) * 8, -160, 320));
            quotaNotice = adapter.notice.length()       ? adapter.notice
                          : isfinite(old) && next > old ? "QUOTA RESTORED"
                          : next < old                  ? "QUOTA UPDATED"
                                                        : "";
            if (quotaNotice.length())
              noticeUntil = millis() + 5000;
          }
        } else if (i == 1)
          changed = adapter.usage(r->body.as<JsonObjectConst>(), root);
        else
          changed = adapter.trace(r->body.as<JsonObjectConst>(), config, root);
        if (changed && power::dataWakes(config.power, usbPower))
          userActivity();
        else if (changed && !screenOff)
          lastActivity = millis();
        if (i == 2 && !screenOff) {
          for (JsonObject e : data["route"]["events"].as<JsonArray>()) {
            if (flights.size() >= 12)
              break;
            String eventKey = e["key"] | "";
            // Event keys are pid:requestId:suffix; include both identity parts.
            int separator = eventKey.indexOf(':', eventKey.indexOf(':') + 1);
            String request = eventKey.substring(0, separator);
            uint32_t start = route_flights::nextStart(flights, request, millis());
            flights.push_back({eventKey, e["kind"] | "", e["target"] | "",
                               request, e["failed"] | false, start});
          }
        }
        if (i == 2)
          data["route"].remove("events");
        if (before != jsonText(root[section]))
          dirty = true;
        data.garbageCollect();
      } else
        adapter.baseline[i] = false;
      if (wasOK != ok)
        dirty = true;
    }
    delete r;
  }
  static bool connected = false;
  bool online = WiFi.status() == WL_CONNECTED;
  if (online != connected) {
    connected = online;
    dirty = true;
    if (online) {
      MDNS.end();
      MDNS.begin("magpie-screen");
      MDNS.addService("http", "tcp", 80);
    }
    adapter.reset();
    for (auto &t : due)
      t = 0;
    if (!online)
      for (int i = 0; i < 3; i++) {
        channelOK[i] = false;
        channelError[i] = "Wi-Fi disconnected";
      }
  }
  if (networkBusy || otaActive || wifiPending || !online ||
      config.url.isEmpty() || config.apiKey.isEmpty())
    return;
  uint32_t now = millis();
  for (int n = 0; n < 3; n++) {
    int i = (pollCursor + n) % 3;
    if (due[i] && (int32_t)(now - due[i]) < 0)
      continue;
    auto *job = new PollJob{config.url, config.apiKey, i, configRevision,
                            i == 2 && adapter.baseline[2] ? adapter.seq : 0, config.background, config.showIdentity};
    uint32_t interval = usbPower ? (i == 2   ? 1000
                                    : i == 1 ? 15000
                                             : 10000)
                                 : pollInterval;
    due[i] = now + interval;
    networkBusy = true;
    pollCursor = (i + 1) % 3;
    xQueueSend(pollJobs, &job, 0);
    break;
  }
}
bool saveStandalone(JsonDocument &d, String &error) {
  if (!d["config"].is<JsonObject>()) {
    error = "Config object required";
    return false;
  }
  DeviceConfig next = config;
  if (!parseConfig(d["config"], next, error))
    return false;
  int n = screen;
  float v = viscosity;
  if (!d["screen"].isNull()) {
    if (!d["screen"].is<int>() || (n = d["screen"].as<int>()) < 1 || n > 3) {
      error = "Invalid screen";
      return false;
    }
  }
  if (!d["viscosity"].isNull()) {
    if (!d["viscosity"].is<float>() ||
        !isfinite(v = d["viscosity"].as<float>()) || v < 0 || v > 1) {
      error = "Invalid viscosity";
      return false;
    }
  }
  DynamicJsonDocument saved(4096);
  configJSON(saved.to<JsonObject>(), next, true);
  String raw;
  serializeJson(saved, raw);
  if (prefs.putString("standalone", raw) != raw.length()) {
    error = "Could not save configuration";
    return false;
  }
  bool source = next.url != config.url || next.apiKey != config.apiKey;
  bool select = next.provider != config.provider ||
                next.accountId != config.accountId ||
                next.windowId != config.windowId;
  bool routing = next.background != config.background || next.showIdentity != config.showIdentity;
  config = next;
  viscosity = v;
  if (prefs.getFloat("viscosity", .45f) != v)
    prefs.putFloat("viscosity", v);
  selectScreen(n);
  updatePalette();
  makePalette();
  if (source || select || routing) {
    configRevision++;
    adapter.reset();
    flights.clear();
    for (auto &t : due)
      t = 0;
    if (source) {
      data.clear();
      adapter.accounts.clear();
      lastState = 0;
      for (int i = 0; i < 3; i++) {
        channelOK[i] = false;
        channelTime[i] = 0;
        channelError[i] = "";
      }
    } else if (select) {
      data.remove("quota");
      channelOK[0] = false;
      lastState = 0;
    }
    if (routing)
      data.remove("route");
  }
  previewBrightness = -1;
  dirty = true;
  return true;
}
void setupStandaloneWeb() {
  auto page = []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html; charset=utf-8", CONFIG_PAGE);
  };
  web.on("/", HTTP_GET, []() {
    web.sendHeader("Location", "/config");
    web.send(302);
  });
  web.on("/config", HTTP_GET, page);
  web.on("/api/status", HTTP_GET, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    StaticJsonDocument<2048> d;
    status(d.to<JsonObject>());
    String body;
    serializeJson(d, body);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", body);
  });
  web.on("/api/config", HTTP_GET, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DynamicJsonDocument d(40000);
    configJSON(d.createNestedObject("config"), config);
    auto accounts=d.createNestedArray("accounts");
    for(JsonObject a:adapter.accounts.as<JsonArray>())accounts.add(a);
    d["ssid"] = ssid;
    status(d.createNestedObject("status"));
    String body;
    serializeJson(d, body);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", body);
  });
  web.on("/api/config", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DynamicJsonDocument d(4096);
    String error;
    bool ok = !deserializeJson(d, web.arg("plain")) && saveStandalone(d, error);
    StaticJsonDocument<256> reply;
    reply["ok"] = ok;
    reply["error"] = error;
    String body;
    serializeJson(reply, body);
    web.send(ok ? 200 : 400, "application/json", body);
  });
  web.on("/api/wifi", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    StaticJsonDocument<512> d;
    String error;
    bool ok = !deserializeJson(d, web.arg("plain"));
    d["op"] = "wifi";
    ok = ok && command(d, false, error);
    StaticJsonDocument<256> reply;
    reply["ok"] = ok;
    reply["error"] = error;
    String body;
    serializeJson(reply, body);
    web.send(ok ? 200 : 400, "application/json", body);
  });
  web.on("/api/preview", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    StaticJsonDocument<256> d;
    bool ok = !deserializeJson(d, web.arg("plain"));
    String source = d["source"] | "";
    int b = d["brightness"] | 0;
    ok = ok && d["brightness"].is<int>() && b >= 10 && b <= 255 &&
         (source == "usb" || source == "battery");
    if (ok) {
      userActivity();
      previewSource = source;
      previewBrightness = b;
      previewUntil = millis() + 5000;
    }
    web.send(ok ? 200 : 400, "application/json",
             ok ? "{\"ok\":true}" : "{\"error\":\"Invalid brightness\"}");
  });
  web.on("/api/wake", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    userActivity();
    web.send(200, "application/json", "{\"ok\":true}");
  });
  web.on("/api/refresh", HTTP_POST, []() {
    if (!authorized()) {
      web.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    for (auto &t : due)
      t = 0;
    web.send(200, "application/json", "{\"ok\":true}");
  });
}
