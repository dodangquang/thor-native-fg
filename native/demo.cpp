#include "engine.hpp"
#include "shaders.hpp"
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"ThorNativeDemo",__VA_ARGS__)
static std::atomic<bool> running{false};static std::atomic<int> mode{2};static std::thread worker;
struct Demo {
 VkInstance instance{};VkSurfaceKHR surface{};VkPhysicalDevice phys{};VkDevice device{};VkQueue queue{};Api a;
 VkSwapchainKHR swapchain{};std::vector<VkImage> images;std::vector<VkSemaphore> ready;
 VkFence fence{},workFence{};bool workPending=false;VkCommandPool pool{};VkCommandBuffer cmd{};Engine e;Image source;Pipeline scene;VkDescriptorSet sceneSet{};VkExtent2D extent{};bool sourceInitialized=false;
 VkFormat swapFormat{};VkBuffer qaBuffer{};VkDeviceMemory qaMemory{};void* qaMapped{};size_t qaFrameBytes=0;int qaChecks=0;
 PFN_vkGetPastPresentationTimingGOOGLE appTiming{};uint32_t appPresentID=100000,appReturned=0,appUnknown=0;
 void pollAppTiming(){
  if(!appTiming)return;VkPastPresentationTimingGOOGLE values[128]{};uint32_t count=128;auto result=appTiming(device,swapchain,&count,values);
  requireVk(result==VK_INCOMPLETE?VK_SUCCESS:result,"AppTimingQuery");
  for(uint32_t n=0;n<count;n++){appReturned++;if(values[n].presentID<=100000||values[n].presentID>appPresentID)appUnknown++;}
  if((appPresentID-100000)%8==0)LOG("APP_TIMING submitted=%u returned=%u unknown=%u",appPresentID-100000,appReturned,appUnknown);
 }
 ~Demo(){if(device){a.DeviceWaitIdle(device);if(qaMapped)a.UnmapMemory(device,qaMemory);if(qaBuffer)a.DestroyBuffer(device,qaBuffer,nullptr);if(qaMemory)a.FreeMemory(device,qaMemory,nullptr);if(scene.pipeline)a.DestroyPipeline(device,scene.pipeline,nullptr);if(scene.layout)a.DestroyPipelineLayout(device,scene.layout,nullptr);if(scene.setLayout)a.DestroyDescriptorSetLayout(device,scene.setLayout,nullptr);if(source.view)a.DestroyImageView(device,source.view,nullptr);if(source.image)a.DestroyImage(device,source.image,nullptr);if(source.memory)a.FreeMemory(device,source.memory,nullptr);e.release();for(auto s:ready)a.DestroySemaphore(device,s,nullptr);if(fence)a.DestroyFence(device,fence,nullptr);if(workFence)a.DestroyFence(device,workFence,nullptr);if(pool)a.DestroyCommandPool(device,pool,nullptr);if(swapchain)a.DestroySwapchainKHR(device,swapchain,nullptr);a.DestroyDevice(device,nullptr);}if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);if(instance)vkDestroyInstance(instance,nullptr);}
 void init(ANativeWindow* window){
  char validation[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.validate",validation);bool validate=validation[0]=='1';
  const char* ex[]={"VK_KHR_surface","VK_KHR_android_surface",VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Thor Native FG Demo";app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=validate?3:2;ci.ppEnabledExtensionNames=ex;
  VkValidationFeatureEnableEXT feature=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT vf{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};vf.enabledValidationFeatureCount=1;vf.pEnabledValidationFeatures=&feature;if(validate)ci.pNext=&vf;
  requireVk(vkCreateInstance(&ci,nullptr,&instance),"DemoCreateInstance");
  VkAndroidSurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};si.window=window;requireVk(vkCreateAndroidSurfaceKHR(instance,&si,nullptr,&surface),"CreateAndroidSurface");
  uint32_t n=0;requireVk(vkEnumeratePhysicalDevices(instance,&n,nullptr),"PhysicalCount");if(!n)throw std::runtime_error("No Vulkan device");std::vector<VkPhysicalDevice> physical(n);vkEnumeratePhysicalDevices(instance,&n,physical.data());phys=physical[0];
  vkGetPhysicalDeviceQueueFamilyProperties(phys,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(phys,&n,families.data());uint32_t family=~0u;
  for(uint32_t i=0;i<n;i++){VkBool32 present=0;vkGetPhysicalDeviceSurfaceSupportKHR(phys,i,surface,&present);if(present&&(families[i].queueFlags&3)==3){family=i;break;}}if(family==~0u)throw std::runtime_error("No graphics/compute/present queue");
  std::vector<float> priorities(1,1.0f);char allQueues[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.demo_all_queues",allQueues);if(allQueues[0]=='1')priorities.assign(families[family].queueCount,1.0f);
  LOG("DEMO_QUEUE_REQUEST count=%zu available=%u",priorities.size(),families[family].queueCount);
  char ownsTiming[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.demo_owns_timing",ownsTiming);
  const char* extensions[]={"VK_KHR_swapchain",VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME};
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=priorities.size();qi.pQueuePriorities=priorities.data();VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=ownsTiming[0]=='1'?2:1;di.ppEnabledExtensionNames=extensions;requireVk(vkCreateDevice(phys,&di,nullptr,&device),"DemoCreateDevice");a.load(device,vkGetDeviceProcAddr);a.GetDeviceQueue(device,family,0,&queue);
  if(ownsTiming[0]=='1'){appTiming=(PFN_vkGetPastPresentationTimingGOOGLE)vkGetDeviceProcAddr(device,"vkGetPastPresentationTimingGOOGLE");if(!appTiming)throw std::runtime_error("Application display timing was not available");}
  VkSurfaceCapabilitiesKHR caps{};requireVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys,surface,&caps),"SurfaceCaps");extent=caps.currentExtent;if(extent.width==~0u)extent={uint32_t(ANativeWindow_getWidth(window)),uint32_t(ANativeWindow_getHeight(window))};
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys,surface,&n,nullptr);std::vector<VkSurfaceFormatKHR> formats(n);vkGetPhysicalDeviceSurfaceFormatsKHR(phys,surface,&n,formats.data());auto chosen=formats[0];for(auto f:formats)if(f.format==VK_FORMAT_R8G8B8A8_UNORM&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)chosen=f;
  swapFormat=chosen.format;
  VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};sc.surface=surface;sc.minImageCount=caps.minImageCount+1;if(caps.maxImageCount)sc.minImageCount=std::min(sc.minImageCount,caps.maxImageCount);sc.imageFormat=chosen.format;sc.imageColorSpace=chosen.colorSpace;sc.imageExtent=extent;sc.imageArrayLayers=1;sc.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;sc.preTransform=caps.currentTransform;sc.compositeAlpha=(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)?VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR:VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;sc.presentMode=VK_PRESENT_MODE_FIFO_KHR;sc.clipped=VK_TRUE;requireVk(a.CreateSwapchainKHR(device,&sc,nullptr,&swapchain),"CreateDemoSwapchain");
  a.GetSwapchainImagesKHR(device,swapchain,&n,nullptr);images.resize(n);a.GetSwapchainImagesKHR(device,swapchain,&n,images.data());ready.resize(n);VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};for(auto& s:ready)requireVk(a.CreateSemaphore(device,&sem,nullptr,&s),"DemoSemaphore");VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"DemoFence");requireVk(a.CreateFence(device,&fi,nullptr,&workFence),"DemoWorkFence");
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"DemoPool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"DemoCommand");
  VkPhysicalDeviceMemoryProperties mem{};vkGetPhysicalDeviceMemoryProperties(phys,&mem);e.init(a,mem,{640,360},extent);source=e.createImage({640,360},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);scene=e.createPipeline(scene_spv,sizeof(scene_spv),{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},16);sceneSet=e.allocate(scene);e.storage(sceneSet,0,source);
  if(mode.load()==4){
   qaFrameBytes=size_t(extent.width)*extent.height*4;VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=qaFrameBytes*2+4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&buffer,nullptr,&qaBuffer),"QaBuffer");VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,qaBuffer,&req);VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=e.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(device,&alloc,nullptr,&qaMemory),"QaMemory");requireVk(a.BindBufferMemory(device,qaBuffer,qaMemory,0),"QaBind");requireVk(a.MapMemory(device,qaMemory,0,buffer.size,0,&qaMapped),"QaMap");
  }
  LOG("READY source=640x360 output=%ux%u; GSR1 + bidirectional block motion; no NPU",extent.width,extent.height);
 }
 void waitWork(){if(workPending){requireVk(a.WaitForFences(device,1,&workFence,VK_TRUE,UINT64_MAX),"DemoWorkComplete");workPending=false;}}
 void begin(){waitWork();requireVk(a.ResetCommandPool(device,pool,0),"DemoResetPool");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;requireVk(a.BeginCommandBuffer(cmd,&bi),"DemoBegin");}
 void submit(){requireVk(a.EndCommandBuffer(cmd),"DemoEnd");VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cmd;requireVk(a.ResetFences(device,1,&workFence),"DemoResetWork");requireVk(a.QueueSubmit(queue,1,&sub,workFence),"DemoSubmit");workPending=true;waitWork();}
 bool show(const Image& image,bool linear){
  uint32_t index=0;a.ResetFences(device,1,&fence);VkResult r=a.AcquireNextImageKHR(device,swapchain,100000000,VK_NULL_HANDLE,fence,&index);if(r==VK_TIMEOUT||r==VK_NOT_READY)return true;if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR)return false;requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"DemoAcquire");
  begin();e.barrier(cmd,images[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  if(linear){VkImageBlit b{};b.srcSubresource=b.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};b.srcOffsets[1]={int32_t(image.size.width),int32_t(image.size.height),1};b.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};a.CmdBlitImage(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,images[index],VK_IMAGE_LAYOUT_GENERAL,1,&b,VK_FILTER_LINEAR);}else e.blit(cmd,image.image,image.size,images[index],extent);
  e.barrier(cmd,images[index],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_WRITE_BIT,0);requireVk(a.EndCommandBuffer(cmd),"ShowEnd");VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cmd;sub.signalSemaphoreCount=1;sub.pSignalSemaphores=&ready[index];requireVk(a.ResetFences(device,1,&workFence),"ShowResetWork");requireVk(a.QueueSubmit(queue,1,&sub,workFence),"ShowSubmit");workPending=true;VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.waitSemaphoreCount=1;pi.pWaitSemaphores=&ready[index];pi.swapchainCount=1;pi.pSwapchains=&swapchain;pi.pImageIndices=&index;
  VkPresentTimeGOOGLE time{appPresentID+1,0};VkPresentTimesInfoGOOGLE times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};times.swapchainCount=1;times.pTimes=&time;if(appTiming){++appPresentID;pi.pNext=&times;}
  r=a.QueuePresentKHR(queue,&pi);if(appTiming&&(appPresentID-100000)%8==0)pollAppTiming();return r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR;
 }
 bool showQa(float sceneTime,int sceneCut){
  if(!qaBuffer)throw std::runtime_error("QA mode must be selected at launch");
  uint32_t index=0;a.ResetFences(device,1,&fence);auto result=a.AcquireNextImageKHR(device,swapchain,100000000,VK_NULL_HANDLE,fence,&index);if(result==VK_TIMEOUT||result==VK_NOT_READY)return true;if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)return false;requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"QaAcquire");
  begin();e.barrier(cmd,images[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  struct{int32_t size[2];float time;int32_t cut;} push{{640,360},sceneTime,sceneCut};
  a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.layout,0,1,&sceneSet,0,nullptr);a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&push);
  VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.srcOffsets[1]={640,360,1};blit.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
  // This is the unmodified application's bilinear blit. Only the external layer
  // may replace it. Compare its actual swapchain pixels with the reference SR.
  a.CmdBlitImage(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,images[index],VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_LINEAR);
  e.barrier(cmd,images[index],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={extent.width,extent.height,1};a.CmdCopyImageToBuffer(cmd,images[index],VK_IMAGE_LAYOUT_GENERAL,qaBuffer,1,&copy);copy.bufferOffset=qaFrameBytes;a.CmdCopyImageToBuffer(cmd,e.upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,qaBuffer,1,&copy);
  // Deliberately do not rebind the application's pipeline/descriptors. An
  // inserted SR compute pass must restore them and preserve partial constants.
  e.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);int32_t flip=sceneCut?0:1;a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,12,4,&flip);a.CmdDispatch(cmd,80,45,1);
  e.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);copy.bufferOffset=qaFrameBytes*2;copy.imageExtent={1,1,1};a.CmdCopyImageToBuffer(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,qaBuffer,1,&copy);
  VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
  e.barrier(cmd,images[index],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_READ_BIT,0);requireVk(a.EndCommandBuffer(cmd),"QaEnd");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&ready[index];requireVk(a.ResetFences(device,1,&workFence),"QaResetWork");requireVk(a.QueueSubmit(queue,1,&submit,workFence),"QaSubmit");workPending=true;VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&ready[index];present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;result=a.QueuePresentKHR(queue,&present);waitWork();
  const auto* actual=(const uint8_t*)qaMapped;const auto* expected=actual+qaFrameBytes;size_t mismatched=0;int largest=0;
  for(size_t pixel=0;pixel<qaFrameBytes;pixel+=4)for(int c=0;c<4;c++){int channel=swapFormat==VK_FORMAT_B8G8R8A8_UNORM&&c<3?2-c:c;int difference=std::abs(int(actual[pixel+c])-int(expected[pixel+channel]));if(difference>1)mismatched++;largest=std::max(largest,difference);}
  const auto* state=actual+qaFrameBytes*2;const int expectedColor[4]={flip?239:10,flip?0:153,flip?0:230,255};bool restored=true;for(int c=0;c<4;c++)restored&=std::abs(int(state[c])-expectedColor[c])<=1;
  LOG("QA_EXTERNAL_SR %s mismatched_channels=%zu max_error=%d input=640x360 output=%ux%u (GPU readback, not a screenshot)",mismatched==0?"PASS":"FAIL",mismatched,largest,extent.width,extent.height);
  LOG("QA_COMPUTE_STATE %s pixel=%u,%u,%u,%u expected=%d,%d,%d,%d",restored?"PASS":"FAIL",state[0],state[1],state[2],state[3],expectedColor[0],expectedColor[1],expectedColor[2],expectedColor[3]);
  if(++qaChecks==3)mode=3;return result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR;
 }
 void run(){
  using Clock=std::chrono::steady_clock;auto next=Clock::now(),since=next;uint64_t frame=0,requests=0;double processing=0;int lastMode=-1;
  char target[PROP_VALUE_MAX]{},jitter[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.demo_fps",target);__system_property_get("debug.thorfg.demo_jitter_ms",jitter);
  int sourceFps=std::atoi(target);if(sourceFps<15||sourceFps>240)sourceFps=30;int frameUs=1000000/sourceFps;
  int jitterUs=std::clamp(std::atoi(jitter)*1000,0,frameUs/2-250);LOG("DEMO_SOURCE_TARGET fps=%d jitter_us=%d",sourceFps,jitterUs);
  while(running){int m=mode.load();if(m!=lastMode){e.hasHistory=false;lastMode=m;LOG("MODE %d",m);}begin();e.barrier(cmd,source.image,sourceInitialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,sourceInitialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);sourceInitialized=true;
   struct {int32_t size[2];float time;int32_t cut;} p{{640,360},float(frame)/sourceFps,int((frame/(sourceFps*5))%2)};a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.layout,0,1,&sceneSet,0,nullptr);a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&p);a.CmdDispatch(cmd,80,45,1);
   auto before=Clock::now();bool had=e.hasHistory;if(m==1||m==2||m==4)e.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL);else e.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);submit();processing+=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
   std::this_thread::sleep_until(next+std::chrono::microseconds((frame%2)?jitterUs:-jitterUs));
   if(m==2&&had&&e.readQuality().allow){if(!show(e.upMid,false))break;requests++;std::this_thread::sleep_until(next+std::chrono::microseconds(frameUs/2));}
   if(m==4){if(!showQa(p.time,p.cut))break;}else if(!show((m==1||m==2)?e.upCurrent:source,m==0||m==3))break;requests++;frame++;next+=std::chrono::microseconds(frameUs);if(next<Clock::now()-std::chrono::milliseconds(100))next=Clock::now();
   if(frame%120==0){double seconds=std::chrono::duration<double>(Clock::now()-since).count();LOG("COUNTERS input=%llu present_requests=%llu elapsed=%.3fs requests_per_s=%.2f avg_record_submit_wait_ms=%.3f (NOT displayed FPS/GPU timestamps)",(unsigned long long)frame,(unsigned long long)requests,seconds,requests/seconds,processing/frame);}
  }
 }
};
extern "C" JNIEXPORT void JNICALL Java_com_thor_nativefg_DemoActivity_startNative(JNIEnv* env,jclass,jobject surface){if(worker.joinable()){running=false;worker.join();}ANativeWindow* window=ANativeWindow_fromSurface(env,surface);running=true;worker=std::thread([window](){try{Demo demo;demo.init(window);demo.run();}catch(const std::exception& e){LOG("ERROR %s",e.what());}running=false;ANativeWindow_release(window);});}
extern "C" JNIEXPORT void JNICALL Java_com_thor_nativefg_DemoActivity_stopNative(JNIEnv*,jclass){running=false;if(worker.joinable())worker.join();}
extern "C" JNIEXPORT void JNICALL Java_com_thor_nativefg_DemoActivity_modeNative(JNIEnv*,jclass,jint value){if(value>=0&&value<=4)mode=value;}
