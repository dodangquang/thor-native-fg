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
// ID 3 is reserved for older runtimes with the experimental blit-history path.
enum class SpatialModel:uint32_t {Sgsr1=0,Neural2D=1,NeuralBounded=2,Spatial9=4};
inline const char* spatialModelName(SpatialModel model){return model==SpatialModel::Spatial9?"Spatial9":model==SpatialModel::NeuralBounded?"Anime4K_CNN_S_bounded":model==SpatialModel::Neural2D?"Anime4K_CNN_S":"SGSR1";}
// No motion vectors: the spatial-only path cannot jitter the game's camera,
// so TAA here is a single cheap history tap + variance-clamp box, not a
// full reprojected temporal filter. Keeps the extra cost at one pass.
struct TAAParams {
 int32_t width,height;
 float blendFactor;
 int32_t firstFrame;
};
struct CASParams {
 int32_t width,height;
 float sharpness;
 int32_t preserveText;
};
struct Engine {
 Api* a{};VkPhysicalDeviceMemoryProperties memory{};VkExtent2D input{},output{},grid{};
 Image history[2],flow,flowFine,flowCost,mid,upCurrent,upMid,aaOutput;
 // Second generated frame for 3x: its own input-resolution midpoint and
 // display-resolution target so two phases can be produced from one flow pass.
 Image mid2,upMid2;
 VkBuffer stats{};VkDeviceMemory statsMemory{};void* statsMapped{};VkSampler sampler{},nearestSampler{};
 VkDescriptorPool descriptors{};Pipeline analyzePipe,qualityPipe,flowPipe,flowReferencePipe,flowRefinePipe,interpolatePipe,upscalePipe;
 VkDescriptorSet analyzeSets[2]{},qualitySet{};
 VkDescriptorSet flowSets[2]{},flowRefineSets[2]{},interpolateSets[2]{},interpolate2Sets[2]{},upSets[3]{},upMid2Set{};
 // One fused dispatch writes both 3x midpoints; the single-phase pipeline stays
 // for2x so its output is unchanged.
 Pipeline interpolate2Pipe;
 bool initialized=false,hasHistory=false,cooperativeFlow=true;int write=0;
 SpatialModel spatialModel=SpatialModel::Sgsr1;
 Image neuralFeatures[2],neural2x;Pipeline neuralPipes[6];VkDescriptorSet neuralSets[6]{};
 bool fxaa=false,smaa=false;Pipeline aaPipe;VkDescriptorPool aaDescriptors{};VkDescriptorSet aaSet{};
 // SMAA runs in two passes through an edge mask; FXAA stays single pass.
 Image aaEdges,aaReal,aaMid;Pipeline smaaEdgePipe,smaaBlendPipe;VkDescriptorSet smaaEdgeSet{},smaaBlendSet{};
 // Frame-generation AA:2x runs SMAA/FXAA on the real and the generated frame
 // before they are presented;3x keeps raw frames.
 Pipeline fgAaPipe;VkDescriptorPool fgAaPool{};VkDescriptorSet fgSmaaEdge[2]{},fgSmaaBlend[2]{},fgFxaaSet[2]{};const Image* realFrame=nullptr;const Image* generatedFrame=nullptr;
 Pipeline taaPipe,casPipe;
 Image taaHistory,taaOutput,casOutput;
 VkDescriptorPool dlssDescriptors{},taaDescriptors{};
 VkDescriptorSet taaSet{},casSet{};
 bool enableTAA=false,enableCAS=false,taaHasHistory=false;
 float casSharpness=0.6f,taaBlend=0.12f;
 // Real motion-compensated temporal reconstruction for Spatial9 (the former
 // "SGSR2" rename): reuses the same optical-flow kernel the frame generator
 // already pays for, run once more at low (input) resolution between this
 // frame's and the previous frame's game-rendered image. The resulting
 // vectors reproject history before the variance-clamp blend, instead of
 // the motionless single history tap used for other spatial models.
 bool motionTAA=false,motionFlowReady=false;Image historyPrevLow;Pipeline sgsr2TemporalPipe;
 VkDescriptorPool flowDescriptors{},temporalDescriptors{};VkDescriptorSet temporalSet{};
 uint32_t frameIndex=0;
 bool hasTemporal()const{return taaPipe.pipeline||sgsr2TemporalPipe.pipeline;}
 // Either FXAA or SMAA owns the single post-AA pass (aaPipe/aaOutput); both
 // consume the CAS/TAA output chain below.
 VkImage spatialOutput()const{return (fxaa||smaa)?aaOutput.image:(casPipe.pipeline?casOutput.image:(hasTemporal()?taaOutput.image:upCurrent.image));}
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
 void initSpatial(Api& api,const VkPhysicalDeviceMemoryProperties& mem,VkExtent2D in,VkExtent2D out,SpatialModel model=SpatialModel::Sgsr1,bool antialias=false,bool sharpen=false,float sharpness=0.6f,bool temporal=false,float temporalBlend=0.12f,bool smaa=false);
 void recordAA(VkCommandBuffer cmd);
 void recordCAS(VkCommandBuffer cmd);
 void recordTAA(VkCommandBuffer cmd);
 void recordMotionFlow(VkCommandBuffer cmd);
 void recordSpatial(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,const VkImageBlit& region);
 void barrier(VkCommandBuffer cmd,VkImage im,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst);
 void blit(VkCommandBuffer cmd,VkImage source,VkExtent2D sourceSize,VkImage destination,VkExtent2D destSize);
 // phase is the temporal position of the generated frame between the previous
 // and current real frame: 0.5 for 2x, 1/3 and 2/3 for 3x.
 void record(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter=true,const EngineTimings* timings=nullptr,float phase=0.5f);
 // Same flow pass, several generated frames. phases[0] lands in upMid and
 // phases[1] in upMid2, so 3x pays for the analysis and flow only once. The
 // caller must readQuality() before deciding to present either frame.
 void recordMulti(VkCommandBuffer cmd,VkImage source,VkImageLayout sourceLayout,bool filter,const EngineTimings* timings,const float* phases,int count);
 // Frame-generation AA (2x only). Lazy: the first 2x frame builds the passes.
 void configureAA(bool useSmaa,bool useFxaa){if(useSmaa==smaa&&useFxaa==fxaa)return;smaa=useSmaa;fxaa=useFxaa;}
 void initFrameAA();void runAA(VkCommandBuffer cmd,const Image& src,const Image& dst,int slot);bool fgAaLazyDone=false;
 VkImage realOutput()const{return realFrame?realFrame->image:upCurrent.image;}
 VkImage generatedOutput()const{return generatedFrame?generatedFrame->image:upMid.image;}
 // The caller must wait for this frame's GPU submission before reading.
 QualityStats readQuality()const;
 void release();
 ~Engine(){release();}
};
