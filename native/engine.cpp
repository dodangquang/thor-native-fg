#include "engine.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <cstring>
uint32_t Engine::memoryType(uint32_t bits,VkMemoryPropertyFlags flags){
 for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
 throw std::runtime_error("No compatible Vulkan memory type");
}
Image Engine::createImage(VkExtent2D size,VkFormat format,VkImageUsageFlags usage){
 Image im{};im.size=size;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={size.width,size.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
 requireVk(a->CreateImage(a->device,&ci,nullptr,&im.image),"CreateImage");
 try{
  VkMemoryRequirements req{};a->GetImageMemoryRequirements(a->device,im.image,&req);VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  requireVk(a->AllocateMemory(a->device,&alloc,nullptr,&im.memory),"AllocateImageMemory");requireVk(a->BindImageMemory(a->device,im.image,im.memory,0),"BindImageMemory");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=im.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};requireVk(a->CreateImageView(a->device,&vi,nullptr,&im.view),"CreateImageView");
 }catch(...){if(im.image)a->DestroyImage(a->device,im.image,nullptr);if(im.memory)a->FreeMemory(a->device,im.memory,nullptr);throw;}
 return im;
}
Pipeline Engine::createPipeline(const uint32_t* code,size_t bytes,const std::vector<VkDescriptorType>& types,uint32_t pushBytes){
 Pipeline p{};std::vector<VkDescriptorSetLayoutBinding> bindings;
 for(uint32_t i=0;i<types.size();i++)bindings.push_back({i,types[i],1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
 VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=bindings.size();si.pBindings=bindings.data();requireVk(a->CreateDescriptorSetLayout(a->device,&si,nullptr,&p.setLayout),"CreateDescriptorSetLayout");
 VkShaderModule module{};
 try{
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,pushBytes};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&p.setLayout;li.pushConstantRangeCount=pushBytes?1:0;li.pPushConstantRanges=&range;requireVk(a->CreatePipelineLayout(a->device,&li,nullptr,&p.layout),"CreatePipelineLayout");
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=bytes;mi.pCode=code;requireVk(a->CreateShaderModule(a->device,&mi,nullptr,&module),"CreateShaderModule");
  VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=p.layout;pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};requireVk(a->CreateComputePipelines(a->device,VK_NULL_HANDLE,1,&pi,nullptr,&p.pipeline),"CreateComputePipeline");
 }catch(...){if(module)a->DestroyShaderModule(a->device,module,nullptr);if(p.layout)a->DestroyPipelineLayout(a->device,p.layout,nullptr);a->DestroyDescriptorSetLayout(a->device,p.setLayout,nullptr);throw;}
 a->DestroyShaderModule(a->device,module,nullptr);return p;
}
VkDescriptorSet Engine::allocate(const Pipeline& p){VkDescriptorSet set{};VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&p.setLayout;requireVk(a->AllocateDescriptorSets(a->device,&ai,&set),"AllocateDescriptorSet");return set;}
void Engine::sampled(VkDescriptorSet set,uint32_t binding,const Image& im,bool nearest){VkDescriptorImageInfo ii{nearest?nearestSampler:sampler,im.view,VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&ii;a->UpdateDescriptorSets(a->device,1,&w,0,nullptr);}
void Engine::storage(VkDescriptorSet set,uint32_t binding,const Image& im){VkDescriptorImageInfo ii{VK_NULL_HANDLE,im.view,VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&ii;a->UpdateDescriptorSets(a->device,1,&w,0,nullptr);}
void Engine::buffer(VkDescriptorSet set,uint32_t binding){VkDescriptorBufferInfo bi{stats,0,sizeof(QualityStats)};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&bi;a->UpdateDescriptorSets(a->device,1,&w,0,nullptr);}
void Engine::init(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out,bool cooperative){
 a=&api;memory=mem;input=in;output=out;cooperativeFlow=cooperative;grid={32,std::max(1u,(32*in.height/in.width))};
 VkImageUsageFlags usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 history[0]=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,usage);history[1]=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,usage);
 flow=createImage(grid,VK_FORMAT_R32G32B32A32_SFLOAT,usage);mid=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,usage);upCurrent=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,usage);upMid=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,usage);
 VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=sizeof(QualityStats);bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a->CreateBuffer(a->device,&bc,nullptr,&stats),"CreateStats");
 VkMemoryRequirements req{};a->GetBufferMemoryRequirements(a->device,stats,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a->AllocateMemory(a->device,&ma,nullptr,&statsMemory),"AllocateStats");requireVk(a->BindBufferMemory(a->device,stats,statsMemory,0),"BindStats");
 requireVk(a->MapMemory(a->device,statsMemory,0,sizeof(QualityStats),0,&statsMapped),"MapStats");
 VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sc.maxLod=0;requireVk(a->CreateSampler(a->device,&sc,nullptr,&sampler),"CreateSampler");sc.magFilter=sc.minFilter=VK_FILTER_NEAREST;requireVk(a->CreateSampler(a->device,&sc,nullptr,&nearestSampler),"CreateNearestSampler");
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,40},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,16},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,12}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=20;dp.poolSizeCount=3;dp.pPoolSizes=sizes;requireVk(a->CreateDescriptorPool(a->device,&dp,nullptr,&descriptors),"CreateDescriptorPool");
 const auto S=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,I=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,B=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
 analyzePipe=createPipeline(analyze_spv,sizeof(analyze_spv),{S,S,B},24);qualityPipe=createPipeline(quality_spv,sizeof(quality_spv),{B},24);
 flowPipe=createPipeline(flow_spv,sizeof(flow_spv),{S,S,I,B},24);flowReferencePipe=createPipeline(flow_reference_spv,sizeof(flow_reference_spv),{S,S,I,B},24);interpolatePipe=createPipeline(interpolate_spv,sizeof(interpolate_spv),{S,S,S,I,B},24);upscalePipe=createPipeline(upscale_spv,sizeof(upscale_spv),{S,I},8);
 for(int i=0;i<2;i++){
  analyzeSets[i]=allocate(analyzePipe);sampled(analyzeSets[i],0,history[1-i]);sampled(analyzeSets[i],1,history[i]);buffer(analyzeSets[i],2);
  flowSets[i]=allocate(flowPipe);sampled(flowSets[i],0,history[1-i]);sampled(flowSets[i],1,history[i]);storage(flowSets[i],2,flow);buffer(flowSets[i],3);
  interpolateSets[i]=allocate(interpolatePipe);sampled(interpolateSets[i],0,history[1-i]);sampled(interpolateSets[i],1,history[i]);sampled(interpolateSets[i],2,flow,true);storage(interpolateSets[i],3,mid);buffer(interpolateSets[i],4);
  upSets[i]=allocate(upscalePipe);sampled(upSets[i],0,history[i]);storage(upSets[i],1,upCurrent);
 }
 upSets[2]=allocate(upscalePipe);sampled(upSets[2],0,mid);storage(upSets[2],1,upMid);
 qualitySet=allocate(qualityPipe);buffer(qualitySet,0);
}
void Engine::initSpatial(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out){
 a=&api;memory=mem;input=in;output=out;
 history[0]=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
 upCurrent=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
 VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
 requireVk(a->CreateSampler(a->device,&sc,nullptr,&sampler),"SpatialSampler");
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
 VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;requireVk(a->CreateDescriptorPool(a->device,&dp,nullptr,&descriptors),"SpatialDescriptors");
 upscalePipe=createPipeline(upscale_spv,sizeof(upscale_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
 upSets[0]=allocate(upscalePipe);sampled(upSets[0],0,history[0]);storage(upSets[0],1,upCurrent);
}
void Engine::recordSpatial(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,const VkImageBlit& region){
 barrier(cmd,history[0].image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
 barrier(cmd,upCurrent.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
 barrier(cmd,source,sourceLayout,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 VkImageBlit copy=region;copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstOffsets[0]={0,0,0};copy.dstOffsets[1]={int32_t(input.width),int32_t(input.height),1};
 a->CmdBlitImage(cmd,source,VK_IMAGE_LAYOUT_GENERAL,history[0].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy,VK_FILTER_NEAREST);
 barrier(cmd,source,VK_IMAGE_LAYOUT_GENERAL,sourceLayout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
 barrier(cmd,history[0].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 int32_t size[]={int32_t(output.width),int32_t(output.height)};
 a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,upscalePipe.pipeline);
 a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,upscalePipe.layout,0,1,&upSets[0],0,nullptr);
 a->CmdPushConstants(cmd,upscalePipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
 a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 initialized=true;
}
void Engine::barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
void Engine::blit(VkCommandBuffer cmd,VkImage src,VkExtent2D s,VkImage dst,VkExtent2D d){VkImageBlit b{};b.srcSubresource=b.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};b.srcOffsets[1]={int32_t(s.width),int32_t(s.height),1};b.dstOffsets[1]={int32_t(d.width),int32_t(d.height),1};a->CmdBlitImage(cmd,src,VK_IMAGE_LAYOUT_GENERAL,dst,VK_IMAGE_LAYOUT_GENERAL,1,&b,VK_FILTER_NEAREST);}
void Engine::record(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter,const EngineTimings* timings){
 auto stamp=[&](uint32_t index){if(timings&&timings->pool)a->CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timings->pool,timings->first+index);};stamp(0);
 if(!initialized){a->CmdFillBuffer(cmd,stats,0,sizeof(QualityStats),0);for(Image* im:{&history[0],&history[1],&flow,&mid,&upCurrent,&upMid}){barrier(cmd,im->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a->CmdClearColorImage(cmd,im->image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);barrier(cmd,im->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);}initialized=true;}
 barrier(cmd,source,sourceLayout,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 blit(cmd,source,input,history[write].image,input);
 barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,nullptr,0,nullptr);
 // Leave the persistent cut holdoff untouched between input frames.
 a->CmdFillBuffer(cmd,stats,0,offsetof(QualityStats,holdoff),0);mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 struct {int32_t input[2],grid[2];float phase;int32_t history;} push{{int32_t(input.width),int32_t(input.height)},{int32_t(grid.width),int32_t(grid.height)},0.5f,hasHistory?1:0};
 auto dispatch=[&](Pipeline& p,VkDescriptorSet set,uint32_t w,uint32_t h,const void* constants,uint32_t bytes){a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&set,0,nullptr);a->CmdPushConstants(cmd,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,bytes,constants);a->CmdDispatch(cmd,(w+7)/8,(h+7)/8,1);};
 stamp(1);
 dispatch(analyzePipe,analyzeSets[write],grid.width*2,grid.height*2,&push,sizeof(push));
 mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(2);dispatch(cooperativeFlow?flowPipe:flowReferencePipe,flowSets[write],grid.width*(cooperativeFlow?8:1),grid.height*(cooperativeFlow?8:1),&push,sizeof(push));mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(3);dispatch(qualityPipe,qualitySet,1,1,&push,sizeof(push));a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(4);dispatch(interpolatePipe,interpolateSets[write],input.width,input.height,&push,sizeof(push));a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(5);
 if(filter){
  int32_t outSize[]={int32_t(output.width),int32_t(output.height)};dispatch(upscalePipe,upSets[write],output.width,output.height,outSize,8);dispatch(upscalePipe,upSets[2],output.width,output.height,outSize,8);
  barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);barrier(cmd,upMid.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 }else{
  // Present-time FG runs at display resolution. Preserve the application's real
  // frame exactly and avoid filtering a frame that was already spatially upscaled.
  barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  barrier(cmd,mid.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  for(Image* image:{&upCurrent,&upMid})barrier(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  blit(cmd,history[write].image,input,upCurrent.image,output);blit(cmd,mid.image,input,upMid.image,output);
  for(Image* image:{&upCurrent,&upMid})barrier(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 }
 barrier(cmd,source,VK_IMAGE_LAYOUT_GENERAL,sourceLayout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
 mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(6);
 hasHistory=true;write=1-write;
}
QualityStats Engine::readQuality()const{QualityStats result{};if(statsMapped)std::memcpy(&result,statsMapped,sizeof(result));return result;}
void Engine::release(){
 if(!a)return;
 for(auto* p:{&analyzePipe,&qualityPipe,&flowPipe,&flowReferencePipe,&interpolatePipe,&upscalePipe}){if(p->pipeline)a->DestroyPipeline(a->device,p->pipeline,nullptr);if(p->layout)a->DestroyPipelineLayout(a->device,p->layout,nullptr);if(p->setLayout)a->DestroyDescriptorSetLayout(a->device,p->setLayout,nullptr);*p={};}
 if(descriptors)a->DestroyDescriptorPool(a->device,descriptors,nullptr);if(sampler)a->DestroySampler(a->device,sampler,nullptr);if(nearestSampler)a->DestroySampler(a->device,nearestSampler,nullptr);
 if(statsMapped)a->UnmapMemory(a->device,statsMemory);statsMapped=nullptr;if(stats)a->DestroyBuffer(a->device,stats,nullptr);if(statsMemory)a->FreeMemory(a->device,statsMemory,nullptr);
 for(Image* im:{&history[0],&history[1],&flow,&mid,&upCurrent,&upMid}){if(im->view)a->DestroyImageView(a->device,im->view,nullptr);if(im->image)a->DestroyImage(a->device,im->image,nullptr);if(im->memory)a->FreeMemory(a->device,im->memory,nullptr);*im={};}
 a=nullptr;
}
