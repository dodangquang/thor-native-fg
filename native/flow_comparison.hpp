#pragma once
#include "engine.hpp"
#include <cstring>

// Optional on-device verification against the original serial implementation.
// Its copies and second engine are deliberately excluded from performance runs.
struct FlowComparison {
 Api* a{};Engine reference;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};
 VkDeviceSize flowBytes=0,rgbaBytes=0;
 void init(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D size){
  a=&api;reference.init(api,mem,size,size,false);flowBytes=VkDeviceSize(reference.grid.width)*reference.grid.height*16;rgbaBytes=VkDeviceSize(size.width)*size.height*4;
  VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=2*flowBytes+4*rgbaBytes;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a->CreateBuffer(a->device,&bc,nullptr,&buffer),"FlowCompareBuffer");
  VkMemoryRequirements req{};a->GetBufferMemoryRequirements(a->device,buffer,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=reference.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  requireVk(a->AllocateMemory(a->device,&ma,nullptr,&memory),"FlowCompareMemory");requireVk(a->BindBufferMemory(a->device,buffer,memory,0),"FlowCompareBind");requireVk(a->MapMemory(a->device,memory,0,VK_WHOLE_SIZE,0,&mapped),"FlowCompareMap");
 }
 void record(VkCommandBuffer cmd,Engine& candidate,VkImage source,VkImageLayout layout,bool hadHistory){
  reference.hasHistory=hadHistory;reference.record(cmd,source,layout,false);
  auto copy=[&](const Image& image,VkDeviceSize offset){candidate.barrier(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy region{};region.bufferOffset=offset;region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={image.size.width,image.size.height,1};a->CmdCopyImageToBuffer(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);};
  copy(candidate.flow,0);copy(reference.flow,flowBytes);copy(candidate.mid,2*flowBytes);copy(reference.mid,2*flowBytes+rgbaBytes);copy(candidate.upCurrent,2*flowBytes+2*rgbaBytes);copy(reference.upCurrent,2*flowBytes+3*rgbaBytes);
  VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a->CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
 }
 uint32_t differences(const Engine& candidate)const{
  const auto* data=static_cast<const uint8_t*>(mapped);auto actual=candidate.readQuality(),expected=reference.readQuality();
  return (std::memcmp(data,data+flowBytes,flowBytes)?1u:0u)|(std::memcmp(data+2*flowBytes,data+2*flowBytes+rgbaBytes,rgbaBytes)?2u:0u)|(std::memcmp(data+2*flowBytes+2*rgbaBytes,data+2*flowBytes+3*rgbaBytes,rgbaBytes)?4u:0u)|(std::memcmp(&actual,&expected,sizeof(actual))?8u:0u);
 }
 ~FlowComparison(){if(a){if(mapped)a->UnmapMemory(a->device,memory);if(buffer)a->DestroyBuffer(a->device,buffer,nullptr);if(memory)a->FreeMemory(a->device,memory,nullptr);}}
};
