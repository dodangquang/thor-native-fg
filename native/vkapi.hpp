#pragma once
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
#define THOR_DEVICE_FUNCTIONS(X) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
 X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(FreeCommandBuffers) X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdPipelineBarrier) X(CmdBlitImage) X(CmdCopyImage) X(CmdCopyImageToBuffer) X(CmdCopyBufferToImage) X(CmdClearColorImage) X(CmdFillBuffer) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) \
 X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences) X(ResetFences) X(QueueSubmit) X(QueueWaitIdle) X(DeviceWaitIdle) \
 X(CreateQueryPool) X(DestroyQueryPool) X(GetQueryPoolResults) X(CmdResetQueryPool) X(CmdWriteTimestamp) \
 X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(GetDeviceQueue) X(GetDeviceQueue2) X(DestroyDevice)
struct Api {
 VkDevice device{};
#define DECLARE(name) PFN_vk##name name{};
 THOR_DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
 void load(VkDevice d,PFN_vkGetDeviceProcAddr get){device=d;
#define LOAD(name) name=(PFN_vk##name)get(d,"vk" #name);
 THOR_DEVICE_FUNCTIONS(LOAD)
#undef LOAD
 }
};
inline void requireVk(VkResult r,const char* stage){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(stage)+" VkResult="+std::to_string(r));}
