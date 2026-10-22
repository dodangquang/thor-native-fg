#include "presentation.hpp"
#include <cstdio>
#include <cmath>
int main(){
 int checks=0,failures=0;auto check=[&](bool value,const char* label){++checks;if(!value)++failures;printf("%s %s\n",value?"PASS":"FAIL",label);};
 const uint64_t base=1000000000ull,period=16666667ull;PresentationWindow uniform;
 for(uint32_t i=0;i<91;i++)uniform.record(base+i*period,i+1,i%2);
 auto stats=uniform.stats(base+90*period+1000000);
 check(stats.valid&&std::abs(stats.fps-60)<0.01,"uniform display timestamps report60FPS");
 check(std::abs(stats.generatedFps-30)<1.1,"generated display rate is counted separately");
 check(std::abs(stats.p95GapMs-16.6667)<0.01,"uniform cadence gap is measured");
 check(!uniform.stats(base+90*period+300000000).valid,"stale presentation history cannot appear current");
 PresentationWindow burst;uint64_t at=base;for(uint32_t i=0;i<100;i++){burst.record(at,i+1,i%2);at+=i%2?25000000ull:8333333ull;}
 auto burstStats=burst.stats(at);check(burstStats.valid&&std::abs(burstStats.fps-60)<1.1&&burstStats.p95GapMs>24&&burstStats.maxGapMs>24,"burst cadence is visible despite an average near60FPS");
 PresentationWindow duplicate;for(uint32_t i=0;i<91;i++){duplicate.record(base+i*period,2*i+1,true);duplicate.record(base+i*period,2*i+2,false);}
 auto duplicateStats=duplicate.stats(base+90*period+1000000);check(duplicateStats.valid&&std::abs(duplicateStats.fps-60)<0.01&&duplicateStats.generatedFps==0,"same display instant never counts as two visible frames");
 PresentationWindow reordered;for(uint32_t i=91;i>0;i--)reordered.record(base+(i-1)*period,i,(i-1)%2);
 check(std::abs(reordered.stats(base+90*period+1000000).fps-60)<0.01,"out-of-order records preserve measured cadence");
 PresentationWindow startup;startup.record(0,1,false);startup.record(base,2,false);startup.record(base+period,3,true);check(!startup.stats(base+period).valid,"startup or zero timestamps do not create a measured FPS claim");
 SourceCadence fast;for(uint32_t i=0;i<90;i++)fast.record(base+i*8333333ull);check(fast.ready()&&!fast.canDouble(8333333),"native120FPS is not reduced to60FPS to force frame generation");
 SourceCadence slow;for(uint32_t i=0;i<25;i++)slow.record(base+i*33333333ull);check(slow.canDouble(8333333),"native30FPS is eligible for doubling on120Hz");
 SourceCadence warm;for(uint32_t i=0;i<5;i++)warm.record(base+i*33333333ull);check(!warm.ready()&&!warm.canDouble(8333333),"startup cadence must be observed before enabling FG");
 slow.record(base+5000000000ull);check(!slow.ready(),"a long pause clears stale source cadence");
 printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
