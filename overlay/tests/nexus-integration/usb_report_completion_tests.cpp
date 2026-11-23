// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <stratosphere/usb/usb_completion_core.hpp>
using namespace ams::usb::completion_diagnostics;
int main(){
 Recorder recorder;NexusUsbCompletionSnapshot snapshot{};
 auto event=MakeEvent(100,0,4096,0x2a,NEXUS_USB_COMPLETION_SOURCE_COMMAND);
 event.duration_ns=10;event.result=0;
 assert(recorder.Record(event)&&recorder.Snapshot(&snapshot,120,7));
 assert(snapshot.command_count==1&&!snapshot.event_count);
 event.result=0x3228c;event.start_ns=200;
 AccumulateHost(event,HostStage::Submit,200,2,4096,1,2,0);
 AccumulateHost(event,HostStage::Wait,202,4,4096,1,2,0x3228c);
 AccumulateHost(event,HostStage::Report,206,1,4096,1,2,0);
 assert(recorder.Record(event)&&recorder.Snapshot(&snapshot,220,7));
 assert(snapshot.command_count==2&&snapshot.event_count==1);
 const auto&saved=snapshot.events[0];assert(saved.result==0x3228c&&saved.opcode==0x2a&&saved.submit_ns==2&&saved.wait_ns==4&&saved.report_ns==1&&saved.max_wait_result==0x3228c);
 assert(saved.write_bytes_before==0); // failed operation never counted as written
 for(unsigned i=0;i<30;++i){event.start_ns+=10;assert(recorder.Record(event));}
 assert(recorder.Snapshot(&snapshot,1000,8)&&snapshot.event_count==NEXUS_USB_COMPLETION_RING&&snapshot.retained_total==31);
 puts("PASS: fast failures retained with command timings; bounded ring and generation stamped at capture.");
}
