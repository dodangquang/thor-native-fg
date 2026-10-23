#include "engine.hpp"
#include "gsr2_shaders.hpp"
#include "lab_device.hpp"
#include <jni.h>
#include <fstream>
#include <chrono>
#include <android/log.h>
#include <cstring>

struct Buffer {
 Device& g;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};
 Buffer(Device& gpu,VkDeviceSize bytes,VkBufferUsageFlags usage):g(gpu){
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;requireVk(g.a.CreateBuffer(g.device,&ci,nullptr,&buffer),"GS2Buffer");VkMemoryRequirements req{};g.a.GetBufferMemoryRequirements(g.device,buffer,&req);
  Engine helper;helper.a=&g.a;helper.memory=g.memory;VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=helper.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);helper.a=nullptr;requireVk(g.a.AllocateMemory(g.device,&ma,nullptr,&memory),"GS2BufferMemory");requireVk(g.a.BindBufferMemory(g.device,buffer,memory,0),"GS2BufferBind");requireVk(g.a.MapMemory(g.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"GS2Map");
 }
 ~Buffer(){if(mapped)g.a.UnmapMemory(g.device,memory);if(buffer)g.a.DestroyBuffer(g.device,buffer,nullptr);if(memory)g.a.FreeMemory(g.device,memory,nullptr);}
};
static int run(const char* path,const char* directory){
 try{
  std::ifstream in(path,std::ios::binary);char magic[8];uint32_t header[5]{};in.read(magic,8);in.read((char*)header,sizeof(header));uint32_t n=header[0],w=header[1],h=header[2],ow=header[3],oh=header[4];
  if(!in||std::memcmp(magic,"THORGS2A",8)||n!=32||!w||!h||ow!=w*2||oh!=h*2||w>960||h>544)throw std::runtime_error("Invalid GSR2 fixture");
  Device g;g.init(true);Engine e;e.a=&g.a;e.memory=g.memory;auto& a=g.a;
  const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  std::vector<Image> images;images.reserve(8);
  for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_UINT})images.push_back(e.createImage({w,h},format,usage));
  for(int k=0;k<3;k++)images.push_back(e.createImage({ow,oh},VK_FORMAT_R16G16B16A16_SFLOAT,usage));
  VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;requireVk(a.CreateSampler(g.device,&sc,nullptr,&e.sampler),"GS2Sampler");sc.magFilter=sc.minFilter=VK_FILTER_NEAREST;requireVk(a.CreateSampler(g.device,&sc,nullptr,&e.nearestSampler),"GS2Nearest");
  std::vector<VkDescriptorType> types(12,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);types[0]=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;types[10]=types[11]=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  e.analyzePipe=e.createPipeline(convert_spv,sizeof(convert_spv),types,0);e.upscalePipe=e.createPipeline(upscale_spv,sizeof(upscale_spv),types,0);
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,18},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=2;dp.poolSizeCount=3;dp.pPoolSizes=sizes;requireVk(a.CreateDescriptorPool(g.device,&dp,nullptr,&e.descriptors),"GS2Descriptors");
  auto convert=e.allocate(e.analyzePipe),up=e.allocate(e.upscalePipe);
  Buffer staging(g,VkDeviceSize(w)*h*24+VkDeviceSize(ow)*oh*8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT),ubo(g,144,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  VkDescriptorBufferInfo bi{ubo.buffer,0,144};VkWriteDescriptorSet bw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};bw.dstBinding=0;bw.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bw.descriptorCount=1;bw.pBufferInfo=&bi;for(auto set:{convert,up}){bw.dstSet=set;a.UpdateDescriptorSets(g.device,1,&bw,0,nullptr);}
  e.sampled(convert,1,images[0],true);e.sampled(convert,2,images[1],true);e.sampled(convert,3,images[2],true);e.storage(convert,10,images[3]);e.storage(convert,11,images[4]);
  e.sampled(up,8,images[3]);e.sampled(up,9,images[4],true);e.storage(up,10,images[5]);
  // Fragment-math adapter samples original RGB; compute variant ignores this.
  e.sampled(up,1,images[0]);
  VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=g.family;requireVk(a.CreateCommandPool(g.device,&pi,nullptr,&pool),"GS2Pool");VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;requireVk(a.AllocateCommandBuffers(g.device,&ca,&cmd),"GS2Command");VkFence fence{};VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(g.device,&fi,nullptr,&fence),"GS2Fence");VkQueryPool qp{};VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qi.queryType=VK_QUERY_TYPE_TIMESTAMP;qi.queryCount=3;requireVk(a.CreateQueryPool(g.device,&qi,nullptr,&qp),"GS2Queries");
  auto barrier=[&](){VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;mb.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,nullptr,0,nullptr);};
  for(uint32_t frame=0;frame<n;frame++){
   in.read((char*)ubo.mapped,144);in.read((char*)staging.mapped,size_t(w)*h*24);if(!in)throw std::runtime_error("Truncated GSR2 fixture");
   e.sampled(up,7,images[6+(1-frame%2)]);e.storage(up,11,images[6+frame%2]);
   requireVk(a.ResetCommandPool(g.device,pool,0),"GS2ResetPool");VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&cb),"GS2Begin");
   if(!frame){for(auto& im:images){e.barrier(cmd,im.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a.CmdClearColorImage(cmd,im.image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);}}barrier();
   VkDeviceSize offset=0;for(int k=0;k<3;k++){VkBufferImageCopy c{};c.bufferOffset=offset;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={w,h,1};a.CmdCopyBufferToImage(cmd,staging.buffer,images[k].image,VK_IMAGE_LAYOUT_GENERAL,1,&c);offset+=VkDeviceSize(w)*h*(k==2?16:4);}barrier();
   a.CmdResetQueryPool(cmd,qp,0,3);a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qp,0);
   auto dispatch=[&](Pipeline& pipe,VkDescriptorSet set,uint32_t x,uint32_t y){a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.layout,0,1,&set,0,nullptr);a.CmdDispatch(cmd,(x+7)/8,(y+7)/8,1);};
   dispatch(e.analyzePipe,convert,w,h);barrier();a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qp,1);dispatch(e.upscalePipe,up,ow,oh);barrier();a.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qp,2);
   VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={ow,oh,1};a.CmdCopyImageToBuffer(cmd,images[5].image,VK_IMAGE_LAYOUT_GENERAL,staging.buffer,1,&c);if(frame<=1){c.bufferOffset=VkDeviceSize(ow)*oh*8;c.imageExtent={w,h,1};for(int k:{0,3,4}){a.CmdCopyImageToBuffer(cmd,images[k].image,VK_IMAGE_LAYOUT_GENERAL,staging.buffer,1,&c);c.bufferOffset+=VkDeviceSize(w)*h*(k==3?8:4);}}VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
   requireVk(a.EndCommandBuffer(cmd),"GS2End");VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;requireVk(a.ResetFences(g.device,1,&fence),"GS2ResetFence");requireVk(a.QueueSubmit(g.queue,1,&si,fence),"GS2Submit");requireVk(a.WaitForFences(g.device,1,&fence,VK_TRUE,UINT64_MAX),"GS2Wait");uint64_t ticks[3]{};requireVk(a.GetQueryPoolResults(g.device,qp,0,3,sizeof(ticks),ticks,8,VK_QUERY_RESULT_64_BIT),"GS2Times");uint64_t mask=g.bits>=64?UINT64_MAX:(uint64_t(1)<<g.bits)-1;double ms=double((ticks[2]-ticks[0])&mask)*g.properties.limits.timestampPeriod/1e6;
   char filename[80];snprintf(filename,sizeof(filename),"/frame-%03u.rgba16f",frame);std::ofstream output(std::string(directory)+filename,std::ios::binary);output.write((char*)staging.mapped,size_t(ow)*oh*8);if(!output)throw std::runtime_error("Cannot write GSR2 output");if(frame<=1){std::ofstream debug(std::string(directory)+(frame?"/diagnostic-001.bin":"/diagnostic.bin"),std::ios::binary);debug.write((char*)staging.mapped+size_t(ow)*oh*8,size_t(w)*h*16);}__android_log_print(ANDROID_LOG_INFO,"ThorGSR2Lab","GSR2_FRAME frame=%u gpu_ms=%.6f input=%ux%u output=%ux%u",frame,ms,w,h,ow,oh);
  }
  if(in.peek()!=EOF)throw std::runtime_error("Unexpected fixture tail");a.DestroyQueryPool(g.device,qp,nullptr);a.DestroyFence(g.device,fence,nullptr);a.DestroyCommandPool(g.device,pool,nullptr);for(auto& im:images){a.DestroyImageView(g.device,im.view,nullptr);a.DestroyImage(g.device,im.image,nullptr);a.FreeMemory(g.device,im.memory,nullptr);}
  __android_log_print(ANDROID_LOG_INFO,"ThorGSR2Lab","GSR2_RESULT frames=32 failures=0 validation=1");return 0;
 }catch(const std::exception& ex){__android_log_print(ANDROID_LOG_ERROR,"ThorGSR2Lab","GSR2_RESULT failures=1 error=%s",ex.what());return 1;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_thor_gsr2qa_SpatialInstrumentation_runLab(JNIEnv* env,jclass,jstring path,jstring output){auto p=env->GetStringUTFChars(path,nullptr);auto o=env->GetStringUTFChars(output,nullptr);int result=run(p,o);env->ReleaseStringUTFChars(path,p);env->ReleaseStringUTFChars(output,o);return result;}
