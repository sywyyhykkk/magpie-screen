"""Exercise the actual quota adapter with disposable HTTP response storage.
Run with Python 3 and a C++17 compiler. Set CXX/SDKROOT if needed on macOS.
"""
import os, subprocess, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = (root/'firmware/src/standalone_data.h').read_text()
quotas = source[source.index('  bool quotas('):source.index('  bool usage(')]
code = r'''
#include <string>
#include <map>
#include <cmath>
#include <cstring>
#include <cassert>
using std::isfinite;
class String: public std::string {
public: using std::string::string;
String(const std::string &s):std::string(s){}
String(int n):std::string(std::to_string(n)){}
};
inline String operator+(const String&a,const String&b){return std::string(a)+std::string(b);}
inline String operator+(const String&a,const char*b){return std::string(a)+b;}
inline String operator+(const char*a,const String&b){return a+std::string(b);}
#include <ArduinoJson.h>
namespace ArduinoJson {template<> struct Converter<String> {
static String fromJson(JsonVariantConst v){return v.as<std::string>();}
static void toJson(const String &s,JsonVariant v){v.set(std::string(s));}
};}
struct DeviceConfig {String provider="codex",accountId,windowId;};
String jsonText(JsonVariantConst v){std::string s;serializeJson(v,s);return s;}
double number(JsonVariantConst v){return v.isNull()?NAN:v.as<double>();}
String accountIdentity(JsonObjectConst a){return a["provider"].as<String>();}
struct Adapter {
DynamicJsonDocument accounts{32768};
bool baseline[3]={};String selection,notice,quotaSignature,resetSignature;
''' + quotas + r'''
};
int main(){
Adapter adapter;DeviceConfig config;DynamicJsonDocument out(4096);
for(int i=0;i<200;i++) {
  {
    DynamicJsonDocument response(4096);
    deserializeJson(response,R"({"data":[{"provider":"codex","windows":[{"name":"7 days","remaining":19}]}]})");
    adapter.quotas(response.as<JsonObjectConst>(),config,out.to<JsonObject>());
    assert(!out["quota"]["window"].as<JsonString>().isLinked());
  }
  // Force another allocation to reuse released HTTP response storage.
  DynamicJsonDocument other(4096);other["payload"]=std::string(2000,'t');
  out.garbageCollect();
  assert(out["quota"]["window"].as<std::string>()=="7 days");
  assert(out["quota"]["remaining"].as<int>()==19);
}
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp);(path/'test.cpp').write_text(code)
    command=[os.environ.get('CXX','c++'),'-std=c++17','-fsanitize=address,undefined']
    if os.environ.get('SDKROOT'): command += ['-isysroot',os.environ['SDKROOT']]
    command += [str(path/'test.cpp'),'-I'+str(root/'firmware/.pio/libdeps/liquid/ArduinoJson/src'),'-o',str(path/'test')]
    subprocess.run(command,check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS: quota window owns its text across 200 HTTP response lifetimes')
