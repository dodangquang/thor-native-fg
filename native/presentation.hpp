#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

struct PresentationStats {
 bool valid=false;double fps=0,generatedFps=0,medianGapMs=0,p95GapMs=0,maxGapMs=0;size_t samples=0;
 // Age begins at the intercepted present call, not at input sampling. Only
 // frames with a returned actual presentation timestamp contribute.
 double realAgeMs=0,realAgeP95Ms=0,generatedAgeMs=0,generatedAgeP95Ms=0;
 size_t realAgeSamples=0,generatedAgeSamples=0;
};
struct PresentationWindow {
 struct Frame {uint64_t time;uint32_t id;bool generated;uint64_t source=0;};
 std::deque<Frame> frames;
 void record(uint64_t time,uint32_t id,bool generated,uint64_t source=0){
  if(!time)return;
  auto at=std::lower_bound(frames.begin(),frames.end(),time,[](const Frame& frame,uint64_t value){return frame.time<value;});
  if(at!=frames.end()&&at->time==time){if(id>at->id)*at={time,id,generated,source};}
  else frames.insert(at,{time,id,generated,source});
  uint64_t newest=frames.back().time;
  while(frames.size()>2&&(frames.size()>256||newest-frames.front().time>1000000000ull))frames.pop_front();
 }
 PresentationStats stats(uint64_t now)const{
  PresentationStats result{};result.samples=frames.size();
  if(frames.size()<2||now<frames.back().time||now-frames.back().time>250000000ull)return result;
  uint64_t span=frames.back().time-frames.front().time;if(span<500000000ull)return result;
  result.valid=true;result.fps=(frames.size()-1)*1e9/double(span);size_t generated=0;std::vector<double> gaps,realAges,generatedAges;
  for(size_t i=1;i<frames.size();i++){
   const auto& frame=frames[i];generated+=frame.generated;gaps.push_back((frame.time-frames[i-1].time)/1e6);
   if(frame.source&&frame.time>=frame.source)(frame.generated?generatedAges:realAges).push_back((frame.time-frame.source)/1e6);
  }
  auto ageStats=[](std::vector<double>& ages,double& median,double& p95,size_t& count){count=ages.size();if(ages.empty())return;std::sort(ages.begin(),ages.end());median=ages[ages.size()/2];p95=ages[std::min(ages.size()-1,size_t(ages.size()*0.95))];};
  ageStats(realAges,result.realAgeMs,result.realAgeP95Ms,result.realAgeSamples);ageStats(generatedAges,result.generatedAgeMs,result.generatedAgeP95Ms,result.generatedAgeSamples);
  result.generatedFps=generated*1e9/double(span);std::sort(gaps.begin(),gaps.end());result.medianGapMs=gaps[gaps.size()/2];result.p95GapMs=gaps[std::min(gaps.size()-1,size_t(gaps.size()*0.95))];result.maxGapMs=gaps.back();return result;
 }
};
struct SourceCadence {
 uint64_t first=0,last=0;std::deque<uint64_t> gaps;bool legacy=false;
 void record(uint64_t now,bool useLegacy=false){
  if(legacy!=useLegacy){first=last=0;gaps.clear();legacy=useLegacy;}
  if(!first)first=now;
  if(last&&now>last){uint64_t gap=now-last;if(gap>200000000ull){gaps.clear();first=now;}else if(!legacy||gap>1000000ull){gaps.push_back(gap);if(gaps.size()>16)gaps.pop_front();}}
  last=now;
 }
 uint64_t interval()const{
  if(gaps.empty())return 33333333ull;
  std::vector<uint64_t> sorted(gaps.begin(),gaps.end());std::sort(sorted.begin(),sorted.end());
  if(legacy)return sorted[sorted.size()/2];
  // Fast/slow arrivals can alternate even when game simulation runs steadily.
  // The upper median selects the slow half and can misclassify90/120FPS as a
  // source fit for2x. Retain sub-ms arrivals and average the central samples;
  // discard one extreme at each end only after enough history, so one hitch
  // does not slow the following output phase for a whole observation window.
  size_t begin=sorted.size()>=8?1:0,end=sorted.size()-begin;uint64_t sum=0;
  for(size_t n=begin;n<end;n++)sum+=sorted[n];
  return (sum+(end-begin)/2)/(end-begin);
 }
 bool ready()const{return last>=first&&last-first>=250000000ull&&gaps.size()>=8;}
 bool canDouble(uint64_t refresh)const{return ready()&&double(interval())>=double(refresh)*1.9;}
};

struct PairSchedule {bool generate;uint64_t generated,real;};
inline PairSchedule planPairPresentation(uint64_t now,uint64_t half,uint64_t refresh,uint64_t lastReal,bool bounded=true,uint64_t source=0){
 half=std::max(half,refresh);
 uint64_t generated=lastReal?lastReal+half:now+refresh;
 if(generated<now+refresh/3)generated=now+refresh;
 // A much faster compute pass must not pull the real frame into the native
 // compositor's first generated-frame slot. Anchor the generated target to
 // the source call as well as GPU readiness; the real target follows by half
 // a source interval. This floor is independent of the shader implementation.
 if(source)generated=std::max(generated,source+2*refresh);
 // A cadence estimate can remain slightly slower than current arrivals.
 // Advancing its phase without a bound accumulates future cycles forever.
 // Spend at most three refresh cycles of lookahead after the input is ready;
 // two cycles rejected alternating24FPS arrivals with6ms jitter and created
 // a59ms display gap. The extra cycle absorbs that burst without unbounded
 // phase accumulation.
 // when that slot is full, preserve the real frame and consume only one slot
 // to catch up. Retain submission order rather than moving queued deadlines
 // backwards. A diagnostic legacy policy permits the same-binary control.
 if(bounded&&generated>now+3*refresh)return {false,0,std::max(now+refresh/3,lastReal+refresh)};
 return {true,generated,generated+half};
}
