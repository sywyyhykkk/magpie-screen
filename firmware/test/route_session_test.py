"""Exercise real session discovery and route adapter with 64-bit Magpie IDs."""
import os, subprocess, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
network = (root/'firmware/src/standalone_network.h').read_text()
data = (root/'firmware/src/standalone_data.h').read_text()
code = r'''
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include "route_flights.h"
class String: public std::string {
public: using std::string::string;
String(const std::string &s):std::string(s){}
String(unsigned int n):std::string(std::to_string(n)){}
String(int n):std::string(std::to_string(n)){}
bool isEmpty() const {return empty();}
int indexOf(char c) const {auto p=find(c);return p==npos?-1:int(p);}
String substring(int a) const {return substr(a);}
String substring(int a,int b) const {return substr(a,b-a);}
};
inline String operator+(const String&a,const String&b){return std::string(a)+std::string(b);}
inline String operator+(const String&a,const char*b){return std::string(a)+b;}
inline String operator+(const char*a,const String&b){return a+std::string(b);}
#include <ArduinoJson.h>
namespace ArduinoJson {template<> struct Converter<String> {
static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
static String fromJson(JsonVariantConst v){return v.as<std::string>();}
static void toJson(const String &s,JsonVariant v){v.set(std::string(s));}
};}
using NetworkDocument=DynamicJsonDocument;
struct PollJob {bool background=false,showIdentity=false;};
struct PollResult {DynamicJsonDocument body{262144};int code=0;String error,mode;};
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
String urlEncode(const String &s){return s;}
String jsonText(JsonVariantConst v){std::string s;serializeJson(v,s);return s;}
int routeFetches=0;
uint64_t requestId=1792755369386ULL;
String routeModel="codex/gpt-test", usageModel="gpt-test";
bool requestDone=true, retried=false;
bool fetchMagpie(const PollJob &,const String &path,JsonDocument &body,JsonDocument &filter,int &code,String &error){
  DynamicJsonDocument fixture(4096);
  if(path.find("usage/requests")!=std::string::npos){
    auto rows=fixture.createNestedArray("rows");
    auto bg=rows.createNestedObject();bg["session"]="background";bg["route_id"]=requestId+1;bg["kind"]="title";
    auto row=rows.createNestedObject();row["session"]="normal";row["route_id"]=requestId;row["provider"]="codex";row["model"]=usageModel;row["providerAccount"]="sample@example.test";
  }else{
    routeFetches++;
    fixture["seq"]=23;
    auto r=fixture.createNestedObject("route");r["id"]=requestId;r["agent"]="codex";r["done"]=requestDone;r["status"]=200;
    auto tries=r.createNestedArray("tries");
    if(retried){auto t=tries.createNestedObject();t["model"]="claude/earlier-model";t["done"]=true;t["status"]=503;}
    auto t=tries.createNestedObject();t["model"]=routeModel;t["done"]=requestDone;t["status"]=200;
  }
  std::string raw;serializeJson(fixture,raw);
  auto e=deserializeJson(body,raw,DeserializationOption::Filter(filter));assert(!e);
  code=200;error="";return true;
}
''' + network[network.index('struct SessionPoller {'):network.index('inline void networkTask')] + r'''
struct DeviceConfig {bool background=false,showIdentity=false;};
struct Adapter {DynamicJsonDocument routes{98304};bool baseline[3]={};uint32_t pid=0,seq=0,routeActivityAt=0;
''' + data[data.index('  bool trace('):data.rindex('\n};')] + r'''
};
int main(){
  SessionPoller poller;PollJob job;PollResult result;Adapter adapter;DeviceConfig cfg;DynamicJsonDocument out(24576);
  assert(poller.read(job,result));
  assert(poller.sessions.size()==1 && poller.sessions[0]=="normal");
  assert(routeFetches==1);
  assert(result.body["routes"][0]["id"].as<uint64_t>()==requestId);
  assert(!adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>()));
  assert(out["route"]["targets"].size()==1);
  assert(out["route"]["events"].size()==0); // Initial session is a baseline.
  requestId++;clockMs+=1000;assert(poller.read(job,result));
  assert(adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>()));
  assert(out["route"]["events"].size()>0);
  assert(out["route"]["targets"][0]["model"].as<std::string>()=="gpt-test");
  assert(out["route"]["targets"][0]["account"].isNull());
  job.showIdentity=true;cfg.showIdentity=true;poller=SessionPoller();
  assert(poller.read(job,result));adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  assert(out["route"]["targets"][0]["account"].as<std::string>()=="sample@example.test");
  // A newer in-flight request must never inherit the last completed account.
  requestId++;clockMs+=1000;assert(poller.read(job,result));
  adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  assert(out["route"]["targets"][0]["account"].isNull());
  job.showIdentity=false;cfg.showIdentity=false;poller=SessionPoller();
  assert(poller.read(job,result));adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  assert(out["route"]["targets"][0]["account"].isNull());
  // Real Magpie routes use openai/model while usage identifies the codex provider.
  routeModel="openai/gpt-test";retried=true;
  job.showIdentity=true;cfg.showIdentity=true;
  SessionPoller aliasPoller;Adapter aliasAdapter;DynamicJsonDocument aliasOut(24576);
  assert(aliasPoller.read(job,result));
  aliasAdapter.trace(result.body.as<JsonObjectConst>(),cfg,aliasOut.to<JsonObject>());
  assert(aliasOut["route"]["targets"][0]["account"].isNull());
  assert(aliasOut["route"]["targets"][1]["account"].as<std::string>()=="sample@example.test");
  // Provider aliases must not attach usage from a different final model.
  usageModel="different-model";aliasPoller=SessionPoller();
  assert(aliasPoller.read(job,result));
  aliasAdapter.trace(result.body.as<JsonObjectConst>(),cfg,aliasOut.to<JsonObject>());
  assert(aliasOut["route"]["targets"][1]["account"].isNull());
  // Even with matching identity data, unfinished requests remain anonymous.
  usageModel="gpt-test";requestDone=false;aliasPoller=SessionPoller();
  assert(aliasPoller.read(job,result));
  aliasAdapter.trace(result.body.as<JsonObjectConst>(),cfg,aliasOut.to<JsonObject>());
  assert(aliasOut["route"]["targets"][1]["account"].isNull());
  routeModel="codex/gpt-test";requestDone=true;retried=false;
  job.showIdentity=false;cfg.showIdentity=false;
  clockMs+=9999;
  assert(!adapter.clearIdleRoute(out.as<JsonObject>(),clockMs));
  clockMs++;
  assert(adapter.clearIdleRoute(out.as<JsonObject>(),clockMs));
  assert(out["route"]["targets"].size()==0);
  // Identical polling must not resurrect expired completed routes.
  assert(poller.read(job,result));adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  assert(out["route"]["targets"].size()==0);
  // An unfinished request remains visible even past the idle deadline.
  result.body["routes"][0]["done"]=false;
  adapter.trace(result.body.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  clockMs+=10001;assert(!adapter.clearIdleRoute(out.as<JsonObject>(),clockMs));
  assert(out["route"]["targets"].size()==1);
  // Cache ordering and eviction must retain the newest IDs, all above UINT32_MAX.
  DynamicJsonDocument snapshot(98304);snapshot["pid"]=2;snapshot["seq"]=1;
  auto ids=snapshot.createNestedArray("ids"),routes=snapshot.createNestedArray("routes");
  for(int i=0;i<100;i++){
    uint64_t id=1792755369000ULL+i;ids.add(id);auto r=routes.createNestedObject();r["id"]=id;r["done"]=true;
    auto t=r["tries"].createNestedObject();t["id"]="seat";t["model"]=std::string("model-")+std::to_string(i);
  }
  adapter.trace(snapshot.as<JsonObjectConst>(),cfg,out.to<JsonObject>());
  assert(adapter.routes.size()==96);
  assert(adapter.routes["1792755369000"].isNull());
  assert(!adapter.routes["1792755369099"].isNull());
  assert(out["route"]["targets"][0]["model"].as<std::string>()=="model-99");
  // One poll with 16 completed requests must retain all 64 event legs, even
  // when the requests share four destinations. The old 12-event cap lost most.
  Adapter concurrent;DynamicJsonDocument concurrentOut(24576),batch(32768);
  batch["pid"]=3;batch["seq"]=1;
  batch.createNestedArray("ids");batch.createNestedArray("routes");
  concurrent.trace(batch.as<JsonObjectConst>(),cfg,concurrentOut.to<JsonObject>());
  batch["seq"]=2;
  for(int i=0;i<16;i++) {
    uint64_t id=1792755370000ULL+i;batch["ids"].add(id);
    auto r=batch["routes"].createNestedObject();r["id"]=id;r["done"]=true;r["status"]=200;
    auto t=r["tries"].createNestedObject();t["id"]=std::string("seat-")+std::to_string(i%4);
    t["model"]=std::string("model-")+std::to_string(i%4);t["done"]=true;t["status"]=200;
  }
  assert(concurrent.trace(batch.as<JsonObjectConst>(),cfg,concurrentOut.as<JsonObject>()));
  assert(!concurrentOut.overflowed());
  assert(concurrentOut["route"]["targets"].size()==4);
  assert(concurrentOut["route"]["events"].size()==64);
  for(int i=0;i<16;i++) {
    const auto prefix=std::string("3:")+std::to_string(1792755370000ULL+i)+":";
    int legs=0;
    for(JsonObject e:concurrentOut["route"]["events"].as<JsonArray>())
      if(e["key"].as<std::string>().find(prefix)==0) legs++;
    assert(legs==4);
  }
  concurrent.trace(batch.as<JsonObjectConst>(),cfg,concurrentOut.as<JsonObject>());
  assert(concurrentOut["route"]["events"].size()==0); // No replay on repeat polls.
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp);(path/'test.cpp').write_text(code)
    command=[os.environ.get('CXX','c++'),'-std=c++17','-fsanitize=address,undefined']
    if os.environ.get('SDKROOT'):command+=['-isysroot',os.environ['SDKROOT']]
    command += [str(path/'test.cpp'),'-I'+str(root/'firmware/src'),'-I'+str(root/'firmware/.pio/libdeps/liquid/ArduinoJson/src'),'-o',str(path/'test')]
    subprocess.run(command,check=True);subprocess.run([str(path/'test')],check=True)
print('PASS: 64-bit session discovery, baseline/new events, route sorting and bounded eviction')
