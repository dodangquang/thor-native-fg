#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace thorAuto {
constexpr uint32_t magic=0x54464732;
constexpr size_t maxConfigBytes=65536;
constexpr const char* configPath="/data/adb/thor-native-fg/auto.conf";
struct Request {uint32_t version=magic,uid=0;char package[256]{};};
struct Response {uint32_t version=magic,selected=0,sr=0;char provider[512]{};};
struct App {uint32_t uid;std::string package;};
struct Config {bool enabled=false,sr=false;std::string provider;std::vector<App> apps;};
inline bool packageValid(const std::string& name){
 if(name.empty()||name.size()>=256||name.front()=='.'||name.back()=='.'||name.find("..")!=std::string::npos)return false;
 for(char c:name)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'))return false;
 return name.find('.')!=std::string::npos;
}
inline bool uidValid(uint32_t uid){return uid%100000>=10000&&uid%100000<90000;}
inline bool providerValid(const std::string& path){
 return path.size()<512&&path.compare(0,10,"/data/app/")==0&&path.size()>10&&path.find("/../")==std::string::npos&&path.find(':')==std::string::npos&&path.find_first_of("\r\n\t '") == std::string::npos&&path.size()>=10&&path.compare(path.size()-10,10,"/lib/arm64")==0;
}
inline bool parse(const std::string& text,Config& out){
 out={};if(text.size()>maxConfigBytes)return false;
 Config next;std::istringstream input(text);std::string line;bool enabled=false,sr=false,provider=false;
 if(!std::getline(input,line)||line!="THOR_AUTO_V1")return false;
 while(std::getline(input,line)){
  if(line.empty())continue;
  if(line=="enabled=0"||line=="enabled=1"){if(enabled)return false;enabled=true;next.enabled=line.back()=='1';}
  else if(line=="sr=0"||line=="sr=1"){if(sr)return false;sr=true;next.sr=line.back()=='1';}
  else if(line.compare(0,9,"provider=")==0){if(provider)return false;provider=true;next.provider=line.substr(9);if(!providerValid(next.provider))return false;}
  else if(line.compare(0,4,"app=")==0){
   auto colon=line.find(':',4);if(colon==std::string::npos||colon==4||colon>11||next.apps.size()>=512)return false;
   uint64_t uid=0;for(size_t i=4;i<colon;i++){if(line[i]<'0'||line[i]>'9')return false;uid=uid*10+(line[i]-'0');}
   std::string package=line.substr(colon+1);if(uid>UINT32_MAX||!uidValid(uid)||!packageValid(package))return false;
   for(const auto& app:next.apps)if(app.package==package)return false;
   next.apps.push_back({uint32_t(uid),package});
  }else return false;
 }
 if(!enabled||!sr||!provider)return false;out=std::move(next);return true;
}
inline Response select(const Config& config,const Request& request){
 Response result{};
 if(request.version!=magic||!config.enabled||!uidValid(request.uid)||!memchr(request.package,0,sizeof(request.package))||!packageValid(request.package))return result;
 for(const auto& app:config.apps)if(app.uid==request.uid&&app.package==request.package){result.selected=1;result.sr=config.sr;std::strncpy(result.provider,config.provider.c_str(),sizeof(result.provider)-1);break;}
 return result;
}
inline std::string withLayer(const std::string& layers){
 std::istringstream names(layers);std::string name;while(std::getline(names,name,':'))if(name=="VK_LAYER_THOR_native_fg")return layers;
 return layers.empty()?"VK_LAYER_THOR_native_fg":"VK_LAYER_THOR_native_fg:"+layers;
}
}
