// Uses the unmodified public Zygisk API (ISC-style license in third_party/zygisk.hpp).
#include <sys/types.h>
#include "../third_party/zygisk.hpp"
#include <android/log.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
class ThorLayerBootstrap : public zygisk::ModuleBase {
 zygisk::Api* api{};JNIEnv* env{};bool selected=false;
public:
 void onLoad(zygisk::Api* a,JNIEnv* e)override{api=a;env=e;char target[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.target",target);if(target[0])__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","module loaded uid=%d",getuid());}
 void preAppSpecialize(zygisk::AppSpecializeArgs* args)override{
  api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
  // Never change system services, isolated service UIDs, or denylisted processes.
  if(args->uid<10000 || args->uid%100000>=90000 || (api->getFlags()&zygisk::PROCESS_ON_DENYLIST))return;
  const char* name=env->GetStringUTFChars(args->nice_name,nullptr);if(!name)return;
  char package[256]{};snprintf(package,sizeof(package),"%s",name);env->ReleaseStringUTFChars(args->nice_name,name);if(char* colon=strchr(package,':'))*colon=0;
  // Read a root-controlled debug property rather than a file through zygote's
  // SELinux domain. Exactly one package is eligible, and the property clears
  // on reboot. Normal apps cannot write this Android debug property.
  char target[PROP_VALUE_MAX]{},active[PROP_VALUE_MAX]{};
  __system_property_get("debug.thorfg.target",target);__system_property_get("debug.thorfg.enabled",active);
  selected=active[0]=='1'&&target[0]&&strcmp(target,package)==0;
  if(selected)__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","target matched app_uid=%d",args->uid);
 }
 void postAppSpecialize(const zygisk::AppSpecializeArgs*)override{
  if(!selected)return;
  // Android GraphicsEnvironment checks PR_GET_DUMPABLE, not possession of su.
  // Scoped to explicitly selected applications; no SELinux/ro.debuggable changes.
  int result=prctl(PR_SET_DUMPABLE,1,0,0,0);
  __android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","selected process dumpable=%d result=%d; layer loading still needs package settings",prctl(PR_GET_DUMPABLE,0,0,0,0),result);
 }
};
REGISTER_ZYGISK_MODULE(ThorLayerBootstrap)
