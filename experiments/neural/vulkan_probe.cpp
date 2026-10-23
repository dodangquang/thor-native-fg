#include "engine.hpp"
#include "neural_shaders.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

struct Layer {uint32_t inputs,outputs,weights,bias;};
int main(int argc,char** argv){
 VkInstance instance{};VkDevice device{};
 try{
  if(argc<4)throw std::runtime_error("Usage: thor-neural-vulkan WEIGHTS WIDTH HEIGHT [INPUT.rgba8 OUTPUT.rgba8]");
  uint32_t width=std::stoul(argv[2]),height=std::stoul(argv[3]);if(width<16||height<16||width>1920||height>1080)throw std::runtime_error("Invalid dimensions");
  bool anime=std::string(argv[1])=="anime4k",naive=argc>6&&std::string(argv[6])=="naive";
  std::ifstream model;char magic[8]{};uint32_t vectorCount=4;Layer layers[4]{};std::vector<float> weights(16);
  if(anime){for(int i=0;i<4;i++)layers[i]={uint32_t(i?4:3),4,0,0};}
  else{
  model.open(argv[1],std::ios::binary);model.read(magic,8);model.read((char*)&vectorCount,4);model.read((char*)layers,sizeof(layers));
  if(!model||std::memcmp(magic,"THORQS2\0",8)||vectorCount>65536||!vectorCount)throw std::runtime_error("Invalid packed model header");
  weights.resize(vectorCount*4);model.read((char*)weights.data(),weights.size()*4);if(!model||model.peek()!=EOF)throw std::runtime_error("Invalid packed weights size");
  for(int i=0;i<4;i++)if(layers[i].inputs!=(i?32:3)||layers[i].outputs!=(i==3?12:32)||layers[i].bias!=layers[i].weights+(layers[i].outputs/4)*9*((layers[i].inputs+3)/4)*4||layers[i].bias+layers[i].outputs/4>vectorCount)throw std::runtime_error("Unexpected layer layout");
  }
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;app.pApplicationName="Thor direct Vulkan neural SR probe";
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;requireVk(vkCreateInstance(&ci,nullptr,&instance),"NeuralInstance");
  uint32_t count=0;vkEnumeratePhysicalDevices(instance,&count,nullptr);if(!count)throw std::runtime_error("No Vulkan GPU");std::vector<VkPhysicalDevice> physical(count);vkEnumeratePhysicalDevices(instance,&count,physical.data());auto gpu=physical[0];
  VkPhysicalDeviceProperties properties{};VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceProperties(gpu,&properties);vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
  if(width*8>properties.limits.maxImageDimension2D)throw std::runtime_error("Packed features exceed image width limit");
  if(!anime&&!naive&&properties.limits.maxComputeSharedMemorySize<17408)throw std::runtime_error("Neural shared tile exceeds device limit");
  VkFormatProperties format{};vkGetPhysicalDeviceFormatProperties(gpu,VK_FORMAT_R16G16B16A16_SFLOAT,&format);if((format.optimalTilingFeatures&(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))!=(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))throw std::runtime_error("RGBA16F storage/sampling unsupported");
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());uint32_t family=0;
  while(family<count&&(!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)||!families[family].timestampValidBits))family++;if(family==count)throw std::runtime_error("No timestamp compute queue");
  float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkPhysicalDeviceFeatures supported{},features{};vkGetPhysicalDeviceFeatures(gpu,&supported);features.shaderStorageImageExtendedFormats=supported.shaderStorageImageExtendedFormats;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.pEnabledFeatures=&features;requireVk(vkCreateDevice(gpu,&di,nullptr,&device),"NeuralDevice");
  Api a;a.load(device,vkGetDeviceProcAddr);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);
  {
   Engine e;e.a=&a;e.memory=memory;
   auto imageUsage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
   e.history[0]=e.createImage({width,height},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
   e.history[1]=e.createImage({width*(anime?1:8),height},VK_FORMAT_R16G16B16A16_SFLOAT,imageUsage);
   e.flow=e.createImage({width*(anime?1:8),height},VK_FORMAT_R16G16B16A16_SFLOAT,imageUsage);
   e.upCurrent=e.createImage({width*2,height*2},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
   VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;requireVk(a.CreateSampler(device,&sc,nullptr,&e.sampler),"NeuralSampler");
   VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,5},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=5;dp.poolSizeCount=3;dp.pPoolSizes=sizes;requireVk(a.CreateDescriptorPool(device,&dp,nullptr,&e.descriptors),"NeuralDescriptors");
   auto makeBuffer=[&](VkDeviceSize bytes,VkBufferUsageFlags usage,VkBuffer& buffer,VkDeviceMemory& allocation,void*& mapped){VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes;bc.usage=usage;requireVk(a.CreateBuffer(device,&bc,nullptr,&buffer),"NeuralBuffer");VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=e.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(device,&ma,nullptr,&allocation),"NeuralBufferMemory");requireVk(a.BindBufferMemory(device,buffer,allocation,0),"NeuralBufferBind");requireVk(a.MapMemory(device,allocation,0,VK_WHOLE_SIZE,0,&mapped),"NeuralBufferMap");};
   makeBuffer(weights.size()*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,e.stats,e.statsMemory,e.statsMapped);std::memcpy(e.statsMapped,weights.data(),weights.size()*4);
   VkBuffer staging{};VkDeviceMemory stagingMemory{};void* mapped{};size_t inputBytes=size_t(width)*height*4,outputBytes=inputBytes*4;makeBuffer(outputBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,staging,stagingMemory,mapped);
   auto* pixels=(uint8_t*)mapped;for(size_t i=0;i<inputBytes;i++)pixels[i]=i%4==3?255:uint8_t((i*13+i/97)%256);
   if(argc>4){std::ifstream input(argv[4],std::ios::binary);input.read((char*)mapped,inputBytes);if(!input||input.peek()!=EOF)throw std::runtime_error("Input image size mismatch");}
   auto S=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,I=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,B=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   if(anime){e.analyzePipe=e.createPipeline(neural_anime0_spv,sizeof(neural_anime0_spv),{S,I,B},32);e.qualityPipe=e.createPipeline(neural_anime1_spv,sizeof(neural_anime1_spv),{S,I,B},32);e.flowPipe=e.createPipeline(neural_anime2_spv,sizeof(neural_anime2_spv),{S,I,B},32);e.flowReferencePipe=e.createPipeline(neural_anime3_spv,sizeof(neural_anime3_spv),{S,I,B},32);e.upscalePipe=e.createPipeline(neural_anime4_spv,sizeof(neural_anime4_spv),{S,I,S},8);}
   else{e.flowPipe=e.createPipeline(naive?neural_conv_naive_spv:neural_conv_spv,naive?sizeof(neural_conv_naive_spv):sizeof(neural_conv_spv),{S,I,B},32);e.upscalePipe=e.createPipeline(neural_shuffle_spv,sizeof(neural_shuffle_spv),{S,I,S},8);}
   Pipeline* pipelines[]={anime?&e.analyzePipe:&e.flowPipe,anime?&e.qualityPipe:&e.flowPipe,&e.flowPipe,anime?&e.flowReferencePipe:&e.flowPipe,&e.upscalePipe};
   VkDescriptorSet sets[5]{};Image* sources[]={&e.history[0],&e.history[1],&e.flow,&e.history[1]};Image* destinations[]={&e.history[1],&e.flow,&e.history[1],&e.flow};
   for(int i=0;i<4;i++){sets[i]=e.allocate(*pipelines[i]);e.sampled(sets[i],0,*sources[i]);e.storage(sets[i],1,*destinations[i]);VkDescriptorBufferInfo bi{e.stats,0,weights.size()*4};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[i];w.dstBinding=2;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&bi;a.UpdateDescriptorSets(device,1,&w,0,nullptr);}
   sets[4]=e.allocate(e.upscalePipe);e.sampled(sets[4],0,e.flow);e.storage(sets[4],1,e.upCurrent);e.sampled(sets[4],2,e.history[0]);
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"NeuralPool");VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"NeuralCommand");
   VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(device,&fi,nullptr,&fence),"NeuralFence");VkQueryPool query{};VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=6;requireVk(a.CreateQueryPool(device,&qp,nullptr,&query),"NeuralQueries");
   uint64_t mask=families[family].timestampValidBits>=64?UINT64_MAX:(uint64_t(1)<<families[family].timestampValidBits)-1;
   int iteration=0,recorded=0;std::vector<double> samples[6];auto start=std::chrono::steady_clock::now();
   while(recorded<20){
    bool measure=iteration>=20&&std::chrono::steady_clock::now()-start>=std::chrono::seconds(1);requireVk(a.ResetCommandPool(device,pool,0),"NeuralReset");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&begin),"NeuralBegin");
    if(!iteration){e.barrier(cmd,e.history[0].image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};a.CmdCopyBufferToImage(cmd,staging,e.history[0].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);e.barrier(cmd,e.history[0].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);for(Image* im:{&e.history[1],&e.flow,&e.upCurrent})e.barrier(cmd,im->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);}
    a.CmdResetQueryPool(cmd,query,0,6);a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query,0);
    for(int i=0;i<5;i++){
     auto& pipeline=*pipelines[i];a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&sets[i],0,nullptr);
     if(i<4){auto& layer=layers[i];int32_t push[]={int32_t(width),int32_t(height),int32_t(layer.inputs),int32_t(layer.outputs),int32_t((layer.inputs+3)/4),int32_t(layer.weights),int32_t(layer.bias),i==0?1:0};a.CmdPushConstants(cmd,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);a.CmdDispatch(cmd,(width+7)/8,(height+7)/8,layer.outputs/4);}
     else{int32_t push[]={int32_t(width),int32_t(height)};a.CmdPushConstants(cmd,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);a.CmdDispatch(cmd,(width*2+7)/8,(height*2+7)/8,1);}
     VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query,i+1);
    }
    if(measure&&recorded==19&&argc>5){e.barrier(cmd,e.upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width*2,height*2,1};a.CmdCopyImageToBuffer(cmd,e.upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,staging,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);}
    requireVk(a.EndCommandBuffer(cmd),"NeuralEnd");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;requireVk(a.ResetFences(device,1,&fence),"NeuralResetFence");requireVk(a.QueueSubmit(queue,1,&submit,fence),"NeuralSubmit");requireVk(a.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"NeuralComplete");
    uint64_t ticks[6]{};requireVk(a.GetQueryPoolResults(device,query,0,6,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"NeuralTimes");if(measure){for(int i=0;i<5;i++)samples[i].push_back(double((ticks[i+1]-ticks[i])&mask)*properties.limits.timestampPeriod/1e6);samples[5].push_back(double((ticks[5]-ticks[0])&mask)*properties.limits.timestampPeriod/1e6);recorded++;}iteration++;
   }
   if(argc>5){std::ofstream output(argv[5],std::ios::binary);output.write((const char*)mapped,outputBytes);if(!output)throw std::runtime_error("Cannot write GPU output fixture");}
   printf("VULKAN_NEURAL gpu=%s model=%s convolution=%s input=%ux%u output=%ux%u arithmetic=fp32 feature_storage=rgba16f samples=%d warmup_frames=%d cpu_fallback=0\n",properties.deviceName,anime?"Anime4K_CNN_S":"QuickSRNetSmall2x",anime?"constant":naive?"naive":"shared",width,height,width*2,height*2,recorded,iteration-recorded);const char* names[]={"conv0","conv1","conv2","conv3","shuffle","total"};for(int i=0;i<6;i++){auto& values=samples[i];std::sort(values.begin(),values.end());printf("VULKAN_NEURAL_STAGE name=%s median_ms=%.4f p95_ms=%.4f min_ms=%.4f max_ms=%.4f\n",names[i],values[values.size()/2],values[size_t(values.size()*.95)],values.front(),values.back());}
   a.UnmapMemory(device,stagingMemory);a.DestroyBuffer(device,staging,nullptr);a.FreeMemory(device,stagingMemory,nullptr);a.DestroyQueryPool(device,query,nullptr);a.DestroyFence(device,fence,nullptr);a.DestroyCommandPool(device,pool,nullptr);
  }
  a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);return 0;
 }catch(const std::exception& error){fprintf(stderr,"VULKAN_NEURAL_ERROR %s\n",error.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 1;}
}
