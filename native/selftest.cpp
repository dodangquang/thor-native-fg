#include "engine.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
int main(){
 VkInstance instance{};VkDevice device{};int failures=0,checks=0;
 try{
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Thor GPU correctness tests";app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;requireVk(vkCreateInstance(&ci,nullptr,&instance),"CreateInstance");uint32_t n=0;vkEnumeratePhysicalDevices(instance,&n,nullptr);if(!n)throw std::runtime_error("No GPU");std::vector<VkPhysicalDevice> physical(n);vkEnumeratePhysicalDevices(instance,&n,physical.data());auto phys=physical[0];VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(phys,&props);printf("GPU %s Vulkan %u.%u.%u\n",props.deviceName,VK_VERSION_MAJOR(props.apiVersion),VK_VERSION_MINOR(props.apiVersion),VK_VERSION_PATCH(props.apiVersion));
  vkGetPhysicalDeviceQueueFamilyProperties(phys,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(phys,&n,families.data());uint32_t family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;if(family==n)throw std::runtime_error("No compute queue");float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;requireVk(vkCreateDevice(phys,&di,nullptr,&device),"CreateDevice");
  Api a;a.load(device,vkGetDeviceProcAddr);VkQueue queue{};a.GetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties mem{};vkGetPhysicalDeviceMemoryProperties(phys,&mem);
  {
   Engine e;e.init(a,mem,{256,144},{512,288});auto source=e.createImage({256,144},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
   VkBuffer buffer{};VkDeviceMemory bufferMemory{};VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=512*288*4;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;requireVk(a.CreateBuffer(device,&bc,nullptr,&buffer),"TestBuffer");VkMemoryRequirements req{};a.GetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=e.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);requireVk(a.AllocateMemory(device,&ma,nullptr,&bufferMemory),"TestMemory");requireVk(a.BindBufferMemory(device,buffer,bufferMemory,0),"TestBind");void* mapped{};requireVk(a.MapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped),"TestMap");
   VkCommandPool pool{};VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;requireVk(a.CreateCommandPool(device,&pi,nullptr,&pool),"TestPool");VkCommandBuffer cmd{};VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;requireVk(a.AllocateCommandBuffers(device,&ca,&cmd),"TestCommands");bool initialized=false;
   auto begin=[&](){a.ResetCommandPool(device,pool,0);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};requireVk(a.BeginCommandBuffer(cmd,&bi),"TestBegin");};
   auto submit=[&](){requireVk(a.EndCommandBuffer(cmd),"TestEnd");VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cmd;requireVk(a.QueueSubmit(queue,1,&sub,VK_NULL_HANDLE),"TestSubmit");requireVk(a.QueueWaitIdle(queue),"TestWait");};
   auto check=[&](bool ok,const char* name){checks++;if(!ok)failures++;printf("%s %s\n",ok?"PASS":"FAIL",name);};
   auto process=[&](const std::vector<uint8_t>& pixels){memcpy(mapped,pixels.data(),pixels.size());begin();e.barrier(cmd,source.image,initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);initialized=true;VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={256,144,1};a.CmdCopyBufferToImage(cmd,buffer,source.image,VK_IMAGE_LAYOUT_GENERAL,1,&region);e.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL);submit();};
   auto read=[&](const Image& im){begin();e.barrier(cmd,im.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={im.size.width,im.size.height,1};a.CmdCopyImageToBuffer(cmd,im.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;a.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);submit();};
   std::vector<uint8_t> pixels(256*144*4);for(size_t i=0;i<pixels.size();i+=4){pixels[i]=40;pixels[i+1]=90;pixels[i+2]=160;pixels[i+3]=255;}
   process(pixels);check(e.readQuality().allow==0&&(e.readQuality().reasons&QUALITY_WARMUP),"first input warms history without generating");read(e.upCurrent);auto* bytes=(uint8_t*)mapped;bool good=true;for(size_t i=0;i<512*288*4;i+=4)if(abs(int(bytes[i])-40)>1||abs(int(bytes[i+1])-90)>1||abs(int(bytes[i+2])-160)>1||bytes[i+3]!=255)good=false;check(good,"SGSR1 2x preserves constant RGB and alpha");
   process(pixels);read(e.mid);check(memcmp(mapped,pixels.data(),pixels.size())==0,"identical frames midpoint exact");check(e.readQuality().allow==1,"stable identical pair remains eligible");
   for(size_t i=0;i<pixels.size();i+=4){pixels[i]=235;pixels[i+1]=15;pixels[i+2]=20;pixels[i+3]=255;}process(pixels);read(e.mid);check(memcmp(mapped,pixels.data(),pixels.size())==0,"hard scene cut uses current frame, no crossfade");check(!e.readQuality().allow&&(e.readQuality().reasons&QUALITY_CUT),"hard cut rejects generated presentation");
   process(pixels);check(!e.readQuality().allow&&(e.readQuality().reasons&QUALITY_HOLDOFF),"first post-cut pair stays on real frames");
   process(pixels);check(!e.readQuality().allow&&(e.readQuality().reasons&QUALITY_HOLDOFF),"second post-cut pair stays on real frames");
   process(pixels);check(e.readQuality().allow==1,"third stable pair recovers after cut");
   auto solid=[&](uint8_t r,uint8_t g,uint8_t b){for(size_t i=0;i<pixels.size();i+=4){pixels[i]=r;pixels[i+1]=g;pixels[i+2]=b;pixels[i+3]=255;}};
   e.hasHistory=false;solid(100,100,100);process(pixels);solid(126,126,126);process(pixels);check(!e.readQuality().allow&&(e.readQuality().reasons&QUALITY_CUT),"low-contrast full-field cut is rejected");
   auto halves=[&](bool reverse){for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;bool red=(x<128)^reverse;pixels[i]=red?200:30;pixels[i+1]=red?30:200;pixels[i+2]=50;pixels[i+3]=255;}};
   e.hasHistory=false;halves(false);process(pixels);halves(true);process(pixels);auto h=e.readQuality();uint32_t histogramDifference=0;for(int i=0;i<64;i++)histogramDifference+=abs(int(h.previousHistogram[i])-int(h.currentHistogram[i]));check(histogramDifference==0,"same-histogram scene-cut fixture is valid");check(!h.allow&&(h.reasons&QUALITY_CUT),"same-histogram spatial cut still rejects generation");
   e.hasHistory=false;solid(255,0,0);process(pixels);solid(0,130,0);process(pixels);auto chroma=e.readQuality();check(float(chroma.lumaSum)/chroma.count/65535<0.005,"equal-luma chromatic cut fixture is valid");check(!chroma.allow&&(chroma.reasons&QUALITY_CUT),"equal-luma chromatic cut rejects generation");
   e.hasHistory=false;solid(63,63,63);process(pixels);solid(65,65,65);process(pixels);check(e.readQuality().allow==1,"small exposure/noise crossing a histogram bin is accepted");
   auto pattern=[](int x,int y,int c){float f=c==0?std::sin(x*0.17f):c==1?std::sin(y*0.13f+x*0.07f):std::sin((x+y)*0.11f);return uint8_t(std::lround(128+80*f));};
   auto frame=[&](int shift){for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;for(int c=0;c<3;c++)pixels[i+c]=pattern(x-shift,y,c);pixels[i+3]=255;}};
   e.hasHistory=false;frame(0);process(pixels);frame(2);process(pixels);read(e.flow);auto* vectors=(float*)mapped;int matching=0,total=0;for(int y=2;y<int(e.grid.height)-2;y++)for(int x=2;x<int(e.grid.width)-2;x++){size_t i=(y*e.grid.width+x)*4;float f=vectors[i]*256,b=vectors[i+2]*256;if(std::abs(f-2)<0.1&&std::abs(b+2)<0.1)matching++;total++;}printf("FLOW translation correct=%d/%d\n",matching,total);check(matching>total*0.85,"bidirectional flow detects +2/-2 pixel translation");check(e.readQuality().allow==1,"reliable in-range motion remains eligible");
   read(e.mid);double error=0,blendError=0;int values=0;for(int y=20;y<124;y++)for(int x=20;x<236;x++)for(int c=0;c<3;c++){int truth=pattern(x-1,y,c);error+=abs(int(bytes[(y*256+x)*4+c])-truth);blendError+=abs((int(pattern(x,y,c))+int(pattern(x-2,y,c)))/2-truth);values++;}error/=values;blendError/=values;printf("MIDPOINT mean_abs_error=%.4f crossfade_error=%.4f\n",error,blendError);check(error<blendError*0.75,"motion compensated midpoint beats simple crossfade");
   auto translation=[&](int shift){read(e.flow);auto* v=(float*)mapped;int ok=0,all=0;for(int y=2;y<int(e.grid.height)-2;y++)for(int x=2;x<int(e.grid.width)-2;x++){size_t i=(y*e.grid.width+x)*4;float f=v[i]*256,b=v[i+2]*256;if(std::abs(f-shift)<0.1&&std::abs(b+shift)<0.1)ok++;all++;}printf("FLOW translation %d correct=%d/%d\n",shift,ok,all);return ok>all*0.85;};
   {
    auto processMulti=[&](const float* phases,int count){begin();e.barrier(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={256,144,1};a.CmdCopyBufferToImage(cmd,buffer,source.image,VK_IMAGE_LAYOUT_GENERAL,1,&region);e.recordMulti(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,false,nullptr,phases,count);submit();};
    e.hasHistory=false;frame(0);process(pixels);frame(3);memcpy(mapped,pixels.data(),pixels.size());float phases[2]={1.0f/3.0f,2.0f/3.0f};processMulti(phases,2);
    auto phaseError=[&](const Image& image,int shift){read(image);double sum=0;int n=0;for(int y=20;y<124;y++)for(int x=20;x<236;x++)for(int c=0;c<3;c++){sum+=std::abs(int(bytes[(y*256+x)*4+c])-int(pattern(x-shift,y,c)));n++;}return sum/n;};
    // A linear blend of a moving sinusoid cannot reproduce the curve exactly at
    // 1/3 and 2/3 (about 3 grey levels here), so the check is that each image
    // matches its own temporal position clearly better than the other one.
    double thirdOwn=phaseError(e.mid,1),thirdOther=phaseError(e.mid,2);
    double twoOwn=phaseError(e.mid2,2),twoOther=phaseError(e.mid2,1);
    {read(e.mid);std::vector<uint8_t> first(bytes,bytes+256*144*4);read(e.mid2);int differs=0;for(size_t i=0;i<first.size();i++)if(first[i]!=bytes[i])differs++;printf("TRIPLE diff_bytes=%d\n",differs);
     read(e.mid);int m1=bytes[(60*256+60)*4];read(e.mid2);int m2=bytes[(60*256+60)*4];auto q=e.readQuality();
     printf("TRIPLE pixel60_60 mid=%d mid2=%d expect_t1=%d expect_t2=%d allow=%u reasons=%u\n",m1,m2,int(pattern(59,60,0)),int(pattern(58,60,0)),q.allow,q.reasons);}
    printf("TRIPLE own_1_3=%.4f other_1_3=%.4f own_2_3=%.4f other_2_3=%.4f\n",thirdOwn,thirdOther,twoOwn,twoOther);
    check(thirdOwn<twoOther*0.5&&twoOwn<thirdOther*0.5,"3x produces distinct 1/3 and 2/3 midpoints from one flow pass");
     // The fused 3x dispatch shares reads between the two positions; the
     // per-phase math must still equal what the single-phase pipeline produces.
     {
      auto readInto=[&](const Image& image,std::vector<uint8_t>& out){read(image);out.assign(bytes,bytes+size_t(256)*144*4);};
      std::vector<uint8_t> fusedA,fusedB,aloneA,aloneB;
      // read() copies into the same staging buffer processMulti() uploads from,
      // so the input frame must be restored before every run.
      memcpy(mapped,pixels.data(),pixels.size());processMulti(phases,2);readInto(e.mid,fusedA);readInto(e.mid2,fusedB);
      memcpy(mapped,pixels.data(),pixels.size());processMulti(&phases[0],1);readInto(e.mid,aloneA);
      memcpy(mapped,pixels.data(),pixels.size());processMulti(&phases[1],1);readInto(e.mid,aloneB);
      int fusedDiffA=0,fusedDiffB=0,maxDiffA=0,maxDiffB=0;long sumDiffA=0,sumDiffB=0;
      for(size_t i=0;i<aloneA.size();i++){int a=std::abs(int(aloneA[i])-int(fusedA[i])),b=std::abs(int(aloneB[i])-int(fusedB[i]));if(a){fusedDiffA++;sumDiffA+=a;maxDiffA=std::max(maxDiffA,a);}if(b){fusedDiffB++;sumDiffB+=b;maxDiffB=std::max(maxDiffB,b);}}
      printf("TRIPLE fused_vs_single_diff=%d,%d max=%d,%d mean=%.4f,%.4f\n",fusedDiffA,fusedDiffB,maxDiffA,maxDiffB,double(sumDiffA)/aloneA.size(),double(sumDiffB)/aloneA.size());
      // The GPU contracts float expressions differently when both positions are
      // inlined into one dispatch, so equality is requested to within one code
      // value per channel instead of bit equality.
      check(maxDiffA<=1&&maxDiffB<=1,"fused3x dispatch matches the single-phase shader within1LSB");
     }
   }
   e.hasHistory=false;frame(0);process(pixels);frame(4);process(pixels);check(translation(4),"flow detects +4/-4 translation at the old window edge");check(e.readQuality().allow==1,"motion at the old search boundary is now eligible");
   e.hasHistory=false;frame(0);process(pixels);frame(8);process(pixels);check(translation(8),"coarse-to-fine flow detects +8/-8 fast translation");check(e.readQuality().allow==1,"fast in-range motion remains eligible");
   e.hasHistory=false;frame(0);process(pixels);frame(20);process(pixels);check(translation(20),"far flow stage matches a 20px fast pan");check(e.readQuality().allow==1,"fast pan inside the far window stays eligible");
   e.hasHistory=false;frame(0);process(pixels);frame(40);process(pixels);check(!e.readQuality().allow,"pan beyond the far search window is still rejected");
   e.hasHistory=false;solid(80,120,160);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=0;process(pixels);solid(80,120,160);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=200;process(pixels);read(e.mid);
   bool midpointAlpha=true;for(size_t i=3;i<pixels.size();i+=4)midpointAlpha&=abs(int(bytes[i])-100)<=1;
   check(e.readQuality().allow==1&&midpointAlpha,"generated alpha-only transition keeps the half-opacity intermediate instead of flashing opaque");
   auto sprite=[&](int left){for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;bool object=x>=left&&x<left+64&&y>=32&&y<112;for(int c=0;c<3;c++){int value=pattern(object?x-left:x,object?y-32:y,c);pixels[i+c]=uint8_t(object?std::min(255,value+65):value);}pixels[i+3]=255;}};
   e.hasHistory=false;sprite(24);process(pixels);double maxSpriteError=0,maxCurrentError=0,maxCrossfadeError=0,maxSpriteLumaError=0;bool spritePairs=true;
   double bandErr=0,coreErr=0,outErr=0;long bandN=0,coreN=0,outN=0;long badByDistance[8]={0},badTotal=0;
   for(int left=28;left<=52;left+=4){auto previous=pixels;sprite(left);auto current=pixels;sprite(left-2);auto expected=pixels;pixels=current;process(pixels);spritePairs&=e.readQuality().allow==1;read(e.mid);double error=0,currentError=0,crossfadeError=0,luma=0,expectedLuma=0;int count=0;
    for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;for(int c=0;c<3;c++){error+=std::abs(int(bytes[i+c])-int(expected[i+c]));currentError+=std::abs(int(current[i+c])-int(expected[i+c]));crossfadeError+=std::abs((int(previous[i+c])+int(current[i+c])+1)/2-int(expected[i+c]));}luma+=bytes[i]*0.299+bytes[i+1]*0.587+bytes[i+2]*0.114;expectedLuma+=expected[i]*0.299+expected[i+1]*0.587+expected[i+2]*0.114;count++;}
    maxSpriteError=std::max(maxSpriteError,error/(count*3));maxCurrentError=std::max(maxCurrentError,currentError/(count*3));maxCrossfadeError=std::max(maxCrossfadeError,crossfadeError/(count*3));maxSpriteLumaError=std::max(maxSpriteLumaError,std::abs(luma-expectedLuma)/count);
    {int bx0=left-2,bx1=left+61,by0=32,by1=111;
     for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;double e3=0;for(int c=0;c<3;c++)e3+=std::abs(int(bytes[i+c])-int(expected[i+c]));int bd=std::min(std::min(x-bx0,bx1-x),std::min(y-by0,by1-y));bool inside=x>=bx0&&x<=bx1&&y>=by0&&y<=by1;
      if(inside&&bd>=8){coreErr+=e3;coreN+=3;}else if(inside||(x>=bx0-8&&x<=bx1+8&&y>=by0-8&&y<=by1+8)){bandErr+=e3;bandN+=3;
       int ad=inside?bd:std::max(std::max(bx0-x,x-bx1),std::max(by0-y,y-by1));
       if(e3/3.0>16.0){badTotal++;if(ad<8)badByDistance[ad]++;}}else{outErr+=e3;outN+=3;}}}
   }
   printf("MOTION_SEQUENCE textured_occluder max_rgb_mae=%.3f current_mae=%.3f crossfade_mae=%.3f max_mean_luma_error=%.3f\n",maxSpriteError,maxCurrentError,maxCrossfadeError,maxSpriteLumaError);
   printf("SPRITE_REGIONS core_mae=%.3f band_mae=%.3f outer_mae=%.3f px=%ld/%ld/%ld\n",coreN?coreErr/coreN:0.0,bandN?bandErr/bandN:0.0,outN?outErr/outN:0.0,coreN/3,bandN/3,outN/3);
   printf("SPRITE_BAD over16=%ld dist0..7=%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld\n",badTotal,badByDistance[0],badByDistance[1],badByDistance[2],badByDistance[3],badByDistance[4],badByDistance[5],badByDistance[6],badByDistance[7]);
   check(spritePairs&&maxSpriteError<1.0&&maxSpriteLumaError<0.1&&maxSpriteError<std::min(maxCurrentError,maxCrossfadeError)*0.5,"textured object midpoint beats current/crossfade with stable coverage and luminance");
   auto tinyPattern=[&](int left){for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;bool mark=x>=left&&x<left+8&&y>=60&&y<68;uint8_t value=uint8_t(((x+y)&1)?96:32);if(mark)value=uint8_t(((x-left+y)&1)?240:8);pixels[i]=pixels[i+1]=pixels[i+2]=value;pixels[i+3]=255;}};
   e.hasHistory=false;tinyPattern(40);process(pixels);auto previousTiny=pixels;tinyPattern(42);auto expectedTiny=pixels;tinyPattern(44);process(pixels);bool tinyAllowed=e.readQuality().allow==1;read(e.mid);
   double tinyError=0,tinyCurrentError=0,tinyCrossfadeError=0;for(int y=52;y<76;y++)for(int x=36;x<60;x++){size_t i=(y*256+x)*4;tinyError+=std::abs(int(bytes[i])-int(expectedTiny[i]));tinyCurrentError+=std::abs(int(pixels[i])-int(expectedTiny[i]));tinyCrossfadeError+=std::abs((int(previousTiny[i])+int(pixels[i])+1)/2-int(expectedTiny[i]));}
   tinyError/=24*24;
   tinyCurrentError/=24*24;tinyCrossfadeError/=24*24;
   printf("MOTION_SEQUENCE 8x8_texture allow=%d generated_mae=%.3f current_mae=%.3f crossfade_mae=%.3f\n",tinyAllowed,tinyError,tinyCurrentError,tinyCrossfadeError);
   bool tinyPreserved=memcmp(mapped,pixels.data(),pixels.size())==0;check((tinyError<2&&tinyError<std::min(tinyCurrentError,tinyCrossfadeError)*0.5)||tinyPreserved,"small moving checker midpoint beats current/crossfade or exactly preserves current when rejected");
   bool repeatedSafe=true;for(int i=0;i<6;i++){solid(i%2?240:20,i%2?10:220,50);process(pixels);repeatedSafe&=!e.readQuality().allow;read(e.mid);repeatedSafe&=memcmp(mapped,pixels.data(),pixels.size())==0;}check(repeatedSafe,"rapid alternating cuts never mix old and new scene in fixture");
   // This timing includes CPU command recording, submission and a queue idle wait.
   auto start=std::chrono::steady_clock::now();for(int i=0;i<20;i++){frame(4+(i%3));process(pixels);}printf("TIMING 256x144 to 512x288 record_submit_wait_mean_ms=%.3f (not pure GPU time or latency)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20);
   {
    Engine spatial;spatial.initSpatial(a,mem,{256,144},{512,288});VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={256,144,1};region.dstOffsets[1]={512,288,1};
    read(e.upCurrent);std::vector<uint8_t> expected(bytes,bytes+512*288*4);
    begin();spatial.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(spatial.upCurrent);
    check(memcmp(mapped,expected.data(),expected.size())==0,"spatial-only SR matches independently executed SGSR1 path");
    read(source);check(memcmp(mapped,pixels.data(),pixels.size())==0,"spatial SR leaves the application's source pixels unchanged");
    begin();spatial.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(spatial.upCurrent);
    check(memcmp(mapped,expected.data(),expected.size())==0,"spatial SR resource reuse preserves output");
   }
   {
    halves(false);process(pixels);Engine crop;crop.initSpatial(a,mem,{128,72},{512,288});VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[0]={128,16,0};region.srcOffsets[1]={256,88,1};region.dstOffsets[1]={512,288,1};
    begin();crop.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(crop.upCurrent);bool correct=true;for(int i=0;i<512*288*4;i+=4)if(abs(int(bytes[i])-30)>1||abs(int(bytes[i+1])-200)>1||abs(int(bytes[i+2])-50)>1||bytes[i+3]!=255)correct=false;
    check(correct,"spatial SR honors the application's source crop without sampling outside it");
    solid(40,90,160);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=77;process(pixels);begin();crop.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(crop.upCurrent);bool alpha=true;for(size_t i=3;i<512*288*4;i+=4)alpha&=abs(int(bytes[i])-77)<=1;check(alpha,"spatial SR preserves non-opaque input alpha");
    Engine passthrough;passthrough.init(a,mem,{256,144},{256,144});begin();passthrough.record(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,false);submit();read(passthrough.upCurrent);
    check(memcmp(mapped,pixels.data(),pixels.size())==0,"present-time FG preserves real frame exactly without a second SGSR filter");
   }
   {
    Engine spatial;spatial.initSpatial(a,mem,{256,144},{512,288},SpatialModel::Spatial9);
    VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={256,144,1};region.dstOffsets[1]={512,288,1};
    auto upscale=[&](){begin();spatial.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(spatial.upCurrent);};
    solid(40,90,160);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=77;process(pixels);upscale();
    bool constant=true;for(size_t i=0;i<512*288*4;i+=4)constant&=abs(int(bytes[i])-40)<=1&&abs(int(bytes[i+1])-90)<=1&&abs(int(bytes[i+2])-160)<=1&&abs(int(bytes[i+3])-77)<=1;
    check(constant,"Spatial9 preserves constant color and non-opaque alpha");
    auto edge=[&](){for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=x<128?64:192;pixels[i+3]=255;}};
    edge();process(pixels);upscale();std::vector<uint8_t> expected(bytes,bytes+512*288*4);
    bool bounded=true;for(size_t i=0;i<expected.size();i+=4)for(int c=0;c<3;c++)bounded&=bytes[i+c]>=64&&bytes[i+c]<=192;
    check(bounded,"Spatial9 step edge has no overshoot or undershoot");
    solid(240,10,20);process(pixels);upscale();edge();process(pixels);upscale();
    check(memcmp(mapped,expected.data(),expected.size())==0,"Spatial9 output is independent of previous scenes");
    read(source);check(memcmp(mapped,pixels.data(),pixels.size())==0,"Spatial9 leaves game source unchanged");
    Engine fractional;fractional.initSpatial(a,mem,{256,144},{479,269},SpatialModel::Spatial9);
    region.dstOffsets[1]={479,269,1};begin();fractional.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(fractional.upCurrent);
    bounded=true;for(size_t i=0;i<479*269*4;i+=4)for(int c=0;c<3;c++)bounded&=bytes[i+c]>=64&&bytes[i+c]<=192;
    check(bounded,"Spatial9 handles fractional scale and odd output dimensions");
   }
   for(auto model:{SpatialModel::Sgsr1,SpatialModel::Spatial9,SpatialModel::NeuralBounded}){
    Engine aa;aa.initSpatial(a,mem,{256,144},{512,288},model,true);
    VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={256,144,1};region.dstOffsets[1]={512,288,1};
    solid(40,90,160);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=77;process(pixels);
    begin();aa.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(aa.upCurrent);std::vector<uint8_t> raw(bytes,bytes+512*288*4);read(aa.aaOutput);
    check(memcmp(mapped,raw.data(),raw.size())==0,"FXAA leaves uniform RGB and alpha unchanged for each SR model");
    for(int y=0;y<144;y++)for(int x=0;x<256;x++){size_t i=(y*256+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=x>y*1.7+16?220:30;pixels[i+3]=uint8_t(x);}
    process(pixels);begin();aa.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(aa.upCurrent);raw.assign(bytes,bytes+512*288*4);read(aa.aaOutput);
    int changed=0;bool alpha=true;for(size_t i=0;i<raw.size();i+=4){changed+=bytes[i]!=raw[i];alpha&=bytes[i+3]==raw[i+3];}
    double rawError=0,aaError=0;
    for(int y=0;y<288;y++)for(int x=0;x<512;x++){
     int covered=0;for(int sy=0;sy<8;sy++)for(int sx=0;sx<8;sx++)covered+=((x+(sx+.5)/8)/2-.5)>((y+(sy+.5)/8)/2-.5)*1.7+16;
     double truth=30+190*covered/64.0;size_t i=(y*512+x)*4;
     rawError+=std::abs(raw[i]-truth);aaError+=std::abs(bytes[i]-truth);
    }
    printf("FXAA_EDGE model=%s no_aa_mae=%.6f fxaa_mae=%.6f reference=8x8_coverage_single_diagonal\n",spatialModelName(model),rawError/(512*288),aaError/(512*288));
    check(changed>100,"FXAA modifies jagged diagonal after each SR model");check(alpha,"FXAA preserves per-pixel alpha exactly");
    check(aa.spatialOutput()==aa.aaOutput.image,"FXAA enabled selects filtered output");
    Engine off;off.initSpatial(a,mem,{256,144},{512,288},model,false);begin();off.recordSpatial(cmd,source.image,VK_IMAGE_LAYOUT_GENERAL,region);submit();read(off.upCurrent);
    check(memcmp(mapped,raw.data(),raw.size())==0&&!off.aaOutput.image&&off.spatialOutput()==off.upCurrent.image,"FXAA disabled preserves original SR output without AA allocation");
   }
   if(families[family].timestampValidBits)for(int mode=0;mode<5;mode++){
    auto model=mode==0?SpatialModel::Sgsr1:SpatialModel::Spatial9;
    Engine spatial;spatial.initSpatial(a,mem,{640,360},{1920,1080},model,mode==2,false,0.6f,mode==3,0.12f,mode==4);auto rendered=e.createImage({640,360},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    auto scene=e.createPipeline(scene_spv,sizeof(scene_spv),{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},16);auto set=e.allocate(scene);e.storage(set,0,rendered);
    VkQueryPool poolTime{};VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;requireVk(vkCreateQueryPool(device,&query,nullptr,&poolTime),"TimestampPool");
    VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={640,360,1};region.dstOffsets[1]={1920,1080,1};std::vector<double> samples;
    for(int iteration=0;iteration<15;iteration++){
     begin();e.barrier(cmd,rendered.image,iteration?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,iteration?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT);
     struct{int32_t size[2];float time;int32_t cut;} push{{640,360},float(iteration)/30.0f,0};a.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.pipeline);a.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,scene.layout,0,1,&set,0,nullptr);a.CmdPushConstants(cmd,scene.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&push);a.CmdDispatch(cmd,80,45,1);
     vkCmdResetQueryPool(cmd,poolTime,0,2);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,poolTime,0);spatial.recordSpatial(cmd,rendered.image,VK_IMAGE_LAYOUT_GENERAL,region);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,poolTime,1);submit();
     uint64_t ticks[2]{};requireVk(vkGetQueryPoolResults(device,poolTime,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"TimestampRead");uint64_t delta=ticks[1]-ticks[0];if(families[family].timestampValidBits<64)delta&=(1ull<<families[family].timestampValidBits)-1;
     if(iteration>=5)samples.push_back(double(delta)*props.limits.timestampPeriod/1000000.0);
    }
    std::sort(samples.begin(),samples.end());printf("GPU_SPATIAL 640x360 to 1920x1080 copy+%s fxaa=%d smaa=%d taa=%d median_ms=%.3f min_ms=%.3f max_ms=%.3f n=%zu warmup=5 (synthetic source; excludes display blit, FG, CPU and latency)\n",spatialModelName(model),mode==2,mode==4,mode==3,samples[samples.size()/2],samples.front(),samples.back(),samples.size());
    vkDestroyQueryPool(device,poolTime,nullptr);a.DestroyPipeline(device,scene.pipeline,nullptr);a.DestroyPipelineLayout(device,scene.layout,nullptr);a.DestroyDescriptorSetLayout(device,scene.setLayout,nullptr);a.DestroyImageView(device,rendered.view,nullptr);a.DestroyImage(device,rendered.image,nullptr);a.FreeMemory(device,rendered.memory,nullptr);
   }
   a.UnmapMemory(device,bufferMemory);a.DestroyBuffer(device,buffer,nullptr);a.FreeMemory(device,bufferMemory,nullptr);a.DestroyImageView(device,source.view,nullptr);a.DestroyImage(device,source.image,nullptr);a.FreeMemory(device,source.memory,nullptr);a.DestroyCommandPool(device,pool,nullptr);
  }
  a.DestroyDevice(device,nullptr);device={};vkDestroyInstance(instance,nullptr);instance={};printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
 }catch(const std::exception& e){fprintf(stderr,"ERROR %s\n",e.what());if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);return 2;}
}
