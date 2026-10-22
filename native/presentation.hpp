#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

struct PresentationStats {bool valid=false;double fps=0,generatedFps=0,medianGapMs=0,p95GapMs=0,maxGapMs=0;size_t samples=0;};
struct PresentationWindow {
 struct Frame {uint64_t time;uint32_t id;bool generated;};
 std::deque<Frame> frames;
 void record(uint64_t time,uint32_t id,bool generated){
  if(!time)return;
  auto at=std::lower_bound(frames.begin(),frames.end(),time,[](const Frame& frame,uint64_t value){return frame.time<value;});
  if(at!=frames.end()&&at->time==time){if(id>at->id)*at={time,id,generated};}
  else frames.insert(at,{time,id,generated});
  uint64_t newest=frames.back().time;
  while(frames.size()>2&&(frames.size()>256||newest-frames.front().time>1000000000ull))frames.pop_front();
 }
 PresentationStats stats(uint64_t now)const{
  PresentationStats result{};result.samples=frames.size();
  if(frames.size()<2||now<frames.back().time||now-frames.back().time>250000000ull)return result;
  uint64_t span=frames.back().time-frames.front().time;if(span<500000000ull)return result;
  result.valid=true;result.fps=(frames.size()-1)*1e9/double(span);size_t generated=0;std::vector<double> gaps;
  for(size_t i=1;i<frames.size();i++){generated+=frames[i].generated;gaps.push_back((frames[i].time-frames[i-1].time)/1e6);}
  result.generatedFps=generated*1e9/double(span);std::sort(gaps.begin(),gaps.end());result.medianGapMs=gaps[gaps.size()/2];result.p95GapMs=gaps[std::min(gaps.size()-1,size_t(gaps.size()*0.95))];result.maxGapMs=gaps.back();return result;
 }
};
struct SourceCadence {
 uint64_t first=0,last=0;std::deque<uint64_t> gaps;
 void record(uint64_t now){
  if(!first)first=now;
  if(last&&now>last){uint64_t gap=now-last;if(gap>200000000ull){gaps.clear();first=now;}else if(gap>1000000ull){gaps.push_back(gap);if(gaps.size()>16)gaps.pop_front();}}
  last=now;
 }
 uint64_t interval()const{if(gaps.empty())return 33333333ull;std::vector<uint64_t> sorted(gaps.begin(),gaps.end());std::sort(sorted.begin(),sorted.end());return sorted[sorted.size()/2];}
 bool ready()const{return last>=first&&last-first>=250000000ull&&gaps.size()>=8;}
 bool canDouble(uint64_t refresh)const{return ready()&&double(interval())>=double(refresh)*1.9;}
};
