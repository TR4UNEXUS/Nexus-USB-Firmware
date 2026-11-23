/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stratosphere.hpp>
#include <stratosphere/usb/usb_completion_probe.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ams;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define R_THROW(rc) return (rc)
#define R_RETURN(rc) return (rc)
#define R_TRY(expr) do{const Result r=(expr);if(R_FAILED(r))return r;}while(0)
#define AMS_ABORT(...) std::abort()
#define ATMOSPHERE_OS_HORIZON 1
constexpr unsigned USB_ENDPOINT_IN=0x80;
constexpr unsigned SfBufferAttr_HipcMapAlias=1,SfBufferAttr_Out=2,SfBufferAttr_In=4,SfBufferAttr_HipcAutoSelect=8;
struct Service{};struct Event{};
struct UsbHsXferReport{u32 xferId{},res{},requestedSize{},transferredSize{};u64 id{};};
struct SfDispatchParams{u32 buffer_attrs[2]{};struct{void*ptr{};size_t size{};}buffers[2]{};};
static void serviceAssumeDomain(Service*){}
static void armDCacheFlush(void*,size_t){}
struct Fake {
    u64 ready{},zero_delay{},setup_advance{};std::vector<u64> events;
    u32 submit_result{},query_result{},native_result{},wait_error{},zero_error{},count{1},length{512};
    unsigned submitted{},queries{},clears{},aborts{},sleeps{},zero_waits{},clock_reads{};
    u32 id{};bool consumed{},wrong_id{};std::vector<u64> waits;
} fake;
static u64 &Now(){return os::host_now_ns;}
static Result eventWait(Event*,u64 timeout){
    fake.waits.push_back(timeout);
    if(!timeout){++fake.zero_waits;Now()+=fake.zero_delay;if(fake.zero_error)return Result(fake.zero_error);}
    if(fake.wait_error)return Result(fake.wait_error);
    const auto end=timeout<=UINT64_MAX-Now()?Now()+timeout:UINT64_MAX;
    if(!fake.events.empty()&&fake.events.front()<=end){Now()=std::max(Now(),fake.events.front());return ResultSuccess();}
    Now()=end;return Result(9);
}
static Result eventClear(Event*){
    ++fake.clears;while(!fake.events.empty()&&fake.events.front()<=Now())fake.events.erase(fake.events.begin());return ResultSuccess();
}
namespace ams {
namespace svc {struct ResultTimedOut:Result{ResultTimedOut():Result(9){}static bool Includes(Result r){return r.GetValue()==9;}};}
namespace hos {constexpr unsigned Version_2_0_0=2,Version_3_0_0=3;inline unsigned version=23;unsigned GetVersion(){return version;}}
struct TimeSpan{u64 ns;static TimeSpan FromMilliSeconds(u64 n){return {n*1000000};}};
namespace os {void SleepThread(TimeSpan span){++fake.sleeps;Now()+=span.ns;}}
namespace usb {
Result ResultAlignmentError(){return Result(11);}Result ResultInternalStateError(){return Result(12);}
bool IsDmaAligned(u64 address){return address%4096==0;}
namespace impl {
u64 UsbNowNs(){if(++fake.clock_reads==2)Now()+=fake.setup_advance;return Now();}
constexpr u64 TransferWaitNs(u64 now,u64 deadline,u64 normal){return deadline==0?normal:(now>=deadline?0:std::min(deadline-now,normal));}
}
namespace probe_trace {template<typename...T>void Observe(T...){};}
namespace startup_history {template<typename...T>void Record(T...){};u32 Micros(u64 n){return n/1000;}}
namespace runtime_diagnostics {inline std::atomic<u64> empty_reports{},invalid_reports{};}
namespace completion_diagnostics {
enum class HostStage{Submit,Wait,Report};struct Span{HostStage stage;u64 elapsed;u32 result;};inline std::vector<Span> spans;
template<class F>Result MeasureHost(HostStage stage,u32,u32,u32,F op){const u64 start=Now();Result rc=op();spans.push_back({stage,Now()-start,rc.GetValue()});return rc;}
}
namespace host {
template<class In,class Out>Result DispatchInOut(::Service*,u32 command,const In&,Out &out,const SfDispatchParams &params={}){
    if(command==4){++fake.submitted;fake.consumed=false;out=++fake.id;return Result(fake.submit_result);}
    if(command<2){out=fake.length;return ResultSuccess();}
    CHECK(command==5);++fake.queries;
    // Counter/policy mutex must not be held across the report IPC.
    NexusUsbCompletionProbeStatus snapshot{};
    CHECK(completion_probe::Control(7,42,{},&snapshot)==completion_probe::Outcome::Success);
    if(fake.query_result)return Result(fake.query_result);
    out=(!fake.consumed&&Now()>=fake.ready)?fake.count:0;
    if(out){
        auto *report=static_cast<UsbHsXferReport*>(params.buffers[0].ptr);
        *report={fake.id+(fake.wrong_id?1u:0u),fake.native_result,512,fake.length,0};fake.consumed=true;
    }
    return ResultSuccess();
}
class EndpointSession{
public:
    bool m_is_initialized=true;
    struct{::Service s;Event eventXfer;struct{u32 bEndpointAddress=2;}desc;}m_session;
    void Abort(){++fake.aborts;m_is_initialized=false;}
    Result PostBuffer(void*,u32,u32*,u64);
};
#include "actual_completion_post_buffer.hpp"
}
}
}
using namespace ams::usb;
static constexpr u64 Ms=1000000;
static NexusUsbCompletionProbeStatus Status(){NexusUsbCompletionProbeStatus s{};CHECK(completion_probe::Control(7,42,{},&s)==completion_probe::Outcome::Success);return s;}
static void Reset(unsigned mode,u64 duration=120000){
    fake={};Now()=1000;hos::version=23;completion_probe::lease=completion_probe::Lease{};
    runtime_diagnostics::empty_reports=0;runtime_diagnostics::invalid_reports=0;completion_diagnostics::spans.clear();
    if(mode<2){NexusUsbCompletionProbeStatus s{};CHECK(completion_probe::Control(7,42,{7,0,1,mode,static_cast<u32>(duration),0},&s)==completion_probe::Outcome::Success);}
}
static Result Run(host::EndpointSession &endpoint,u32 &bytes,u64 deadline=0){alignas(4096)static char data[4096];return endpoint.PostBuffer(data,512,&bytes,deadline);}
static void SuccessfulCases(){
    for(unsigned mode:{0u,1u,2u}){
        Reset(mode);fake.ready=Now()+5*Ms;fake.events={fake.ready};host::EndpointSession ep;u32 bytes=999;
        CHECK(R_SUCCEEDED(Run(ep,bytes)));CHECK(bytes==512&&fake.submitted==1&&fake.queries==1&&fake.aborts==0);
        CHECK(Status().probe_queries==0&&fake.zero_waits==0);
    }
    Reset(1);fake.ready=Now()+50*Ms;host::EndpointSession ep;u32 bytes=999;
    CHECK(R_SUCCEEDED(Run(ep,bytes)));CHECK(bytes==512&&fake.submitted==1&&fake.queries==1&&Now()==1000+100*Ms);
    auto s=Status();CHECK(s.slice_timeouts==1&&s.probe_queries==1&&s.probe_ready==1&&!s.probe_empty&&!s.event_races&&!s.probe_errors);
    CHECK(fake.clears==1&&fake.zero_waits==1);
    Reset(1);fake.ready=Now()+250*Ms;host::EndpointSession later;bytes=999;
    CHECK(R_SUCCEEDED(Run(later,bytes)));s=Status();CHECK(s.probe_queries==3&&s.probe_empty==2&&s.probe_ready==1);
    CHECK(fake.submitted==1&&!fake.sleeps&&runtime_diagnostics::empty_reports==0);
    // A late event observed by the zero-timeout recheck uses the normal report path.
    Reset(1);fake.ready=Now()+100*Ms+1;fake.events={fake.ready};fake.zero_delay=1;host::EndpointSession race;
    CHECK(R_SUCCEEDED(Run(race,bytes)));s=Status();CHECK(s.slice_timeouts==1&&s.event_races==1&&!s.probe_queries&&fake.queries==1);
    // Late signal from a probed completion becomes a harmless empty event on the next transfer.
    Reset(1);fake.ready=Now()+50*Ms;fake.events={Now()+110*Ms};host::EndpointSession stale;
    CHECK(R_SUCCEEDED(Run(stale,bytes)));fake.ready=1000+300*Ms;fake.events.push_back(fake.ready);
    CHECK(R_SUCCEEDED(Run(stale,bytes)));CHECK(fake.submitted==2&&!fake.aborts&&runtime_diagnostics::empty_reports==1&&fake.sleeps==1);
    // Existing event-only stale-notification handling is preserved.
    Reset(0);fake.ready=Now()+250*Ms;fake.events={Now(),fake.ready};host::EndpointSession normal;
    CHECK(R_SUCCEEDED(Run(normal,bytes)));CHECK(fake.submitted==1&&runtime_diagnostics::empty_reports==1&&fake.sleeps==1&&!Status().probe_queries);
    // Expiry changes only the next transfer; no in-flight replay or mode switch.
    Reset(1,1);fake.ready=Now()+250*Ms;host::EndpointSession expiry;
    CHECK(R_SUCCEEDED(Run(expiry,bytes)));s=Status();CHECK(!s.flags&&s.expirations==1&&s.probe_queries==3);
    fake.ready=Now()+250*Ms;fake.events={fake.ready};const auto waits=fake.waits.size();
    CHECK(R_SUCCEEDED(Run(expiry,bytes)));CHECK(fake.waits.size()==waits+1&&fake.waits.back()>100*Ms&&Status().probe_queries==3);
    // HOS1.x never consults assisted waiting.
    Reset(1);hos::version=1;host::EndpointSession legacy;CHECK(R_SUCCEEDED(Run(legacy,bytes)));CHECK(fake.waits.empty()&&fake.queries==0&&Status().probe_queries==0);
}
static void ErrorsAndDeadlines(){
    for(unsigned mode:{0u,1u}){
        Reset(mode);fake.ready=Now()+1*Ms;fake.events={fake.ready};fake.native_result=99;fake.length=0xfffffff9;host::EndpointSession ep;u32 bytes=999;
        CHECK(Run(ep,bytes).GetValue()==99);CHECK(!bytes&&!fake.aborts&&!runtime_diagnostics::invalid_reports);
    }
    Reset(1);fake.ready=Now()+1*Ms;fake.native_result=99;fake.length=0xfffffff9;host::EndpointSession native;u32 bytes=999;
    CHECK(Run(native,bytes).GetValue()==99);auto s=Status();CHECK(!bytes&&s.probe_ready==1&&!s.probe_errors&&!fake.aborts);
    for(unsigned malformed=0;malformed<3;++malformed){
        Reset(1);fake.ready=Now()+1*Ms;if(malformed==0)fake.wrong_id=true;if(malformed==1)fake.count=2;if(malformed==2)fake.length=513;
        host::EndpointSession bad;bytes=999;CHECK(Run(bad,bytes).GetValue()==12);s=Status();
        CHECK(!bytes&&fake.aborts==1&&fake.submitted==1&&s.probe_errors==1&&!s.probe_ready&&runtime_diagnostics::invalid_reports==1);
    }
    Reset(1);fake.ready=UINT64_MAX;fake.query_result=77;host::EndpointSession query;bytes=999;
    CHECK(Run(query,bytes).GetValue()==77);CHECK(!bytes&&fake.aborts==1&&Status().probe_errors==1);
    Reset(1);fake.submit_result=78;host::EndpointSession submit;bytes=999;
    CHECK(Run(submit,bytes).GetValue()==78);CHECK(fake.submitted==1&&fake.waits.empty()&&!fake.queries&&!fake.aborts&&!bytes);
    Reset(1);fake.wait_error=79;host::EndpointSession wait;CHECK(Run(wait,bytes).GetValue()==79);CHECK(fake.aborts==1&&!fake.queries);
    Reset(1);fake.zero_error=80;host::EndpointSession zero;CHECK(Run(zero,bytes).GetValue()==80);CHECK(fake.aborts==1&&!fake.queries&&Status().slice_timeouts==1);
    for(unsigned mode:{0u,1u}){
        Reset(mode);fake.ready=UINT64_MAX;const auto start=Now();host::EndpointSession timeout;bytes=999;
        CHECK(Run(timeout,bytes,start+250*Ms).GetValue()==9);CHECK(Now()==start+250*Ms&&fake.aborts==1&&fake.submitted==1&&!bytes);
        CHECK(fake.queries==(mode?3u:1u));CHECK(Status().probe_empty==(mode?3u:1u));
    }
    // Deadline consumed during zero-timeout recheck: one final report query.
    Reset(1);fake.ready=UINT64_MAX;fake.zero_delay=60*Ms;host::EndpointSession crossing;bytes=999;
    CHECK(Run(crossing,bytes,Now()+150*Ms).GetValue()==9);CHECK(fake.submitted==1&&fake.queries==1&&fake.aborts==1&&!bytes);
    // Policy/setup consumed the budget: no transfer is submitted.
    Reset(1);fake.setup_advance=60*Ms;host::EndpointSession setup;bytes=999;
    CHECK(Run(setup,bytes,Now()+50*Ms).GetValue()==9);CHECK(!fake.submitted&&!fake.queries&&!fake.aborts&&!bytes);
    Reset(1);host::EndpointSession expired;CHECK(Run(expired,bytes,Now()).GetValue()==9);CHECK(fake.submitted==0);
    // Empty probes never renew the deadline; one final query is allowed.
    Reset(1);fake.ready=UINT64_MAX;const auto start=Now();host::EndpointSession overall;
    CHECK(Run(overall,bytes).GetValue()==9);CHECK(Now()==start+15000*Ms&&fake.submitted==1&&fake.queries==150&&fake.aborts==1);
}
static void FinalCompletionRecovery(){
    for(unsigned mode:{0u,1u,2u}){
        for(unsigned outcome=0;outcome<8;++outcome){
            Reset(mode);const auto start=Now();fake.ready=start+50*Ms;
            // No event arrives. The transfer owns the endpoint exactly once.
            if(outcome==1)fake.ready=UINT64_MAX;
            if(outcome==2)fake.wrong_id=true;
            if(outcome==3)fake.count=2;
            if(outcome==4)fake.length=513;
            if(outcome==5){fake.native_result=99;fake.length=0xfffffff9;}
            if(outcome==6)fake.query_result=77;
            if(outcome==7)fake.length=0; // valid short completion; BOT checks it
            host::EndpointSession ep;u32 bytes=999;
            const auto rc=Run(ep,bytes,start+50*Ms).GetValue();
            CHECK(fake.submitted==1&&fake.queries==1&&Now()==start+50*Ms);
            if(outcome==0||outcome==7){CHECK(!rc&&!fake.aborts&&bytes==(outcome?0:512));}
            else if(outcome==1){CHECK(rc==9&&fake.aborts==1&&!bytes);}
            else if(outcome==5){CHECK(rc==99&&!bytes);}
            else if(outcome==6){CHECK(rc==77&&fake.aborts==1&&!bytes);}
            else {CHECK(rc==12&&fake.aborts==1&&!bytes);}
        }
    }
    // Default event-only timeout also recovers, with no shortened polling loop.
    Reset(0);fake.ready=Now()+1*Ms;host::EndpointSession ep;u32 bytes{};
    CHECK(R_SUCCEEDED(Run(ep,bytes))&&bytes==512&&fake.waits.size()==1&&fake.queries==1&&fake.submitted==1);
}
static void LeaseRules(){
    using completion_probe::Outcome;completion_probe::Lease l;NexusUsbCompletionProbeStatus s{};
    CHECK(l.Mode(0)==0&&!l.Active());CHECK(l.Control(1,7,42,{},&s)==Outcome::Success);CHECK(s.size==144&&s.mode==0&&s.default_mode==0&&!s.flags);
    CHECK(l.Control(1,7,42,{7,0,1,1,1,0},&s)==Outcome::Success);const u64 token=s.token;
    CHECK(s.flags==3&&s.mode==1&&s.deadline_ns==1000001);CHECK(l.Control(2,7,43,{},&s)==Outcome::Success&&s.flags==1);
    CHECK(l.Control(2,7,43,{7,token,2,0,0,0},&s)==Outcome::Denied);
    CHECK(l.Control(2,7,42,{7,token+1,2,0,0,0},&s)==Outcome::Denied);
    CHECK(l.Control(2,7,42,{8,token,2,0,0,0},&s)==Outcome::MediaChanged);
    CHECK(l.Control(2,7,42,{7,0,1,0,1,0},&s)==Outcome::Busy);
    CHECK(l.Mode(1000000)==1&&l.Mode(1000001)==0&&!l.Active());
    CHECK(l.Control(1000002,7,42,{7,token,2,0,0,0},&s)==Outcome::Success&&s.expirations==1&&!s.flags);
    CHECK(l.Control(1,7,42,{7,0,1,0,120000,0},&s)==Outcome::Success&&s.mode==0&&s.flags==3&&s.token>token);
    CHECK(l.Control(2,8,42,{},&s)==Outcome::Success&&s.cancellations==1&&!s.flags);
    for(unsigned invalid=0;invalid<9;++invalid){auto r=NexusUsbCompletionProbeRequest{7,0,1,1,1,0};if(invalid==0)r.generation=0;if(invalid==1)r.token=1;if(invalid==2)r.mode=2;if(invalid==3)r.duration_ms=0;if(invalid==4)r.duration_ms=120001;if(invalid==5)r.reserved=1;if(invalid==6)r.operation=3;if(invalid==7)r.operation=0;if(invalid==8)r.operation=2;CHECK(l.Control(1,7,42,r,&s)==Outcome::InvalidParameter);}
    CHECK(l.Control(UINT64_MAX-10,7,42,{7,0,1,1,1,0},&s)==Outcome::InvalidParameter);
    CHECK(l.Control(1,7,0,{},&s)==Outcome::InvalidParameter);CHECK(l.Control(1,7,42,{},nullptr)==Outcome::InvalidParameter);
    CHECK(l.Control(1,7,42,{7,0,1,1,1,0},&s)==Outcome::Success);l.Invalidate();l.Invalidate();
    CHECK(l.Control(2,7,42,{},&s)==Outcome::Success&&s.cancellations==2&&!s.flags);
}
int main(){LeaseRules();SuccessfulCases();ErrorsAndDeadlines();FinalCompletionRecovery();std::printf("PASS %u actual PostBuffer/lease checks; no replay, event races, native errors, absolute deadlines and default mode\n",checks);}
