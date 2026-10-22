#include "queue_plan.hpp"
#include <iostream>
#include <stdexcept>
int main(){
 int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';};
 std::vector<VkQueueFamilyProperties> families(2);families[0].queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;families[0].queueCount=3;families[1].queueFlags=VK_QUEUE_TRANSFER_BIT;families[1].queueCount=1;
 VkDeviceQueueCreateInfo normal{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};normal.queueCount=1;
 auto plan=reserveFrameQueue(families,{normal});check(plan.info==0&&plan.index==1,"reserve an unused queue after the application's own index");
 normal.queueCount=3;check(reserveFrameQueue(families,{normal}).info<0,"all available queues already requested preserves the original device");
 normal.queueCount=2;auto protectedQueue=normal;protectedQueue.flags=VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;protectedQueue.queueCount=1;
 check(reserveFrameQueue(families,{normal,protectedQueue}).info<0,"protected and unprotected requests share the family's capacity");
 normal.queueCount=1;check(reserveFrameQueue(families,{protectedQueue,normal}).info==1,"only extend an unprotected queue request");
 check(reserveFrameQueue(families,{protectedQueue}).info<0,"never create a private queue by modifying a protected request");
 normal.queueFamilyIndex=1;check(reserveFrameQueue(families,{normal}).info<0,"transfer-only queues cannot execute optical flow");
 normal.queueFamilyIndex=9;check(reserveFrameQueue(families,{normal}).info<0,"invalid family is left for the original Vulkan validation");
 check(reserveFrameQueue(families,{}).info<0,"empty queue list has no reservation");
 std::cout<<"RESULT checks="<<checks<<" failures=0\n";
}
