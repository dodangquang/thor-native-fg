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
 auto alternating=[&](uint64_t shortGap,uint64_t longGap){SourceCadence cadence;uint64_t time=base;cadence.record(time);for(int i=0;i<48;i++){time+=i%2?longGap:shortGap;cadence.record(time);}return cadence;};
 auto jitter60=alternating(8000000,25333334);
 check(std::abs(double(jitter60.interval())-16666667)<500000,"bursty60FPS is paced near its average interval, not its longest half");
 auto burst60=alternating(500000,32833334);
 check(std::abs(double(burst60.interval())-16666667)<500000,"sub-millisecond bursts still contribute to the real source cadence");
 auto jitter90=alternating(2000000,20222222);
 check(jitter90.ready()&&!jitter90.canDouble(8333333),"bursty native90FPS is not misclassified for2x on a120Hz screen");
 auto burst120=alternating(500000,16166666);
 check(burst120.ready()&&!burst120.canDouble(8333333),"bursty native120FPS is preserved even when alternate gaps are below1ms");
 SourceCadence hitch;uint64_t time=base;hitch.record(time);for(int i=0;i<24;i++){time+=16666667;hitch.record(time);}time+=100000000;hitch.record(time);
 check(std::abs(double(hitch.interval())-16666667)<500000,"one long hitch does not impose a slower pacing interval on following frames");
 SourceCadence changing;time=base;changing.record(time);for(int i=0;i<24;i++){time+=16666667;changing.record(time);}
 for(int i=0;i<4;i++){time+=8333333;changing.record(time);}
 check(!changing.canDouble(8333333),"a switch from60to120FPS stops FG within four source arrivals");
 for(int i=0;i<16;i++){time+=33333333;changing.record(time);}
 check(changing.canDouble(8333333)&&std::abs(double(changing.interval())-33333333)<500000,"a later30FPS phase is recognized from fresh history");
 changing.record(time+33333333,true);check(!changing.ready(),"changing diagnostic cadence policy clears the previous phase");
 PresentationWindow age;for(uint32_t i=0;i<91;i++){uint64_t presented=base+i*period;bool generated=i%2;age.record(presented,i+1,generated,presented-(generated?10000000:30000000));}
 auto ages=age.stats(base+90*period+1000000);
 check(ages.valid&&ages.realAgeSamples>20&&ages.generatedAgeSamples>20&&std::abs(ages.realAgeMs-30)<0.01&&std::abs(ages.generatedAgeMs-10)<0.01,"API-to-presentation age separates real and generated frames");
 PresentationWindow invalidAge;for(uint32_t i=0;i<91;i++){uint64_t presented=base+i*period;invalidAge.record(presented,i+1,false,i%2?0:presented+1000000);}
 auto unknown=invalidAge.stats(base+90*period+1000000);
 check(unknown.valid&&unknown.realAgeSamples==0&&unknown.generatedAgeSamples==0,"missing or reversed source clocks never manufacture a latency measurement");
 printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
