#include "auto_config.hpp"
#include <iostream>
#include <stdexcept>
int main(){
 int checks=0;auto check=[&](bool value,const char* label){checks++;if(!value)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';};
 std::string good="THOR_AUTO_V1\nenabled=1\nsr=1\nprovider=/data/app/~~test/com.thor.nativefg-abc/lib/arm64\napp=10234:com.droiddeck.launcher\napp=10235:org.ppsspp.ppsspp\n";
 thorAuto::Config config;check(thorAuto::parse(good,config),"valid root-owned automatic configuration parses");
 thorAuto::Request request{};request.uid=10234;strcpy(request.package,"com.droiddeck.launcher");
 auto selected=thorAuto::select(config,request);check(selected.selected==1&&selected.sr==1,"exact UID/package pair selects DroidDeck");
 request.uid++;check(thorAuto::select(config,request).selected==0,"same package with a different UID is not selected");request.uid--;
 strcpy(request.package,"com.droiddeck.launcher.other");check(!thorAuto::select(config,request).selected,"package prefix is not a match");
 strcpy(request.package,"org.ppsspp.ppsspp");request.uid=10235;check(thorAuto::select(config,request).selected,"second emulator does not need a global single-app target");
 config.enabled=false;check(!thorAuto::select(config,request).selected,"automatic master disable blocks new injection");config.enabled=true;
 request.version=0;check(!thorAuto::select(config,request).selected,"unknown companion protocol fails closed");request.version=thorAuto::magic;
 memset(request.package,'x',sizeof(request.package));check(!thorAuto::select(config,request).selected,"unterminated IPC package is rejected");
 check(!thorAuto::uidValid(1000)&&!thorAuto::uidValid(99001)&&thorAuto::uidValid(110234)&&!thorAuto::uidValid(101000),"system and isolated UIDs are excluded across Android users");
 auto bad=[&](std::string value,const char* label){thorAuto::Config out;check(!thorAuto::parse(value,out),label);};
 bad("THOR_AUTO_V2\n"+good,"unknown file version rejected");
 bad(good+"enabled=1\n","duplicate control fields rejected");
 bad(good+"app=10234:com.droiddeck.launcher\n","duplicate package rules rejected");
 bad(good+"app=1000:com.android.systemui\n","system UID cannot be enrolled");
 bad(good+"app=10299:com.foo;id\n","shell metacharacters cannot enter a package rule");
 bad(good+"app=99999999999:com.foo.bar\n","overflowing UID rejected");
 bad(good+"unexpected=1\n","unknown configuration keys rejected");
 bad(std::string(65537,'a'),"oversized configuration rejected");
 std::string traversal=good;traversal.replace(traversal.find("/data/app/"),10,"/data/app/../");bad(traversal,"provider traversal rejected");
 std::string injection=good;injection.insert(injection.find("/lib/arm64"),":/data/local/tmp");bad(injection,"extra provider search paths rejected");
 check(thorAuto::withLayer("")=="VK_LAYER_THOR_native_fg","empty layer list enables only Thor");
 check(thorAuto::withLayer("VK_LAYER_KHRONOS_validation")=="VK_LAYER_THOR_native_fg:VK_LAYER_KHRONOS_validation","existing diagnostic layers are preserved");
 check(thorAuto::withLayer("VK_LAYER_THOR_native_fg:VK_LAYER_KHRONOS_validation")=="VK_LAYER_THOR_native_fg:VK_LAYER_KHRONOS_validation","Thor is never injected twice");
 std::cout<<"RESULT checks="<<checks<<" failures=0\n";
}
