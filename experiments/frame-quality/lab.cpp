#include "engine.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

// Runs the production FG engine. Readbacks are not a frame-pacing or latency test.
int main(int argc,char** argv){
 if(argc!=3)return 2;
 VkInstance instance{};VkDevice device{};
 try{
  std::ifstream input(argv[1],std::ios::binary);char magic[8]{};uint32_t shape[3]{};
  input.read(magic,8);input.read((char*)shape,sizeof(shape));
  if(!input||memcmp(magic,"THORFGQ1",8)||shape[0]<16||shape[1]<16||shape[0]>1920||shape[1]>1080||!shape[2]||shape[2]>256)throw std::runtime_error("Invalid fixtures");
  VkExtent2D size{shape[0],shape[1]};size_t bytes=size_t(size.width)*size.height*4;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;requireVk(vkCreateInstance(&ci,nullptr,&instance),"Instance");
  uint32_t n=0;requireVk(vkEnumeratePhysicalDevices(instance,&n,nullptr),"GPUCount");if(!n)throw std::runtime_error("No GPU");
  std::vector<VkPhysicalDevice> physical(n);requireVk(vkEnumeratePhysicalDevices(instance,&n,physical.data()),"GPUs");auto gpu=physical[0];
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families.data());
  uint32_t family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;if(family==n)throw std::runtime_error("No compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"Device");
  Api a;a.load(device,vkGetDeviceProcAddr);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties mem{};vkGetPhysicalDeviceMemoryProperties(gpu,&mem);
  {
   Engine e;e.init(a,mem,size,size);auto source=e.createImage(size,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
   VkBuffer staging{};VkDeviceMemory allocation{};void* mapped{};
   VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&bc,nullptr,&staging),"Staging");
   VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,staging,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=e.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   requireVk(a.AllocateMemory(device,&ma,nullptr,&allocation),"Memory");requireVk(a.BindBufferMemory(device,staging,allocation,0),"Bind");requireVk(a.MapMemory(device,allocation,0,VK_WHOLE_SIZE,0,&mapped),"Map");
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"Pool");
   VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"Command");
   VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"Fence");
   auto begin=[&]{requireVk(a.ResetCommandPool(device,pool,0),"Reset");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&bi),"Begin");};
   auto submit=[&]{requireVk(a.EndCommandBuffer(cmd),"End");requireVk(a.ResetFences(device,1,&fence),"ResetFence");VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;requireVk(a.QueueSubmit(queue,1,&si,fence),"Submit");requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,5000000000ULL),"Wait");};
   bool initialized=false;std::ofstream metrics(std::string(argv[2])+"/metrics.csv");metrics<<"id,allow,reasons,valid,boundary,count\n";
   for(uint32_t id=0;id<shape[2];id++){
    e.hasHistory=false;
    for(int frame=0;frame<2;frame++){
     input.read((char*)mapped,bytes);if(!input)throw std::runtime_error("Truncated pair");begin();
     e.barrier(cmd,source.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);initialized=true;
     VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={size.width,size.height,1};a.CmdCopyBufferToImage(cmd,staging,source.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
     e.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,false);submit();
    }
    auto quality=e.readQuality();metrics<<id<<','<<quality.allow<<','<<quality.reasons<<','<<quality.valid<<','<<quality.boundary<<','<<quality.count<<'\n';
    begin();e.barrier(cmd,e.mid.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy read{};read.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};read.imageExtent={size.width,size.height,1};a.CmdCopyImageToBuffer(cmd,e.mid.image,VK_IMAGE_LAYOUT_GENERAL,staging,1,&read);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);submit();
    char name[32];snprintf(name,sizeof(name),"/%03u-mid.rgba",id);std::ofstream result(std::string(argv[2])+name,std::ios::binary);result.write((char*)mapped,bytes);if(!result)throw std::runtime_error("Output write failed");
    printf("FG_PAIR id=%u allow=%u reasons=%u\n",id,quality.allow,quality.reasons);
   }
   if(input.peek()!=EOF)throw std::runtime_error("Trailing input");
   a.DestroyFence(device,fence,nullptr);a.DestroyCommandPool(device,pool,nullptr);a.UnmapMemory(device,allocation);a.DestroyBuffer(device,staging,nullptr);a.FreeMemory(device,allocation,nullptr);
   a.DestroyImageView(device,source.view,nullptr);a.DestroyImage(device,source.image,nullptr);a.FreeMemory(device,source.memory,nullptr);
  }
  a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);instance={};printf("FG_LAB_RESULT pairs=%u failures=0\n",shape[2]);return 0;
 }catch(const std::exception& error){fprintf(stderr,"FG_LAB_ERROR %s\n",error.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 1;}
}
