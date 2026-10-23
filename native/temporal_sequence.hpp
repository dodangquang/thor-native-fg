#pragma once
#include <cstdint>
#include <vector>
#include <limits>

enum class TemporalStatus {Ready,Busy,InvalidInput,InvalidFrame,InvalidTicket,Faulted};
struct TemporalTicket {uint64_t token=0,frame=0,scene=0;uint32_t slot=0;bool reset=false;};

// Host bookkeeping only. The GPU wrapper must verify the associated submission
// fence before retiring a slot; callers cannot turn a still-used UBO into free
// storage merely by recording the next frame. Recording may also be abandoned.
class TemporalSequence {
 enum class Phase {Free,Recorded,Submitted};
 struct Slot {Phase phase=Phase::Free;TemporalTicket ticket{};};
 std::vector<Slot> slots;uint64_t generation=0,lastFrame=0,lastScene=0;
 bool prepared=false,committed=false,fault=false;uint32_t pending=0;
 bool matches(const TemporalTicket& t)const{return t.slot<slots.size()&&t.token&&slots[t.slot].ticket.token==t.token&&slots[t.slot].ticket.frame==t.frame&&slots[t.slot].ticket.scene==t.scene;}
public:
 explicit TemporalSequence(uint32_t capacity=3):slots(capacity){}
 TemporalStatus begin(uint64_t frame,uint64_t scene,bool reset,TemporalTicket& out){
  if(fault)return TemporalStatus::Faulted;
  if(prepared)return TemporalStatus::Busy;
  if(committed&&frame<=lastFrame)return TemporalStatus::InvalidFrame;
  uint32_t index=0;while(index<slots.size()&&slots[index].phase!=Phase::Free)index++;
  if(index==slots.size())return TemporalStatus::Busy;
  if(generation==std::numeric_limits<uint64_t>::max()){fault=true;return TemporalStatus::Faulted;}
  TemporalTicket ticket{++generation,frame,scene,index,reset||!committed||scene!=lastScene||(lastFrame!=std::numeric_limits<uint64_t>::max()&&frame!=lastFrame+1)};
  slots[index]={Phase::Recorded,ticket};pending=index;prepared=true;out=ticket;return TemporalStatus::Ready;
 }
 TemporalStatus submitted(const TemporalTicket& t){
  if(fault)return TemporalStatus::Faulted;
  if(!prepared||!matches(t)||pending!=t.slot||slots[t.slot].phase!=Phase::Recorded)return TemporalStatus::InvalidTicket;
  slots[t.slot].phase=Phase::Submitted;prepared=false;committed=true;lastFrame=t.frame;lastScene=t.scene;return TemporalStatus::Ready;
 }
 TemporalStatus abort(const TemporalTicket& t){
  if(!prepared||!matches(t)||pending!=t.slot||slots[t.slot].phase!=Phase::Recorded)return TemporalStatus::InvalidTicket;
  slots[t.slot].phase=Phase::Free;prepared=false;return TemporalStatus::Ready;
 }
 TemporalStatus retired(const TemporalTicket& t){
  if(!matches(t)||slots[t.slot].phase!=Phase::Submitted)return TemporalStatus::InvalidTicket;
  slots[t.slot].phase=Phase::Free;return TemporalStatus::Ready;
 }
 bool isSubmitted(const TemporalTicket& t)const{return matches(t)&&slots[t.slot].phase==Phase::Submitted;}
 bool isRecorded(const TemporalTicket& t)const{return matches(t)&&slots[t.slot].phase==Phase::Recorded;}
 bool hasHistory()const{return committed;}
 bool hasPrepared()const{return prepared;}
 uint32_t submittedCount()const{uint32_t n=0;for(const auto& s:slots)n+=s.phase==Phase::Submitted;return n;}
 bool idle()const{return !prepared&&!submittedCount();}
 void fail(){fault=true;prepared=false;}
};
