#include "temporal_sequence.hpp"
#include <cstdio>
int main(){
 int checks=0,failures=0;auto check=[&](bool ok,const char* name){checks++;failures+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",name);};using S=TemporalStatus;
 TemporalSequence seq(3);TemporalTicket a,b,c,d;
 check(seq.begin(0,1,false,a)==S::Ready&&a.reset,"first frame requires reset");
 check(seq.begin(1,1,false,b)==S::Busy,"second recording cannot bypass pending acknowledgement");
 check(seq.retired(a)==S::InvalidTicket,"unsubmitted commands cannot retire");
 check(seq.abort(a)==S::Ready&&!seq.hasHistory(),"abandoned first command does not initialize GPU history");
 check(seq.begin(0,1,false,b)==S::Ready&&b.reset&&b.token!=a.token,"aborted slot reuses storage with a new generation");
 check(seq.submitted(a)==S::InvalidTicket,"stale aborted token cannot acknowledge another frame");
 check(seq.submitted(b)==S::Ready&&seq.hasHistory(),"successful submission advances history");
 check(seq.abort(b)==S::InvalidTicket,"submitted work cannot be cancelled as an abandoned recording");
 check(seq.begin(0,1,false,a)==S::InvalidFrame,"duplicate source frame rejected");
 check(seq.begin(1,1,false,a)==S::Ready&&!a.reset,"consecutive frame uses history");check(seq.submitted(a)==S::Ready,"second frame submitted");
 check(seq.begin(2,1,false,c)==S::Ready&&!c.reset,"third frame recorded into distinct slot");check(seq.submitted(c)==S::Ready,"third frame submitted");
 check(seq.submittedCount()==3&&seq.begin(3,1,false,d)==S::Busy,"in-flight capacity cannot overwrite UBOs or descriptors");
 check(seq.retired(b)==S::Ready,"completed fence may free oldest slot");check(seq.retired(b)==S::InvalidTicket,"double retirement rejected");
 check(seq.begin(3,1,false,d)==S::Ready&&d.slot==b.slot&&!d.reset,"retired slot may be reused without resetting history");
 check(seq.submitted(b)==S::InvalidTicket,"old generation cannot submit recycled slot");
 check(seq.abort(d)==S::Ready,"unsubmitted next frame can be abandoned");
 check(seq.begin(5,1,false,d)==S::Ready&&d.reset,"source gap resets history");check(seq.abort(d)==S::Ready,"gap reset is not committed on abort");
 check(seq.begin(3,1,false,d)==S::Ready&&!d.reset,"abort leaves prior submitted source index intact");check(seq.abort(d)==S::Ready,"consecutive abort");
 check(seq.begin(3,2,false,d)==S::Ready&&d.reset,"scene identity change forces reset");check(seq.submitted(d)==S::Ready,"scene transition submitted");
 check(seq.retired(a)==S::Ready&&seq.retired(c)==S::Ready&&seq.retired(d)==S::Ready&&seq.idle(),"all independent slots retired");
 check(seq.begin(4,2,true,d)==S::Ready&&d.reset,"explicit renderer reset honored");seq.fail();check(seq.begin(5,2,false,a)==S::Faulted,"uncertain submission prevents further scheduling");
 printf("RESULT temporal_sequence checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
