#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

// An application may acquire its next image while our presenter is still
// working. Native WSI calls on that swapchain require host synchronization.
// Never hold that lock over a blocking acquire: the presenter may need it to
// return the very image the acquisition is waiting for.
template<class Acquire,class Status>
VkResult guardedWsiAcquire(std::mutex& mutex,uint64_t timeout,Acquire acquire,Status status){
 using Clock=std::chrono::steady_clock;auto start=Clock::now();
 for(;;){
  VkResult result;
  {std::lock_guard<std::mutex> lock(mutex);result=status();if(result>=0)result=acquire();}
  if(result!=VK_NOT_READY&&result!=VK_TIMEOUT)return result;
  if(!timeout)return result;
  if(timeout!=UINT64_MAX){
   auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
   if(elapsed>=timeout)return VK_TIMEOUT;
   std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<uint64_t>(200000,timeout-elapsed)));
  }else std::this_thread::sleep_for(std::chrono::microseconds(200));
 }
}
