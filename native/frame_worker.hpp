#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// One in-flight frame per swapchain. The producer can render the next game
// frame, but cannot accumulate an unbounded queue of old interpolation jobs.
class FrameWorker {
 std::mutex mutex;std::condition_variable changed;std::thread thread;
 std::function<void()> task;bool busy=false,stopping=false;
 void run(){
  std::unique_lock<std::mutex> lock(mutex);
  for(;;){
   changed.wait(lock,[&]{return stopping||bool(task);});
   if(stopping&&!task)break;
   auto current=std::move(task);task={};lock.unlock();current();current={};lock.lock();
   busy=false;changed.notify_all();
  }
 }
public:
 void wait(){std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return !busy;});}
 void submit(std::function<void()> next){
  std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return !busy;});
  if(!thread.joinable())thread=std::thread([this]{run();});
  task=std::move(next);busy=true;changed.notify_all();
 }
 void stop(){
  wait();{std::lock_guard<std::mutex> lock(mutex);stopping=true;changed.notify_all();}
  if(thread.joinable())thread.join();
 }
 ~FrameWorker(){stop();}
};
