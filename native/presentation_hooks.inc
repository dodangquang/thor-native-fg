static uint64_t monotonicNs(){timespec time{};clock_gettime(CLOCK_MONOTONIC,&time);return uint64_t(time.tv_sec)*1000000000+time.tv_nsec;}
static void pollPresentation(Device& d,Swap& s){
 if(!s.timing||!d.pastTiming)return;
 VkPastPresentationTimingGOOGLE values[128]{};uint32_t count=128;VkResult result;
 {std::lock_guard<std::mutex> lock(s.wsiMutex);result=d.pastTiming(d.handle,s.handle,&count,values);}
 if(result!=VK_SUCCESS&&result!=VK_INCOMPLETE){s.timing=false;LOG("DISPLAY_TIMING unavailable result=%d; request estimate retained",result);return;}
 for(uint32_t i=0;i<count;i++){
  auto found=s.pendingPresents.find(values[i].presentID);if(found==s.pendingPresents.end())continue;
  s.presentation.record(values[i].actualPresentTime,values[i].presentID,found->second.generated,found->second.source);
  char trace[PROP_VALUE_MAX]{};__system_property_get("debug.thorfg.trace",trace);
  if(trace[0]=='1')LOG("THOR_PRESENT id=%u generated=%d source_ns=%llu actual_ns=%llu earliest_ns=%llu desired_ns=%llu margin_ns=%llu",values[i].presentID,found->second.generated,(unsigned long long)found->second.source,(unsigned long long)values[i].actualPresentTime,(unsigned long long)values[i].earliestPresentTime,(unsigned long long)values[i].desiredPresentTime,(unsigned long long)values[i].presentMargin);
  s.pendingPresents.erase(found);
 }
}
static VkResult timedPresent(Device& d,Swap& s,VkQueue queue,const VkPresentInfoKHR* info,bool generated,uint64_t desired=0){
 if(!s.timing||info->pNext)return layerPresent(d,queue,info);
 uint32_t id=++s.presentID;if(!id)id=++s.presentID;
 VkPresentTimeGOOGLE time{id,desired};VkPresentTimesInfoGOOGLE times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};times.swapchainCount=1;times.pTimes=&time;
 VkPresentInfoKHR changed=*info;changed.pNext=&times;auto result=layerPresent(d,queue,&changed);
 if(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR){
  s.pendingPresents[id]={generated,s.cadence.last};
  if(s.pendingPresents.size()>512){for(auto i=s.pendingPresents.begin();i!=s.pendingPresents.end();){if(uint32_t(id-i->first)>256)i=s.pendingPresents.erase(i);else ++i;}}
 }
 return result;
}
