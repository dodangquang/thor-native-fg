#pragma once
#include "engine.hpp"
#include <memory>

struct Swap;
struct SpatialFrame { VkExtent2D input{};bool pending=false;SpatialModel model=SpatialModel::Sgsr1; };
struct SpatialHit {std::weak_ptr<Swap> swap;uint32_t index;VkExtent2D input;SpatialModel model=SpatialModel::Sgsr1;};
struct ComputeBinding {
 enum Kind {Pipeline,Descriptors,Push} kind;
 VkPipeline pipeline{};VkPipelineLayout layout{};VkShaderStageFlags stages{};
 uint32_t first=0,offset=0;
 std::vector<VkDescriptorSet> sets;std::vector<uint32_t> offsets;std::vector<uint8_t> bytes;
};
struct SpatialCommands {
 VkCommandPool pool{};uint32_t family=~0u;VkCommandBufferLevel level{};
 bool blocked=false,watch=false;size_t next=0;
 std::vector<ComputeBinding> bindings;
 std::vector<std::unique_ptr<Engine>> engines;
 std::vector<SpatialHit> hits;
 void reset(){blocked=false;next=0;bindings.clear();hits.clear();}
};
