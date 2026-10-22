#include "frame_worker.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(){
 int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';};
 FrameWorker worker;std::promise<void> entered,release;auto gate=release.get_future().share();std::atomic<bool> done{false};
 worker.submit([&]{entered.set_value();gate.wait();done=true;});entered.get_future().wait();
 check(!done,"producer returns while GPU-equivalent frame work is still blocked");
 auto waiting=std::async(std::launch::async,[&]{worker.wait();});
 check(waiting.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"drain cannot return before the owned frame completes");
 release.set_value();waiting.get();check(done,"drain establishes completion visibility");
 auto resource=std::make_shared<int>(7);std::weak_ptr<int> weak=resource;
 worker.submit([owned=resource]{if(*owned!=7)std::terminate();});resource.reset();worker.wait();
 check(weak.expired(),"captured frame resources are released before drain returns");
 std::atomic<int> count{0},active{0},overlap{0},outOfOrder{0};
 for(int n=0;n<1000;n++)worker.submit([&,n]{if(active.fetch_add(1))overlap++;if(count.fetch_add(1)!=n)outOfOrder++;active--;});
 worker.wait();check(count==1000&&!outOfOrder,"one thousand frames complete in producer order without drops");
 check(!overlap,"a swapchain never executes two quality/presentation jobs concurrently");
 std::atomic<bool> stoppedFrame{false};worker.submit([&]{stoppedFrame=true;});worker.stop();
 check(stoppedFrame,"shutdown drains an accepted frame before joining the thread");
 worker.stop();check(true,"repeated shutdown is safe");
 std::cout<<"RESULT checks="<<checks<<" failures=0\n";
}
