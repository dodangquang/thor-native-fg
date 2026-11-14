#include "engine.hpp"
#include "shaders.hpp"
#include "aa_scene.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Headless anti-aliasing bench: renders the synthetic scene at the input size,
// runs the same spatial path the layer uses (blit -> upscale/AA -> output) and
// writes one PPM for visual comparison. CPU wall time per frame is reported so
// quality changes can be weighed against cost.
static void writePpm(const char* path,const uint8_t* rgba,uint32_t width,uint32_t height){
 FILE* file=std::fopen(path,"wb");if(!file)throw std::runtime_error("ppm open failed");
 std::fprintf(file,"P6\n%u %u\n255\n",width,height);
 std::vector<uint8_t> row(size_t(width)*3);
 for(uint32_t y=0;y<height;y++){
  for(uint32_t x=0;x<width;x++){const uint8_t* p=rgba+(size_t(y)*width+x)*4;row[size_t(x)*3]=p[0];row[size_t(x)*3+1]=p[1];row[size_t(x)*3+2]=p[2];}
  std::fwrite(row.data(),1,row.size(),file);
 }
 std::fclose(file);
}
int main(int argc,char** argv){
 VkInstance instance{};VkDevice device{};
 try{
  uint32_t inW=640,inH=360,outW=1280,outH=720;const char* mode="smaa";const char* out="/data/local/tmp/aa.ppm";int frames=120;
  for(int i=1;i<argc;i++){
   if(!std::strcmp(argv[i],"-mode")&&i+1<argc)mode=argv[++i];
   else if(!std::strcmp(argv[i],"-in")&&i+1<argc)std::sscanf(argv[++i],"%ux%u",&inW,&inH);
   else if(!std::strcmp(argv[i],"-out")&&i+1<argc)std::sscanf(argv[++i],"%ux%u",&outW,&outH);
   else if(!std::strcmp(argv[i],"-ppm")&&i+1<argc)out=argv[++i];
   else if(!std::strcmp(argv[i],"-frames")&&i+1<argc)frames=std::atoi(argv[++i]);
  }
  bool maskMode=!std::strcmp(mode,"mask");bool smaa=!std::strcmp(mode,"smaa")||maskMode,fxaa=!std::strcmp(mode,"fxaa"),taa=!std::strcmp(mode,"taa"),off=!std::strcmp(mode,"off");
  if(off){smaa=fxaa=taa=false;}
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Thor AA bench";app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;requireVk(vkCreateInstance(&ci,nullptr,&instance),"AaInstance");
  uint32_t count=0;requireVk(vkEnumeratePhysicalDevices(instance,&count,nullptr),"AaPhysCount");if(!count)throw std::runtime_error("No GPU");
  std::vector<VkPhysicalDevice> physical(count);requireVk(vkEnumeratePhysicalDevices(instance,&count,physical.data()),"AaPhys");auto gpu=physical[0];
  VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(gpu,&properties);
  VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
  uint32_t family=0;while(family<count&&(!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)||!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)))family++;
  if(family==count)throw std::runtime_error("No compute+graphics queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"AaDevice");
  Api a;a.load(device,vkGetDeviceProcAddr);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);
  {
   Engine engine;engine.initSpatial(a,memory,{inW,inH},{outW,outH},SpatialModel::Sgsr1,fxaa,false,0.6f,taa,0.12f,smaa);
   auto source=engine.createImage({inW,inH},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
   auto scene=engine.createPipeline(aa_scene_spv,sizeof(aa_scene_spv),{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},16);VkDescriptorPoolSize sceneSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1};VkDescriptorPoolCreateInfo scenePool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};scenePool.maxSets=1;scenePool.poolSizeCount=1;scenePool.pPoolSizes=&sceneSize;VkDescriptorPool sceneDescriptors{};requireVk(a.CreateDescriptorPool(device,&scenePool,nullptr,&sceneDescriptors),"AaScenePool");VkDescriptorSetAllocateInfo sceneAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sceneAlloc.descriptorPool=sceneDescriptors;sceneAlloc.descriptorSetCount=1;sceneAlloc.pSetLayouts=&scene.setLayout;VkDescriptorSet set{};requireVk(a.AllocateDescriptorSets(device,&sceneAlloc,&set),"AaSceneSet");engine.storage(set,0,source);
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"AaPool");
   VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"AaCmd");
   VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"AaFence");
   VkBuffer readback{};VkDeviceMemory readbackMemory{};void* pixels{};VkDeviceSize bytes=VkDeviceSize(outW)*outH*4;
   VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&bc,nullptr,&readback),"AaBuffer");
   VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,readback,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=engine.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   requireVk(a.AllocateMemory(device,&ma,nullptr,&readbackMemory),"AaMemory");requireVk(a.BindBufferMemory(device,readback,readbackMemory,0),"AaBind");requireVk(a.MapMemory(device,readbackMemory,0,VK_WHOLE_SIZE,0,&pixels),"AaMap");
   VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={int32_t(inW),int32_t(inH),1};region.dstOffsets[1]={int32_t(inW),int32_t(inH),1};
   double total=0;int measured=0;
   for(int frame=0;frame<frames;frame++){
    requireVk(a.ResetCommandPool(device,pool,0),"AaReset");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&bi),"AaBegin");
    struct{int32_t size[2];float time;int32_t cut;}push{{int32_t(inW),int32_t(inH)},float(frame)/30.0f,0};
    engine.barrier(cmd,source.image,frame?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,frame?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
    a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.layout,0,1,&set,0,nullptr);a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&push);a.CmdDispatch(cmd,(inW+7)/8,(inH+7)/8,1);
    engine.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    engine.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);
    VkImage output=maskMode?engine.aaEdges.image:engine.spatialOutput();
    engine.barrier(cmd,output,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={outW,outH,1};a.CmdCopyImageToBuffer(cmd,output,VK_IMAGE_LAYOUT_GENERAL,readback,1,&copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    requireVk(a.EndCommandBuffer(cmd),"AaEnd");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;requireVk(a.ResetFences(device,1,&fence),"AaResetFence");
    auto start=std::chrono::steady_clock::now();requireVk(a.QueueSubmit(queue,1,&submit,fence),"AaSubmit");requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"AaWait");
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(frame>=20){total+=ms;++measured;}
    if(frame==frames-1)writePpm(out,(const uint8_t*)pixels,outW,outH);
   }
   printf("AA_BENCH gpu=%s mode=%s in=%ux%u out=%ux%u frames=%d median_ms=%.3f\n",properties.deviceName,mode,inW,inH,outW,outH,measured,total/std::max(1,measured));
   a.UnmapMemory(device,readbackMemory);a.DestroyBuffer(device,readback,nullptr);a.FreeMemory(device,readbackMemory,nullptr);a.DestroyFence(device,fence,nullptr);a.DestroyCommandPool(device,pool,nullptr);
   a.DestroyPipeline(device,scene.pipeline,nullptr);a.DestroyPipelineLayout(device,scene.layout,nullptr);a.DestroyDescriptorSetLayout(device,scene.setLayout,nullptr);
   a.DestroyImageView(device,source.view,nullptr);a.DestroyImage(device,source.image,nullptr);a.FreeMemory(device,source.memory,nullptr);
  }
  a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);return 0;
 }catch(const std::exception& error){fprintf(stderr,"ERROR %s\n",error.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 1;}
}
