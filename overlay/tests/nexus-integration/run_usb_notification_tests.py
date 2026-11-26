"""Exercise the firmware's actual eject/connection notification branches."""
from pathlib import Path
from tempfile import TemporaryDirectory
import subprocess

root = Path(__file__).resolve().parents[2]
source = root / "stratosphere/ams_mitm/source/usb_mitm"
module = (source / "usbmitm_module.cpp").read_text()
connection = module[module.index("        void NotifyUsbConnection("):module.index("        void SetContentRouteAppliedState(")]
eject = (source / "usb_eject_commands.hpp").read_text()
completion = eject[eject.index("    static u64 last_announced_request"):eject.rindex("    R_SUCCEED();")]

test = r'''
#include <cassert>
#include <string>
#include <vector>
#include <usb_eject.hpp>
#include <usb_eject_protocol.hpp>
using u32=uint32_t; using u64=uint64_t;
namespace usb=ams::usb;
namespace ams::usb::storage { enum class State { Ready, Off }; }
struct { usb::eject::Status e{}; auto GetEjectStatus(){return e;} } g_storage_service;
std::vector<std::string> events;
bool writable = true;
bool WriteUltrahandNotification(const char*, const char*, const char* icon) {
    if(writable) events.emplace_back(icon);
    return writable;
}
CONNECTION
void poll(const usb::eject::Status& e, usb::storage::State state, u64 g_eject_ack, u32 g_eject_ack_result=0) {
    struct {usb::storage::State state;} s{state};
    u64 g_eject_boot=1234, now=500;
    COMPLETION
}
int main() {
    using enum usb::eject::Phase;
    using enum usb::storage::State;
    using usb::eject::Stage;
    auto& e=g_storage_service.e;
    NotifyUsbConnection(true,"disk");
    NotifyUsbConnection(false,"disk");
    assert((events==std::vector<std::string>{"ams-usb-connected","ams-usb-disconnected"}));
    events.clear();
    e.request=10; e.generation=3;
    for (auto p : {Pending,Flushing,Failed}) {
        e.phase=p; NotifyUsbConnection(false,"disk"); poll(e,Off,10);
    }
    e.phase=Safe; e.stage=Stage::Complete;
    NotifyUsbConnection(false,"disk");
    poll(e,Ready,10); poll(e,Off,9); poll(e,Off,10,1);
    e.handles=1; poll(e,Off,10); e.handles=0;
    e.result=42; poll(e,Off,10); e.result=0;
    writable=false; poll(e,Off,10);
    assert(events.empty());
    writable=true; poll(e,Off,10); poll(e,Off,10);
    assert((events==std::vector<std::string>{"ams-usb-safe"}));
    e.phase=Online; NotifyUsbConnection(true,"disk");
    e.phase=Safe; e.request=11; poll(e,Off,11);
    assert((events==std::vector<std::string>{"ams-usb-safe","ams-usb-connected","ams-usb-safe"}));
}
'''
with TemporaryDirectory() as directory:
    path = Path(directory)
    (path/"events.cpp").write_text(test.replace("CONNECTION",connection).replace("COMPLETION",completion))
    subprocess.run(["g++","-std=c++20","-O2","-Wall","-Wextra","-Werror",
                    "-I"+str(root/"libraries/libstratosphere/include/stratosphere/usb"),
                    str(path/"events.cpp"),"-o",str(path/"events")], check=True)
    subprocess.run([str(path/"events")], check=True)
print("PASS connect, unplug, incomplete/busy/failed eject, stale acknowledgement, retry and one cue per completed eject.")
