#pragma once
#include "standalone_data.h"
#include <HTTPClient.h>
struct PsramAllocator {
  void *allocate(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void deallocate(void *p) { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) {
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};
using NetworkDocument = BasicJsonDocument<PsramAllocator>;
struct PollJob {
  String url, key;
  int channel;
  uint32_t revision, after;
  bool background;
  bool showIdentity;
};
struct PollResult {
  int channel, code = 0;
  uint32_t revision;
  NetworkDocument body{262144};
  String error, mode;
};
QueueHandle_t pollJobs, pollResults;
inline void makeFilter(int channel, JsonDocument &d, bool showIdentity = false) {
  if (channel == 0) {
    auto a = d["data"].createNestedObject();
    for (const char *k :
         {"provider", "name", "user", "kind", "error", "readAt"})
      a[k] = true;
    auto w = a["windows"].createNestedObject();
    for (const char *k : {"name", "used", "remaining", "error", "resetsAt"})
      w[k] = true;
    a["resetsUsed"] = true;
  } else if (channel == 1) {
    for (const char *k :
         {"calls", "errors", "input", "output", "cache_read", "cache_write",
          "cost", "unpriced", "decode_ms", "decode_out", "decode_n", "floor_ms",
          "floor_out", "floor_n"})
      d[k] = true;
    auto m = d["models"].createNestedObject();
    for (const char *k : {"provider", "input", "output"})
      m[k] = true;
  } else {
    for (const char *k : {"pid", "seq", "ids"})
      d[k] = true;
    auto r = d["routes"].createNestedObject();
    for (const char *k :
         {"id", "agent", "kind", "provider", "model", "done", "status", "error"})
      r[k] = true;
    for (const char *list : {"order", "left"}) {
      auto s = r[list].createNestedObject();
      for (const char *k : {"id", "provider", "model"})
        s[k] = true;
      if(showIdentity) s["who"]=true;
    }
    auto t = r["tries"].createNestedObject();
    for (const char *k : {"id", "model", "done", "status", "fail", "ttft",
                          "firstText", "firstReasoning"})
      t[k] = true;
  }
}

inline bool fetchMagpie(const PollJob &job, const String &path, JsonDocument &body,
                        JsonDocument &filter, int &code, String &error) {
  WiFiClient client; HTTPClient http;
  http.useHTTP10(true);http.setConnectTimeout(3000);
  http.setTimeout(job.channel==0?15000:5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,job.url+path)){error="Connection failed";return false;}
  http.addHeader("Authorization","Bearer "+job.key);code=http.GET();
  error="";
  if(code==200) {
    auto e=deserializeJson(body,http.getStream(),DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16));
    if(e)error=String("JSON: ")+e.c_str();
  } else error="HTTP "+String(code);
  http.end();return error.isEmpty();
}
inline String urlEncode(const String &s) {
  String out;const char *hex="0123456789ABCDEF";
  for(unsigned char c:s) {
    if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=(char)c;
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
struct SessionPoller {
  struct UsageAccount {
    String provider, model, name;
  };
  std::vector<String> sessions;
  std::map<String,uint32_t> observed;
  std::map<String,UsageAccount> accountByRequest;
  uint32_t discovered=0, generation=1, sequence=0;
  bool read(const PollJob &job, PollResult &result) {
    if(!discovered || millis()-discovered>=10000) {
      StaticJsonDocument<512> filter;auto row=filter["rows"].createNestedObject();
      row["session"]=true;row["kind"]=true;row["route_id"]=true;
      if(job.showIdentity){row["providerAccount"]=true;row["provider"]=true;row["model"]=true;}
      NetworkDocument discovery(65536);
      if(!fetchMagpie(job,"/v1/magpie/usage/requests?period=today&limit=100",discovery,filter,result.code,result.error))return false;
      if(!discovery["rows"].is<JsonArray>()){result.error="Unexpected session list";return false;}
      std::vector<String> next;
      accountByRequest.clear();
      for(JsonObject r:discovery["rows"].as<JsonArray>()) {
        String id=r["session"] | "";
        if(id.isEmpty()||id.length()>128 || !(r["route_id"] | uint64_t(0)) || (!job.background && strlen(r["kind"] | "")))continue;
        if(job.showIdentity) accountByRequest[id+":"+jsonText(r["route_id"])]=
          {String(r["provider"]|""),String(r["model"]|""),String(r["providerAccount"]|"")};
        if(std::find(next.begin(),next.end(),id)==next.end())next.push_back(id);
        if(next.size()==4)break;
      }
      for(auto it=observed.begin();it!=observed.end();) {
        if(std::find(next.begin(),next.end(),it->first)==next.end())it=observed.erase(it);else ++it;
      }
      sessions=next;discovered=millis();
    }
    result.body.clear();auto ids=result.body.createNestedArray("ids"), routes=result.body.createNestedArray("routes");
    bool restarted=false;
    for(const auto &session:sessions) {
      StaticJsonDocument<2048> filter;filter["seq"]=true;
      auto f=filter.createNestedObject("route");
      for(const char *k:{"id","agent","kind","model","done","status","error","ttft_ms"})f[k]=true;
      auto t=f["tries"].createNestedObject();
      for(const char *k:{"model","done","status","fail"})t[k]=true;
      DynamicJsonDocument response(12288);
      if(!fetchMagpie(job,"/v1/magpie/route?session="+urlEncode(session),response,filter,result.code,result.error))return false;
      if(!response["seq"].is<uint32_t>() || (!response["route"].isNull() && !response["route"].is<JsonObject>())){result.error="Unexpected session route";return false;}
      uint32_t seq=response["seq"] | 0u;
      bool first=observed.find(session)==observed.end();
      if(!first && seq<observed[session])restarted=true;
      observed[session]=seq;sequence=std::max(sequence,seq);
      JsonObject r=response["route"];
      if(r.isNull())continue;
      ids.add(r["id"]);
      auto dest=routes.createNestedObject();dest.set(r);dest["_baseline"]=first;
      auto order=dest.createNestedArray("order");
      int attempt=0;
      for(JsonObject tr:dest["tries"].as<JsonArray>()) {
        String full=tr["model"] | "";int slash=full.indexOf('/');
        String provider=slash<0?"unknown":full.substring(0,slash),model=slash<0?full:full.substring(slash+1);
        tr["id"]=full;tr["model"]=model;
        auto seat=order.createNestedObject();seat["id"]=full;seat["provider"]=provider;seat["model"]=model;
        // Usage identifies only the final attempt of this exact completed request.
        if(job.showIdentity && dest["done"]==true && ++attempt==(int)dest["tries"].size()) {
          auto found=accountByRequest.find(session+":"+jsonText(dest["id"]));
          if(found!=accountByRequest.end()) {
            const auto &account=found->second;
            // Route prefixes can be API aliases (openai) for usage providers (codex).
            if(account.provider==provider || (account.model.length() &&
               (account.model==model || account.model==full))) seat["who"]=account.name;
          }
        }
        dest["provider"]=provider;
      }
      auto tries=dest["tries"].as<JsonArray>();
      if(tries.size() && (dest["ttft_ms"] | 0)>0)tries[tries.size()-1]["ttft"]=dest["ttft_ms"];
    }
    if(restarted)generation++;
    result.body["pid"]=generation;result.body["seq"]=sequence;
    result.code=200;result.error="";result.mode="sessions";return true;
  }
};
inline void networkTask(void *) {
  bool sessionMode=false;uint32_t revision=0;SessionPoller sessions;
  for(;;) {
    PollJob *job;xQueueReceive(pollJobs,&job,portMAX_DELAY);
    auto *r=new PollResult;r->channel=job->channel;r->revision=job->revision;
    if(revision!=job->revision) {revision=job->revision;sessionMode=false;sessions=SessionPoller();}
    if(job->channel==2 && sessionMode)sessions.read(*job,*r);
    else {
      StaticJsonDocument<4096> filter;makeFilter(job->channel,filter,job->showIdentity);
      const char *paths[]={"/v1/magpie/quotas","/v1/magpie/usage?period=today","/v1/magpie/trace?after="};
      String path=paths[job->channel];if(job->channel==2)path+=String(job->after);
      bool ok=fetchMagpie(*job,path,r->body,filter,r->code,r->error);
      if(job->channel==2 && r->code==403) {
        sessionMode=true;sessions.read(*job,*r);
      } else if(ok && ((job->channel==0 && !r->body["data"].is<JsonArray>()) ||
                      (job->channel==1 && (!r->body["calls"].is<double>() || !r->body["models"].is<JsonArray>())) ||
                      (job->channel==2 && (!r->body["routes"].is<JsonArray>() || !r->body["ids"].is<JsonArray>() || !r->body["seq"].is<uint32_t>()))))r->error="Unexpected Magpie response";
      else if(job->channel==2)r->mode="trace";
    }
    delete job;xQueueSend(pollResults,&r,portMAX_DELAY);
  }
}
