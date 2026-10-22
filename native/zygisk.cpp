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
#include <cstdlib>
#include <sys/stat.h>
#include <sys/socket.h>
#include "auto_config.hpp"

static thorAuto::Response autoConfig;
static bool autoReady=false;
static std::string automaticLayers="VK_LAYER_THOR_native_fg";
static void (*originalLayerPaths)(JNIEnv*,jclass,jobject,jstring)=nullptr;
static void (*originalDebugLayers)(JNIEnv*,jclass,jstring)=nullptr;
static std::string javaString(JNIEnv* env,jstring value){
 if(!value)return {};const char* chars=env->GetStringUTFChars(value,nullptr);if(!chars)return {};
 std::string copy(chars);env->ReleaseStringUTFChars(value,chars);return copy;
}
static void autoDebugLayers(JNIEnv* env,jclass type,jstring layers){
 if(!originalDebugLayers)return;
 if(!autoReady){originalDebugLayers(env,type,layers);return;}
 automaticLayers=thorAuto::withLayer(javaString(env,layers));jstring joined=env->NewStringUTF(automaticLayers.c_str());
 if(joined){originalDebugLayers(env,type,joined);env->DeleteLocalRef(joined);}
}
static void autoLayerPaths(JNIEnv* env,jclass type,jobject loader,jstring paths){
 if(!originalLayerPaths)return;
 if(!autoReady){originalLayerPaths(env,type,loader,paths);return;}
 // GraphicsEnvironment calls this during application binding, before the game
 // initializes Vulkan. Only this selected child process receives these paths.
 auto value=javaString(env,paths);if(!value.empty())value+=':';value+=autoConfig.provider;
 jstring joined=env->NewStringUTF(value.c_str());jstring layer=env->NewStringUTF(automaticLayers.c_str());
 if(joined&&layer){originalDebugLayers(env,type,layer);originalLayerPaths(env,type,loader,joined);__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","AUTO_LAYER_PATH ready version=2");}
 else if(!env->ExceptionCheck())originalLayerPaths(env,type,loader,paths);
 if(joined)env->DeleteLocalRef(joined);if(layer)env->DeleteLocalRef(layer);
}
static void wireTimeout(int fd){timeval wait{0,150000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof(wait));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&wait,sizeof(wait));}
static void autoCompanion(int client){
 wireTimeout(client);thorAuto::Request request{};thorAuto::Response response{};
 if(recv(client,&request,sizeof(request),MSG_WAITALL)!=sizeof(request))return;
 int fd=open(thorAuto::configPath,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat info{};
 if(fd>=0){
  if(fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==0&&!(info.st_mode&022)&&info.st_size>0&&uint64_t(info.st_size)<=thorAuto::maxConfigBytes){
   std::string text(size_t(info.st_size),'\0');size_t done=0;
   while(done<text.size()){ssize_t n=read(fd,text.data()+done,text.size()-done);if(n<=0)break;done+=size_t(n);}
   thorAuto::Config parsed;if(done==text.size()&&thorAuto::parse(text,parsed))response=thorAuto::select(parsed,request);
  }close(fd);
 }
 send(client,&response,sizeof(response),MSG_NOSIGNAL);
}
class ThorLayerBootstrap : public zygisk::ModuleBase {
 zygisk::Api* api{};JNIEnv* env{};bool selected=false;
public:
 void onLoad(zygisk::Api* a,JNIEnv* e)override{api=a;env=e;char target[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.target",target);if(target[0])__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","module loaded uid=%d",getuid());}
 void preAppSpecialize(zygisk::AppSpecializeArgs* args)override{
  // Never change system services, isolated service UIDs, or denylisted processes.
  if(!thorAuto::uidValid(args->uid) || (api->getFlags()&zygisk::PROCESS_ON_DENYLIST)){api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
  const char* name=env->GetStringUTFChars(args->nice_name,nullptr);if(!name){api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
  char package[256]{};snprintf(package,sizeof(package),"%s",name);env->ReleaseStringUTFChars(args->nice_name,name);if(char* colon=strchr(package,':'))*colon=0;
  char target[PROP_VALUE_MAX]{},active[PROP_VALUE_MAX]{};
  __system_property_get("debug.thorfg.target",target);__system_property_get("debug.thorfg.enabled",active);
  selected=active[0]=='1'&&target[0]&&strcmp(target,package)==0;
  char automatic[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.auto",automatic);
  if(automatic[0]=='1'){
   int fd=api->connectCompanion();
   if(fd>=0){wireTimeout(fd);thorAuto::Request request{};request.uid=args->uid;snprintf(request.package,sizeof(request.package),"%s",package);
    thorAuto::Response response{};
    if(send(fd,&request,sizeof(request),MSG_NOSIGNAL)==sizeof(request)&&recv(fd,&response,sizeof(response),MSG_WAITALL)==sizeof(response)&&response.version==thorAuto::magic&&response.selected==1&&memchr(response.provider,0,sizeof(response.provider))&&thorAuto::providerValid(response.provider))autoConfig=response;
    close(fd);
   }
  }
  if(autoConfig.selected){
   JNINativeMethod hooks[]={{const_cast<char*>("setLayerPaths"),const_cast<char*>("(Ljava/lang/ClassLoader;Ljava/lang/String;)V"),(void*)autoLayerPaths},{const_cast<char*>("setDebugLayers"),const_cast<char*>("(Ljava/lang/String;)V"),(void*)autoDebugLayers}};
   api->hookJniNativeMethods(env,"android/os/GraphicsEnvironment",hooks,2);
   originalLayerPaths=(decltype(originalLayerPaths))hooks[0].fnPtr;originalDebugLayers=(decltype(originalDebugLayers))hooks[1].fnPtr;
   autoReady=originalLayerPaths&&originalDebugLayers;
   // Hook addresses must remain mapped for the lifetime of this process.
   if(autoReady){selected=true;setenv("THOR_FG_AUTO_MATCH","1",1);__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","AUTO_SELECTED package=%s uid=%d version=2",package,args->uid);}
   else __android_log_print(ANDROID_LOG_ERROR,"ThorNativeBootstrap","AUTO_UNAVAILABLE GraphicsEnvironment JNI signatures differ; manual mode retained");
  }else api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
  if(selected)__android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","target matched app_uid=%d",args->uid);
 }
 void postAppSpecialize(const zygisk::AppSpecializeArgs*)override{
  if(!selected)return;
  // Android GraphicsEnvironment checks PR_GET_DUMPABLE, not possession of su.
  // Scoped to explicitly selected applications; no SELinux/ro.debuggable changes.
  int result=prctl(PR_SET_DUMPABLE,1,0,0,0);
  __android_log_print(ANDROID_LOG_INFO,"ThorNativeBootstrap","selected process dumpable=%d result=%d auto=%d version=2",prctl(PR_GET_DUMPABLE,0,0,0,0),result,autoReady);
 }
};
REGISTER_ZYGISK_MODULE(ThorLayerBootstrap)
REGISTER_ZYGISK_COMPANION(autoCompanion)
