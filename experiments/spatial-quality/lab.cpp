#include "engine.hpp"
#include <android/log.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#ifdef THOR_SPATIAL_LAB_JNI
#include <jni.h>
#endif

#define REPORT(...) do { printf(__VA_ARGS__);printf("\n");__android_log_print(ANDROID_LOG_INFO,"ThorSpatialLab",__VA_ARGS__); } while(false)

struct Device {
 VkInstance instance{};VkDevice device{};Api a;VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceProperties properties{};
 VkQueue queue{};uint32_t family=0,bits=0;
 void init(bool validation){
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;app.pApplicationName="Thor production spatial quality lab";
  const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT vf{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};vf.enabledValidationFeatureCount=1;vf.pEnabledValidationFeatures=&sync;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;if(validation){ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&extension;ci.pNext=&vf;}
  requireVk(vkCreateInstance(&ci,nullptr,&instance),"SpatialLabInstance");uint32_t n=0;requireVk(vkEnumeratePhysicalDevices(instance,&n,nullptr),"SpatialLabGPUCount");if(!n)throw std::runtime_error("No GPU");std::vector<VkPhysicalDevice> devices(n);requireVk(vkEnumeratePhysicalDevices(instance,&n,devices.data()),"SpatialLabGPUs");auto gpu=devices[0];
  vkGetPhysicalDeviceProperties(gpu,&properties);vkGetPhysicalDeviceMemoryProperties(gpu,&memory);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families.data());
  while(family<n&&(!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)||!families[family].timestampValidBits))family++;if(family==n)throw std::runtime_error("No timestamp compute queue");bits=families[family].timestampValidBits;
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
  // Match a game that enables no optional float16/storage features.
  requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"SpatialLabDevice");a.load(device,vkGetDeviceProcAddr);a.GetDeviceQueue(device,family,0,&queue);
 }
 ~Device(){if(device){a.DeviceWaitIdle(device);a.DestroyDevice(device,nullptr);}if(instance)vkDestroyInstance(instance,nullptr);}
};

struct Fixture {
 Device& g;Engine gsr,neural,bounded;Image source,linear;VkBuffer staging{};VkDeviceMemory allocation{};void* mapped{};VkCommandPool pool{};VkCommandBuffer cmd{};VkFence fence{};VkQueryPool queries{};bool linearReady=false;
 explicit Fixture(Device& device):g(device){}
 void init(VkExtent2D in,VkExtent2D out){
  auto& a=g.a;gsr.initSpatial(a,g.memory,in,out,SpatialModel::Sgsr1);neural.initSpatial(a,g.memory,in,out,SpatialModel::Neural2D);bounded.initSpatial(a,g.memory,in,out,SpatialModel::NeuralBounded);
  source=gsr.createImage(in,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  linear=gsr.createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=std::max(VkDeviceSize(in.width)*in.height,VkDeviceSize(out.width)*out.height)*4;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(g.device,&bc,nullptr,&staging),"SpatialLabBuffer");VkMemoryRequirements requirements{};a.GetBufferMemoryRequirements(g.device,staging,&requirements);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=requirements.size;ma.memoryTypeIndex=gsr.memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(g.device,&ma,nullptr,&allocation),"SpatialLabMemory");requireVk(a.BindBufferMemory(g.device,staging,allocation,0),"SpatialLabBind");requireVk(a.MapMemory(g.device,allocation,0,VK_WHOLE_SIZE,0,&mapped),"SpatialLabMap");
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=g.family;requireVk(a.CreateCommandPool(g.device,&pi,nullptr,&pool),"SpatialLabPool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;requireVk(a.AllocateCommandBuffers(g.device,&ca,&cmd),"SpatialLabCommand");VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(g.device,&fi,nullptr,&fence),"SpatialLabFence");VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=2;requireVk(a.CreateQueryPool(g.device,&qp,nullptr,&queries),"SpatialLabQueries");
 }
 void begin(){auto& a=g.a;requireVk(a.ResetCommandPool(g.device,pool,0),"SpatialLabReset");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&bi),"SpatialLabBegin");}
 void submit(){auto& a=g.a;requireVk(a.EndCommandBuffer(cmd),"SpatialLabEnd");VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;requireVk(a.ResetFences(g.device,1,&fence),"SpatialLabResetFence");requireVk(a.QueueSubmit(g.queue,1,&si,fence),"SpatialLabSubmit");requireVk(a.WaitForFences(g.device,1,&fence,VK_TRUE,UINT64_MAX),"SpatialLabWait");}
 void upload(const std::vector<uint8_t>& pixels){
  std::memcpy(mapped,pixels.data(),pixels.size());begin();gsr.barrier(cmd,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={source.size.width,source.size.height,1};g.a.CmdCopyBufferToImage(cmd,staging,source.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);gsr.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);submit();
 }
 void run(uint32_t id,int model,const std::string& directory){
  auto& a=g.a;auto in=source.size,out=linear.size;VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={int32_t(in.width),int32_t(in.height),1};region.dstOffsets[1]={int32_t(out.width),int32_t(out.height),1};
  std::vector<double> times;using Clock=std::chrono::steady_clock;auto start=Clock::now();int iteration=0;const char* names[]={"bilinear","gsr1","neural2d","bounded"};
  while(times.size()<20){
   bool measure=iteration>=5&&(in.width<960||Clock::now()-start>=std::chrono::milliseconds(250));bool readback=measure&&times.size()==19;
   begin();a.CmdResetQueryPool(cmd,queries,0,2);a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,0);
   VkImage result{};
   if(model==0){gsr.barrier(cmd,linear.image,linearReady?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,linearReady?VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);a.CmdBlitImage(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,linear.image,VK_IMAGE_LAYOUT_GENERAL,1,&region,VK_FILTER_LINEAR);gsr.barrier(cmd,linear.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);linearReady=true;result=linear.image;}
   else{Engine& engine=model==1?gsr:model==2?neural:bounded;engine.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);result=engine.upCurrent.image;}
   a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
   if(readback){
    VkMemoryBarrier war{VK_STRUCTURE_TYPE_MEMORY_BARRIER};war.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;war.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&war,0,nullptr,0,nullptr);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={out.width,out.height,1};a.CmdCopyImageToBuffer(cmd,result,VK_IMAGE_LAYOUT_GENERAL,staging,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
   }
   submit();uint64_t ticks[2]{};requireVk(a.GetQueryPoolResults(g.device,queries,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"SpatialLabTimes");uint64_t mask=g.bits>=64?UINT64_MAX:(uint64_t(1)<<g.bits)-1;if(measure)times.push_back(double((ticks[1]-ticks[0])&mask)*g.properties.limits.timestampPeriod/1e6);iteration++;
  }
  char filename[80];std::snprintf(filename,sizeof(filename),"/case-%03u-%s.rgba8",id,names[model]);std::ofstream file(directory+filename,std::ios::binary);file.write((const char*)mapped,size_t(out.width)*out.height*4);if(!file)throw std::runtime_error("Cannot save spatial output");std::sort(times.begin(),times.end());
  REPORT("SPATIAL_LAB case=%u model=%s input=%ux%u output=%ux%u gpu_median_ms=%.6f gpu_p95_ms=%.6f samples=%zu validation_scope=core timings_exclude_upload_readback_game_present=1",id,names[model],in.width,in.height,out.width,out.height,(times[9]+times[10])*.5,times[18],times.size());
 }
 ~Fixture(){auto& a=g.a;if(mapped)a.UnmapMemory(g.device,allocation);if(staging)a.DestroyBuffer(g.device,staging,nullptr);if(allocation)a.FreeMemory(g.device,allocation,nullptr);if(queries)a.DestroyQueryPool(g.device,queries,nullptr);if(fence)a.DestroyFence(g.device,fence,nullptr);if(pool)a.DestroyCommandPool(g.device,pool,nullptr);for(Image* image:{&source,&linear}){if(image->view)a.DestroyImageView(g.device,image->view,nullptr);if(image->image)a.DestroyImage(g.device,image->image,nullptr);if(image->memory)a.FreeMemory(g.device,image->memory,nullptr);}}
};

static int runLab(const char* path,const char* directory,bool validation){
 try{
  std::ifstream input(path,std::ios::binary);char magic[8]{};uint32_t count=0;input.read(magic,8);input.read((char*)&count,4);if(!input||std::memcmp(magic,"THORQLT1",8)||!count||count>64)throw std::runtime_error("Invalid spatial quality fixture header");
  struct stat status{};if(stat(directory,&status)||!S_ISDIR(status.st_mode))throw std::runtime_error("Output directory does not exist");
  Device gpu;gpu.init(validation);REPORT("SPATIAL_LAB_BEGIN gpu=%s cases=%u validation=%d",gpu.properties.deviceName,count,validation);
  for(uint32_t id=0;id<count;id++){
   uint32_t shape[4]{};input.read((char*)shape,sizeof(shape));uint32_t w=shape[0],h=shape[1],ow=shape[2],oh=shape[3];if(!input||w<16||h<16||w>1920||h>1080||ow<w||oh<h||ow>3840||oh>2160)throw std::runtime_error("Invalid spatial quality dimensions");std::vector<uint8_t> pixels(size_t(w)*h*4);input.read((char*)pixels.data(),pixels.size());if(!input)throw std::runtime_error("Truncated spatial quality fixture");Fixture fixture(gpu);fixture.init({w,h},{ow,oh});fixture.upload(pixels);fixture.run(id,0,directory);for(int order=0;order<3;order++)fixture.run(id,1+(id+order)%3,directory);
  }
  if(input.peek()!=EOF)throw std::runtime_error("Unexpected trailing spatial data");REPORT("SPATIAL_LAB_RESULT cases=%u outputs=%u failures=0 validation=%d",count,count*4,validation);return 0;
 }catch(const std::exception& error){REPORT("SPATIAL_LAB_RESULT failures=1 error=%s",error.what());return 1;}
}
#ifdef THOR_SPATIAL_LAB_JNI
extern "C" JNIEXPORT jint JNICALL Java_com_thor_spatialqa_SpatialInstrumentation_runLab(JNIEnv* env,jclass,jstring path,jstring output){const char* p=env->GetStringUTFChars(path,nullptr);const char* o=env->GetStringUTFChars(output,nullptr);int result=runLab(p,o,true);env->ReleaseStringUTFChars(path,p);env->ReleaseStringUTFChars(output,o);return result;}
#else
int main(int argc,char** argv){return argc==3?runLab(argv[1],argv[2],false):2;}
#endif
