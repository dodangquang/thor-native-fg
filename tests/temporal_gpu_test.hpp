#pragma once
#include "temporal.hpp"
#include <android/log.h>
#include <memory>
#include <cstring>

// Test-only timeline semaphore holds three submissions pending while their
// host UBOs/descriptors are prepared. The production engine does not use or
// require timeline features; the separate featureless32-frame run is retained.
struct TemporalTestFrame {
 Api& a;Engine helper;Image images[3];VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};
 VkCommandPool pool{};VkCommandBuffer cmd{};VkFence fence{};bool initialized=false;
 std::vector<uint8_t> upload,reference;TemporalFrame input;TemporalOutput output;
 static constexpr uint32_t w=66,h=42,ow=w*2,oh=h*2;
 explicit TemporalTestFrame(Api& api):a(api){helper.a=&a;}
 void init(const VkPhysicalDeviceMemoryProperties& mem,uint32_t family,int frame){
  helper.memory=mem;const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkFormat formats[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT};for(int k=0;k<3;k++)images[k]=helper.createImage({w,h},formats[k],usage);
  VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=VkDeviceSize(ow)*oh*8;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(a.device,&bc,nullptr,&buffer),"TemporalTestBuffer");VkMemoryRequirements req{};a.GetBufferMemoryRequirements(a.device,buffer,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=helper.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(a.device,&ma,nullptr,&memory),"TemporalTestMemory");requireVk(a.BindBufferMemory(a.device,buffer,memory,0),"TemporalTestBind");requireVk(a.MapMemory(a.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"TemporalTestMap");
  VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;requireVk(a.CreateCommandPool(a.device,&pc,nullptr,&pool),"TemporalTestPool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;requireVk(a.AllocateCommandBuffers(a.device,&ca,&cmd),"TemporalTestCmd");VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};requireVk(a.CreateFence(a.device,&fc,nullptr,&fence),"TemporalTestFence");
  upload.resize(w*h*24);for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
   size_t at=y*w+x;uint8_t rgba[]={uint8_t(40+frame*17+(x*7+y*3)%80),uint8_t(70+(x*2+y*9+frame*11)%90),uint8_t(150-(x*3+frame*5)%80),255};std::memcpy(upload.data()+at*4,rgba,4);float depth=.7f;std::memcpy(upload.data()+w*h*4+at*4,&depth,4);float velocity[]={32767.0f/65535,32767.0f/65535,0,0};std::memcpy(upload.data()+w*h*8+at*16,velocity,16);
  }
  input.frame=frame;input.scene=0;input.params.renderSize[0]=w;input.params.renderSize[1]=h;input.params.displaySize[0]=ow;input.params.displaySize[1]=oh;input.params.sameCamera=1;input.params.reset=1;for(int k=0;k<4;k++)input.params.clipToPrevClip[k*5]=1;
  float jitterX[]={0,.25f,-.375f,.125f},jitterY[]={-.1667f,.2f,-.35f,.4f};input.params.jitterOffset[0]=jitterX[frame];input.params.jitterOffset[1]=jitterY[frame];
  input.color={images[0].view,VK_IMAGE_LAYOUT_GENERAL,formats[0],{w,h}};input.depth={images[1].view,VK_IMAGE_LAYOUT_GENERAL,formats[1],{w,h}};input.velocity={images[2].view,VK_IMAGE_LAYOUT_GENERAL,formats[2],{w,h}};
 }
 void begin(){requireVk(a.ResetCommandPool(a.device,pool,0),"TemporalTestReset");VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&cb),"TemporalTestBegin");}
 void recordInput(){
  std::memcpy(mapped,upload.data(),upload.size());VkDeviceSize offset=0;
  for(int k=0;k<3;k++){helper.barrier(cmd,images[k].image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);VkBufferImageCopy copy{};copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};a.CmdCopyBufferToImage(cmd,buffer,images[k].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);offset+=VkDeviceSize(w)*h*(k==2?16:4);helper.barrier(cmd,images[k].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);}initialized=true;
 }
 void recordOutput(){VkMemoryBarrier war{VK_STRUCTURE_TYPE_MEMORY_BARRIER};war.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;war.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&war,0,nullptr,0,nullptr);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={ow,oh,1};a.CmdCopyImageToBuffer(cmd,output.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);}
 void submit(TemporalEngine& engine,VkQueue queue,VkSemaphore gate={}){
  requireVk(a.EndCommandBuffer(cmd),"TemporalTestEnd");requireVk(a.ResetFences(a.device,1,&fence),"TemporalTestFenceReset");VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};uint64_t value=1;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  if(gate){timeline.waitSemaphoreValueCount=1;timeline.pWaitSemaphoreValues=&value;si.pNext=&timeline;si.waitSemaphoreCount=1;si.pWaitSemaphores=&gate;si.pWaitDstStageMask=&stage;}
  auto status=a.QueueSubmit(queue,1,&si,fence);auto ack=engine.submitted(output.ticket,queue,fence,status);requireVk(status,"TemporalTestSubmit");if(ack!=TemporalStatus::Ready)throw std::runtime_error("TemporalTest submit acknowledgement");
 }
 void wait(){requireVk(a.WaitForFences(a.device,1,&fence,VK_TRUE,UINT64_MAX),"TemporalTestWait");}
 ~TemporalTestFrame(){if(mapped)a.UnmapMemory(a.device,memory);if(buffer)a.DestroyBuffer(a.device,buffer,nullptr);if(memory)a.FreeMemory(a.device,memory,nullptr);if(fence)a.DestroyFence(a.device,fence,nullptr);if(pool)a.DestroyCommandPool(a.device,pool,nullptr);for(auto& im:images){if(im.view)a.DestroyImageView(a.device,im.view,nullptr);if(im.image)a.DestroyImage(a.device,im.image,nullptr);if(im.memory)a.FreeMemory(a.device,im.memory,nullptr);}helper.a=nullptr;}
};
struct TemporalTestGate {
 Api& a;VkSemaphore semaphore{};PFN_vkSignalSemaphoreKHR signal{};bool signaled=false;
 explicit TemporalTestGate(Api& api):a(api){signal=(PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(a.device,"vkSignalSemaphoreKHR");if(!signal)throw std::runtime_error("Timeline test gate unavailable");VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};ci.pNext=&type;requireVk(a.CreateSemaphore(a.device,&ci,nullptr,&semaphore),"TemporalTestGate");}
 void open(){if(signaled)return;VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};info.semaphore=semaphore;info.value=1;requireVk(signal(a.device,&info),"TemporalTestGateSignal");signaled=true;}
 ~TemporalTestGate(){if(semaphore){if(!signaled){VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};info.semaphore=semaphore;info.value=1;signal(a.device,&info);}a.DeviceWaitIdle(a.device);a.DestroySemaphore(a.device,semaphore,nullptr);}}
};
static void temporalGpuTests(Api& a,const VkPhysicalDeviceMemoryProperties& memory,VkQueue queue,uint32_t family){
 int checks=0;auto check=[&](bool ok,const char* name){checks++;__android_log_print(ANDROID_LOG_INFO,"ThorTemporalTest","TEMPORAL_GPU %s %s",ok?"PASS":"FAIL",name);if(!ok)throw std::runtime_error(name);};using S=TemporalStatus;using F=TemporalTestFrame;
 std::vector<std::unique_ptr<F>> frames;for(int k=0;k<4;k++){frames.push_back(std::make_unique<F>(a));frames.back()->init(memory,family,k);}
 TemporalEngine serial,batch;serial.init(a,memory,queue,{F::w,F::h},{F::ow,F::oh},1);batch.init(a,memory,queue,{F::w,F::h},{F::ow,F::oh},3);
 // Declared after engines/frames so its exception cleanup signals before any
 // context destructor can wait for queue idle. No indefinite test wait on abort.
 TemporalTestGate gate(a);
 for(auto& f:frames){f->begin();f->recordInput();check(serial.record(f->cmd,f->input,f->output)==S::Ready,"serial reference recorded");f->recordOutput();f->submit(serial,queue);f->wait();check(serial.retire(f->output.ticket)==S::Ready,"serial reference retired");const auto* data=(const uint8_t*)f->mapped;f->reference.assign(data,data+F::ow*F::oh*8);}
 check(serial.release()==S::Ready,"serial release after fences");
 auto& cancel=*frames[3];cancel.begin();TemporalFrame abandoned=cancel.input;abandoned.frame=999;TemporalOutput cancelled;check(batch.record(cancel.cmd,abandoned,cancelled)==S::Ready,"unsubmitted first recording prepared");requireVk(a.EndCommandBuffer(cancel.cmd),"TemporalCancelEnd");requireVk(a.ResetCommandPool(a.device,cancel.pool,0),"TemporalCancelReset");check(batch.abort(cancelled.ticket)==S::Ready,"abandoned command cancelled without advancing history");
 for(int k=0;k<3;k++){
  auto& f=*frames[k];f.begin();f.recordInput();TemporalFrame invalid=f.input;invalid.depth.view={};TemporalOutput rejected;check(batch.record(f.cmd,invalid,rejected)==S::InvalidInput,"missing renderer depth rejected before recording");
  check(batch.record(f.cmd,f.input,f.output)==S::Ready,"batched frame recorded");f.recordOutput();f.submit(batch,queue,k==0?gate.semaphore:VK_NULL_HANDLE);
 }
 check(a.GetFenceStatus(a.device,frames[0]->fence)==VK_NOT_READY,"timeline gate proves actual GPU work is pending");check(batch.retire(frames[0]->output.ticket)==S::Busy,"unfinished fence cannot retire input resources");check(batch.release()==S::Busy,"context cannot release pending GPU work");
 auto& fourth=*frames[3];fourth.begin();TemporalOutput blocked;check(batch.record(fourth.cmd,fourth.input,blocked)==S::Busy,"fourth frame cannot overwrite three pending slots");requireVk(a.EndCommandBuffer(fourth.cmd),"TemporalBusyEnd");
 gate.open();
 for(int k=0;k<3;k++){auto& f=*frames[k];f.wait();check(batch.retire(f.output.ticket)==S::Ready,"batched frame retired after real fence");check(std::memcmp(f.mapped,f.reference.data(),f.reference.size())==0,"queued output bit-exact to isolated reference");}
 auto stale=frames[0]->output.ticket;fourth.begin();fourth.recordInput();check(batch.record(fourth.cmd,fourth.input,fourth.output)==S::Ready,"completed slot reused");check(batch.submitted(stale,queue,frames[0]->fence,VK_SUCCESS)==S::InvalidTicket,"stale generation cannot acknowledge recycled slot");fourth.recordOutput();fourth.submit(batch,queue);fourth.wait();check(batch.retire(fourth.output.ticket)==S::Ready,"reused slot retired");check(std::memcmp(fourth.mapped,fourth.reference.data(),fourth.reference.size())==0,"reused output bit-exact to isolated reference");check(batch.release()==S::Ready,"batch release after all fences");
 __android_log_print(ANDROID_LOG_INFO,"ThorTemporalTest","TEMPORAL_GPU_RESULT checks=%d failures=0 profile=timeline_gate",checks);
}
