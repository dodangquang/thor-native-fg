#include "engine.hpp"
#include <android/log.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <jni.h>
#define INFO(...) do{printf(__VA_ARGS__);printf("\n");__android_log_print(ANDROID_LOG_INFO,"ThorNeuralQA",__VA_ARGS__);}while(false)

static int runNeuralTests(const char* path,bool validation){
 VkInstance instance{};VkDevice device{};int checks=0,failures=0;
 try{
  std::ifstream fixtures(path,std::ios::binary);char magic[8]{};uint32_t count=0;fixtures.read(magic,8);fixtures.read((char*)&count,4);
  if(!fixtures||std::memcmp(magic,"THORNSR1",8)||!count||count>16)throw std::runtime_error("Invalid neural fixture header");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;app.pApplicationName="Thor neural SR correctness";
  const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};features.enabledValidationFeatureCount=1;features.pEnabledValidationFeatures=&sync;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
  if(validation){ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&extension;ci.pNext=&features;}
  requireVk(vkCreateInstance(&ci,nullptr,&instance),"NeuralTestInstance");
  uint32_t n=0;vkEnumeratePhysicalDevices(instance,&n,nullptr);if(!n)throw std::runtime_error("No GPU");std::vector<VkPhysicalDevice> physical(n);vkEnumeratePhysicalDevices(instance,&n,physical.data());auto gpu=physical[0];
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families.data());uint32_t family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;if(family==n)throw std::runtime_error("No compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
  // Deliberately request no optional image/float16 features, as a normal game
  // may not enable them. These shaders use only core RGBA16F storage formats.
  requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"NeuralTestDevice");Api a;a.load(device,vkGetDeviceProcAddr);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(gpu,&memory);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);
  for(uint32_t test=0;test<count;test++){
   uint32_t shape[8]{};fixtures.read((char*)shape,sizeof(shape));auto [sw,sh,cx,cy,width,height,ow,oh]=std::array<uint32_t,8>{shape[0],shape[1],shape[2],shape[3],shape[4],shape[5],shape[6],shape[7]};
   if(!fixtures||!width||!height||sw>64||sh>64||ow>128||oh>128||cx+width>sw||cy+height>sh)throw std::runtime_error("Invalid neural fixture dimensions");
   size_t inputBytes=size_t(sw)*sh*4,outputBytes=size_t(ow)*oh*4;std::vector<uint8_t> input(inputBytes),expected(outputBytes);fixtures.read((char*)input.data(),inputBytes);fixtures.read((char*)expected.data(),outputBytes);if(!fixtures)throw std::runtime_error("Truncated neural fixture");
   Engine e;e.initSpatial(a,memory,{width,height},{ow,oh},SpatialModel::Neural2D);auto source=e.createImage({sw,sh},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
   VkBuffer buffer{};VkDeviceMemory allocation{};void* mapped{};VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=std::max(inputBytes,outputBytes);bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&bc,nullptr,&buffer),"NeuralTestBuffer");VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=e.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(device,&ma,nullptr,&allocation),"NeuralTestMemory");requireVk(a.BindBufferMemory(device,buffer,allocation,0),"NeuralTestBind");requireVk(a.MapMemory(device,allocation,0,VK_WHOLE_SIZE,0,&mapped),"NeuralTestMap");
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"NeuralTestPool");VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"NeuralTestCommand");VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"NeuralTestFence");
   for(int repeat=0;repeat<2;repeat++){
    std::memcpy(mapped,input.data(),inputBytes);requireVk(a.ResetCommandPool(device,pool,0),"NeuralTestReset");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&begin),"NeuralTestBegin");e.barrier(cmd,source.image,repeat?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,repeat?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={sw,sh,1};a.CmdCopyBufferToImage(cmd,buffer,source.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[0]={int32_t(cx),int32_t(cy),0};region.srcOffsets[1]={int32_t(cx+width),int32_t(cy+height),1};region.dstOffsets[1]={int32_t(ow),int32_t(oh),1};e.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);
    // The staging buffer was read by upload before it is overwritten by the
    // output readback. Include that WAR dependency explicitly for sync QA.
    VkMemoryBarrier war{VK_STRUCTURE_TYPE_MEMORY_BARRIER};war.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;war.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&war,0,nullptr,0,nullptr);
    copy.imageExtent={ow,oh,1};a.CmdCopyImageToBuffer(cmd,e.upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);requireVk(a.EndCommandBuffer(cmd),"NeuralTestEnd");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;requireVk(a.ResetFences(device,1,&fence),"NeuralTestResetFence");requireVk(a.QueueSubmit(queue,1,&submit,fence),"NeuralTestSubmit");requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"NeuralTestComplete");
    int largest=0,alpha=0;for(size_t i=0;i<outputBytes;i++){int difference=std::abs(int(((uint8_t*)mapped)[i])-int(expected[i]));largest=std::max(largest,difference);if(i%4==3)alpha=std::max(alpha,difference);}
    if(largest>2||alpha>1){INFO("NEURAL_FIXTURE FAIL case=%u repeat=%d max_byte_error=%d alpha_error=%d",test,repeat,largest,alpha);++failures;}
    ++checks;if(largest<=2&&alpha<=1)INFO("NEURAL_FIXTURE PASS case=%u repeat=%d crop=%u,%u,%ux%u output=%ux%u max_byte_error=%d alpha_error=%d",test,repeat,cx,cy,width,height,ow,oh,largest,alpha);
   }
   a.UnmapMemory(device,allocation);a.DestroyBuffer(device,buffer,nullptr);a.FreeMemory(device,allocation,nullptr);a.DestroyFence(device,fence,nullptr);a.DestroyCommandPool(device,pool,nullptr);a.DestroyImageView(device,source.view,nullptr);a.DestroyImage(device,source.image,nullptr);a.FreeMemory(device,source.memory,nullptr);
  }
  if(fixtures.peek()!=EOF)throw std::runtime_error("Unexpected trailing neural fixture data");a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);instance={};INFO("NEURAL_RESULT checks=%d failures=%d validation=%d",checks,failures,validation);return failures?1:0;
 }catch(const std::exception& error){INFO("NEURAL_RESULT checks=%d failures=1 error=%s",checks,error.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 1;}
}
#ifdef THOR_NEURAL_JNI
extern "C" JNIEXPORT jint JNICALL Java_com_thor_neuralqa_HeadlessInstrumentation_runTests(JNIEnv* env,jclass,jstring path){const char* value=env->GetStringUTFChars(path,nullptr);int result=runNeuralTests(value,true);env->ReleaseStringUTFChars(path,value);return result;}
#else
int main(int argc,char** argv){if(argc!=2)return 2;return runNeuralTests(argv[1],false);}
#endif
