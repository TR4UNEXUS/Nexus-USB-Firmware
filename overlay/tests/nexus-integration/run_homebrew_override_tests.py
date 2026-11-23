from pathlib import Path
import subprocess, tempfile
W = Path(__file__).resolve().parent
S = W.parents[1]
def function(text, marker):
    start = text.index(marker)
    begin = text.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
cfg = (S / 'libraries/libstratosphere/source/cfg/cfg_override.board.nintendo_nx.inc').read_text()
hid = (S / 'libraries/libstratosphere/source/hid/hid_api.cpp').read_text()
common = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
using u64=std::uint64_t;
using Result=unsigned;
constexpr u64 HidNpadButton_R=1ull<<7, HidNpadButton_L=1ull<<6;
constexpr unsigned Module_Libnx=345,LibnxError_InitFail_HID=32;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_THROW(x) return (x)
#define R_SUCCEED() return 0
#define R_TRY(x) do {const auto rc=(x);if(rc)return rc;}while(0)
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
namespace ams {
namespace ncm {
using ProgramId=u64;
namespace SystemProgramId {constexpr ProgramId Hid=0x0100000000000013ull;}
namespace SystemAppletId {constexpr ProgramId Start=0x0100000000001000ull,Qlaunch=Start,PhotoViewer=0x010000000000100dull;}
bool IsApplicationId(ProgramId id){return id>=0x0100000000010000ull;}
}
bool local_home=true,local_hid=false,backend_hid=true,backend_error=false,dirty_failure=false,sd_ready=true;
int backend_queries=0,hid_initializations=0,refreshes=0;
u64 buttons=0;
namespace ldr {
bool HasLaunchedBootProgram(ncm::ProgramId id){return (id==ncm::SystemAppletId::Qlaunch&&local_home)||(id==ncm::SystemProgramId::Hid&&local_hid);}
namespace backend {
Result HasLaunchedBootProgram(bool*out,ncm::ProgramId id){++backend_queries;*out=dirty_failure||(id==ncm::SystemProgramId::Hid&&backend_hid);return backend_error?123u:0u;}
}
}
namespace pm::info {
LOOKUP
bool HasLaunchedBootProgram(ncm::ProgramId id){bool out=false;assert(HasLaunchedBootProgram(&out,id)==0);return out;}
}
namespace hid {
bool g_initialized_hid=false,active=false;
bool serviceIsActive(void*){return active;}
void* hidGetServiceSession(){return nullptr;}
void InitializeHid(){assert(backend_hid||local_hid);active=true;++hid_initializations;}
HID_GATE
Result GetKeysHeld(u64*out){R_TRY(EnsureHidInitialized());*out=buttons;return 0;}
}
namespace cfg {
struct OverrideLocale{};
namespace impl {
using OverrideStatusFlag=unsigned;
constexpr unsigned OverrideStatusFlag_AddressSpace64Bit39=0x100,OverrideStatusFlag_AddressSpaceMask=0xf00;
}
struct OverrideStatus {
 u64 keys_held=0;unsigned flags=0;
 void SetHbl(){flags|=1;}bool IsHbl()const{return flags&1;}
 void SetProgramSpecific(){flags|=2;}void SetCheatEnabled(){flags|=4;}
};
DEFAULTS
HELPERS
bool IsSdCardInitialized(){return sd_ready;}
void RefreshOverrideConfiguration(){++refreshes;}
ContentSpecificOverrideConfig GetContentOverrideConfig(ncm::ProgramId){return {g_default_override_key,g_default_cheat_enable_key,{}};}
CAPTURE
}
}
int main(int argc,char**argv){
 assert(argc==2);const int mode=std::atoi(argv[1]);using namespace ams;
 auto id=ncm::SystemAppletId::PhotoViewer;
 bool expected=FIXED;
 if(mode==0){local_hid=true;expected=true;} // N3 single-loader arrangement
 if(mode==2){buttons=HidNpadButton_R;expected=false;}
 if(mode==3){buttons=HidNpadButton_R;id=0x0100000000011234ull;}
 if(mode==4){id=0x0100000000011234ull;expected=false;}
 if(mode==5){local_home=false;expected=false;}
 if(mode==6){backend_hid=false;expected=false;}
 if(mode==7){backend_error=true;expected=false;}
 if(mode==8){backend_error=true;dirty_failure=true;expected=false;}
 if(mode==9){sd_ready=false;expected=false;}
 if(mode==10){id=0x010000000000001full;expected=false;}
 if(mode==11){cfg::g_hbl_override_config.program_configs[0].override_key.override_by_default=false;buttons=HidNpadButton_R;}
 if(mode==12){cfg::g_hbl_override_config.program_configs[0].override_key.override_by_default=false;expected=false;}
 if(mode==13){buttons=HidNpadButton_L;}
 const auto status=cfg::CaptureOverrideStatus(id);
 assert(status.IsHbl()==expected);
 if(expected){assert(hid_initializations==1);assert((status.flags&cfg::impl::OverrideStatusFlag_AddressSpaceMask)==cfg::impl::OverrideStatusFlag_AddressSpace64Bit39);assert(!(status.flags&4));}
 if(mode==0||mode==9||mode==10)assert(backend_queries==0);
 if(mode==5)assert(backend_queries==FIXED);
 if(mode==5||mode==10)assert(status.flags==2&&hid_initializations==0&&refreshes==0);
 if(mode==6||mode==7||mode==8||mode==9)assert(!status.IsHbl()&&hid_initializations==0);
 if(mode==13&&FIXED)assert(!(status.flags&2));
 if(mode==4&&FIXED)assert((status.flags&6)==6); // normal content/mod and cheat selection remains available
 if(mode==1){printf(FIXED?"PASS: split-loader Album opens homebrew\n":"REPRODUCED: old split-loader Album opens photo gallery\n");}
 if(mode==3){printf(FIXED?"PASS: split-loader R + game opens homebrew\n":"REPRODUCED: old split-loader R + game misses homebrew\n");}
}
'''
defaults = cfg[cfg.index('        /* Types. */'):cfg.index('        /* Helpers. */')]
helpers = '\n'.join(function(cfg, marker) for marker in (
    'constexpr inline bool IsOverrideMatch', 'inline bool IsAnySpecificHblProgramId',
    'inline bool IsSpecificHblProgramId', 'inline bool IsAnyApplicationHblProgramId'))
common = common.replace('DEFAULTS', defaults).replace('HELPERS', helpers)
common = common.replace('HID_GATE', function(hid, 'Result EnsureHidInitialized()'))
common = common.replace('CAPTURE', function(cfg, 'OverrideStatus CaptureOverrideStatus('))
with tempfile.TemporaryDirectory(prefix='nexus-album-') as folder:
    t = Path(folder)
    count = 0
    for fixed, tree in ((1, S),):
        source = (tree / 'stratosphere/nexus_loader/source/ldr_launch_record.cpp').read_text()
        lookup = function(source[source.index('namespace ams::pm::info'):], 'Result HasLaunchedBootProgram(')
        code = common.replace('LOOKUP', lookup).replace('FIXED', str(fixed))
        (t / 'check.cpp').write_text(code)
        for opt in ('-O0', '-O2'):
            subprocess.run(['g++', '-std=c++23', opt, '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', str(t / 'check.cpp'), '-o', str(t / 'check')], check=True)
            for mode in range(14):
                subprocess.run([str(t / 'check'), str(mode)], check=True)
                count += 1
    print(f'PASS: {count} scenarios using production boot lookup, HID gate, override defaults and selection logic.')
