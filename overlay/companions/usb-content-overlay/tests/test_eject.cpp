#include "eject_view.hpp"
#include "usb_eject.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace nexus_eject;
int main() {
    Snapshot s{123,1000,7,0,0,0,0,0,0,0,1};
    eject_view::Status view;
    assert(!view.Observe(s,1100)); // A plausible old file is insufficient.
    assert(!view.Observe(s,1200)); // Re-reading it cannot make it live.
    s.sample_ms=2000;assert(view.Observe(s,2100));assert(view.CanEject(2100));
    s.handles=1;view.Observe(s,2100);assert(view.CanEject(2100) && !view.Safe(2100));
    s.handles=0;
    ams::usb::eject::Gate gate;gate.NewGeneration(7);
    assert(gate.Acquire(7));assert(!gate.Request(44,7));gate.Release(7);
    assert(!gate.Request(44,6));assert(gate.Request(44,7));assert(!gate.Acquire(7));
    s.request=s.ack=44;s.phase=1;s.stage=1;s.ready=0;view.Observe(s,2100);
    assert(!view.Safe(2100));assert(!view.CanEject(2100));assert(!view.CanResume(2100));
    for(unsigned stage=2;stage<=6;++stage) {
        s.phase=2;s.stage=stage;view.Observe(s,2100);assert(!view.Safe(2100));
        auto failed=gate;failed.At(static_cast<ams::usb::eject::Stage>(stage));failed.Fail(0x402);
        assert(failed.Blocked() && !failed.Resume());
        s.phase=4;s.result=0x402;view.Observe(s,2100);assert(!view.Safe(2100));s.result=0;
    }
    gate.Complete();assert(gate.Blocked());assert(!gate.Acquire(7));
    s.phase=3;s.stage=7;s.sample_ms=3000;view.Observe(s,3100);
    assert(view.Safe(3100));assert(view.CanResume(3100));assert(!view.CanEject(3100));
    for(unsigned field=0;field<6;++field) {
        auto bad=s;
        switch(field) {case 0:bad.ack++;break;case 1:bad.ack_result=1;break;case 2:bad.ready=1;break;
                      case 3:bad.handles=1;break;case 4:bad.result=1;break;case 5:bad.stage=6;break;}
        view.Observe(bad,3100);assert(!view.Safe(3100));
    }
    view.Observe(s,3100);assert(!view.Safe(6001)); // stale success never greenlights unplug
    view.Missing();assert(!view.Safe(3100));
    auto other=s;other.boot=456;other.sample_ms=3100;assert(!view.Observe(other,3200));
    assert(!view.Safe(3200));other.sample_ms=3200;assert(view.Observe(other,3300));
    assert(view.Safe(3300)); // reopening the overlay can observe the same live completed eject
    assert(gate.Resume());assert(gate.Acquire(7));
    Request req{Magic,123,3000,7,44,Eject,0};
    assert(Valid(req,123,4000));assert(!Valid(req,456,4000));assert(!Valid(req,123,9001));
    assert(!Valid(req,123,2999));req.reserved=1;assert(!Valid(req,123,4000));
    const char *record="NXEJ1 123 3000 7 44 44 0 3 7 0 0 0\n";Snapshot parsed{};
    assert(Parse(record,strlen(record),parsed));assert(Safe(parsed,123,44));
    for(size_t n=0;n<strlen(record);++n)assert(!Parse(record,n,parsed));
    assert(!Parse("NXEJ1 123 3000 7 44 44 0 3 7 0 0 0\nX",strlen(record)+1,parsed));
    std::cout<<"PASS: live heartbeat, in-use gate, every flush failure stage, confirmed safe, stale/reboot rejection, resume and malformed records\n";
}
