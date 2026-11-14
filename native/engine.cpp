#include "engine.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <cstring>
#include <android/log.h>
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
 flow=createImage(grid,VK_FORMAT_R32G32B32A32_SFLOAT,usage);flowFine=createImage({grid.width*2,grid.height*2},VK_FORMAT_R32G32B32A32_SFLOAT,usage);flowCost=createImage(grid,VK_FORMAT_R16G16B16A16_SFLOAT,usage);mid=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,usage);mid2=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,usage);upCurrent=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,usage);upMid=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,usage);upMid2=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,usage);
 VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=sizeof(QualityStats);bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a->CreateBuffer(a->device,&bc,nullptr,&stats),"CreateStats");
 VkMemoryRequirements req{};a->GetBufferMemoryRequirements(a->device,stats,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a->AllocateMemory(a->device,&ma,nullptr,&statsMemory),"AllocateStats");requireVk(a->BindBufferMemory(a->device,stats,statsMemory,0),"BindStats");
 requireVk(a->MapMemory(a->device,statsMemory,0,sizeof(QualityStats),0,&statsMapped),"MapStats");
 VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sc.maxLod=0;requireVk(a->CreateSampler(a->device,&sc,nullptr,&sampler),"CreateSampler");sc.magFilter=sc.minFilter=VK_FILTER_NEAREST;requireVk(a->CreateSampler(a->device,&sc,nullptr,&nearestSampler),"CreateNearestSampler");
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,49},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,23},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,14}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=23;dp.poolSizeCount=3;dp.pPoolSizes=sizes;requireVk(a->CreateDescriptorPool(a->device,&dp,nullptr,&descriptors),"CreateDescriptorPool");
 const auto S=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,I=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,B=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
 analyzePipe=createPipeline(analyze_spv,sizeof(analyze_spv),{S,S,B},24);qualityPipe=createPipeline(quality_spv,sizeof(quality_spv),{B},24);
 flowPipe=createPipeline(flow_spv,sizeof(flow_spv),{S,S,I,B,I},24);flowReferencePipe=createPipeline(flow_reference_spv,sizeof(flow_reference_spv),{S,S,I,B,I},24);flowRefinePipe=createPipeline(flow_refine_spv,sizeof(flow_refine_spv),{S,S,S,I},24);interpolatePipe=createPipeline(interpolate_spv,sizeof(interpolate_spv),{S,S,S,I,B,S},24);interpolate2Pipe=createPipeline(interpolate2_spv,sizeof(interpolate2_spv),{S,S,S,I,B,S,I},28);upscalePipe=createPipeline(upscale_spv,sizeof(upscale_spv),{S,I},8);
 for(int i=0;i<2;i++){
  analyzeSets[i]=allocate(analyzePipe);sampled(analyzeSets[i],0,history[1-i]);sampled(analyzeSets[i],1,history[i]);buffer(analyzeSets[i],2);
  flowSets[i]=allocate(flowPipe);sampled(flowSets[i],0,history[1-i]);sampled(flowSets[i],1,history[i]);storage(flowSets[i],2,flow);storage(flowSets[i],4,flowCost);buffer(flowSets[i],3);
  flowRefineSets[i]=allocate(flowRefinePipe);sampled(flowRefineSets[i],0,history[1-i]);sampled(flowRefineSets[i],1,history[i]);sampled(flowRefineSets[i],2,flow,true);storage(flowRefineSets[i],3,flowFine);
  interpolateSets[i]=allocate(interpolatePipe);sampled(interpolateSets[i],0,history[1-i]);sampled(interpolateSets[i],1,history[i]);sampled(interpolateSets[i],2,flowFine,true);storage(interpolateSets[i],3,mid);buffer(interpolateSets[i],4);sampled(interpolateSets[i],5,flowCost);
  interpolate2Sets[i]=allocate(interpolate2Pipe);sampled(interpolate2Sets[i],0,history[1-i]);sampled(interpolate2Sets[i],1,history[i]);sampled(interpolate2Sets[i],2,flowFine,true);storage(interpolate2Sets[i],3,mid);buffer(interpolate2Sets[i],4);sampled(interpolate2Sets[i],5,flowCost);storage(interpolate2Sets[i],6,mid2);
  upSets[i]=allocate(upscalePipe);sampled(upSets[i],0,history[i]);storage(upSets[i],1,upCurrent);
 }
 upSets[2]=allocate(upscalePipe);sampled(upSets[2],0,mid);storage(upSets[2],1,upMid);
 upMid2Set=allocate(upscalePipe);sampled(upMid2Set,0,mid2);storage(upMid2Set,1,upMid2);
 qualitySet=allocate(qualityPipe);buffer(qualitySet,0);
}
void Engine::initSpatial(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out,SpatialModel model,bool antialias,bool sharpen,float sharpness,bool temporal,float temporalBlend,bool smaa){
 a=&api;memory=mem;input=in;output=out;spatialModel=model;fxaa=antialias&&!smaa;this->smaa=smaa;enableCAS=sharpen;casSharpness=sharpness;enableTAA=temporal;taaBlend=temporalBlend;taaHasHistory=false;write=0;initialized=false;
 VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
 requireVk(a->CreateSampler(a->device,&sc,nullptr,&sampler),"SpatialSampler");
 history[0]=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
 upCurrent=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
 if(enableTAA){
  motionTAA=model==SpatialModel::Spatial9;
  taaOutput=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
  taaHistory=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  if(motionTAA){
   // Reuse the frame generator's own optical-flow kernel at the game's low
   // input resolution so history gets reprojected instead of blended in
   // place. This only adds one more flow dispatch (cheap: it runs at input,
   // not output, resolution) on top of the already-cheap history tap.
   grid={32,std::max(1u,(32*in.height/in.width))};
   historyPrevLow=createImage(in,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
   flow=createImage(grid,VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
   flowCost=createImage(grid,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
   VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=sizeof(QualityStats);bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a->CreateBuffer(a->device,&bc,nullptr,&stats),"SpatialFlowStats");
   VkMemoryRequirements req{};a->GetBufferMemoryRequirements(a->device,stats,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a->AllocateMemory(a->device,&ma,nullptr,&statsMemory),"AllocateSpatialFlowStats");requireVk(a->BindBufferMemory(a->device,stats,statsMemory,0),"BindSpatialFlowStats");
   requireVk(a->MapMemory(a->device,statsMemory,0,sizeof(QualityStats),0,&statsMapped),"MapSpatialFlowStats");std::memset(statsMapped,0,sizeof(QualityStats));
   const auto S=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,I=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,B=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   flowPipe=createPipeline(flow_spv,sizeof(flow_spv),{S,S,I,B,I},24);
   VkDescriptorPoolSize flowSizes[]={{S,2},{I,2},{B,1}};
   VkDescriptorPoolCreateInfo flowPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};flowPool.maxSets=1;flowPool.poolSizeCount=3;flowPool.pPoolSizes=flowSizes;
   requireVk(a->CreateDescriptorPool(a->device,&flowPool,nullptr,&flowDescriptors),"FlowDescriptors");
   VkDescriptorSetAllocateInfo flowAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};flowAlloc.descriptorPool=flowDescriptors;flowAlloc.descriptorSetCount=1;flowAlloc.pSetLayouts=&flowPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&flowAlloc,&flowSets[0]),"FlowSet");
   sampled(flowSets[0],0,historyPrevLow);sampled(flowSets[0],1,history[0]);storage(flowSets[0],2,flow);storage(flowSets[0],4,flowCost);buffer(flowSets[0],3);
   sgsr2TemporalPipe=createPipeline(sgsr2_temporal_spv,sizeof(sgsr2_temporal_spv),{S,S,S,I},sizeof(TAAParams));
   VkDescriptorPoolSize temporalSizes[]={{S,3},{I,1}};
   VkDescriptorPoolCreateInfo temporalPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};temporalPool.maxSets=1;temporalPool.poolSizeCount=2;temporalPool.pPoolSizes=temporalSizes;
   requireVk(a->CreateDescriptorPool(a->device,&temporalPool,nullptr,&temporalDescriptors),"TemporalDescriptors");
   VkDescriptorSetAllocateInfo temporalAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};temporalAlloc.descriptorPool=temporalDescriptors;temporalAlloc.descriptorSetCount=1;temporalAlloc.pSetLayouts=&sgsr2TemporalPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&temporalAlloc,&temporalSet),"TemporalSet");
   sampled(temporalSet,0,upCurrent);sampled(temporalSet,1,taaHistory);sampled(temporalSet,2,flow);storage(temporalSet,3,taaOutput);
  }else{
   // No motion vectors for this model: cheap single history tap + clamp.
   VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
   VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
   requireVk(a->CreateDescriptorPool(a->device,&pool,nullptr,&taaDescriptors),"TaaDescriptors");
   taaPipe=createPipeline(taa_spv,sizeof(taa_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},sizeof(TAAParams));
   VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocateInfo.descriptorPool=taaDescriptors;allocateInfo.descriptorSetCount=1;allocateInfo.pSetLayouts=&taaPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&allocateInfo,&taaSet),"TaaSet");
   sampled(taaSet,0,upCurrent);sampled(taaSet,1,taaHistory);storage(taaSet,2,taaOutput);
  }
 }
 if(enableCAS){
  casOutput=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
  requireVk(a->CreateDescriptorPool(a->device,&pool,nullptr,&dlssDescriptors),"CasDescriptors");
  casPipe=createPipeline(cas_spv,sizeof(cas_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},sizeof(CASParams));
  VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocateInfo.descriptorPool=dlssDescriptors;allocateInfo.descriptorSetCount=1;allocateInfo.pSetLayouts=&casPipe.setLayout;
  requireVk(a->AllocateDescriptorSets(a->device,&allocateInfo,&casSet),"CasSet");sampled(casSet,0,hasTemporal()?taaOutput:upCurrent);storage(casSet,1,casOutput);
 }
 if(fxaa||smaa){
  aaOutput=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=smaa?2:1;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
  requireVk(a->CreateDescriptorPool(a->device,&pool,nullptr,&aaDescriptors),"AaDescriptors");
  const Image& aaInput=casPipe.pipeline?casOutput:(hasTemporal()?taaOutput:upCurrent);
  if(smaa){
   // Two passes: an edge mask, then a coverage-weighted blend. The mask lets the
   // blend pass measure how far a pixel sits from the ends of its edge run, so
   // long diagonals get a straight line instead of the old fixed-weight blur.
   aaEdges=createImage(out,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
   smaaEdgePipe=createPipeline(smaa_edge_spv,sizeof(smaa_edge_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
   smaaBlendPipe=createPipeline(smaa_blend_spv,sizeof(smaa_blend_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
   VkDescriptorSetAllocateInfo edgeAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};edgeAlloc.descriptorPool=aaDescriptors;edgeAlloc.descriptorSetCount=1;edgeAlloc.pSetLayouts=&smaaEdgePipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&edgeAlloc,&smaaEdgeSet),"SmaaEdgeSet");sampled(smaaEdgeSet,0,aaInput);storage(smaaEdgeSet,1,aaEdges);
   VkDescriptorSetAllocateInfo blendAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};blendAlloc.descriptorPool=aaDescriptors;blendAlloc.descriptorSetCount=1;blendAlloc.pSetLayouts=&smaaBlendPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&blendAlloc,&smaaBlendSet),"SmaaBlendSet");sampled(smaaBlendSet,0,aaInput);sampled(smaaBlendSet,1,aaEdges);storage(smaaBlendSet,2,aaOutput);
  }else{
   aaPipe=createPipeline(fxaa_spv,sizeof(fxaa_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
   VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocateInfo.descriptorPool=aaDescriptors;allocateInfo.descriptorSetCount=1;allocateInfo.pSetLayouts=&aaPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&allocateInfo,&aaSet),"AaSet");sampled(aaSet,0,aaInput);storage(aaSet,1,aaOutput);
  }
 }
 bool neural=model==SpatialModel::Neural2D||model==SpatialModel::NeuralBounded;
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,neural?8u:1u},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,neural?6u:1u}};
 VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=neural?6:1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;requireVk(a->CreateDescriptorPool(a->device,&dp,nullptr,&descriptors),"SpatialDescriptors");
 if(neural){
  const auto S=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,I=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  for(auto& feature:neuralFeatures)feature=createImage(in,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT);
  if(out.width!=in.width*2||out.height!=in.height*2)neural2x=createImage({in.width*2,in.height*2},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
  const uint32_t* code[]={neural0_spv,neural1_spv,neural2_spv,neural3_spv,neural4_spv,neural5_spv};size_t bytes[]={sizeof(neural0_spv),sizeof(neural1_spv),sizeof(neural2_spv),sizeof(neural3_spv),sizeof(neural4_spv),sizeof(neural5_spv)};
  for(int i=0;i<(neural2x.image?6:5);i++)neuralPipes[i]=createPipeline(code[i],bytes[i],i<4?std::vector<VkDescriptorType>{S,I}:std::vector<VkDescriptorType>{S,I,S},i<4?8:12);
  Image* inputs[]={&history[0],&neuralFeatures[0],&neuralFeatures[1],&neuralFeatures[0],&neuralFeatures[1]};
  Image* outputs[]={&neuralFeatures[0],&neuralFeatures[1],&neuralFeatures[0],&neuralFeatures[1],neural2x.image?&neural2x:&upCurrent};
  for(int i=0;i<5;i++){neuralSets[i]=allocate(neuralPipes[i]);sampled(neuralSets[i],0,*inputs[i]);storage(neuralSets[i],1,*outputs[i]);if(i==4)sampled(neuralSets[i],2,history[0]);}
  if(neural2x.image){neuralSets[5]=allocate(neuralPipes[5]);sampled(neuralSets[5],0,neural2x);storage(neuralSets[5],1,upCurrent);sampled(neuralSets[5],2,history[0]);}
  return;
 }
 upscalePipe=createPipeline(model==SpatialModel::Spatial9?sgsr2_spv:upscale_spv,model==SpatialModel::Spatial9?sizeof(sgsr2_spv):sizeof(upscale_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
 upSets[0]=allocate(upscalePipe);sampled(upSets[0],0,history[0]);storage(upSets[0],1,upCurrent);
}
void Engine::recordCAS(VkCommandBuffer cmd){
 if(!casPipe.pipeline)return;
 barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 barrier(cmd,casOutput.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
 // The sharpening kernel was tuned at a ~2x upscale. A bigger scale factor
 // spreads each source pixel over more output pixels (softer edges), so it
 // needs more sharpening; a small/near-1x factor already has crisp source
 // detail, so the same fixed strength would over-sharpen and ring. Scale
 // sharpness with the actual ratio instead of applying it unconditionally.
 float scaleFactor=input.width?float(output.width)/float(input.width):2.0f;
 float adaptive=casSharpness*std::clamp(scaleFactor/2.0f,0.5f,1.5f);
 CASParams params{(int32_t)output.width,(int32_t)output.height,adaptive,1};
 a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,casPipe.pipeline);
 a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,casPipe.layout,0,1,&casSet,0,nullptr);
 a->CmdPushConstants(cmd,casPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);
 a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 barrier(cmd,casOutput.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
}
void Engine::recordMotionFlow(VkCommandBuffer cmd){
 // Snapshot the still-current contents of history[0] (last frame's game
 // image) into historyPrevLow before the caller overwrites history[0] with
 // this frame's source, then match the two with the frame generator's own
 // flow kernel. Skipped on the very first frame, when there is nothing yet
 // to compare against.
 if(!taaHasHistory)return;
 barrier(cmd,historyPrevLow.image,motionFlowReady?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,motionFlowReady?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
 barrier(cmd,history[0].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 blit(cmd,history[0].image,input,historyPrevLow.image,input);
 barrier(cmd,historyPrevLow.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 barrier(cmd,history[0].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
 motionFlowReady=true;
}
void Engine::recordTAA(VkCommandBuffer cmd){
 if(!hasTemporal())return;
 if(motionTAA){
  barrier(cmd,flow.image,taaHasHistory?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,taaHasHistory?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
  if(taaHasHistory){
   struct {int32_t input[2],grid[2];float phase;int32_t history;} push{{int32_t(input.width),int32_t(input.height)},{int32_t(grid.width),int32_t(grid.height)},0.5f,1};
   a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,flowPipe.pipeline);
   a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,flowPipe.layout,0,1,&flowSets[0],0,nullptr);
   a->CmdPushConstants(cmd,flowPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
   a->CmdDispatch(cmd,grid.width,grid.height,1);
  }
  barrier(cmd,flow.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 }
 barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 barrier(cmd,taaOutput.image,taaHasHistory?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,taaHasHistory?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
 TAAParams params{(int32_t)output.width,(int32_t)output.height,taaBlend,taaHasHistory?0:1};
 const Pipeline& pipe=motionTAA?sgsr2TemporalPipe:taaPipe;const VkDescriptorSet& set=motionTAA?temporalSet:taaSet;
 a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.pipeline);
 a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe.layout,0,1,&set,0,nullptr);
 a->CmdPushConstants(cmd,pipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);
 a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 barrier(cmd,taaOutput.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
 // Copy this frame's result into the persistent history buffer for next
 // frame. Same extents, so this is a cheap GPU blit, not a filtered pass.
 barrier(cmd,taaHistory.image,taaHasHistory?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,taaHasHistory?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
 blit(cmd,taaOutput.image,output,taaHistory.image,output);
 barrier(cmd,taaHistory.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 taaHasHistory=true;
}
void Engine::recordAA(VkCommandBuffer cmd){
 if(!fxaa&&!smaa)return;
 VkImage inImage=casPipe.pipeline?casOutput.image:upCurrent.image;
 if(smaa){
  int32_t size[]={int32_t(output.width),int32_t(output.height)};
  barrier(cmd,inImage,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  barrier(cmd,aaEdges.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaEdgePipe.pipeline);
  a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaEdgePipe.layout,0,1,&smaaEdgeSet,0,nullptr);
  a->CmdPushConstants(cmd,smaaEdgePipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
  a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
  barrier(cmd,aaEdges.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  barrier(cmd,aaOutput.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaBlendPipe.pipeline);
  a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaBlendPipe.layout,0,1,&smaaBlendSet,0,nullptr);
  a->CmdPushConstants(cmd,smaaBlendPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
  a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
  barrier(cmd,aaOutput.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  return;
 }
 barrier(cmd,inImage,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 barrier(cmd,aaOutput.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
 int32_t size[]={int32_t(output.width),int32_t(output.height)};
 a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,aaPipe.pipeline);
 a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,aaPipe.layout,0,1,&aaSet,0,nullptr);
 a->CmdPushConstants(cmd,aaPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
 a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 barrier(cmd,aaOutput.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
}
void Engine::recordSpatial(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,const VkImageBlit& region){
 // Must run before history[0] below is overwritten with this frame's
 // source: it still holds the previous frame's image at this point.
 if(motionTAA)recordMotionFlow(cmd);
 barrier(cmd,history[0].image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
 barrier(cmd,upCurrent.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
 barrier(cmd,source,sourceLayout,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 VkImageBlit copy=region;copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstOffsets[0]={0,0,0};copy.dstOffsets[1]={int32_t(input.width),int32_t(input.height),1};
 a->CmdBlitImage(cmd,source,VK_IMAGE_LAYOUT_GENERAL,history[0].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy,VK_FILTER_NEAREST);
 barrier(cmd,source,VK_IMAGE_LAYOUT_GENERAL,sourceLayout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
 barrier(cmd,history[0].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(spatialModel==SpatialModel::Neural2D||spatialModel==SpatialModel::NeuralBounded){
  for(auto& feature:neuralFeatures)barrier(cmd,feature.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT):0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  if(neural2x.image)barrier(cmd,neural2x.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
  // Clamp once, at the final output resolution. Do not clamp an intermediate
  // 2x image before it is resized to another scale.
  int32_t size[]={int32_t(input.width),int32_t(input.height),spatialModel==SpatialModel::NeuralBounded&&!neural2x.image?1:0};
  for(int i=0;i<5;i++){
   auto& pipeline=neuralPipes[i];a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&neuralSets[i],0,nullptr);a->CmdPushConstants(cmd,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,i<4?8:12,size);a->CmdDispatch(cmd,((i==4?input.width*2:input.width)+7)/8,((i==4?input.height*2:input.height)+7)/8,1);
   VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memoryBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;memoryBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&memoryBarrier,0,nullptr,0,nullptr);
  }
  if(neural2x.image){
   barrier(cmd,neural2x.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
   // Resample neural RGB while taking alpha directly from the original crop,
   // avoiding two successive 8-bit alpha filters at non-integer output scales.
   int32_t outSize[]={int32_t(output.width),int32_t(output.height),spatialModel==SpatialModel::NeuralBounded?1:0};auto& pipeline=neuralPipes[5];a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&neuralSets[5],0,nullptr);a->CmdPushConstants(cmd,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,12,outSize);a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
  }
  barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);recordTAA(cmd);recordCAS(cmd);recordAA(cmd);initialized=true;return;
 }
 int32_t size[]={int32_t(output.width),int32_t(output.height)};
 a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,upscalePipe.pipeline);
 a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,upscalePipe.layout,0,1,&upSets[0],0,nullptr);
 a->CmdPushConstants(cmd,upscalePipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
 a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 recordTAA(cmd);recordCAS(cmd);recordAA(cmd);initialized=true;
}
void Engine::barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
void Engine::blit(VkCommandBuffer cmd,VkImage src,VkExtent2D s,VkImage dst,VkExtent2D d){VkImageBlit b{};b.srcSubresource=b.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};b.srcOffsets[1]={int32_t(s.width),int32_t(s.height),1};b.dstOffsets[1]={int32_t(d.width),int32_t(d.height),1};a->CmdBlitImage(cmd,src,VK_IMAGE_LAYOUT_GENERAL,dst,VK_IMAGE_LAYOUT_GENERAL,1,&b,VK_FILTER_NEAREST);}
void Engine::initFrameAA(){
 if(fgAaLazyDone)return;
 aaEdges=createImage(output,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
 aaReal=createImage(output,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
 aaMid=createImage(output,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4}};
 VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=4;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
 requireVk(a->CreateDescriptorPool(a->device,&pool,nullptr,&fgAaPool),"FgAaPool");
 if(smaa){
  smaaEdgePipe=createPipeline(smaa_edge_spv,sizeof(smaa_edge_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
  smaaBlendPipe=createPipeline(smaa_blend_spv,sizeof(smaa_blend_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
  for(int slot=0;slot<2;slot++){
   const Image& src=slot?upMid:upCurrent;const Image& dst=slot?aaMid:aaReal;
   VkDescriptorSetAllocateInfo edge{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};edge.descriptorPool=fgAaPool;edge.descriptorSetCount=1;edge.pSetLayouts=&smaaEdgePipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&edge,&fgSmaaEdge[slot]),"FgEdgeSet");sampled(fgSmaaEdge[slot],0,src);storage(fgSmaaEdge[slot],1,aaEdges);
   VkDescriptorSetAllocateInfo blend{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};blend.descriptorPool=fgAaPool;blend.descriptorSetCount=1;blend.pSetLayouts=&smaaBlendPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&blend,&fgSmaaBlend[slot]),"FgBlendSet");sampled(fgSmaaBlend[slot],0,src);sampled(fgSmaaBlend[slot],1,aaEdges);storage(fgSmaaBlend[slot],2,dst);
  }
 }else{
  fgAaPipe=createPipeline(fxaa_spv,sizeof(fxaa_spv),{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},8);
  for(int slot=0;slot<2;slot++){
   const Image& src=slot?upMid:upCurrent;const Image& dst=slot?aaMid:aaReal;
   VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=fgAaPool;info.descriptorSetCount=1;info.pSetLayouts=&fgAaPipe.setLayout;
   requireVk(a->AllocateDescriptorSets(a->device,&info,&fgFxaaSet[slot]),"FgFxaaSet");sampled(fgFxaaSet[slot],0,src);storage(fgFxaaSet[slot],1,dst);
  }
 }
 fgAaLazyDone=true;
 __android_log_print(ANDROID_LOG_INFO,"ThorNativeLayer","THOR_FG_AA mode=%s targets=2x output=%ux%u",smaa?"smaa":"fxaa",output.width,output.height);
}
void Engine::runAA(VkCommandBuffer cmd,const Image& src,const Image& dst,int slot){
 int32_t size[]={int32_t(output.width),int32_t(output.height)};
 barrier(cmd,src.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(smaa){
  barrier(cmd,aaEdges.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaEdgePipe.pipeline);
  a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaEdgePipe.layout,0,1,&fgSmaaEdge[slot],0,nullptr);
  a->CmdPushConstants(cmd,smaaEdgePipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
  a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
  barrier(cmd,aaEdges.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  barrier(cmd,dst.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaBlendPipe.pipeline);
  a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,smaaBlendPipe.layout,0,1,&fgSmaaBlend[slot],0,nullptr);
  a->CmdPushConstants(cmd,smaaBlendPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
  a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 }else{
  barrier(cmd,dst.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,fgAaPipe.pipeline);
  a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,fgAaPipe.layout,0,1,&fgFxaaSet[slot],0,nullptr);
  a->CmdPushConstants(cmd,fgAaPipe.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
  a->CmdDispatch(cmd,(output.width+7)/8,(output.height+7)/8,1);
 }
 barrier(cmd,dst.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
}void Engine::recordMulti(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter,const EngineTimings* timings,const float* phases,int count){
 auto stamp=[&](uint32_t index){if(timings&&timings->pool)a->CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timings->pool,timings->first+index);};stamp(0);
 if(!initialized){a->CmdFillBuffer(cmd,stats,0,sizeof(QualityStats),0);for(Image* im:{&history[0],&history[1],&flow,&mid,&mid2,&upCurrent,&upMid,&upMid2}){barrier(cmd,im->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};a->CmdClearColorImage(cmd,im->image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);barrier(cmd,im->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);}initialized=true;}
 barrier(cmd,source,sourceLayout,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 blit(cmd,source,input,history[write].image,input);
 barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,nullptr,0,nullptr);
 // Leave the persistent cut holdoff untouched between input frames.
 a->CmdFillBuffer(cmd,stats,0,offsetof(QualityStats,holdoff),0);mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 struct {int32_t input[2],grid[2];float phase;int32_t history;} push{{int32_t(input.width),int32_t(input.height)},{int32_t(grid.width),int32_t(grid.height)},0.0f,hasHistory?1:0};
 auto dispatch=[&](Pipeline& p,VkDescriptorSet set,uint32_t w,uint32_t h,const void* constants,uint32_t bytes){a->CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);a->CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&set,0,nullptr);a->CmdPushConstants(cmd,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,bytes,constants);a->CmdDispatch(cmd,(w+7)/8,(h+7)/8,1);};
 stamp(1);
 dispatch(analyzePipe,analyzeSets[write],grid.width*2,grid.height*2,&push,sizeof(push));
 mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(2);dispatch(cooperativeFlow?flowPipe:flowReferencePipe,flowSets[write],grid.width*(cooperativeFlow?8:1),grid.height*(cooperativeFlow?8:1),&push,sizeof(push));mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 // Hierarchical refinement: search a small window around the bilinear coarse
 // vector on a 2x finer grid, so edges get motion at four times the resolution.
 stamp(9);dispatch(flowRefinePipe,flowRefineSets[write],grid.width*16,grid.height*16,&push,sizeof(push));mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(3);dispatch(qualityPipe,qualitySet,1,1,&push,sizeof(push));a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(4);
 // One flow pass, several temporal positions. Each generated frame keeps its
 // own input-resolution midpoint so the upscale/finish step below can turn it
 // into a display-resolution image without waiting for the next input frame.
 Image* mids[2]={&mid,&mid2};
 if(count>1){
  // Both 3x positions share the flow, match-cost, current-frame and static
  // patch reads, so one dispatch writes both midpoints. The per-phase math is
  // unchanged; only the repeated full-resolution reads disappear.
  struct {int32_t input[2],grid[2];float phase0,phase1;int32_t history;} multi{{int32_t(input.width),int32_t(input.height)},{int32_t(grid.width*2),int32_t(grid.height*2)},std::clamp(phases[0],0.0f,1.0f),std::clamp(phases[1],0.0f,1.0f),hasHistory?1:0};
  dispatch(interpolate2Pipe,interpolate2Sets[write],input.width,input.height,&multi,sizeof(multi));
  a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
  for(int k=0;k<2;k++)barrier(cmd,mids[k]->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
 }else{
  for(int k=0;k<count;k++){
   push.phase=std::clamp(phases[k],0.0f,1.0f);push.grid[0]=int32_t(grid.width*2);push.grid[1]=int32_t(grid.height*2);
   dispatch(interpolatePipe,interpolateSets[write],input.width,input.height,&push,sizeof(push));
   a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
   barrier(cmd,mids[k]->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT);
  }
 }
 stamp(5);
 if(filter){
  int32_t outSize[]={int32_t(output.width),int32_t(output.height)};dispatch(upscalePipe,upSets[write],output.width,output.height,outSize,8);dispatch(upscalePipe,upSets[2],output.width,output.height,outSize,8);if(count>1)dispatch(upscalePipe,upMid2Set,output.width,output.height,outSize,8);
  barrier(cmd,upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);barrier(cmd,upMid.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);if(count>1)barrier(cmd,upMid2.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 }else{
  // Present-time FG runs at display resolution. Preserve the application's real
  // frame exactly and avoid filtering a frame that was already spatially upscaled.
  barrier(cmd,history[write].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  barrier(cmd,mid.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  if(count>1)barrier(cmd,mid2.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  for(Image* image:{&upCurrent,&upMid})barrier(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  if(count>1)barrier(cmd,upMid2.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  blit(cmd,history[write].image,input,upCurrent.image,output);blit(cmd,mid.image,input,upMid.image,output);if(count>1)blit(cmd,mid2.image,input,upMid2.image,output);
  for(Image* image:{&upCurrent,&upMid})barrier(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  if(count>1)barrier(cmd,upMid2.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  // AA runs on 2x only: one real + one generated frame per source frame fits the budget.
  // 3x keeps raw frames, the extra pass is not worth it at ~90 fps of output.
  realFrame=nullptr;generatedFrame=nullptr;
  if(count==1&&(smaa||fxaa)){
   initFrameAA();runAA(cmd,upCurrent,aaReal,0);runAA(cmd,upMid,aaMid,1);realFrame=&aaReal;generatedFrame=&aaMid;
  }
 }
 barrier(cmd,source,VK_IMAGE_LAYOUT_GENERAL,sourceLayout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
 mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);
 stamp(6);
 hasHistory=true;write=1-write;
}
void Engine::record(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter,const EngineTimings* timings,float phase){recordMulti(cmd,source,sourceLayout,filter,timings,&phase,1);}
QualityStats Engine::readQuality()const{QualityStats result{};if(statsMapped)std::memcpy(&result,statsMapped,sizeof(result));return result;}
void Engine::release(){
 if(!a)return;
 if(fgAaPool)a->DestroyDescriptorPool(a->device,fgAaPool,nullptr);
 for(auto* p:{&analyzePipe,&qualityPipe,&flowPipe,&flowReferencePipe,&flowRefinePipe,&interpolatePipe,&interpolate2Pipe,&upscalePipe,&aaPipe,&smaaEdgePipe,&smaaBlendPipe,&casPipe,&taaPipe,&sgsr2TemporalPipe}){if(p->pipeline)a->DestroyPipeline(a->device,p->pipeline,nullptr);if(p->layout)a->DestroyPipelineLayout(a->device,p->layout,nullptr);if(p->setLayout)a->DestroyDescriptorSetLayout(a->device,p->setLayout,nullptr);*p={};}
 for(auto& p:neuralPipes){if(p.pipeline)a->DestroyPipeline(a->device,p.pipeline,nullptr);if(p.layout)a->DestroyPipelineLayout(a->device,p.layout,nullptr);if(p.setLayout)a->DestroyDescriptorSetLayout(a->device,p.setLayout,nullptr);p={};}
 if(dlssDescriptors)a->DestroyDescriptorPool(a->device,dlssDescriptors,nullptr);
 if(aaDescriptors)a->DestroyDescriptorPool(a->device,aaDescriptors,nullptr);
 if(taaDescriptors)a->DestroyDescriptorPool(a->device,taaDescriptors,nullptr);
 if(flowDescriptors)a->DestroyDescriptorPool(a->device,flowDescriptors,nullptr);
 if(temporalDescriptors)a->DestroyDescriptorPool(a->device,temporalDescriptors,nullptr);
 if(descriptors)a->DestroyDescriptorPool(a->device,descriptors,nullptr);if(sampler)a->DestroySampler(a->device,sampler,nullptr);if(nearestSampler)a->DestroySampler(a->device,nearestSampler,nullptr);
 if(statsMapped)a->UnmapMemory(a->device,statsMemory);statsMapped=nullptr;if(stats)a->DestroyBuffer(a->device,stats,nullptr);if(statsMemory)a->FreeMemory(a->device,statsMemory,nullptr);
 for(Image* im:{&history[0],&history[1],&flow,&flowCost,&mid,&mid2,&upCurrent,&upMid,&upMid2,&aaOutput,&aaEdges,&aaReal,&aaMid,&flowFine,&casOutput,&taaHistory,&taaOutput,&historyPrevLow,&neuralFeatures[0],&neuralFeatures[1],&neural2x}){if(im->view)a->DestroyImageView(a->device,im->view,nullptr);if(im->image)a->DestroyImage(a->device,im->image,nullptr);if(im->memory)a->FreeMemory(a->device,im->memory,nullptr);*im={};}
 a=nullptr;
}
