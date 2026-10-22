#include "engine.hpp"
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <unistd.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"ThorNativeLayer",__VA_ARGS__)
#define EXPORT extern "C" __attribute__((visibility("default")))
static const char* layerName="VK_LAYER_THOR_native_fg";
static void* key(const void* object){return object?*(void*const*)object:nullptr;}
struct Instance { VkInstance handle{};PFN_vkGetInstanceProcAddr gipa{}; };
struct Swap {
 VkSwapchainKHR handle{};VkExtent2D extent{};VkFormat format{};bool supported=false,disabled=false;
 std::vector<VkImage> images;std::vector<VkSemaphore> ready;
 std::unique_ptr<Engine> engine;VkCommandPool pool{};VkCommandBuffer cmd{};VkFence acquired{};
 uint32_t family=~0u;uint64_t sources=0,generated=0,skipped=0,cutSkipped=0,holdSkipped=0,flowSkipped=0;
 std::chrono::steady_clock::time_point metricsAt=std::chrono::steady_clock::now();
 uint64_t metricsSources=0,metricsGenerated=0;
};
struct Device {
 VkDevice handle{};VkPhysicalDevice physical{};std::shared_ptr<Instance> instance;Api a;
 VkPhysicalDeviceMemoryProperties memory{};std::vector<VkQueueFamilyProperties> families;
 PFN_vkGetDeviceProcAddr gdpa{};PFN_vkSetDeviceLoaderData setLoaderData{};
 std::unordered_map<VkSwapchainKHR,std::shared_ptr<Swap>> swaps;std::mutex mutex;
};
struct Queue { std::shared_ptr<Device> device;uint32_t family; };
static std::mutex globalMutex;
static std::unordered_map<void*,std::shared_ptr<Instance>> instances;
static std::unordered_map<void*,std::shared_ptr<Device>> devices;
static std::unordered_map<VkQueue,Queue> queues;
static std::shared_ptr<Device> deviceFor(VkDevice device){std::lock_guard<std::mutex> l(globalMutex);auto i=devices.find(key(device));return i==devices.end()?nullptr:i->second;}
static std::shared_ptr<Instance> instanceFor(const void* instance){std::lock_guard<std::mutex> l(globalMutex);auto i=instances.find(key(instance));return i==instances.end()?nullptr:i->second;}
static bool enabled(){char value[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.enabled",value);return strcmp(value,"1")==0;}
static void reportMetrics(Swap& s,uint32_t qualityFlags,double blockingMs){
 auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-s.metricsAt).count();
 if(seconds<1.0)return;
 // These are incoming and generated PRESENT REQUEST rates. Neither rate is a
 // scanout measurement. Keep dimensions in the protocol so the HUD cannot
 // mislabel the current same-size filter as active super resolution.
 LOG("THOR_METRICS v=1 pid=%d swap=%llu eligible=%d in_w=%u in_h=%u out_w=%u out_h=%u source_fps=%.2f generated_fps=%.2f sources=%llu generated=%llu cuts=%llu holds=%llu flow_skips=%llu flags=%u blocking_ms=%.3f",
  getpid(),(unsigned long long)(uintptr_t)s.handle,s.supported&&!s.disabled,
  s.extent.width,s.extent.height,s.extent.width,s.extent.height,
  (s.sources-s.metricsSources)/seconds,(s.generated-s.metricsGenerated)/seconds,
  (unsigned long long)s.sources,(unsigned long long)s.generated,
  (unsigned long long)s.cutSkipped,(unsigned long long)s.holdSkipped,(unsigned long long)s.flowSkipped,qualityFlags,blockingMs);
 s.metricsAt=now;s.metricsSources=s.sources;s.metricsGenerated=s.generated;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*);
static VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* alloc,VkInstance* out){
 auto* link=(VkLayerInstanceCreateInfo*)ci->pNext;
 while(link && !(link->sType==VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO&&link->function==VK_LAYER_LINK_INFO))link=(VkLayerInstanceCreateInfo*)link->pNext;
 if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
 auto get=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto fn=(PFN_vkCreateInstance)get(VK_NULL_HANDLE,"vkCreateInstance");link->u.pLayerInfo=link->u.pLayerInfo->pNext;
 VkResult r=fn(ci,alloc,out);if(r==VK_SUCCESS){auto i=std::make_shared<Instance>();i->handle=*out;i->gipa=get;std::lock_guard<std::mutex> l(globalMutex);instances[key(*out)]=i;LOG("INSTANCE loaded active=%d (not proof of frame generation)",enabled());}return r;
}
static VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks* alloc){auto data=instanceFor(i);if(!data)return;auto fn=(PFN_vkDestroyInstance)data->gipa(i,"vkDestroyInstance");{std::lock_guard<std::mutex> l(globalMutex);instances.erase(key(i));}fn(i,alloc);}
static VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* alloc,VkDevice* out){
 auto inst=instanceFor(physical);if(!inst)return VK_ERROR_INITIALIZATION_FAILED;
 auto* link=(VkLayerDeviceCreateInfo*)ci->pNext;PFN_vkSetDeviceLoaderData callback=nullptr;
 for(auto* p=link;p;p=(VkLayerDeviceCreateInfo*)p->pNext)if(p->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO&&p->function==VK_LOADER_DATA_CALLBACK)callback=p->u.pfnSetDeviceLoaderData;
 while(link && !(link->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO&&link->function==VK_LAYER_LINK_INFO))link=(VkLayerDeviceCreateInfo*)link->pNext;
 if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
 auto get=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;auto gipa=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
 auto fn=(PFN_vkCreateDevice)gipa(inst->handle,"vkCreateDevice");link->u.pLayerInfo=link->u.pLayerInfo->pNext;
 auto r=fn(physical,ci,alloc,out);if(r!=VK_SUCCESS)return r;
 auto d=std::make_shared<Device>();d->handle=*out;d->physical=physical;d->instance=inst;d->gdpa=get;d->setLoaderData=callback;d->a.load(*out,get);
 auto mem=(PFN_vkGetPhysicalDeviceMemoryProperties)inst->gipa(inst->handle,"vkGetPhysicalDeviceMemoryProperties");mem(physical,&d->memory);
 auto fam=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)inst->gipa(inst->handle,"vkGetPhysicalDeviceQueueFamilyProperties");uint32_t n=0;fam(physical,&n,nullptr);d->families.resize(n);fam(physical,&n,d->families.data());
 {std::lock_guard<std::mutex> l(globalMutex);devices[key(*out)]=d;}
 LOG("DEVICE registered");return r;
}
static void releaseSwap(Device& d,Swap& s){auto& a=d.a;a.DeviceWaitIdle(d.handle);s.engine.reset();for(auto sem:s.ready)if(sem)a.DestroySemaphore(d.handle,sem,nullptr);if(s.acquired)a.DestroyFence(d.handle,s.acquired,nullptr);if(s.pool)a.DestroyCommandPool(d.handle,s.pool,nullptr);s.pool={};s.acquired={};s.ready.clear();}
static VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks* alloc){auto d=deviceFor(device);if(!d)return;{std::lock_guard<std::mutex> l(d->mutex);for(auto& entry:d->swaps)releaseSwap(*d,*entry.second);d->swaps.clear();}{std::lock_guard<std::mutex> l(globalMutex);for(auto i=queues.begin();i!=queues.end();)if(i->second.device==d)i=queues.erase(i);else ++i;devices.erase(key(device));}d->a.DestroyDevice(device,alloc);}
static void trackQueue(std::shared_ptr<Device> d,VkQueue queue,uint32_t family){if(!queue)return;std::lock_guard<std::mutex> l(globalMutex);queues[queue]={d,family};}
static VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue* out){auto d=deviceFor(device);if(!d)return;d->a.GetDeviceQueue(device,family,index,out);trackQueue(d,*out,family);}
static VKAPI_ATTR void VKAPI_CALL getQueue2(VkDevice device,const VkDeviceQueueInfo2* ci,VkQueue* out){auto d=deviceFor(device);if(!d)return;d->a.GetDeviceQueue2(device,ci,out);trackQueue(d,*out,ci->queueFamilyIndex);}
static VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device,const VkSwapchainCreateInfoKHR* ci,const VkAllocationCallbacks* alloc,VkSwapchainKHR* out){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto capsFn=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)d->instance->gipa(d->instance->handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
 auto formatFn=(PFN_vkGetPhysicalDeviceFormatProperties)d->instance->gipa(d->instance->handle,"vkGetPhysicalDeviceFormatProperties");
 VkSurfaceCapabilitiesKHR caps{};VkFormatProperties fp{},rp{};formatFn(d->physical,ci->imageFormat,&fp);formatFn(d->physical,VK_FORMAT_R8G8B8A8_UNORM,&rp);
 auto changed=*ci;VkImageUsageFlags transfers=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 bool ok=capsFn&&capsFn(d->physical,ci->surface,&caps)==VK_SUCCESS&&(caps.supportedUsageFlags&transfers)==transfers;
 ok=ok && (fp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT)&&(rp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(rp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT);
 ok=ok && (ci->imageFormat==VK_FORMAT_R8G8B8A8_UNORM||ci->imageFormat==VK_FORMAT_B8G8R8A8_UNORM)&&ci->imageArrayLayers==1&&ci->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR&&!(ci->flags&VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)&&ci->imageExtent.width<=4096&&ci->imageExtent.height<=4096;
 // Only FIFO is supported by the simple proof-of-concept presenter.
 ok=ok && ci->presentMode==VK_PRESENT_MODE_FIFO_KHR;
 if(ok){changed.imageUsage|=transfers;changed.minImageCount=std::max(changed.minImageCount,caps.minImageCount+1);if(caps.maxImageCount&&changed.minImageCount>caps.maxImageCount)ok=false;}
 auto r=d->a.CreateSwapchainKHR(device,ok?&changed:ci,alloc,out);
 if(r!=VK_SUCCESS&&ok){ok=false;r=d->a.CreateSwapchainKHR(device,ci,alloc,out);}
 if(r==VK_SUCCESS){auto s=std::make_shared<Swap>();s->handle=*out;s->extent=ci->imageExtent;s->format=ci->imageFormat;s->supported=ok;
 uint32_t n=0;d->a.GetSwapchainImagesKHR(device,*out,&n,nullptr);s->images.resize(n);d->a.GetSwapchainImagesKHR(device,*out,&n,s->images.data());
 std::lock_guard<std::mutex> l(d->mutex);d->swaps[*out]=s;LOG("SWAPCHAIN %ux%u images=%u eligible=%d active=%d",s->extent.width,s->extent.height,n,ok,enabled());}return r;
}
static VKAPI_ATTR void VKAPI_CALL destroySwapchain(VkDevice device,VkSwapchainKHR handle,const VkAllocationCallbacks* alloc){auto d=deviceFor(device);if(!d)return;{std::lock_guard<std::mutex> l(d->mutex);auto i=d->swaps.find(handle);if(i!=d->swaps.end()){releaseSwap(*d,*i->second);d->swaps.erase(i);}}d->a.DestroySwapchainKHR(device,handle,alloc);}
static bool prepare(Device& d,Swap& s,uint32_t family){
 if(s.engine)return s.family==family;
 if(family>=d.families.size()||(d.families[family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))!=(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))return false;
 auto& a=d.a;s.family=family;
 try{
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(d.handle,&pi,nullptr,&s.pool),"CreateLayerPool");
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=s.pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(d.handle,&ca,&s.cmd),"AllocateLayerCommand");
  if(d.setLoaderData)requireVk(d.setLoaderData(d.handle,s.cmd),"SetLoaderData");
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(d.handle,&fi,nullptr,&s.acquired),"CreateAcquireFence");
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};s.ready.resize(s.images.size());for(auto& sem:s.ready)requireVk(a.CreateSemaphore(d.handle,&si,nullptr,&sem),"CreatePresentSemaphore");
  s.engine=std::make_unique<Engine>();s.engine->init(a,d.memory,s.extent,s.extent);LOG("PIPELINE ready original=%ux%u output=%ux%u; same-size SGSR filter, no render-resolution reduction",s.extent.width,s.extent.height,s.extent.width,s.extent.height);return true;
 }catch(const std::exception& e){LOG("DISABLED %s",e.what());s.disabled=true;releaseSwap(d,s);return false;}
}
static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue,const VkPresentInfoKHR* info){
 Queue q{};{std::lock_guard<std::mutex> l(globalMutex);auto it=queues.find(queue);if(it==queues.end())return VK_ERROR_INITIALIZATION_FAILED;q=it->second;}
 auto d=q.device;auto& a=d->a;std::lock_guard<std::mutex> lock(d->mutex);
 if(info->swapchainCount!=1||info->pNext||!enabled())return a.QueuePresentKHR(queue,info);
 auto found=d->swaps.find(info->pSwapchains[0]);if(found==d->swaps.end())return a.QueuePresentKHR(queue,info);auto& s=*found->second;++s.sources;
 if(!s.supported||s.disabled||!prepare(*d,s,q.family)){auto result=a.QueuePresentKHR(queue,info);reportMetrics(s,0,0);return result;}
 uint32_t original=info->pImageIndices[0],extra=~0u;if(original>=s.images.size())return a.QueuePresentKHR(queue,info);
 auto start=std::chrono::steady_clock::now();
 // Deliberately conservative synchronization for this experimental implementation.
 // This stalls the caller and is not a low-latency async proxy swapchain.
 auto idle=a.QueueWaitIdle(queue);if(idle!=VK_SUCCESS)return idle;
 a.ResetCommandPool(d->handle,s.pool,0);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 auto br=a.BeginCommandBuffer(s.cmd,&bi);if(br!=VK_SUCCESS)return br;
 s.engine->record(s.cmd,s.images[original],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 auto er=a.EndCommandBuffer(s.cmd);if(er!=VK_SUCCESS)return er;
 std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 VkSubmitInfo input{VK_STRUCTURE_TYPE_SUBMIT_INFO};input.waitSemaphoreCount=info->waitSemaphoreCount;input.pWaitSemaphores=info->pWaitSemaphores;input.pWaitDstStageMask=stages.data();input.commandBufferCount=1;input.pCommandBuffers=&s.cmd;
 auto sr=a.QueueSubmit(queue,1,&input,VK_NULL_HANDLE);if(sr!=VK_SUCCESS)return sr;
 idle=a.QueueWaitIdle(queue);if(idle!=VK_SUCCESS)return idle;
 const auto quality=s.engine->readQuality();bool pair=quality.allow!=0;
 if(quality.reasons&QUALITY_CUT)++s.cutSkipped;
 if(quality.reasons&QUALITY_HOLDOFF)++s.holdSkipped;
 if(quality.reasons&(QUALITY_LOW_FLOW|QUALITY_FAST_MOTION))++s.flowSkipped;
 // Acquire only after the GPU has accepted the pair. A rejected pair must
 // neither occupy a presentation slot with a duplicate nor leak an acquired
 // spare. The original game's image is preserved exactly on this path.
 if(pair){
  a.ResetFences(d->handle,1,&s.acquired);auto ar=a.AcquireNextImageKHR(d->handle,s.handle,0,VK_NULL_HANDLE,s.acquired,&extra);
  if(ar!=VK_SUCCESS&&ar!=VK_SUBOPTIMAL_KHR){++s.skipped;pair=false;}
  else {auto wr=a.WaitForFences(d->handle,1,&s.acquired,VK_TRUE,UINT64_MAX);if(wr!=VK_SUCCESS)return wr;}
 }
 auto counters=[&](){double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();reportMetrics(s,quality.reasons,ms);if(s.sources%60==0){LOG("COUNTERS sources=%llu generated_present_requests=%llu spare_skips=%llu cut_skips=%llu hold_skips=%llu flow_skips=%llu last_quality_flags=%u last_blocking_ms=%.3f (NOT displayed FPS)",(unsigned long long)s.sources,(unsigned long long)s.generated,(unsigned long long)s.skipped,(unsigned long long)s.cutSkipped,(unsigned long long)s.holdSkipped,(unsigned long long)s.flowSkipped,quality.reasons,ms);}};
 if(!pair){VkPresentInfoKHR real=*info;real.waitSemaphoreCount=0;real.pWaitSemaphores=nullptr;auto result=a.QueuePresentKHR(queue,&real);counters();return result;}
 a.ResetCommandPool(d->handle,s.pool,0);br=a.BeginCommandBuffer(s.cmd,&bi);if(br!=VK_SUCCESS)return br;
 auto output=[&](VkImage from,uint32_t index,VkImageLayout oldLayout){s.engine->barrier(s.cmd,s.images[index],oldLayout,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);s.engine->blit(s.cmd,from,s.extent,s.images[index],s.extent);s.engine->barrier(s.cmd,s.images[index],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_WRITE_BIT,0);};
 output(s.engine->upCurrent.image,original,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);if(pair)output(s.engine->upMid.image,extra,VK_IMAGE_LAYOUT_UNDEFINED);
 er=a.EndCommandBuffer(s.cmd);if(er!=VK_SUCCESS)return er;
 VkSemaphore signals[]={s.ready[original],s.ready[extra]};
 VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&s.cmd;sub.signalSemaphoreCount=2;sub.pSignalSemaphores=signals;
 sr=a.QueueSubmit(queue,1,&sub,VK_NULL_HANDLE);if(sr!=VK_SUCCESS)return sr;
 VkResult generatedResult=VK_SUCCESS;
 if(pair){VkPresentInfoKHR gen{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};gen.waitSemaphoreCount=1;gen.pWaitSemaphores=&s.ready[extra];gen.swapchainCount=1;gen.pSwapchains=&s.handle;gen.pImageIndices=&extra;generatedResult=a.QueuePresentKHR(queue,&gen);if(generatedResult==VK_SUCCESS||generatedResult==VK_SUBOPTIMAL_KHR)++s.generated;}
 VkPresentInfoKHR real=*info;real.waitSemaphoreCount=1;real.pWaitSemaphores=&s.ready[original];auto r=a.QueuePresentKHR(queue,&real);
 idle=a.QueueWaitIdle(queue);if(idle!=VK_SUCCESS)return idle;
 counters();
 if(r==VK_SUCCESS&&generatedResult!=VK_SUCCESS&&generatedResult!=VK_SUBOPTIMAL_KHR)return generatedResult;return r;
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* count,VkLayerProperties* properties){if(!properties){*count=1;return VK_SUCCESS;}if(*count==0)return VK_INCOMPLETE;VkLayerProperties p{};strncpy(p.layerName,layerName,sizeof(p.layerName)-1);p.specVersion=VK_API_VERSION_1_1;p.implementationVersion=1;strncpy(p.description,"Thor Native FG experimental SGSR1 + block motion interpolation",sizeof(p.description)-1);properties[0]=p;*count=1;return VK_SUCCESS;}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* name,uint32_t* count,VkExtensionProperties*){if(name&&strcmp(name,layerName))return VK_ERROR_LAYER_NOT_PRESENT;*count=0;return VK_SUCCESS;}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice,uint32_t* count,VkLayerProperties* p){return vkEnumerateInstanceLayerProperties(count,p);}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice,const char*,uint32_t* count,VkExtensionProperties*){*count=0;return VK_SUCCESS;}
static PFN_vkVoidFunction intercept(const char* name){
#define HOOK(n,f) if(!strcmp(name,"vk" #n))return (PFN_vkVoidFunction)f;
 HOOK(GetInstanceProcAddr,vkGetInstanceProcAddr) HOOK(GetDeviceProcAddr,vkGetDeviceProcAddr)
 HOOK(CreateInstance,createInstance) HOOK(DestroyInstance,destroyInstance) HOOK(CreateDevice,createDevice) HOOK(DestroyDevice,destroyDevice)
 HOOK(GetDeviceQueue,getQueue) HOOK(GetDeviceQueue2,getQueue2) HOOK(CreateSwapchainKHR,createSwapchain) HOOK(DestroySwapchainKHR,destroySwapchain) HOOK(QueuePresentKHR,present)
 HOOK(EnumerateInstanceLayerProperties,vkEnumerateInstanceLayerProperties) HOOK(EnumerateInstanceExtensionProperties,vkEnumerateInstanceExtensionProperties)
#undef HOOK
 return nullptr;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char* name){if(!name)return nullptr;auto fn=intercept(name);if(fn)return fn;auto data=instanceFor(i);return data?data->gipa(i,name):nullptr;}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char* name){if(!name)return nullptr;auto fn=intercept(name);if(fn)return fn;auto data=deviceFor(d);return data?data->gdpa(d,name):nullptr;}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version){if(!version)return VK_ERROR_INITIALIZATION_FAILED;version->loaderLayerInterfaceVersion=std::min(version->loaderLayerInterfaceVersion,2u);version->pfnGetInstanceProcAddr=vkGetInstanceProcAddr;version->pfnGetDeviceProcAddr=vkGetDeviceProcAddr;return VK_SUCCESS;}
