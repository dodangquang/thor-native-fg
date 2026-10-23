#pragma once
#include "vkapi.hpp"
#include "quality.hpp"
#include <array>
#include <vector>
struct Image { VkImage image{};VkDeviceMemory memory{};VkImageView view{};VkExtent2D size{}; };
struct Pipeline {VkPipeline pipeline{};VkPipelineLayout layout{};VkDescriptorSetLayout setLayout{};};
// Seven optional timestamps delimit six input stages. The owner resets the
// query range and waits for this submission before reading it.
struct EngineTimings {VkQueryPool pool{};uint32_t first=0;};
enum class SpatialModel:uint32_t {Sgsr1=0,Neural2D=1,NeuralBounded=2,Sgsr2=3};
inline const char* spatialModelName(SpatialModel model){return model==SpatialModel::Sgsr2?"SGSR2":model==SpatialModel::NeuralBounded?"Anime4K_CNN_S_bounded":model==SpatialModel::Neural2D?"Anime4K_CNN_S":"SGSR1";}
struct Engine {
 Api* a{};VkPhysicalDeviceMemoryProperties memory{};VkExtent2D input{},output{},grid{};
 Image history[2],flow,mid,upCurrent,upMid,sgsr2History[2];
 VkBuffer stats{};VkDeviceMemory statsMemory{};void* statsMapped{};VkSampler sampler{},nearestSampler{};
 VkDescriptorPool descriptors{};Pipeline analyzePipe,qualityPipe,flowPipe,flowReferencePipe,interpolatePipe,upscalePipe;
 VkDescriptorSet analyzeSets[2]{},qualitySet{};
 VkDescriptorSet flowSets[2]{},interpolateSets[2]{},upSets[3]{};
 bool initialized=false,hasHistory=false,cooperativeFlow=true;int write=0;
 SpatialModel spatialModel=SpatialModel::Sgsr1;
 Image neuralFeatures[2],neural2x;Pipeline neuralPipes[6];VkDescriptorSet neuralSets[6]{};
 VkImage spatialOutput()const{return spatialModel==SpatialModel::Sgsr2?sgsr2History[1-write].image:upCurrent.image;}
 uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags);
 Image createImage(VkExtent2D size,VkFormat format,VkImageUsageFlags usage);
 Pipeline createPipeline(const uint32_t* code,size_t bytes,const std::vector<VkDescriptorType>& bindings,uint32_t pushBytes);
 VkDescriptorSet allocate(const Pipeline& p);
 void sampled(VkDescriptorSet set,uint32_t binding,const Image& im,bool nearest=false);
 void storage(VkDescriptorSet set,uint32_t binding,const Image& im);
 void buffer(VkDescriptorSet set,uint32_t binding);
 void init(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out,bool cooperative=true);
 // Spatial-only path for a real low-resolution application blit. No FG images
 // or CPU readback are allocated by this path.
 void initSpatial(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out,SpatialModel model=SpatialModel::Sgsr1);
 void recordSpatial(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,const VkImageBlit& region);
 void barrier(VkCommandBuffer cmd,VkImage im,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst);
 void blit(VkCommandBuffer cmd,VkImage source,VkExtent2D sourceSize,VkImage destination,VkExtent2D destSize);
 void record(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter=true,const EngineTimings* timings=nullptr);
 // The caller must wait for this frame's GPU submission before reading.
 QualityStats readQuality()const;
 void release();
 ~Engine(){release();}
};
