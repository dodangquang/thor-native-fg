#pragma once
#include "engine.hpp"
#include "temporal_sequence.hpp"
#include <cstddef>

// Exact std140 layout used by both adapted SGSR2 passes. Matrix is column-major.
struct alignas(16) TemporalParams {
 uint32_t renderSize[2]{},displaySize[2]{};
 float renderSizeRcp[2]{},displaySizeRcp[2]{},jitterOffset[2]{},padding[2]{};
 float clipToPrevClip[16]{};
 float preExposure=1,cameraFovAngleHor=1,cameraNear=.1f,minLerpContribution=.1f;
 uint32_t sameCamera=0,reset=0;uint32_t tailPadding[2]{};
};
static_assert(sizeof(TemporalParams)==144);
static_assert(offsetof(TemporalParams,clipToPrevClip)==48);
static_assert(offsetof(TemporalParams,preExposure)==112);
static_assert(offsetof(TemporalParams,reset)==132);

struct TemporalImage {
 VkImageView view{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
 VkFormat format=VK_FORMAT_UNDEFINED;VkExtent2D size{};
};
struct TemporalFrame {
 uint64_t frame=0,scene=0;TemporalParams params{};
 TemporalImage color,depth,velocity;
};
struct TemporalOutput {TemporalTicket ticket{};VkImage image{};VkImageView view{};VkExtent2D size{};VkFormat format=VK_FORMAT_R16G16B16A16_SFLOAT;};

// Cooperative renderer API: same device/serial queue, pre-HUD LINEAR LDR color,
// normal-Z depth and encoded UV-down object velocity (zero sentinel for camera).
// Caller owns input handles/layout transitions; it must preserve sampled input
// resources, command buffers and submission fences until retire() succeeds.
// A color-only final compositor cannot supply this contract automatically.
class TemporalEngine {
 struct Slot {VkBuffer ubo{};VkDeviceMemory memory{};void* mapped{};VkDescriptorSet convert{},upscale{};Image output{};VkFence fence{};};
 Engine resources;std::vector<Slot> slots;Image motion,history[2];
 TemporalSequence sequence;VkQueue queue{};VkExtent2D input{},output{};uint32_t write=0;
 bool ready=false,uncertain=false;
 void sampled(VkDescriptorSet set,uint32_t binding,const TemporalImage& image,VkSampler sampler);
 bool valid(const TemporalFrame& frame)const;
 void destroy()noexcept;
public:
 TemporalEngine()=default;TemporalEngine(const TemporalEngine&)=delete;TemporalEngine& operator=(const TemporalEngine&)=delete;
 ~TemporalEngine(){destroy();}
 void init(Api& api,const VkPhysicalDeviceMemoryProperties& memory,VkQueue serialQueue,VkExtent2D in,VkExtent2D out,uint32_t capacity=3);
 // Leaves app compute binding state changed. Caller must restore it before
 // continuing its own dispatches. Invalid/Busy returns record no GPU commands.
 TemporalStatus record(VkCommandBuffer cmd,const TemporalFrame& frame,TemporalOutput& result);
 // Call immediately AFTER submitting the recorded command on the declared queue.
 // The fence must belong to that submission and stay unreset until retire().
 TemporalStatus submitted(const TemporalTicket& ticket,VkQueue submittedQueue,VkFence fence,VkResult result);
 // Only for a command buffer that has been discarded/reset WITHOUT submitting.
 TemporalStatus abort(const TemporalTicket& ticket);
 TemporalStatus retire(const TemporalTicket& ticket);
 TemporalStatus release();
};
