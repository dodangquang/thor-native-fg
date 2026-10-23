#include "temporal.hpp"
#include "temporal_shaders.hpp"
#include <cmath>
#include <cstring>

void TemporalEngine::sampled(VkDescriptorSet set,uint32_t binding,const TemporalImage& image,VkSampler sampler){
 VkDescriptorImageInfo info{sampler,image.view,image.layout};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=binding;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;resources.a->UpdateDescriptorSets(resources.a->device,1,&write,0,nullptr);
}
bool TemporalEngine::valid(const TemporalFrame& f)const{
 if(!ready||f.params.renderSize[0]!=input.width||f.params.renderSize[1]!=input.height||f.params.displaySize[0]!=output.width||f.params.displaySize[1]!=output.height)return false;
 for(const auto* image:{&f.color,&f.depth,&f.velocity})if(!image->view||image->size.width!=input.width||image->size.height!=input.height||(image->layout!=VK_IMAGE_LAYOUT_GENERAL&&image->layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&!(image==&f.depth&&image->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)))return false;
 if(f.color.format!=VK_FORMAT_R8G8B8A8_UNORM&&f.color.format!=VK_FORMAT_R16G16B16A16_SFLOAT)return false;
 if(f.depth.format!=VK_FORMAT_R32_SFLOAT&&f.depth.format!=VK_FORMAT_D16_UNORM&&f.depth.format!=VK_FORMAT_D24_UNORM_S8_UINT&&f.depth.format!=VK_FORMAT_D32_SFLOAT)return false;
 if(f.velocity.format!=VK_FORMAT_R16G16_SFLOAT&&f.velocity.format!=VK_FORMAT_R32G32_SFLOAT&&f.velocity.format!=VK_FORMAT_R16G16B16A16_SFLOAT&&f.velocity.format!=VK_FORMAT_R32G32B32A32_SFLOAT)return false;
 for(int axis=0;axis<2;axis++)if(!std::isfinite(f.params.jitterOffset[axis])||std::abs(f.params.jitterOffset[axis])>.5f)return false;
 if(f.params.preExposure!=1||!std::isfinite(f.params.cameraFovAngleHor)||f.params.cameraFovAngleHor<=0||!std::isfinite(f.params.cameraNear)||f.params.cameraNear<=0||!std::isfinite(f.params.minLerpContribution)||f.params.minLerpContribution<0||f.params.minLerpContribution>1||f.params.reset>1||f.params.sameCamera>1)return false;
 double norm=0;for(float value:f.params.clipToPrevClip){if(!std::isfinite(value))return false;norm+=std::abs(value);}return norm>0;
}
void TemporalEngine::init(Api& api,const VkPhysicalDeviceMemoryProperties& memory,VkQueue serialQueue,VkExtent2D in,VkExtent2D out,uint32_t capacity){
 if(resources.a)throw std::runtime_error("Temporal context already initialized");
 if(!serialQueue||!api.GetFenceStatus||!api.device||capacity<1||capacity>8||in.width<16||in.height<16||out.width>1920||out.height>1080||out.width<=in.width||out.height<=in.height)throw std::runtime_error("Unsupported temporal context configuration");
 resources.a=&api;resources.memory=memory;queue=serialQueue;input=in;output=out;sequence=TemporalSequence(capacity);write=0;
 try{
  const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  motion=resources.createImage(in,VK_FORMAT_R16G16B16A16_SFLOAT,usage);for(auto& image:history)image=resources.createImage(out,VK_FORMAT_R16G16B16A16_SFLOAT,usage);
  VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;requireVk(api.CreateSampler(api.device,&sc,nullptr,&resources.sampler),"TemporalSampler");sc.magFilter=sc.minFilter=VK_FILTER_NEAREST;requireVk(api.CreateSampler(api.device,&sc,nullptr,&resources.nearestSampler),"TemporalNearest");
  std::vector<VkDescriptorType> types(12,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);types[0]=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;types[10]=types[11]=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  resources.analyzePipe=resources.createPipeline(temporal_convert_spv,sizeof(temporal_convert_spv),types,0);resources.upscalePipe=resources.createPipeline(temporal_upscale_spv,sizeof(temporal_upscale_spv),types,0);
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,18*capacity},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2*capacity},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4*capacity}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=2*capacity;dp.poolSizeCount=3;dp.pPoolSizes=sizes;requireVk(api.CreateDescriptorPool(api.device,&dp,nullptr,&resources.descriptors),"TemporalDescriptors");
  slots.resize(capacity);
  for(auto& slot:slots){
   slot.output=resources.createImage(out,VK_FORMAT_R16G16B16A16_SFLOAT,usage);slot.convert=resources.allocate(resources.analyzePipe);slot.upscale=resources.allocate(resources.upscalePipe);
   VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=sizeof(TemporalParams);bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;requireVk(api.CreateBuffer(api.device,&bi,nullptr,&slot.ubo),"TemporalUBO");VkMemoryRequirements req{};api.GetBufferMemoryRequirements(api.device,slot.ubo,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=resources.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(api.AllocateMemory(api.device,&ma,nullptr,&slot.memory),"TemporalUBOMemory");requireVk(api.BindBufferMemory(api.device,slot.ubo,slot.memory,0),"TemporalUBOBind");requireVk(api.MapMemory(api.device,slot.memory,0,VK_WHOLE_SIZE,0,&slot.mapped),"TemporalUBOMap");
   VkDescriptorBufferInfo ub{slot.ubo,0,sizeof(TemporalParams)};VkWriteDescriptorSet bw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};bw.dstBinding=0;bw.descriptorCount=1;bw.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bw.pBufferInfo=&ub;for(auto set:{slot.convert,slot.upscale}){bw.dstSet=set;api.UpdateDescriptorSets(api.device,1,&bw,0,nullptr);}
   resources.storage(slot.convert,10,motion);resources.sampled(slot.upscale,8,motion);resources.storage(slot.upscale,10,slot.output);
  }
  ready=true;
 }catch(...){destroy();throw;}
}
TemporalStatus TemporalEngine::record(VkCommandBuffer cmd,const TemporalFrame& f,TemporalOutput& result){
 if(!cmd||!valid(f))return TemporalStatus::InvalidInput;
 TemporalTicket ticket;bool first=!sequence.hasHistory();auto status=sequence.begin(f.frame,f.scene,f.params.reset!=0,ticket);if(status!=TemporalStatus::Ready)return status;
 auto& slot=slots[ticket.slot];auto& a=*resources.a;TemporalParams params=f.params;params.reset=ticket.reset?1u:0u;
 for(int i=0;i<2;i++){params.renderSizeRcp[i]=1.0f/f.params.renderSize[i];params.displaySizeRcp[i]=1.0f/f.params.displaySize[i];}std::memcpy(slot.mapped,&params,sizeof(params));
 sampled(slot.convert,2,f.depth,resources.nearestSampler);sampled(slot.convert,3,f.velocity,resources.nearestSampler);sampled(slot.upscale,1,f.color,resources.sampler);resources.sampled(slot.upscale,7,history[1-write]);resources.storage(slot.upscale,11,history[write]);
 auto memory=[&](VkAccessFlags src,VkAccessFlags dst){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=src;b.dstAccessMask=dst;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);};
 memory(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
 if(first){
  resources.barrier(cmd,motion.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  for(auto& image:history){resources.barrier(cmd,image.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue clear{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a.CmdClearColorImage(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&range);}
  for(auto& item:slots)resources.barrier(cmd,item.output.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  memory(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 }
 auto dispatch=[&](Pipeline& pipeline,VkDescriptorSet set,VkExtent2D size){a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&set,0,nullptr);a.CmdDispatch(cmd,(size.width+7)/8,(size.height+7)/8,1);};
 dispatch(resources.analyzePipe,slot.convert,input);memory(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);dispatch(resources.upscalePipe,slot.upscale,output);memory(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT);
 result={ticket,slot.output.image,slot.output.view,output,VK_FORMAT_R16G16B16A16_SFLOAT};return TemporalStatus::Ready;
}
TemporalStatus TemporalEngine::submitted(const TemporalTicket& ticket,VkQueue submittedQueue,VkFence fence,VkResult result){
 if(!ready||!sequence.isRecorded(ticket))return TemporalStatus::InvalidTicket;
 if(submittedQueue!=queue||!fence||result!=VK_SUCCESS){uncertain=true;sequence.fail();return TemporalStatus::Faulted;}
 auto status=sequence.submitted(ticket);if(status==TemporalStatus::Ready){slots[ticket.slot].fence=fence;write=1-write;}return status;
}
TemporalStatus TemporalEngine::abort(const TemporalTicket& ticket){return ready?sequence.abort(ticket):TemporalStatus::InvalidTicket;}
TemporalStatus TemporalEngine::retire(const TemporalTicket& ticket){
 if(!ready||!sequence.isSubmitted(ticket))return TemporalStatus::InvalidTicket;
 auto& slot=slots[ticket.slot];auto result=resources.a->GetFenceStatus(resources.a->device,slot.fence);
 if(result==VK_NOT_READY)return TemporalStatus::Busy;
 if(result!=VK_SUCCESS){uncertain=true;sequence.fail();return TemporalStatus::Faulted;}
 slot.fence={};return sequence.retired(ticket);
}
TemporalStatus TemporalEngine::release(){if(!resources.a)return TemporalStatus::Ready;if(uncertain)return TemporalStatus::Faulted;if(!sequence.idle())return TemporalStatus::Busy;destroy();return TemporalStatus::Ready;}
void TemporalEngine::destroy()noexcept{
 if(!resources.a)return;auto& a=*resources.a;
 // Defensive destructor cleanup only; the normal explicit release is nonblocking.
 if(sequence.submittedCount()||uncertain)a.DeviceWaitIdle(a.device);
 auto image=[&](Image& im){if(im.view)a.DestroyImageView(a.device,im.view,nullptr);if(im.image)a.DestroyImage(a.device,im.image,nullptr);if(im.memory)a.FreeMemory(a.device,im.memory,nullptr);im={};};
 for(auto& slot:slots){if(slot.mapped)a.UnmapMemory(a.device,slot.memory);if(slot.ubo)a.DestroyBuffer(a.device,slot.ubo,nullptr);if(slot.memory)a.FreeMemory(a.device,slot.memory,nullptr);image(slot.output);}slots.clear();image(motion);for(auto& im:history)image(im);resources.release();ready=false;uncertain=false;queue={};
}
