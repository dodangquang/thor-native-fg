#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
struct QueueReservation {int info=-1;uint32_t index=0;};
inline QueueReservation reserveFrameQueue(const std::vector<VkQueueFamilyProperties>& families,const std::vector<VkDeviceQueueCreateInfo>& queues){
 for(size_t n=0;n<queues.size();n++){
  const auto& queue=queues[n];if(queue.flags||!queue.queueCount||queue.queueFamilyIndex>=families.size())continue;
  const auto& family=families[queue.queueFamilyIndex];
  if((family.queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))!=(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))continue;
  uint64_t requested=0;for(const auto& other:queues)if(other.queueFamilyIndex==queue.queueFamilyIndex)requested+=other.queueCount;
  if(requested<family.queueCount)return {int(n),queue.queueCount};
 }
 return {};
}
