#include "wsi_guard.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
int main(){
 int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';};
 std::mutex mutex;auto ok=[]{return VK_SUCCESS;};int calls=0;
 auto result=guardedWsiAcquire(mutex,0,[&]{++calls;return VK_SUCCESS;},ok);
 check(result==VK_SUCCESS&&calls==1,"successful acquisition is made exactly once");
 result=guardedWsiAcquire(mutex,0,[]{return VK_NOT_READY;},ok);
 check(result==VK_NOT_READY,"zero timeout retains nonblocking NOT_READY");
 auto start=std::chrono::steady_clock::now();
 result=guardedWsiAcquire(mutex,2000000,[]{return VK_NOT_READY;},ok);
 auto elapsed=std::chrono::steady_clock::now()-start;
 check(result==VK_TIMEOUT&&elapsed>=std::chrono::milliseconds(2)&&elapsed<std::chrono::seconds(1),"finite acquisition timeout expires without holding the presenter lock");
 std::promise<void> attempted;std::atomic<bool> ready{false};std::atomic<int> attempts{0};
 auto pending=std::async(std::launch::async,[&]{return guardedWsiAcquire(mutex,UINT64_MAX,[&]{if(!attempts++)attempted.set_value();return ready?VK_SUCCESS:VK_NOT_READY;},ok);});
 attempted.get_future().wait();
 {std::lock_guard<std::mutex> present(mutex);ready=true;}
 check(pending.get()==VK_SUCCESS,"presenter can release an image while an infinite acquire is waiting");
 std::atomic<VkResult> status{VK_SUCCESS};attempts=0;std::promise<void> retrying;
 auto failure=std::async(std::launch::async,[&]{return guardedWsiAcquire(mutex,UINT64_MAX,[&]{if(!attempts++)retrying.set_value();return VK_NOT_READY;},[&]{return status.load();});});
 retrying.get_future().wait();status=VK_ERROR_OUT_OF_DATE_KHR;
 check(failure.get()==VK_ERROR_OUT_OF_DATE_KHR,"deferred presentation failure interrupts an outstanding acquire");
 result=guardedWsiAcquire(mutex,UINT64_MAX,[]{return VK_SUBOPTIMAL_KHR;},ok);
 check(result==VK_SUBOPTIMAL_KHR,"native SUBOPTIMAL is returned with its acquired image");
 calls=0;result=guardedWsiAcquire(mutex,UINT64_MAX,[&]{++calls;return VK_SUCCESS;},[]{return VK_ERROR_DEVICE_LOST;});
 check(result==VK_ERROR_DEVICE_LOST&&!calls,"known device loss does not consume another image or semaphore");
 std::cout<<"RESULT checks="<<checks<<" failures=0\n";
}
