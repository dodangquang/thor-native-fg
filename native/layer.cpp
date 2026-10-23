#include "engine.hpp"
#include "flow_comparison.hpp"
#include "sr_state.hpp"
#include "presentation.hpp"
#include "frame_worker.hpp"
#include "queue_plan.hpp"
#include "wsi_guard.hpp"
#include "presenter_policy.hpp"
#include <atomic>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <time.h>
#include <cstdlib>
#include <cstdio>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"ThorNativeLayer",__VA_ARGS__)
#define EXPORT extern "C" __attribute__((visibility("default")))
static const char* layerName="VK_LAYER_THOR_native_fg";
static void* key(const void* object){return object?*(void*const*)object:nullptr;}
struct Instance { VkInstance handle{};PFN_vkGetInstanceProcAddr gipa{}; };
struct Swap {
 VkSwapchainKHR handle{};VkExtent2D extent{};VkFormat format{};bool supported=false,disabled=false;
 std::vector<VkImage> images;std::vector<VkSemaphore> ready;
 std::unique_ptr<Engine> engine;VkCommandPool pool{};VkCommandBuffer cmd{};VkFence acquired{};
 std::unique_ptr<FlowComparison> flowComparison;uint32_t flowCompared=0;bool flowComparePending=false;
 uint32_t family=~0u;uint64_t sources=0,generated=0,skipped=0,cutSkipped=0,holdSkipped=0,flowSkipped=0;
 std::chrono::steady_clock::time_point metricsAt=std::chrono::steady_clock::now();
 uint64_t metricsSources=0,metricsGenerated=0;
 std::vector<SpatialFrame> spatialFrames;
 VkExtent2D spatialInput{};bool spatialActive=false;SpatialModel spatialModel=SpatialModel::Sgsr1;uint64_t spatialPresents=0;
 bool spatialCompatible=false;
 bool timing=false;uint32_t presentID=0;PresentationWindow presentation;
 struct PendingPresent{bool generated;uint64_t source;};std::unordered_map<uint32_t,PendingPresent> pendingPresents;
 SourceCadence cadence;uint64_t refreshNs=16666667,lastRealDesired=0,rateSkipped=0,budgetSkipped=0,sourceArrivalNs=0;bool scheduleBounded=false;
 VkFence workDone{};bool workPending=false;
 VkQueryPool queries{};uint32_t timestampBits=0;bool outputTimed=false;
 uint32_t costFrames=0,inputGpuSamples=0,outputGpuSamples=0;
 double inputGpuTotal=0,outputGpuTotal=0,qualityWaitTotal=0,previousWaitTotal=0,presentTotal=0,callTotal=0;
 std::mutex producerMutex,wsiMutex;FrameWorker worker;SourceCadence producerCadence;
 std::atomic<VkResult> deferredResult{VK_SUCCESS};
 std::atomic<double> lastSourceCallMs{0},lastBackpressureMs{0};bool asyncFrame=false,sharedFrame=false;
};
struct Device {
 VkDevice handle{};VkPhysicalDevice physical{};std::shared_ptr<Instance> instance;Api a;
 VkPhysicalDeviceMemoryProperties memory{};std::vector<VkQueueFamilyProperties> families;
 PFN_vkGetDeviceProcAddr gdpa{};PFN_vkSetDeviceLoaderData setLoaderData{};
 std::unordered_map<VkSwapchainKHR,std::shared_ptr<Swap>> swaps;std::mutex mutex;
 std::unordered_map<VkCommandPool,uint32_t> appPools;
 std::unordered_map<VkCommandBuffer,SpatialCommands> appCommands;
 bool spatialDiagnostic=false;
 PFN_vkGetPastPresentationTimingGOOGLE pastTiming{};bool applicationTiming=false;
 PFN_vkGetRefreshCycleDurationGOOGLE refreshTiming{};
 float timestampPeriod=0;
 VkQueue privateQueue{};uint32_t privateFamily=~0u;std::mutex privateQueueMutex;bool sharedPresenter=false;
};
struct Queue { std::shared_ptr<Device> device;uint32_t family; };
static std::mutex globalMutex;
static std::unordered_map<void*,std::shared_ptr<Instance>> instances;
static std::unordered_map<void*,std::shared_ptr<Device>> devices;
static std::unordered_map<VkQueue,Queue> queues;
static std::shared_ptr<Device> deviceFor(VkDevice device){std::lock_guard<std::mutex> l(globalMutex);auto i=devices.find(key(device));return i==devices.end()?nullptr:i->second;}
static std::shared_ptr<Instance> instanceFor(const void* instance){std::lock_guard<std::mutex> l(globalMutex);auto i=instances.find(key(instance));return i==instances.end()?nullptr:i->second;}
static VkResult layerSubmit(Device& d,VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence fence){
 std::unique_lock<std::mutex> lock(d.privateQueueMutex,std::defer_lock);if(d.sharedPresenter||queue==d.privateQueue)lock.lock();
 return d.a.QueueSubmit(queue,count,submits,fence);
}
static VkResult layerPresent(Device& d,VkQueue queue,const VkPresentInfoKHR* info){
 std::vector<std::shared_ptr<Swap>> swaps;
 {std::lock_guard<std::mutex> metadata(d.mutex);for(uint32_t n=0;n<info->swapchainCount;n++){auto found=d.swaps.find(info->pSwapchains[n]);if(found!=d.swaps.end())swaps.push_back(found->second);}}
 std::sort(swaps.begin(),swaps.end(),[](const auto& a,const auto& b){return std::less<Swap*>{}(a.get(),b.get());});
 swaps.erase(std::unique(swaps.begin(),swaps.end()),swaps.end());
 std::vector<std::unique_lock<std::mutex>> wsiLocks;for(auto& swap:swaps)wsiLocks.emplace_back(swap->wsiMutex);
 std::unique_lock<std::mutex> lock(d.privateQueueMutex,std::defer_lock);if(d.sharedPresenter||queue==d.privateQueue)lock.lock();
 auto result=d.a.QueuePresentKHR(queue,info);
 static std::atomic<unsigned> advisoryLogs{0};
 if(result<0||(result>0&&advisoryLogs.fetch_add(1)<4))LOG("WSI_PRESENT result=%d swaps=%u",result,info->swapchainCount);
 return result;
}
static void drainDevice(Device& d){
 std::vector<std::shared_ptr<Swap>> swaps;{std::lock_guard<std::mutex> lock(d.mutex);for(auto& item:d.swaps)swaps.push_back(item.second);}
 for(auto& swap:swaps)swap->worker.wait();
}
static VkResult layerDeviceIdle(Device& d){std::lock_guard<std::mutex> lock(d.privateQueueMutex);return d.a.DeviceWaitIdle(d.handle);}
enum class PresenterMode{Automatic,Private,Shared,Fence};
static PresenterMode presenterMode(){
 char value[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.presenter",value);
 if(!value[0]||!strcmp(value,"auto")){
  static const bool fence=[](){char process[256]{};auto file=std::fopen("/proc/self/cmdline","rb");if(file){std::fread(process,1,sizeof(process)-1,file);std::fclose(file);}return defaultFencePresenter(process);}();
  return fence?PresenterMode::Fence:PresenterMode::Automatic;
 }
 if(!strcmp(value,"async"))return PresenterMode::Private;if(!strcmp(value,"shared"))return PresenterMode::Shared;return PresenterMode::Fence;
}
static bool asyncRequested(){return presenterMode()!=PresenterMode::Fence;}
static bool autoProcess(){const char* selected=std::getenv("THOR_FG_AUTO_MATCH");return selected&&strcmp(selected,"1")==0;}
static bool enabled(){char value[PROP_VALUE_MAX]{};__system_property_get(autoProcess()?"debug.thorfg.auto":"debug.thorfg.enabled",value);return strcmp(value,"1")==0;}
static bool srEnabled(){char value[PROP_VALUE_MAX]{};__system_property_get(autoProcess()?"debug.thorfg.auto_sr":"debug.thorfg.sr",value);return strcmp(value,"1")==0;}
static bool fgEnabled(){char value[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.fg",value);return value[0]!='0';}
#include "sr_hooks.inc"
#include "presentation_hooks.inc"
static void reportMetrics(Swap& s,uint32_t qualityFlags,double blockingMs){
 auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-s.metricsAt).count();
 if(seconds<1.0)return;
 timespec boot{};clock_gettime(CLOCK_BOOTTIME,&boot);uint64_t uptime=uint64_t(boot.tv_sec)*1000+boot.tv_nsec/1000000;
 auto display=s.presentation.stats(monotonicNs());
 // These are incoming and generated PRESENT REQUEST rates. Neither rate is a
 // scanout measurement. Keep dimensions in the protocol so the HUD cannot
 // mislabel the current same-size filter as active super resolution.
 LOG("THOR_METRICS v=2 pid=%d swap=%llu eligible=%d in_w=%u in_h=%u out_w=%u out_h=%u source_fps=%.2f generated_fps=%.2f sources=%llu generated=%llu cuts=%llu holds=%llu flow_skips=%llu flags=%u blocking_ms=%.3f sr=%d sa=%u sr_in_w=%u sr_in_h=%u sr_presents=%llu uptime_ms=%llu display_valid=%d display_fps=%.2f display_generated_fps=%.2f display_samples=%zu gap_median_ms=%.3f gap_p95_ms=%.3f gap_max_ms=%.3f rate_skips=%llu fg_gpu_input_ms=%.3f fg_gpu_output_ms=%.3f fg_gpu_input_samples=%u fg_gpu_output_samples=%u fg_cpu_quality_wait_ms=%.3f fg_cpu_previous_wait_ms=%.3f fg_cpu_present_ms=%.3f fg_cpu_processing_ms=%.3f fg_cost_frames=%u presenter=%s fg_cpu_source_call_last_ms=%.3f fg_async_backpressure_last_ms=%.3f source_interval_ms=%.3f cadence_policy=%s flow=%s present_policy=%s budget_skips=%llu",
  getpid(),(unsigned long long)(uintptr_t)s.handle,s.supported&&!s.disabled,
  s.extent.width,s.extent.height,s.extent.width,s.extent.height,
  (s.sources-s.metricsSources)/seconds,(s.generated-s.metricsGenerated)/seconds,
  (unsigned long long)s.sources,(unsigned long long)s.generated,
  (unsigned long long)s.cutSkipped,(unsigned long long)s.holdSkipped,(unsigned long long)s.flowSkipped,qualityFlags,blockingMs,
  s.spatialActive,uint32_t(s.spatialModel),s.spatialInput.width,s.spatialInput.height,(unsigned long long)s.spatialPresents,(unsigned long long)uptime,
  display.valid,display.fps,display.generatedFps,display.samples,display.medianGapMs,display.p95GapMs,display.maxGapMs,(unsigned long long)s.rateSkipped,
  s.inputGpuTotal/std::max(1u,s.inputGpuSamples),s.outputGpuTotal/std::max(1u,s.outputGpuSamples),s.inputGpuSamples,s.outputGpuSamples,
  s.qualityWaitTotal/std::max(1u,s.costFrames),s.previousWaitTotal/std::max(1u,s.costFrames),s.presentTotal/std::max(1u,s.costFrames),s.callTotal/std::max(1u,s.costFrames),s.costFrames,s.asyncFrame?(s.sharedFrame?"async_shared":"async_queue"):"fence",s.lastSourceCallMs.load(),s.lastBackpressureMs.load(),s.cadence.interval()/1e6,s.cadence.legacy?"legacy":"trimmed_mean",s.engine?(s.engine->cooperativeFlow?"cooperative":"serial"):"none",s.scheduleBounded?"bounded":"legacy",(unsigned long long)s.budgetSkipped);
 // liblog truncates formatted messages at1023bytes on this Android build.
 // Keep optional frame-age diagnostics separate from the HUD protocol.
 LOG("THOR_FRAME_AGE v=1 pid=%d swap=%llu uptime_ms=%llu "
     "api_to_present_real_ms=%.3f api_to_present_real_p95_ms=%.3f "
     "api_to_present_generated_ms=%.3f api_to_present_generated_p95_ms=%.3f "
     "api_to_present_real_samples=%zu api_to_present_generated_samples=%zu",
     getpid(),(unsigned long long)(uintptr_t)s.handle,(unsigned long long)uptime,
     display.realAgeMs,display.realAgeP95Ms,display.generatedAgeMs,display.generatedAgeP95Ms,
     display.realAgeSamples,display.generatedAgeSamples);
 s.metricsAt=now;s.metricsSources=s.sources;s.metricsGenerated=s.generated;
 s.costFrames=s.inputGpuSamples=s.outputGpuSamples=0;
 s.inputGpuTotal=s.outputGpuTotal=s.qualityWaitTotal=s.previousWaitTotal=s.presentTotal=s.callTotal=0;
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
 auto fn=(PFN_vkCreateDevice)gipa(inst->handle,"vkCreateDevice");auto* downstreamLink=link->u.pLayerInfo->pNext;link->u.pLayerInfo=downstreamLink;
 auto changed=*ci;std::vector<const char*> extensions;bool timing=false;
 for(uint32_t i=0;i<ci->enabledExtensionCount;i++){extensions.push_back(ci->ppEnabledExtensionNames[i]);if(!strcmp(ci->ppEnabledExtensionNames[i],VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))timing=true;}
 bool applicationTiming=timing;
 if(enabled()&&!timing){
  auto enumerate=(PFN_vkEnumerateDeviceExtensionProperties)gipa(inst->handle,"vkEnumerateDeviceExtensionProperties");uint32_t count=0;
  if(enumerate&&enumerate(physical,nullptr,&count,nullptr)==VK_SUCCESS){std::vector<VkExtensionProperties> supported(count);auto er=enumerate(physical,nullptr,&count,supported.data());if(er==VK_SUCCESS)for(const auto& extension:supported)if(!strcmp(extension.extensionName,VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME)){timing=true;extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);break;}}
 }
 changed.enabledExtensionCount=extensions.size();changed.ppEnabledExtensionNames=extensions.data();
 auto fam=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)inst->gipa(inst->handle,"vkGetPhysicalDeviceQueueFamilyProperties");uint32_t familyCount=0;fam(physical,&familyCount,nullptr);
 std::vector<VkQueueFamilyProperties> families(familyCount);fam(physical,&familyCount,families.data());
 std::vector<VkDeviceQueueCreateInfo> queueInfos(ci->pQueueCreateInfos,ci->pQueueCreateInfos+ci->queueCreateInfoCount);
 std::vector<std::vector<float>> priorities(queueInfos.size());uint32_t privateFamily=~0u,privateIndex=0;
 auto reservation=reserveFrameQueue(families,queueInfos);
 auto mode=presenterMode();
 if(enabled()&&asyncRequested()&&reservation.info<0)LOG("ASYNC_QUEUE unavailable no compatible spare queue");
 if(enabled()&&asyncRequested()&&mode!=PresenterMode::Shared&&reservation.info>=0){
  size_t n=size_t(reservation.info);auto& queue=queueInfos[n];
  priorities[n].assign(queue.pQueuePriorities,queue.pQueuePriorities+queue.queueCount);priorities[n].push_back(0.5f);
  privateFamily=queue.queueFamilyIndex;privateIndex=queue.queueCount++;queue.pQueuePriorities=priorities[n].data();
  changed.pQueueCreateInfos=queueInfos.data();
 }
 auto r=fn(physical,&changed,alloc,out);
 if(r!=VK_SUCCESS&&privateFamily!=~0u){LOG("ASYNC_QUEUE unavailable device result=%d; original queue configuration retained",r);privateFamily=~0u;changed.pQueueCreateInfos=ci->pQueueCreateInfos;link->u.pLayerInfo=downstreamLink;r=fn(physical,&changed,alloc,out);}
 if(r!=VK_SUCCESS)return r;
 auto d=std::make_shared<Device>();d->handle=*out;d->physical=physical;d->instance=inst;d->gdpa=get;d->setLoaderData=callback;d->a.load(*out,get);
 // Some mobile drivers expose only one graphics queue. This experimental
 // path synchronizes ALL supported queue host calls, including app submits,
 // while retaining application/GPU order on that original queue.
 bool sharedSafe=true;for(const auto* extension:extensions)if(!strcmp(extension,"VK_NV_device_diagnostic_checkpoints")||!strcmp(extension,"VK_NV_low_latency2")||!strcmp(extension,"VK_INTEL_performance_query"))sharedSafe=false;
 d->families=std::move(families);
 if(privateFamily!=~0u){d->privateFamily=privateFamily;d->a.GetDeviceQueue(*out,privateFamily,privateIndex,&d->privateQueue);if(d->setLoaderData&&d->privateQueue&&d->setLoaderData(*out,d->privateQueue)!=VK_SUCCESS)d->privateQueue=VK_NULL_HANDLE;LOG("ASYNC_QUEUE family=%u private_index=%u available=%d",privateFamily,privateIndex,d->privateQueue!=VK_NULL_HANDLE);}
 d->sharedPresenter=enabled()&&sharedSafe&&(mode==PresenterMode::Shared||(mode==PresenterMode::Automatic&&!d->privateQueue));
 if(mode==PresenterMode::Shared||(mode==PresenterMode::Automatic&&!d->privateQueue))LOG("ASYNC_SHARED available=%d",d->sharedPresenter);
 d->applicationTiming=applicationTiming;
 // GOOGLE timing records are returned only once. An app that enabled this
 // extension owns its records; internal polling must never consume them.
 if(timing){if(!applicationTiming)d->pastTiming=(PFN_vkGetPastPresentationTimingGOOGLE)get(*out,"vkGetPastPresentationTimingGOOGLE");d->refreshTiming=(PFN_vkGetRefreshCycleDurationGOOGLE)get(*out,"vkGetRefreshCycleDurationGOOGLE");}
 auto mem=(PFN_vkGetPhysicalDeviceMemoryProperties)inst->gipa(inst->handle,"vkGetPhysicalDeviceMemoryProperties");mem(physical,&d->memory);
 auto propertiesFn=(PFN_vkGetPhysicalDeviceProperties)inst->gipa(inst->handle,"vkGetPhysicalDeviceProperties");VkPhysicalDeviceProperties properties{};propertiesFn(physical,&properties);d->timestampPeriod=properties.limits.timestampPeriod;
 {std::lock_guard<std::mutex> l(globalMutex);devices[key(*out)]=d;}
 LOG("DEVICE registered display_timing=%d application_timing=%d",d->pastTiming!=nullptr,d->applicationTiming);return r;
}
static void releaseSwap(Device& d,Swap& s){auto& a=d.a;layerDeviceIdle(d);s.workPending=false;s.flowComparison.reset();s.engine.reset();for(auto sem:s.ready)if(sem)a.DestroySemaphore(d.handle,sem,nullptr);if(s.acquired)a.DestroyFence(d.handle,s.acquired,nullptr);if(s.workDone)a.DestroyFence(d.handle,s.workDone,nullptr);if(s.queries)a.DestroyQueryPool(d.handle,s.queries,nullptr);if(s.pool)a.DestroyCommandPool(d.handle,s.pool,nullptr);s.pool={};s.acquired={};s.workDone={};s.queries={};s.ready.clear();}
static VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks* alloc){auto d=deviceFor(device);if(!d)return;drainDevice(*d);{std::lock_guard<std::mutex> l(d->mutex);d->a.DeviceWaitIdle(device);d->appCommands.clear();d->appPools.clear();for(auto& entry:d->swaps){entry.second->worker.stop();releaseSwap(*d,*entry.second);}d->swaps.clear();}{std::lock_guard<std::mutex> l(globalMutex);for(auto i=queues.begin();i!=queues.end();)if(i->second.device==d)i=queues.erase(i);else ++i;devices.erase(key(device));}d->a.DestroyDevice(device,alloc);}
static void trackQueue(std::shared_ptr<Device> d,VkQueue queue,uint32_t family){if(!queue)return;std::lock_guard<std::mutex> l(globalMutex);queues[queue]={d,family};}
static VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue* out){auto d=deviceFor(device);if(!d)return;d->a.GetDeviceQueue(device,family,index,out);trackQueue(d,*out,family);}
static VKAPI_ATTR void VKAPI_CALL getQueue2(VkDevice device,const VkDeviceQueueInfo2* ci,VkQueue* out){auto d=deviceFor(device);if(!d)return;d->a.GetDeviceQueue2(device,ci,out);trackQueue(d,*out,ci->queueFamilyIndex);}
static VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device,const VkSwapchainCreateInfoKHR* ci,const VkAllocationCallbacks* alloc,VkSwapchainKHR* out){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 if(ci->oldSwapchain){std::shared_ptr<Swap> previous;{std::lock_guard<std::mutex> lock(d->mutex);auto found=d->swaps.find(ci->oldSwapchain);if(found!=d->swaps.end())previous=found->second;}if(previous)previous->worker.wait();}
 auto capsFn=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)d->instance->gipa(d->instance->handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
 auto formatFn=(PFN_vkGetPhysicalDeviceFormatProperties)d->instance->gipa(d->instance->handle,"vkGetPhysicalDeviceFormatProperties");
 VkSurfaceCapabilitiesKHR caps{};VkFormatProperties fp{},rp{};formatFn(d->physical,ci->imageFormat,&fp);formatFn(d->physical,VK_FORMAT_R8G8B8A8_UNORM,&rp);
 auto changed=*ci;VkImageUsageFlags transfers=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 bool ok=capsFn&&capsFn(d->physical,ci->surface,&caps)==VK_SUCCESS&&(caps.supportedUsageFlags&transfers)==transfers;
 ok=ok && (fp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT)&&(rp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(rp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT);
 ok=ok && (ci->imageFormat==VK_FORMAT_R8G8B8A8_UNORM||ci->imageFormat==VK_FORMAT_B8G8R8A8_UNORM)&&ci->imageArrayLayers==1&&ci->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR&&!(ci->flags&VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)&&ci->imageExtent.width<=4096&&ci->imageExtent.height<=4096;
 // Only FIFO is supported by the simple proof-of-concept presenter.
 ok=ok && ci->presentMode==VK_PRESENT_MODE_FIFO_KHR;
 LOG("SWAPCHAIN_INPUT format=%u colorspace=%u mode=%u usage=%u flags=%u layers=%u requested_images=%u surface_min=%u surface_max=%u surface_usage=%u format_features=%u working_features=%u compatible=%d",ci->imageFormat,ci->imageColorSpace,ci->presentMode,ci->imageUsage,ci->flags,ci->imageArrayLayers,ci->minImageCount,caps.minImageCount,caps.maxImageCount,caps.supportedUsageFlags,fp.optimalTilingFeatures,rp.optimalTilingFeatures,ok);
 if(ok){changed.imageUsage|=transfers;changed.minImageCount=std::max(changed.minImageCount,caps.minImageCount+1);if(caps.maxImageCount&&changed.minImageCount>caps.maxImageCount)ok=false;}
 auto r=d->a.CreateSwapchainKHR(device,ok?&changed:ci,alloc,out);
 if(r!=VK_SUCCESS&&ok){ok=false;r=d->a.CreateSwapchainKHR(device,ci,alloc,out);}
 if(r==VK_SUCCESS){auto s=std::make_shared<Swap>();s->handle=*out;s->extent=ci->imageExtent;s->format=ci->imageFormat;s->supported=ok;
 uint32_t n=0;d->a.GetSwapchainImagesKHR(device,*out,&n,nullptr);s->images.resize(n);d->a.GetSwapchainImagesKHR(device,*out,&n,s->images.data());
 s->spatialFrames.resize(n);
 s->spatialCompatible=ci->compositeAlpha==VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR||ci->compositeAlpha==VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
 s->timing=d->pastTiming!=nullptr;
 if(d->refreshTiming){VkRefreshCycleDurationGOOGLE cycle{};if(d->refreshTiming(device,*out,&cycle)==VK_SUCCESS&&cycle.refreshDuration)s->refreshNs=cycle.refreshDuration;}
 std::lock_guard<std::mutex> l(d->mutex);d->swaps[*out]=s;LOG("SWAPCHAIN %ux%u images=%u eligible=%d active=%d composite_alpha=%u",s->extent.width,s->extent.height,n,ok,enabled(),ci->compositeAlpha);}return r;
}
static VKAPI_ATTR void VKAPI_CALL destroySwapchain(VkDevice device,VkSwapchainKHR handle,const VkAllocationCallbacks* alloc){auto d=deviceFor(device);if(!d)return;std::shared_ptr<Swap> swap;{std::lock_guard<std::mutex> lock(d->mutex);auto i=d->swaps.find(handle);if(i!=d->swaps.end())swap=i->second;}if(swap){std::lock_guard<std::mutex> lock(swap->producerMutex);swap->worker.stop();releaseSwap(*d,*swap);std::lock_guard<std::mutex> metadata(d->mutex);d->swaps.erase(handle);}d->a.DestroySwapchainKHR(device,handle,alloc);}
static bool prepare(Device& d,Swap& s,uint32_t family){
 if(s.engine)return s.family==family;
 if(family>=d.families.size()||(d.families[family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))!=(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))return false;
 auto& a=d.a;s.family=family;
 try{
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(d.handle,&pi,nullptr,&s.pool),"CreateLayerPool");
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=s.pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(d.handle,&ca,&s.cmd),"AllocateLayerCommand");
  if(d.setLoaderData)requireVk(d.setLoaderData(d.handle,s.cmd),"SetLoaderData");
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(d.handle,&fi,nullptr,&s.acquired),"CreateAcquireFence");
  requireVk(a.CreateFence(d.handle,&fi,nullptr,&s.workDone),"CreateWorkFence");
  s.timestampBits=d.families[family].timestampValidBits;
  if(s.timestampBits&&d.timestampPeriod>0){VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qi.queryType=VK_QUERY_TYPE_TIMESTAMP;qi.queryCount=4;auto qr=a.CreateQueryPool(d.handle,&qi,nullptr,&s.queries);if(qr!=VK_SUCCESS){s.queries={};LOG("GPU_TIMING unavailable result=%d",qr);}}
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};s.ready.resize(s.images.size());for(auto& sem:s.ready)requireVk(a.CreateSemaphore(d.handle,&si,nullptr,&sem),"CreatePresentSemaphore");
  char flowMode[PROP_VALUE_MAX]{},compare[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.flow",flowMode);__system_property_get("debug.thorfg.flow_compare",compare);bool cooperative=strcmp(flowMode,"serial")!=0;
  s.engine=std::make_unique<Engine>();s.engine->init(a,d.memory,s.extent,s.extent,cooperative);
  if(compare[0]=='1'){s.flowComparison=std::make_unique<FlowComparison>();s.flowComparison->init(a,d.memory,s.extent);}
  // Pipeline compilation/allocation is layer work, not the game's cadence.
  // Observe fresh arrivals afterward while retaining the real source timestamp
  // separately for frame-age reporting, including this potentially slow call.
  s.producerCadence={};s.producerCadence.record(monotonicNs());s.cadence=s.producerCadence;
  LOG("FLOW selected=%s compare=%d",cooperative?"cooperative":"serial",s.flowComparison!=nullptr);LOG("PIPELINE ready FG_input=%ux%u output=%ux%u presenter_capability=%s gpu_timing=%d; real frame preserved, spatial SR reported separately",s.extent.width,s.extent.height,s.extent.width,s.extent.height,d.sharedPresenter?"shared":(d.privateQueue?"private":"fence"),s.queries!=VK_NULL_HANDLE);return true;
 }catch(const std::exception& e){LOG("DISABLED %s",e.what());s.disabled=true;releaseSwap(d,s);return false;}
}
static void collectGpuTime(Device& d,Swap& s,uint32_t first,bool output){
 if(!s.queries)return;
 // Called only after this submission's fence has signaled. Timestamp queries
 // are optional instrumentation, never a reason to block the whole queue.
 uint64_t ticks[2]{};auto result=d.a.GetQueryPoolResults(d.handle,s.queries,first,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
 if(result!=VK_SUCCESS)return;
 uint64_t mask=s.timestampBits>=64?UINT64_MAX:((uint64_t(1)<<s.timestampBits)-1);
 double ms=double((ticks[1]-ticks[0])&mask)*d.timestampPeriod/1e6;
 if(output){s.outputGpuTotal+=ms;++s.outputGpuSamples;}else{s.inputGpuTotal+=ms;++s.inputGpuSamples;}
}
static VkResult waitLayerWork(Device& d,Swap& s){
 if(!s.workPending)return VK_SUCCESS;
 auto result=d.a.WaitForFences(d.handle,1,&s.workDone,VK_TRUE,UINT64_MAX);
 if(result==VK_SUCCESS){s.workPending=false;if(s.outputTimed)collectGpuTime(d,s,2,true);s.outputTimed=false;}
 return result;
}
static VkResult finishPreparedFrame(std::shared_ptr<Device> d,std::shared_ptr<Swap> swap,VkQueue queue,uint32_t original,std::chrono::steady_clock::time_point start,double previousWaitMs){
 auto& a=d->a;auto& s=*swap;uint32_t extra=~0u;double qualityWaitMs=0,presentMs=0;
 VkPresentInfoKHR base{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};base.swapchainCount=1;base.pSwapchains=&s.handle;base.pImageIndices=&original;const auto* info=&base;
 VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 VkResult waited=VK_SUCCESS,reset=VK_SUCCESS,br=VK_SUCCESS,er=VK_SUCCESS,sr=VK_SUCCESS;
 auto waitStart=std::chrono::steady_clock::now();waited=waitLayerWork(*d,s);if(waited!=VK_SUCCESS)return waited;
 qualityWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waitStart).count();collectGpuTime(*d,s,0,false);
 const auto quality=s.engine->readQuality();bool pair=quality.allow!=0;
 if(s.flowComparePending){uint32_t difference=s.flowComparison->differences(*s.engine);++s.flowCompared;LOG("THOR_FLOW_COMPARE %s frame=%u differences=%u candidate=%s",difference?"FAIL":"PASS",s.flowCompared,difference,s.engine->cooperativeFlow?"cooperative":"serial");s.flowComparePending=false;if(difference){s.disabled=true;pair=false;}if(s.flowCompared>=8||difference)s.flowComparison.reset();}
 if(quality.reasons&QUALITY_CUT)++s.cutSkipped;
 if(quality.reasons&QUALITY_HOLDOFF)++s.holdSkipped;
 if(quality.reasons&(QUALITY_LOW_FLOW|QUALITY_FAST_MOTION))++s.flowSkipped;
 uint64_t generatedTime=0,realTime=0;bool budgetRejected=false;
 char pacing[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.pacing",pacing);
 if(pair&&s.timing&&pacing[0]!='0'){
  auto plan=planPairPresentation(monotonicNs(),s.cadence.interval()/2,s.refreshNs,s.lastRealDesired,s.scheduleBounded,s.sourceArrivalNs);
  generatedTime=plan.generated;realTime=plan.real;
  if(!plan.generate){pair=false;budgetRejected=true;++s.budgetSkipped;}
 }
 // Acquire only after the GPU has accepted the pair. A rejected pair must
 // neither occupy a presentation slot with a duplicate nor leak an acquired
 // spare. The original game's image is preserved exactly on this path.
 if(pair){
  a.ResetFences(d->handle,1,&s.acquired);VkResult ar;
  {std::lock_guard<std::mutex> lock(s.wsiMutex);ar=a.AcquireNextImageKHR(d->handle,s.handle,0,VK_NULL_HANDLE,s.acquired,&extra);}
  if(ar!=VK_SUCCESS&&ar!=VK_SUBOPTIMAL_KHR){++s.skipped;pair=false;}
  else {auto wr=a.WaitForFences(d->handle,1,&s.acquired,VK_TRUE,UINT64_MAX);if(wr!=VK_SUCCESS)return wr;}
 }
 auto counters=[&](){double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++s.costFrames;s.qualityWaitTotal+=qualityWaitMs;s.previousWaitTotal+=previousWaitMs;s.presentTotal+=presentMs;s.callTotal+=ms;double blocking=s.asyncFrame?s.lastSourceCallMs.load():ms;reportMetrics(s,quality.reasons,blocking);if(s.sources%60==0){LOG("COUNTERS sources=%llu generated_present_requests=%llu spare_skips=%llu cut_skips=%llu hold_skips=%llu flow_skips=%llu last_quality_flags=%u last_blocking_ms=%.3f (NOT displayed FPS)",(unsigned long long)s.sources,(unsigned long long)s.generated,(unsigned long long)s.skipped,(unsigned long long)s.cutSkipped,(unsigned long long)s.holdSkipped,(unsigned long long)s.flowSkipped,quality.reasons,blocking);}};
 if(!pair){s.lastRealDesired=budgetRejected?realTime:0;VkPresentInfoKHR real=*info;real.waitSemaphoreCount=0;real.pWaitSemaphores=nullptr;auto presentStart=std::chrono::steady_clock::now();auto result=timedPresent(*d,s,queue,&real,false,s.lastRealDesired);presentMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presentStart).count();counters();return result;}
 reset=a.ResetCommandPool(d->handle,s.pool,0);if(reset!=VK_SUCCESS)return reset;br=a.BeginCommandBuffer(s.cmd,&bi);if(br!=VK_SUCCESS)return br;
 if(s.queries)a.CmdWriteTimestamp(s.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,s.queries,2);
 auto output=[&](VkImage from,uint32_t index,VkImageLayout oldLayout){s.engine->barrier(s.cmd,s.images[index],oldLayout,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);s.engine->blit(s.cmd,from,s.extent,s.images[index],s.extent);s.engine->barrier(s.cmd,s.images[index],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_WRITE_BIT,0);};
 output(s.engine->upCurrent.image,original,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);if(pair)output(s.engine->upMid.image,extra,VK_IMAGE_LAYOUT_UNDEFINED);
 if(s.queries)a.CmdWriteTimestamp(s.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s.queries,3);
 er=a.EndCommandBuffer(s.cmd);if(er!=VK_SUCCESS)return er;
 VkSemaphore signals[]={s.ready[original],s.ready[extra]};
 VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&s.cmd;sub.signalSemaphoreCount=2;sub.pSignalSemaphores=signals;
 reset=a.ResetFences(d->handle,1,&s.workDone);if(reset!=VK_SUCCESS)return reset;
 sr=layerSubmit(*d,queue,1,&sub,s.workDone);if(sr!=VK_SUCCESS)return sr;s.workPending=true;s.outputTimed=s.queries!=VK_NULL_HANDLE;
 // The preflight may reject before acquiring a spare. If it accepts, later
 // time cannot invalidate its lookahead bound. Choose the final deadlines
 // after the output submission: counting acquisition/recording time as driver
 // lead can make both queued frames target the same physical display slot.
 if(s.timing&&pacing[0]!='0'){
  auto plan=planPairPresentation(monotonicNs(),s.cadence.interval()/2,s.refreshNs,s.lastRealDesired,false,s.sourceArrivalNs);
  generatedTime=plan.generated;realTime=plan.real;
 }
 s.lastRealDesired=realTime;
 VkResult generatedResult=VK_SUCCESS;
 auto presentStart=std::chrono::steady_clock::now();
 if(pair){VkPresentInfoKHR gen{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};gen.waitSemaphoreCount=1;gen.pWaitSemaphores=&s.ready[extra];gen.swapchainCount=1;gen.pSwapchains=&s.handle;gen.pImageIndices=&extra;generatedResult=timedPresent(*d,s,queue,&gen,true,generatedTime);if(generatedResult==VK_SUCCESS||generatedResult==VK_SUBOPTIMAL_KHR)++s.generated;}
 VkPresentInfoKHR real=*info;real.waitSemaphoreCount=1;real.pWaitSemaphores=&s.ready[original];auto r=timedPresent(*d,s,queue,&real,false,realTime);
 presentMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presentStart).count();
 counters();
 if(r==VK_SUCCESS&&generatedResult!=VK_SUCCESS&&generatedResult!=VK_SUBOPTIMAL_KHR)return generatedResult;return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue,const VkPresentInfoKHR* info){
 Queue q{};{std::lock_guard<std::mutex> l(globalMutex);auto it=queues.find(queue);if(it==queues.end())return VK_ERROR_INITIALIZATION_FAILED;q=it->second;}
 auto d=q.device;auto& a=d->a;
 if(info->swapchainCount!=1){
  drainDevice(*d);std::vector<std::shared_ptr<Swap>> skipped;
  {std::lock_guard<std::mutex> metadata(d->mutex);for(uint32_t n=0;n<info->swapchainCount;n++){auto found=d->swaps.find(info->pSwapchains[n]);if(found!=d->swaps.end())skipped.push_back(found->second);}}
  bool supported=skipped.size()==info->swapchainCount;
  for(auto& item:skipped){std::lock_guard<std::mutex> producer(item->producerMutex);item->worker.wait();supported=supported&&item->supported;if(item->engine)item->engine->hasHistory=false;item->lastRealDesired=0;}
  if(supported&&d->privateQueue&&d->privateFamily==q.family)queue=d->privateQueue;
  return layerPresent(*d,queue,info);
 }
 std::shared_ptr<Swap> swap;{std::lock_guard<std::mutex> metadata(d->mutex);auto found=d->swaps.find(info->pSwapchains[0]);if(found!=d->swaps.end())swap=found->second;}
 if(!swap)return layerPresent(*d,queue,info);auto& s=*swap;
 if(s.supported&&d->privateQueue&&d->privateFamily==q.family)queue=d->privateQueue;
 auto callStart=std::chrono::steady_clock::now();uint64_t arrivedNs=monotonicNs();
 struct CallCost{Swap& swap;std::chrono::steady_clock::time_point start;~CallCost(){swap.lastSourceCallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} callCost{s,callStart};
 std::lock_guard<std::mutex> producer(s.producerMutex);
 auto backpressureStart=std::chrono::steady_clock::now();s.worker.wait();s.lastBackpressureMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-backpressureStart).count();
 s.asyncFrame=false;s.sourceArrivalNs=arrivedNs;
 auto deferred=s.deferredResult.load();if(deferred<0){
  // A previous asynchronous request failed after its caller returned. Consume
  // this request's binary wait semaphores before propagating the surface error.
  std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkSubmitInfo consume{VK_STRUCTURE_TYPE_SUBMIT_INFO};consume.waitSemaphoreCount=info->waitSemaphoreCount;consume.pWaitSemaphores=info->pWaitSemaphores;consume.pWaitDstStageMask=stages.data();
  if(info->waitSemaphoreCount)layerSubmit(*d,queue,1,&consume,VK_NULL_HANDLE);
  if(info->pResults)info->pResults[0]=deferred;return deferred;
 }
 if(!enabled()){
  // Hot disable must not leave an old scene/cadence eligible for interpolation
  // when the user enables the automatic mode again in the same process.
  if(s.engine)s.engine->hasHistory=false;s.cadence={};s.producerCadence={};s.lastRealDesired=0;s.spatialActive=false;
  {std::lock_guard<std::mutex> metadata(d->mutex);if(info->pImageIndices[0]<s.spatialFrames.size())s.spatialFrames[info->pImageIndices[0]].pending=false;}
  return layerPresent(*d,queue,info);
 }
 ++s.sources;
 s.spatialActive=false;uint32_t presented=info->pImageIndices[0];
 {std::lock_guard<std::mutex> metadata(d->mutex);if(presented<s.spatialFrames.size()&&s.spatialFrames[presented].pending){s.spatialActive=true;s.spatialInput=s.spatialFrames[presented].input;s.spatialModel=s.spatialFrames[presented].model;s.spatialFrames[presented].pending=false;++s.spatialPresents;}}
 pollPresentation(*d,s);
 char cadencePolicy[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.cadence",cadencePolicy);
 s.producerCadence.record(arrivedNs,!strcmp(cadencePolicy,"legacy"));s.cadence=s.producerCadence;
 char schedulePolicy[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.schedule",schedulePolicy);s.scheduleBounded=!strcmp(schedulePolicy,"bounded");
 if(info->pNext){if(s.engine)s.engine->hasHistory=false;s.lastRealDesired=0;auto result=layerPresent(*d,queue,info);reportMetrics(s,0,0);return result;}
 if(!fgEnabled()){
  if(s.engine)s.engine->hasHistory=false;s.lastRealDesired=0;
  auto result=timedPresent(*d,s,queue,info,false);reportMetrics(s,64u,0);return result;
 }
 // Observe an initial native cadence without FG work. Never halve a game's
 // original120FPS just to report60+60 on a120Hz panel.
 if(!s.cadence.canDouble(s.refreshNs)){
  if(s.cadence.ready())++s.rateSkipped;
  if(s.engine)s.engine->hasHistory=false;s.lastRealDesired=0;
  auto result=timedPresent(*d,s,queue,info,false);reportMetrics(s,s.cadence.ready()?32u:QUALITY_WARMUP,0);return result;
 }
 if(!s.supported||s.disabled||!prepare(*d,s,q.family)){auto result=layerPresent(*d,queue,info);reportMetrics(s,0,0);return result;}
 bool asynchronous=asyncRequested()&&((d->privateQueue&&d->privateFamily==q.family)||d->sharedPresenter);
 // Keep history on the same queue even when switching the worker off live.
 // A host fence wait alone is not a replacement for a cross-queue dependency.
 if(d->privateQueue&&d->privateFamily==q.family)queue=d->privateQueue;s.asyncFrame=asynchronous;s.sharedFrame=asynchronous&&queue!=d->privateQueue;
 uint32_t original=info->pImageIndices[0];if(original>=s.images.size())return layerPresent(*d,queue,info);
 auto start=std::chrono::steady_clock::now();
 // Only the layer's previous output must finish before reusing its command
 // pool/history. The game's render-complete semaphores order the new input.
 // The async worker waits for the input fence on its private queue. The
 // synchronous fallback retains the original wait. Semaphores remain per WSI image
 // and are reused only after that image has been acquired again.
 auto waited=waitLayerWork(*d,s);if(waited!=VK_SUCCESS)return waited;
 double previousWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 auto reset=a.ResetCommandPool(d->handle,s.pool,0);if(reset!=VK_SUCCESS)return reset;
 VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 auto br=a.BeginCommandBuffer(s.cmd,&bi);if(br!=VK_SUCCESS)return br;
 if(s.queries){a.CmdResetQueryPool(s.cmd,s.queries,0,4);a.CmdWriteTimestamp(s.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,s.queries,0);}
 char flowMode[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.flow",flowMode);s.engine->cooperativeFlow=strcmp(flowMode,"serial")!=0;
 bool hadHistory=s.engine->hasHistory;s.engine->record(s.cmd,s.images[original],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,false);
 s.flowComparePending=s.flowComparison!=nullptr;if(s.flowComparePending)s.flowComparison->record(s.cmd,*s.engine,s.images[original],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,hadHistory);
 if(s.queries)a.CmdWriteTimestamp(s.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s.queries,1);
 auto er=a.EndCommandBuffer(s.cmd);if(er!=VK_SUCCESS)return er;
 std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 VkSubmitInfo input{VK_STRUCTURE_TYPE_SUBMIT_INFO};input.waitSemaphoreCount=info->waitSemaphoreCount;input.pWaitSemaphores=info->pWaitSemaphores;input.pWaitDstStageMask=stages.data();input.commandBufferCount=1;input.pCommandBuffers=&s.cmd;
 reset=a.ResetFences(d->handle,1,&s.workDone);if(reset!=VK_SUCCESS)return reset;
 auto sr=layerSubmit(*d,queue,1,&input,s.workDone);if(sr!=VK_SUCCESS)return sr;s.workPending=true;
 if(asynchronous){
  // Only owned handles/scalars enter the job. No application arrays, pResults,
  // pNext chains or stack pointers outlive vkQueuePresentKHR.
  try{s.worker.submit([d,swap,queue,original,start,previousWaitMs]() noexcept {
   try{swap->deferredResult=finishPreparedFrame(d,swap,queue,original,start,previousWaitMs);}
   catch(const std::exception& error){LOG("ASYNC_ERROR %s",error.what());swap->deferredResult=VK_ERROR_INITIALIZATION_FAILED;}
   catch(...){LOG("ASYNC_ERROR unknown exception");swap->deferredResult=VK_ERROR_INITIALIZATION_FAILED;}
  });}catch(const std::exception& error){LOG("ASYNC_FALLBACK worker unavailable: %s",error.what());s.asyncFrame=false;auto result=finishPreparedFrame(d,swap,queue,original,start,previousWaitMs);if(info->pResults)info->pResults[0]=result;return result;}
  // A previous asynchronous presentation's advisory status belongs to this
  // API, not vkAcquireNextImageKHR. Eden recreates on acquire SUBOPTIMAL but
  // correctly tolerates presentation SUBOPTIMAL (e.g. Android preTransform).
  auto result=deferred==VK_SUBOPTIMAL_KHR?VK_SUBOPTIMAL_KHR:VK_SUCCESS;
  if(info->pResults)info->pResults[0]=result;return result;
 }
 auto result=finishPreparedFrame(d,swap,queue,original,start,previousWaitMs);if(info->pResults)info->pResults[0]=result;return result;
}
static std::shared_ptr<Swap> findSwap(Device& d,VkSwapchainKHR handle){std::lock_guard<std::mutex> lock(d.mutex);auto found=d.swaps.find(handle);return found==d.swaps.end()?nullptr:found->second;}
static VKAPI_ATTR VkResult VKAPI_CALL acquireNext(VkDevice device,VkSwapchainKHR handle,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;auto swap=findSwap(*d,handle);
 auto deferred=swap?swap->deferredResult.load():VK_SUCCESS;if(deferred<0)return deferred;
 if(!swap||(!d->privateQueue&&!d->sharedPresenter))return d->a.AcquireNextImageKHR(device,handle,timeout,semaphore,fence,index);
 auto result=guardedWsiAcquire(swap->wsiMutex,timeout,[&]{return d->a.AcquireNextImageKHR(device,handle,0,semaphore,fence,index);},[&]{return swap->deferredResult.load();});
 if(result!=VK_SUCCESS&&result!=VK_TIMEOUT&&result!=VK_NOT_READY)LOG("WSI_ACQUIRE result=%d deferred=%d",result,deferred);
 return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL acquireNext2(VkDevice device,const VkAcquireNextImageInfoKHR* info,uint32_t* index){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;auto swap=findSwap(*d,info->swapchain);
 auto deferred=swap?swap->deferredResult.load():VK_SUCCESS;if(deferred<0)return deferred;
 auto fn=(PFN_vkAcquireNextImage2KHR)d->gdpa(device,"vkAcquireNextImage2KHR");if(!fn)return VK_ERROR_EXTENSION_NOT_PRESENT;
 if(!swap||(!d->privateQueue&&!d->sharedPresenter))return fn(device,info,index);auto changed=*info;changed.timeout=0;
 auto result=guardedWsiAcquire(swap->wsiMutex,info->timeout,[&]{return fn(device,&changed,index);},[&]{return swap->deferredResult.load();});
 return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL deviceWait(VkDevice device){auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;drainDevice(*d);return layerDeviceIdle(*d);}
static VKAPI_ATTR VkResult VKAPI_CALL queueWait(VkQueue queue){
 std::shared_ptr<Device> d;{std::lock_guard<std::mutex> lock(globalMutex);auto found=queues.find(queue);if(found!=queues.end())d=found->second.device;}
 if(!d)return VK_ERROR_INITIALIZATION_FAILED;drainDevice(*d);
 std::lock_guard<std::mutex> lock(d->privateQueueMutex);
 if(d->privateQueue){auto result=d->a.QueueWaitIdle(d->privateQueue);if(result!=VK_SUCCESS)return result;}
 return d->a.QueueWaitIdle(queue);
}
static VKAPI_ATTR VkResult VKAPI_CALL queueBindSparse(VkQueue queue,uint32_t count,const VkBindSparseInfo* info,VkFence fence){
 auto d=srQueueDevice(queue);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto fn=(PFN_vkQueueBindSparse)d->gdpa(d->handle,"vkQueueBindSparse");if(!fn)return VK_ERROR_EXTENSION_NOT_PRESENT;
 std::unique_lock<std::mutex> lock(d->privateQueueMutex,std::defer_lock);if(d->sharedPresenter)lock.lock();return fn(queue,count,info,fence);
}
static VKAPI_ATTR void VKAPI_CALL queueBeginLabel(VkQueue queue,const VkDebugUtilsLabelEXT* label){
 auto d=srQueueDevice(queue);if(!d)return;auto fn=(PFN_vkQueueBeginDebugUtilsLabelEXT)d->gdpa(d->handle,"vkQueueBeginDebugUtilsLabelEXT");
 std::unique_lock<std::mutex> lock(d->privateQueueMutex,std::defer_lock);if(d->sharedPresenter)lock.lock();if(fn)fn(queue,label);
}
static VKAPI_ATTR void VKAPI_CALL queueEndLabel(VkQueue queue){
 auto d=srQueueDevice(queue);if(!d)return;auto fn=(PFN_vkQueueEndDebugUtilsLabelEXT)d->gdpa(d->handle,"vkQueueEndDebugUtilsLabelEXT");
 std::unique_lock<std::mutex> lock(d->privateQueueMutex,std::defer_lock);if(d->sharedPresenter)lock.lock();if(fn)fn(queue);
}
static VKAPI_ATTR void VKAPI_CALL queueInsertLabel(VkQueue queue,const VkDebugUtilsLabelEXT* label){
 auto d=srQueueDevice(queue);if(!d)return;auto fn=(PFN_vkQueueInsertDebugUtilsLabelEXT)d->gdpa(d->handle,"vkQueueInsertDebugUtilsLabelEXT");
 std::unique_lock<std::mutex> lock(d->privateQueueMutex,std::defer_lock);if(d->sharedPresenter)lock.lock();if(fn)fn(queue,label);
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
 HOOK(AcquireNextImageKHR,acquireNext) HOOK(AcquireNextImage2KHR,acquireNext2) HOOK(DeviceWaitIdle,deviceWait) HOOK(QueueWaitIdle,queueWait)
 HOOK(QueueBindSparse,queueBindSparse) HOOK(QueueBeginDebugUtilsLabelEXT,queueBeginLabel) HOOK(QueueEndDebugUtilsLabelEXT,queueEndLabel) HOOK(QueueInsertDebugUtilsLabelEXT,queueInsertLabel)
 HOOK(CreateCommandPool,srCreatePool) HOOK(DestroyCommandPool,srDestroyPool) HOOK(AllocateCommandBuffers,srAllocateCommands) HOOK(FreeCommandBuffers,srFreeCommands) HOOK(ResetCommandPool,srResetPool) HOOK(BeginCommandBuffer,srBegin)
 HOOK(CmdBindPipeline,srBindPipeline) HOOK(CmdBindDescriptorSets,srBindDescriptors) HOOK(CmdPushConstants,srPushConstants) HOOK(CmdBlitImage,srBlit)
 HOOK(CmdPushDescriptorSetKHR,srPushDescriptors) HOOK(CmdBindDescriptorBuffersEXT,srBindDescriptorBuffers)
 HOOK(CmdBindShadersEXT,srBindShaders)
 HOOK(QueueSubmit,srSubmit) HOOK(QueueSubmit2,srSubmit2) HOOK(QueueSubmit2KHR,srSubmit2KHR)
 HOOK(EnumerateInstanceLayerProperties,vkEnumerateInstanceLayerProperties) HOOK(EnumerateInstanceExtensionProperties,vkEnumerateInstanceExtensionProperties)
#undef HOOK
 return nullptr;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char* name){if(!name)return nullptr;auto fn=intercept(name);if(!i)return fn;auto data=instanceFor(i);auto next=data?data->gipa(i,name):nullptr;return fn&&next?fn:next;}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char* name){if(!name)return nullptr;auto data=deviceFor(d);auto next=data?data->gdpa(d,name):nullptr;auto fn=intercept(name);return fn&&next?fn:next;}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version){if(!version)return VK_ERROR_INITIALIZATION_FAILED;version->loaderLayerInterfaceVersion=std::min(version->loaderLayerInterfaceVersion,2u);version->pfnGetInstanceProcAddr=vkGetInstanceProcAddr;version->pfnGetDeviceProcAddr=vkGetDeviceProcAddr;return VK_SUCCESS;}
