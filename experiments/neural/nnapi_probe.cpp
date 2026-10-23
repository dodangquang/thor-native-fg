#include <android/NeuralNetworks.h>
#include <cstdio>
#include <cstdint>

int main(){
 uint32_t count=0;int result=ANeuralNetworks_getDeviceCount(&count);
 printf("NNAPI_DEVICES result=%d count=%u\n",result,count);
 for(uint32_t i=0;i<count;i++){
  ANeuralNetworksDevice* device=nullptr;const char *name="",*version="";int32_t type=0;int64_t level=0;
  if(ANeuralNetworks_getDevice(i,&device))continue;
  ANeuralNetworksDevice_getName(device,&name);ANeuralNetworksDevice_getVersion(device,&version);
  ANeuralNetworksDevice_getType(device,&type);ANeuralNetworksDevice_getFeatureLevel(device,&level);
  printf("NNAPI_DEVICE index=%u type=%d feature=%lld name=%s version=%s\n",i,type,(long long)level,name,version);
 }
 return result?1:0;
}
