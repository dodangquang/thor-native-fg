// Included after the layer's dispatch/state declarations. Internal engine calls
// use the downstream Api table, so they never recurse into these hooks.
static std::shared_ptr<Device> srDevice(VkCommandBuffer cmd){return deviceFor((VkDevice)cmd);}
static void srRemember(SpatialCommands& command,ComputeBinding binding){
 if(!command.watch||command.blocked)return;
 if(command.bindings.size()>=4096){command.blocked=true;return;}
 command.bindings.push_back(std::move(binding));
}
static VKAPI_ATTR VkResult VKAPI_CALL srCreatePool(VkDevice device,const VkCommandPoolCreateInfo* ci,const VkAllocationCallbacks* alloc,VkCommandPool* out){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto result=d->a.CreateCommandPool(device,ci,alloc,out);
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);d->appPools[*out]=ci->queueFamilyIndex;}
 return result;
}
static VKAPI_ATTR void VKAPI_CALL srDestroyPool(VkDevice device,VkCommandPool pool,const VkAllocationCallbacks* alloc){
 auto d=deviceFor(device);if(!d)return;
 {std::lock_guard<std::mutex> lock(d->mutex);for(auto i=d->appCommands.begin();i!=d->appCommands.end();)if(i->second.pool==pool)i=d->appCommands.erase(i);else ++i;d->appPools.erase(pool);}
 d->a.DestroyCommandPool(device,pool,alloc);
}
static VKAPI_ATTR VkResult VKAPI_CALL srAllocateCommands(VkDevice device,const VkCommandBufferAllocateInfo* ci,VkCommandBuffer* out){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto result=d->a.AllocateCommandBuffers(device,ci,out);
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);auto pool=d->appPools.find(ci->commandPool);if(pool!=d->appPools.end())for(uint32_t n=0;n<ci->commandBufferCount;n++){auto& command=d->appCommands[out[n]];command.pool=ci->commandPool;command.family=pool->second;command.level=ci->level;}}
 return result;
}
static VKAPI_ATTR void VKAPI_CALL srFreeCommands(VkDevice device,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands){
 auto d=deviceFor(device);if(!d)return;
 {std::lock_guard<std::mutex> lock(d->mutex);for(uint32_t n=0;n<count;n++)d->appCommands.erase(commands[n]);}
 d->a.FreeCommandBuffers(device,pool,count,commands);
}
static VKAPI_ATTR VkResult VKAPI_CALL srResetPool(VkDevice device,VkCommandPool pool,VkCommandPoolResetFlags flags){
 auto d=deviceFor(device);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto result=d->a.ResetCommandPool(device,pool,flags);
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);for(auto& item:d->appCommands)if(item.second.pool==pool)item.second.reset();}
 return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL srBegin(VkCommandBuffer cmd,const VkCommandBufferBeginInfo* ci){
 auto d=srDevice(cmd);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto result=d->a.BeginCommandBuffer(cmd,ci);
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);auto i=d->appCommands.find(cmd);if(i!=d->appCommands.end()){
  auto& command=i->second;command.reset();
  command.watch=enabled()&&srEnabled()&&command.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY&&!(ci->flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)&&command.family<d->families.size()&&(d->families[command.family].queueFlags&VK_QUEUE_COMPUTE_BIT);
 }}return result;
}
static VKAPI_ATTR void VKAPI_CALL srBindPipeline(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipeline){
 auto d=srDevice(cmd);if(!d)return;d->a.CmdBindPipeline(cmd,point,pipeline);
 if(point==VK_PIPELINE_BIND_POINT_COMPUTE){std::lock_guard<std::mutex> lock(d->mutex);auto i=d->appCommands.find(cmd);if(i!=d->appCommands.end()&&i->second.watch){ComputeBinding binding{};binding.kind=ComputeBinding::Pipeline;binding.pipeline=pipeline;srRemember(i->second,std::move(binding));}}
}
static VKAPI_ATTR void VKAPI_CALL srBindDescriptors(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t dynamicCount,const uint32_t* offsets){
 auto d=srDevice(cmd);if(!d)return;d->a.CmdBindDescriptorSets(cmd,point,layout,first,count,sets,dynamicCount,offsets);
 if(point==VK_PIPELINE_BIND_POINT_COMPUTE){std::lock_guard<std::mutex> lock(d->mutex);auto i=d->appCommands.find(cmd);if(i!=d->appCommands.end()&&i->second.watch){ComputeBinding binding{};binding.kind=ComputeBinding::Descriptors;binding.layout=layout;binding.first=first;if(count)binding.sets.assign(sets,sets+count);if(dynamicCount)binding.offsets.assign(offsets,offsets+dynamicCount);srRemember(i->second,std::move(binding));}}
}
static VKAPI_ATTR void VKAPI_CALL srPushConstants(VkCommandBuffer cmd,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void* data){
 auto d=srDevice(cmd);if(!d)return;d->a.CmdPushConstants(cmd,layout,stages,offset,size,data);
 if(stages&VK_SHADER_STAGE_COMPUTE_BIT){std::lock_guard<std::mutex> lock(d->mutex);auto i=d->appCommands.find(cmd);if(i!=d->appCommands.end()&&i->second.watch){ComputeBinding binding{};binding.kind=ComputeBinding::Push;binding.layout=layout;binding.stages=stages;binding.offset=offset;binding.bytes.assign((const uint8_t*)data,(const uint8_t*)data+size);srRemember(i->second,std::move(binding));}}
}
static void srBlock(Device& d,VkCommandBuffer cmd){std::lock_guard<std::mutex> lock(d.mutex);auto i=d.appCommands.find(cmd);if(i!=d.appCommands.end())i->second.blocked=true;}
static VKAPI_ATTR void VKAPI_CALL srPushDescriptors(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t set,uint32_t count,const VkWriteDescriptorSet* writes){
 auto d=srDevice(cmd);if(!d)return;auto fn=(PFN_vkCmdPushDescriptorSetKHR)d->gdpa(d->handle,"vkCmdPushDescriptorSetKHR");if(point==VK_PIPELINE_BIND_POINT_COMPUTE)srBlock(*d,cmd);if(fn)fn(cmd,point,layout,set,count,writes);
}
static VKAPI_ATTR void VKAPI_CALL srBindDescriptorBuffers(VkCommandBuffer cmd,uint32_t count,const VkDescriptorBufferBindingInfoEXT* bindings){
 auto d=srDevice(cmd);if(!d)return;auto fn=(PFN_vkCmdBindDescriptorBuffersEXT)d->gdpa(d->handle,"vkCmdBindDescriptorBuffersEXT");srBlock(*d,cmd);if(fn)fn(cmd,count,bindings);
}
static VKAPI_ATTR void VKAPI_CALL srBindShaders(VkCommandBuffer cmd,uint32_t count,const VkShaderStageFlagBits* stages,const VkShaderEXT* shaders){
 auto d=srDevice(cmd);if(!d)return;auto fn=(PFN_vkCmdBindShadersEXT)d->gdpa(d->handle,"vkCmdBindShadersEXT");for(uint32_t i=0;i<count;i++)if(stages[i]&VK_SHADER_STAGE_COMPUTE_BIT)srBlock(*d,cmd);if(fn)fn(cmd,count,stages,shaders);
}
static void srRestore(Device& d,VkCommandBuffer cmd,const SpatialCommands& command){
 for(const auto& binding:command.bindings)switch(binding.kind){
 case ComputeBinding::Pipeline:d.a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,binding.pipeline);break;
 case ComputeBinding::Descriptors:d.a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,binding.layout,binding.first,binding.sets.size(),binding.sets.data(),binding.offsets.size(),binding.offsets.data());break;
 case ComputeBinding::Push:d.a.CmdPushConstants(cmd,binding.layout,binding.stages,binding.offset,binding.bytes.size(),binding.bytes.data());break;
 }
}
static bool srReplace(Device& d,VkCommandBuffer cmd,VkImage src,VkImageLayout srcLayout,VkImage dst,VkImageLayout dstLayout,uint32_t count,const VkImageBlit* regions){
 if(!enabled()||!srEnabled()||count!=1||src==dst)return false;
 if(!d.spatialDiagnostic){auto tracked=d.appCommands.find(cmd);LOG("SR_TRACK pools=%zu commands=%zu found=%d watch=%d blocked=%d",d.appPools.size(),d.appCommands.size(),tracked!=d.appCommands.end(),tracked!=d.appCommands.end()&&tracked->second.watch,tracked!=d.appCommands.end()&&tracked->second.blocked);d.spatialDiagnostic=true;}
 auto found=d.appCommands.find(cmd);if(found==d.appCommands.end())return false;auto& command=found->second;
 if(!command.watch||command.blocked)return false;
 const auto& region=*regions;
 if(region.srcSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||region.dstSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||region.srcSubresource.mipLevel||region.dstSubresource.mipLevel||region.srcSubresource.baseArrayLayer||region.dstSubresource.baseArrayLayer||region.srcSubresource.layerCount!=1||region.dstSubresource.layerCount!=1)return false;
 if(region.srcOffsets[0].x<0||region.srcOffsets[0].y<0||region.srcOffsets[0].z!=0||region.srcOffsets[1].z!=1||region.dstOffsets[0].x||region.dstOffsets[0].y||region.dstOffsets[0].z||region.dstOffsets[1].z!=1)return false;
 int32_t width=region.srcOffsets[1].x-region.srcOffsets[0].x,height=region.srcOffsets[1].y-region.srcOffsets[0].y;
 if(width<16||height<16)return false;
 std::shared_ptr<Swap> swap;uint32_t index=0;
 for(const auto& item:d.swaps){auto match=std::find(item.second->images.begin(),item.second->images.end(),dst);if(match!=item.second->images.end()){swap=item.second;index=match-item.second->images.begin();break;}}
 if(!swap||!swap->supported||!swap->spatialCompatible||region.dstOffsets[1].x!=int32_t(swap->extent.width)||region.dstOffsets[1].y!=int32_t(swap->extent.height)||width>=int32_t(swap->extent.width)||height>=int32_t(swap->extent.height))return false;
 try{
  size_t slot=command.next;
  if(command.engines.size()<=slot)command.engines.resize(slot+1);
  auto& engine=command.engines[slot];VkExtent2D input{uint32_t(width),uint32_t(height)};
  if(!engine||engine->input.width!=input.width||engine->input.height!=input.height||engine->output.width!=swap->extent.width||engine->output.height!=swap->extent.height){
   auto replacement=std::make_unique<Engine>();replacement->initSpatial(d.a,d.memory,input,swap->extent);engine=std::move(replacement);
   LOG("SR_PIPELINE input=%ux%u output=%ux%u source=application_blit algorithm=SGSR1 (spatial, not temporal DLSS)",input.width,input.height,swap->extent.width,swap->extent.height);
  }
  command.hits.push_back({swap,index,input});command.next++;
  engine->recordSpatial(cmd,src,srcLayout,region);
  VkImageBlit output=region;output.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};output.srcOffsets[0]={0,0,0};output.srcOffsets[1]={int32_t(swap->extent.width),int32_t(swap->extent.height),1};
  d.a.CmdBlitImage(cmd,engine->upCurrent.image,VK_IMAGE_LAYOUT_GENERAL,dst,dstLayout,1,&output,VK_FILTER_NEAREST);
  srRestore(d,cmd,command);return true;
 }catch(const std::exception& error){command.blocked=true;LOG("SR_FALLBACK %s",error.what());return false;}
}
static VKAPI_ATTR void VKAPI_CALL srBlit(VkCommandBuffer cmd,VkImage src,VkImageLayout srcLayout,VkImage dst,VkImageLayout dstLayout,uint32_t count,const VkImageBlit* regions,VkFilter filter){
 auto d=srDevice(cmd);if(!d)return;
 {std::lock_guard<std::mutex> lock(d->mutex);if(srReplace(*d,cmd,src,srcLayout,dst,dstLayout,count,regions))return;}
 d->a.CmdBlitImage(cmd,src,srcLayout,dst,dstLayout,count,regions,filter);
}
static std::shared_ptr<Device> srQueueDevice(VkQueue queue){std::lock_guard<std::mutex> lock(globalMutex);auto found=queues.find(queue);return found==queues.end()?nullptr:found->second.device;}
static void srSubmitted(Device& d,VkCommandBuffer cmd){
 auto command=d.appCommands.find(cmd);if(command==d.appCommands.end())return;
 for(const auto& hit:command->second.hits)if(auto swap=hit.swap.lock())if(hit.index<swap->spatialFrames.size())swap->spatialFrames[hit.index]={hit.input,true};
}
static VKAPI_ATTR VkResult VKAPI_CALL srSubmit(VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence fence){
 auto d=srQueueDevice(queue);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto result=layerSubmit(*d,queue,count,submits,fence);
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);for(uint32_t n=0;n<count;n++)for(uint32_t i=0;i<submits[n].commandBufferCount;i++)srSubmitted(*d,submits[n].pCommandBuffers[i]);}
 return result;
}
static VkResult srSubmit2Common(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,VkFence fence,const char* name){
 auto d=srQueueDevice(queue);if(!d)return VK_ERROR_INITIALIZATION_FAILED;
 auto fn=(PFN_vkQueueSubmit2)d->gdpa(d->handle,name);if(!fn)return VK_ERROR_EXTENSION_NOT_PRESENT;
 VkResult result;{std::unique_lock<std::mutex> lock(d->privateQueueMutex,std::defer_lock);if(d->sharedPresenter||queue==d->privateQueue)lock.lock();result=fn(queue,count,submits,fence);}
 if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(d->mutex);for(uint32_t n=0;n<count;n++)for(uint32_t i=0;i<submits[n].commandBufferInfoCount;i++)srSubmitted(*d,submits[n].pCommandBufferInfos[i].commandBuffer);}
 return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL srSubmit2(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,VkFence fence){return srSubmit2Common(queue,count,submits,fence,"vkQueueSubmit2");}
static VKAPI_ATTR VkResult VKAPI_CALL srSubmit2KHR(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,VkFence fence){return srSubmit2Common(queue,count,submits,fence,"vkQueueSubmit2KHR");}
