#include "engine.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Headless saturated-workload profiling. These timestamps exclude source scene
// rendering, acquisition, presentation and input latency. No device settings change.
int main(int argc,char** argv){
 VkInstance instance{};VkDevice device{};
 try{
  uint32_t width=argc>1?std::atoi(argv[1]):1920,height=argc>2?std::atoi(argv[2]):1080;
  if(width<64||height<64||width>4096||height>4096)throw std::runtime_error("Invalid benchmark dimensions");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Thor GPU stage benchmark";app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;requireVk(vkCreateInstance(&ci,nullptr,&instance),"BenchInstance");
  uint32_t count=0;requireVk(vkEnumeratePhysicalDevices(instance,&count,nullptr),"BenchPhysicalCount");if(!count)throw std::runtime_error("No GPU");
  std::vector<VkPhysicalDevice> physical(count);requireVk(vkEnumeratePhysicalDevices(instance,&count,physical.data()),"BenchPhysical");auto gpu=physical[0];
  VkPhysicalDeviceProperties properties{};VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceProperties(gpu,&properties);vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
  uint32_t family=0;while(family<count&&(!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)||!families[family].timestampValidBits))family++;
  if(family==count)throw std::runtime_error("No timestamp-capable compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"BenchDevice");
  Api a;a.load(device,vkGetDeviceProcAddr);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);
  {
   bool serial=argc>3&&!std::strcmp(argv[3],"serial"),toggle=argc>3&&!std::strcmp(argv[3],"compare_toggle"),compare=argc>3&&!std::strcmp(argv[3],"compare");compare|=toggle;
   Engine engine;engine.init(a,memory,{width,height},{width,height},!serial);
   Engine reference;if(compare)reference.init(a,memory,{width,height},{width,height},false);
   auto source=engine.createImage({width,height},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
   auto scene=engine.createPipeline(scene_spv,sizeof(scene_spv),{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},16);auto set=engine.allocate(scene);engine.storage(set,0,source);
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"BenchPool");
   VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"BenchCommand");
   VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"BenchFence");
   EngineTimings timing{};VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=7;requireVk(a.CreateQueryPool(device,&query,nullptr,&timing.pool),"BenchQueries");
   VkDeviceSize flowBytes=VkDeviceSize(engine.grid.width)*engine.grid.height*16,rgbaBytes=VkDeviceSize(width)*height*4;
   VkBuffer readback{};VkDeviceMemory readbackMemory{};void* pixels{};int compared=0;
   if(compare){
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=2*flowBytes+5*rgbaBytes;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&bc,nullptr,&readback),"CompareBuffer");
    VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,readback,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=engine.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    requireVk(a.AllocateMemory(device,&ma,nullptr,&readbackMemory),"CompareMemory");requireVk(a.BindBufferMemory(device,readback,readbackMemory,0),"CompareBind");requireVk(a.MapMemory(device,readbackMemory,0,VK_WHOLE_SIZE,0,&pixels),"CompareMap");
   }
   uint64_t mask=families[family].timestampValidBits>=64?UINT64_MAX:(uint64_t(1)<<families[family].timestampValidBits)-1;
   std::vector<double> samples[7];int iteration=0,recorded=0;using Clock=std::chrono::steady_clock;auto start=Clock::now();
   while(recorded<80){
    bool measure=iteration>=20&&Clock::now()-start>=std::chrono::seconds(1);if(toggle)engine.cooperativeFlow=iteration%2==0;
    requireVk(a.ResetCommandPool(device,pool,0),"BenchReset");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&bi),"BenchBegin");
    engine.barrier(cmd,source.image,iteration?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,iteration?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
    struct{int32_t size[2];float time;int32_t cut;} push{{int32_t(width),int32_t(height)},float(iteration)/30,0};
    if(compare){int phase=(iteration/20)%4;push.time=phase==0?0:phase==2?float(iteration)*.2f:float(iteration)/30;push.cut=phase==3?iteration%2:0;if(iteration%57==0)engine.hasHistory=reference.hasHistory=false;}
    a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.layout,0,1,&set,0,nullptr);a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&push);a.CmdDispatch(cmd,(width+7)/8,(height+7)/8,1);
    a.CmdResetQueryPool(cmd,timing.pool,0,7);engine.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,false,&timing);
    if(compare){
     reference.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,false);
     auto copy=[&](const Image& image,VkDeviceSize offset){engine.barrier(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy region{};region.bufferOffset=offset;region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={image.size.width,image.size.height,1};a.CmdCopyImageToBuffer(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,readback,1,&region);};
     copy(engine.flow,0);copy(reference.flow,flowBytes);copy(engine.mid,2*flowBytes);copy(reference.mid,2*flowBytes+rgbaBytes);copy(engine.upCurrent,2*flowBytes+2*rgbaBytes);copy(reference.upCurrent,2*flowBytes+3*rgbaBytes);copy(source,2*flowBytes+4*rgbaBytes);
     VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    }
    requireVk(a.EndCommandBuffer(cmd),"BenchEnd");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;requireVk(a.ResetFences(device,1,&fence),"BenchResetFence");requireVk(a.QueueSubmit(queue,1,&submit,fence),"BenchSubmit");requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"BenchComplete");
    uint64_t ticks[7]{};requireVk(a.GetQueryPoolResults(device,timing.pool,0,7,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"BenchReadQueries");
    if(compare){
     auto* data=static_cast<const uint8_t*>(pixels);auto quality=engine.readQuality(),original=reference.readQuality();
     if(std::memcmp(data,data+flowBytes,flowBytes)||std::memcmp(data+2*flowBytes,data+2*flowBytes+rgbaBytes,rgbaBytes)||std::memcmp(data+2*flowBytes+2*rgbaBytes,data+2*flowBytes+3*rgbaBytes,rgbaBytes)||std::memcmp(data+2*flowBytes+2*rgbaBytes,data+2*flowBytes+4*rgbaBytes,rgbaBytes)||std::memcmp(&quality,&original,sizeof(quality))){fprintf(stderr,"COMPARE FAIL frame=%d phase=%d\n",iteration,(iteration/20)%4);throw std::runtime_error("Cooperative/serial GPU outputs differ");}
     compared++;
    }
    if(measure){for(int stage=0;stage<6;stage++)samples[stage].push_back(double((ticks[stage+1]-ticks[stage])&mask)*properties.limits.timestampPeriod/1e6);samples[6].push_back(double((ticks[6]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6);recorded++;}iteration++;
   }
   const char* names[]={"copy_reset","analyze","flow","quality","interpolate","finish_copies","total"};
   printf("GPU_BENCH gpu=%s width=%u height=%u samples=%d warmup_frames=%d filter=0 flow=%s compare=%d\n",properties.deviceName,width,height,recorded,iteration-recorded,toggle?"alternating":serial?"serial":"cooperative",compare);
   for(int stage=0;stage<7;stage++){auto& values=samples[stage];std::sort(values.begin(),values.end());printf("GPU_STAGE name=%s median_ms=%.4f p95_ms=%.4f min_ms=%.4f max_ms=%.4f\n",names[stage],values[values.size()/2],values[size_t(values.size()*.95)],values.front(),values.back());}
   if(compare)printf("COMPARE PASS frames=%d flow=bit_exact midpoint=bit_exact quality=bit_exact real=bit_exact_source fixture=stable_motion_fast_motion_cuts_history_reset\n",compared);
   if(pixels)a.UnmapMemory(device,readbackMemory);if(readback)a.DestroyBuffer(device,readback,nullptr);if(readbackMemory)a.FreeMemory(device,readbackMemory,nullptr);
   a.DestroyQueryPool(device,timing.pool,nullptr);a.DestroyFence(device,fence,nullptr);a.DestroyCommandPool(device,pool,nullptr);
   a.DestroyPipeline(device,scene.pipeline,nullptr);a.DestroyPipelineLayout(device,scene.layout,nullptr);a.DestroyDescriptorSetLayout(device,scene.setLayout,nullptr);
   a.DestroyImageView(device,source.view,nullptr);a.DestroyImage(device,source.image,nullptr);a.FreeMemory(device,source.memory,nullptr);
  }
  a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);instance={};return 0;
 }catch(const std::exception& error){fprintf(stderr,"ERROR %s\n",error.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 1;}
}
