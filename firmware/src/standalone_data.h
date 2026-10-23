#pragma once
#include "standalone_config.h"
#include <ArduinoJson.h>
#include <map>
#include <cmath>
#include <algorithm>
#include <vector>
#include <mbedtls/sha256.h>
inline String jsonText(JsonVariantConst v) {
  String s;
  serializeJson(v, s);
  return s;
}
inline String compactNumber(double n) {
  if (!isfinite(n) || n < 0)
    return "--";
  if (n >= 1e9)
    return String(n / 1e9, 1) + "B";
  if (n >= 1e6)
    return String(n / 1e6, 1) + "M";
  if (n >= 1e3)
    return String(n / 1e3, 1) + "K";
  return String(n, 0);
}
inline double number(JsonVariantConst v) {
  return v.is<double>() && isfinite(v.as<double>()) && v.as<double>() >= 0
             ? v.as<double>()
             : NAN;
}
inline String percentage(double n) {
  return isfinite(n) ? String(n * 100, 1) + "%" : "--";
}
inline String accountIdentity(JsonObjectConst a) {
  StaticJsonDocument<1024> d;
  auto arr = d.to<JsonArray>();
  for (const char *k : {"provider", "name", "user", "kind"})
    arr.add(a[k] | "");
  String raw = jsonText(arr);
  unsigned char digest[32];
  mbedtls_sha256_ret((const unsigned char *)raw.c_str(), raw.length(), digest,
                     0);
  char id[25];
  for (int i = 0; i < 12; i++)
    sprintf(id + 2 * i, "%02x", digest[i]);
  return id;
}
struct DataAdapter {
  DynamicJsonDocument accounts{32768}, routes{98304};
  bool baseline[3] = {false, false, false};
  String quotaSignature, usageSignature, resetSignature, selection;
  String notice;
  uint32_t routeActivityAt = 0;
  uint32_t pid = 0, seq = 0;
  void reset() {
    for (auto &b : baseline)
      b = false;
    routes.clear();
    seq = pid = 0;
  }
  bool quotas(JsonObjectConst j, const DeviceConfig &cfg, JsonObject out) {
    auto list = accounts.to<JsonArray>();
    JsonObjectConst chosen;
    for (JsonObjectConst a : j["data"].as<JsonArrayConst>()) {
      if (list.size() >= 64)
        break;
      String id = accountIdentity(a);
      auto row = list.createNestedObject();
      row["id"] = id;
      row["provider"] = a["provider"];
      row["label"] = String(a["name"] | "") +
                     (a["user"].isNull() ? "" : " · " + a["user"].as<String>());
      auto windows = row.createNestedArray("windows");
      std::map<String, int> counts;
      for (JsonObjectConst w : a["windows"].as<JsonArrayConst>()) {
        String name = w["name"] | "";
        auto v = windows.createNestedObject();
        v["id"] = name + ":" + String(counts[name]++);
        v["name"] = name;
      }
      if (String(a["provider"] | "") == cfg.provider &&
          (cfg.accountId.length() ? cfg.accountId == id : chosen.isNull()))
        chosen = a;
    }
    JsonObjectConst window;
    String wid;
    std::map<String, int> counts;
    for (JsonObjectConst w : chosen["windows"].as<JsonArrayConst>()) {
      String name = w["name"] | "", id = name + ":" + String(counts[name]++);
      if (cfg.windowId.length() ? cfg.windowId == id : window.isNull()) {
        window = w;
        wid = id;
      }
    }
    auto q = out.createNestedObject("quota");
    q["provider"] = cfg.provider;
    q["remaining"] = nullptr;
    // Own the string: the HTTP response document is freed after this update.
    q["window"] = String(window["name"] | "");
    double n = number(window["remaining"]);
    if (window["remaining"].isNull())
      n = 100 - number(window["used"]);
    if (!chosen.isNull() && !window.isNull() && !strlen(chosen["error"] | "") &&
        !strlen(window["error"] | "") && isfinite(n) && n >= 0 && n <= 100)
      q["remaining"] = n;
    // Only business values and explicit reset records count as activity.
    String identity =
        chosen.isNull() ? cfg.provider : accountIdentity(chosen) + ":" + wid;
    String reset = jsonText(chosen["resetsUsed"]);
    String sig = identity + ":" + jsonText(q["remaining"]) + ":" + reset;
    bool changed =
        baseline[0] && identity == selection && sig != quotaSignature;
    notice =
        changed && reset != resetSignature && reset != "null" && reset != "[]"
            ? "RESET APPLIED"
            : "";
    quotaSignature = sig;
    resetSignature = reset;
    selection = identity;

    baseline[0] = true;
    return changed;
  }
  bool usage(JsonObjectConst j, JsonObject out) {
    String sig = jsonText(j);
    bool changed = baseline[1] && sig != usageSignature;
    usageSignature = sig;
    baseline[1] = true;
    auto u = out.createNestedObject("usage");
    double input = number(j["input"]), output = number(j["output"]),
           cr = number(j["cache_read"]), cw = number(j["cache_write"]);
    double calls = number(j["calls"]), errors = number(j["errors"]),
           cost = number(j["cost"]);
    u["total"] = compactNumber(input + output + cr + cw);
    u["input"] = compactNumber(input);
    u["output"] = compactNumber(output);
    u["cache"] = compactNumber(cr + cw);
    u["calls"] = compactNumber(calls);
    u["success"] =
        percentage(calls > 0 ? std::max(0.0, 1 - errors / calls) : NAN);
    u["cacheRate"] =
        percentage(input + cr + cw > 0 ? cr / (input + cr + cw) : NAN);
    u["cost"] = isfinite(cost) ? "$" + (cost >= 1000 ? compactNumber(cost)
                                                     : String(cost, 2))
                               : "--";
    u["unpriced"] = j["unpriced"];
    double measured =
        number(j["decode_ms"]) > 0 && number(j["decode_n"]) > 0
            ? number(j["decode_out"]) * 1000 / number(j["decode_ms"])
            : NAN;
    double floor = number(j["floor_ms"]) > 0 && number(j["floor_n"]) > 0
                       ? number(j["floor_out"]) * 1000 / number(j["floor_ms"])
                       : NAN;
    bool useFloor =
        isfinite(floor) &&
        (!isfinite(measured) || number(j["floor_n"]) > number(j["decode_n"]));
    u["speed"] =
        (useFloor ? ">=" : "") + compactNumber(useFloor ? floor : measured);
    std::map<String, double> sums;
    for (JsonObjectConst m : j["models"].as<JsonArrayConst>()) {
      String p = m["provider"] | "unknown";
      sums[p] += number(m["input"]) + number(m["output"]);
    }
    std::vector<std::pair<String, double>> sorted(sums.begin(), sums.end());
    std::sort(
        sorted.begin(), sorted.end(),
        [](const std::pair<String, double> &a,
           const std::pair<String, double> &b) { return a.second > b.second; });
    auto rows = u.createNestedArray("providers");
    double other = 0;
    for (size_t i = 0; i < sorted.size(); i++) {
      if (sorted.size() > 3 && i >= 2) {
        other += sorted[i].second;
        if (i + 1 < sorted.size())
          continue;
      }
      auto r = rows.createNestedObject();
      double n = sorted.size() > 3 && i >= 2 ? other : sorted[i].second;
      r["name"] =
          sorted.size() > 3 && i >= 2 ? String("OTHER") : sorted[i].first;
      r["tokens"] = compactNumber(n);
      r["fraction"] =
          (input + output) > 0 && isfinite(n) ? n / (input + output) : 0;
    }
    return changed;
  }
  bool trace(JsonObjectConst j, const DeviceConfig &cfg, JsonObject out) {
    uint32_t nextPid = j["pid"] | 0u, nextSeq = j["seq"] | 0u;
    if (nextPid != pid || nextSeq < seq) {
      routes.clear();
      baseline[2] = false;
    }
    pid = nextPid;
    seq = nextSeq;
    if (!routes.is<JsonObject>())
      routes.to<JsonObject>();
    auto result = out.createNestedObject("route");
    auto events = result.createNestedArray("events");
    // Keep only IDs still owned by the upstream snapshot.
    std::vector<String> remove;
    for (JsonPair r : routes.as<JsonObject>()) {
      bool present = false;
      for (JsonVariantConst id : j["ids"].as<JsonArrayConst>())
        if (id.as<String>() == r.key().c_str()) {
          present = true;
          break;
        }
      if (!present)
        remove.push_back(r.key().c_str());
    }
    for (auto &id : remove)
      routes.remove(id);
    bool suppress=false;
    auto push = [&](String id, const char *kind, String suffix, String dest,
                    bool failed = false) {
      if (!baseline[2] || suppress || events.size() >= 12)
        return;
      auto e = events.createNestedObject();
      e["key"] = String(pid) + ":" + id + ":" + suffix;
      e["kind"] = kind;
      e["target"] = dest;
      e["failed"] = failed;
    };
    bool changed = false;
    for (JsonObjectConst r : j["routes"].as<JsonArrayConst>()) {
      suppress = r["_baseline"] | false;
      String id = r["id"].as<String>();
      if (!cfg.background && strlen(r["kind"] | "")) {
        routes.remove(id);
        continue;
      }
      JsonObject old = routes[id];
      if (old.isNull() && routes.size() >= 96) { // Bounded latest-route cache.
        uint64_t lowest = UINT64_MAX;
        String oldest;
        for (JsonPair p : routes.as<JsonObject>()) {
          uint64_t n = p.value()["id"].as<uint64_t>();
          if (n < lowest) {
            lowest = n;
            oldest = p.key().c_str();
          }
        }
        if (r["id"].as<uint64_t>() < lowest)
          continue;
        routes.remove(oldest);
      }
      DynamicJsonDocument next(8192);
      auto nr = next.to<JsonObject>();
      nr["id"] = r["id"];
      nr["agent"] = String(r["agent"] | "codex");
      nr["done"] = r["done"] | false;
      nr["status"] = r["status"] | 0;
      nr["failed"] = strlen(r["error"] | "") > 0 || (r["status"] | 0) >= 400;
      auto targets = nr.createNestedArray("targets");
      if (old.isNull())
        push(id, "request", "request", "");
      int index = 0;
      for (JsonObjectConst t : r["tries"].as<JsonArrayConst>()) {
        if (index >= 8)
          break;
        String account;
        String provider = r["provider"] | "",
               model = t["model"] | (r["model"] | "");
        for (const char *list : {"order", "left"})
          for (JsonObjectConst seat : r[list].as<JsonArrayConst>())
            if (String(seat["id"] | "") == String(t["id"] | "")) {
              provider = seat["provider"] | provider;
              if(cfg.showIdentity) account=String(seat["who"] | "");
              if (!model.length())
                model = seat["model"] | "";
            }
        String dest = String(t["id"] | "") + ":" + provider + "/" + model;
        auto a = targets.createNestedObject();
        a["key"] = dest;
        a["provider"] = provider;
        if(cfg.showIdentity && account.length()) a["account"]=account;
        a["model"] = model;
        a["done"] = t["done"] | false;
        a["failed"] = strlen(t["fail"] | "") > 0 || (t["status"] | 0) >= 400;
        a["response"] = (t["ttft"] | 0) > 0 || (t["firstText"] | 0) > 0 ||
                        (t["firstReasoning"] | 0) > 0;
        auto before = old["targets"][index];
        bool same = dest == String(before["key"] | "");
        if (!same)
          push(id, "attempt", "attempt:" + String(index), dest);
        if ((a["done"] == true && (!same || before["done"] != true)) ||
            (a["done"] != true && a["response"] == true &&
             (!same || before["response"] != true)))
          push(id, "response",
               String(a["done"] == true ? "end:" : "response:") + String(index),
               dest, a["failed"]);
        index++;
      }
      if (nr["done"] == true && old["done"] != true)
        push(id, "result", "result", "", nr["failed"]);
      if (baseline[2] && !suppress && jsonText(old) != jsonText(nr))
        changed = true;
      if(jsonText(old)!=jsonText(nr)) routeActivityAt=millis();
      routes[id].set(nr);
    }
    std::vector<JsonObject> ordered;
    for (JsonPair p : routes.as<JsonObject>())
      ordered.push_back(p.value().as<JsonObject>());
    std::sort(ordered.begin(), ordered.end(), [](JsonObject a, JsonObject b) {
      bool ad = a["done"] | false, bd = b["done"] | false;
      return ad != bd ? !ad : a["id"].as<uint64_t>() > b["id"].as<uint64_t>();
    });
    result["state"] = ordered.empty()                ? "empty"
                      : ordered[0]["done"] != true   ? "pending"
                      : ordered[0]["failed"] == true ? "failure"
                                                     : "success";
    auto sources = result.createNestedArray("sources");
    if (!ordered.empty()) sources.add(String(ordered[0]["agent"] | "codex"));
    auto targets = result.createNestedArray("targets");
    std::vector<String> keys;
    for (auto r : ordered)
      for (JsonObject t : r["targets"].as<JsonArray>()) {
        String k = t["key"] | "";
        if (targets.size() >= 4 ||
            std::find(keys.begin(), keys.end(), k) != keys.end())
          continue;
        keys.push_back(k);
        targets.add(t);
      }
    clearIdleRoute(out,millis());
    routes.garbageCollect();
    baseline[2] = true;
    return changed;
  }
  bool clearIdleRoute(JsonObject out,uint32_t now) {
    if(uint32_t(now-routeActivityAt)<10000) return false;
    for(JsonPair p:routes.as<JsonObject>())
      if(p.value()["done"]!=true) return false;
    auto r=out["route"].as<JsonObject>();
    if(r.isNull() || r["targets"].size()==0) return false;
    r["targets"].clear();r["sources"].clear();r["events"].clear();r["state"]="empty";
    return true;
  }

};
