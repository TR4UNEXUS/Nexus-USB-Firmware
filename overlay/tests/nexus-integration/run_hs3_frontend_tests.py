"""Exercise production proxy methods with instrumented Horizon/backend stubs."""
from pathlib import Path
import re,subprocess,tempfile,json
W=Path(__file__).resolve().parent;S=W.parents[1]
def extract(t,signature):
    start=t.index(signature);op=t.index('{',start);end=op+1;depth=1
    while depth:
        depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
t=(S/'stratosphere/nexus_loader/source/ldr_loader_service.cpp').read_text()
names=['CreateProcess','GetProgramInfo','PinProgram','UnpinProgram','GetProcessModuleInfo','RegisterExternalCode','UnregisterExternalCode','HasLaunchedBootProgram','SetEnabledProgramVerification']
methods=[extract(t,('void' if n in ('UnregisterExternalCode','HasLaunchedBootProgram') else 'Result')+' LoaderService::'+n+'(') for n in names]
helpers='\n'.join(extract(t,x) for x in ('bool HasExternalProgram(', 'void ForgetExternalProgram(', 'bool UseFrontend(', 'bool IsValidProgramAttributes('))
decls='\n'.join(m[:m.index('{')].replace('LoaderService::','')+';' for m in methods)
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <cstdio>
#include "stratosphere/ldr/ldr_nexus_routing.hpp"
using u64=std::uint64_t;using u32=std::uint32_t;using u8=std::uint8_t;
struct Result {int value{};};
#define R_SUCCEEDED(x) ((x).value==0)
#define R_FAILED(x) ((x).value!=0)
#define R_TRY(x) do {const auto rc=(x);if(R_FAILED(rc))return rc;}while(0)
#define R_RETURN(x) return (x)
#define R_THROW(x) return (x)
#define R_SUCCEED() return Result{}
#define R_UNLESS(c,r) do {if(!(c))return (r);}while(0)
#define R_DISCARD(x) (void)(x)
namespace ams {
namespace os {using NativeHandle=int;constexpr int InvalidNativeHandle=0;struct ProcessId{u64 value;};}
namespace ncm {
struct ProgramId {u64 value;auto operator<=>(const ProgramId&)const=default;};constexpr ProgramId InvalidProgramId{};
enum class StorageId:u8 {None=0,Host=1,GameCard=2,BuiltInSystem=3,BuiltInUser=4,SdCard=5};
enum class ContentMetaPlatform:u8 {Nx=0,Other=1};
struct ProgramLocation{ProgramId program_id;u8 storage_id;static ProgramLocation Make(ProgramId p,StorageId s){return {p,static_cast<u8>(s)};}};
bool IsSystemProgramId(ProgramId p){return (p.value>=0x0100000000000000 && p.value<=0x01000000000007ff)||(p.value>=0x010000000000b000 && p.value<=0x010000000000bfff)||p.value==0x010000000000d609||p.value==0x010000000000d623;}
}
namespace cfg {struct OverrideStatus{u64 flags;};}
namespace fs {constexpr int EntryNameLengthMax=64,ContentAttributes_None=0;}
namespace usb::content {enum class LoaderProbeStage {LoaderCreateProcessAttempt,LoaderCreateProcessPathResult,LoaderGetProgramInfoAttempt,LoaderGetProgramInfoPathResult,LoaderAliasFallbackAttempt,LoaderAliasFallbackResult};}
namespace ldr {
struct PinId{u64 value;};struct ModuleInfo{u64 marker;};struct ProgramInfo{ncm::ProgramId program_id;u64 marker;};
struct ProgramAttributes{ncm::ContentMetaPlatform platform{};u8 content_attributes{};};
struct ResultNotPinned:Result{ResultNotPinned():Result{9}{}static bool Includes(Result r){return r.value==9;}};
Result ResultMaxProcess(){return {10};}Result ResultInvalidProgramAttributes(){return {11};}
struct ArgumentStore {struct Entry {char argument[8];size_t argument_size;};Entry *Get(ncm::ProgramId){return nullptr;}Result Set(ncm::ProgramId,const void*,size_t){return {};}} g_argument_store;
struct Calls {int pin{},unpin{},create{},info{},modules{},external{},unregister{},verification{};u64 last_pin{};bool enabled{};Result failure{};u64 next_pin=0xffffffffffffffffull;};
Calls local,remote;
namespace backend {
Result PinProgram(PinId*out,const ncm::ProgramLocation&,const cfg::OverrideStatus&){++remote.pin;*out={remote.next_pin};return remote.failure;}
Result UnpinProgram(PinId p){++remote.unpin;remote.last_pin=p.value;return remote.failure;}
Result CreateProcess(os::NativeHandle*out,PinId p,u32,os::NativeHandle,const ProgramAttributes&){++remote.create;remote.last_pin=p.value;*out=R_SUCCEEDED(remote.failure)?22:0;return remote.failure;}
Result GetProgramInfo(ProgramInfo*out,cfg::OverrideStatus*,const ncm::ProgramLocation&loc,const ProgramAttributes&){++remote.info;*out={loc.program_id,22};return remote.failure;}
Result GetProcessModuleInfo(u32*count,ModuleInfo*out,size_t cap,os::ProcessId){++remote.modules;*count=cap?1:0;if(cap)*out={22};return remote.failure;}
Result RegisterExternalCode(os::NativeHandle*out,ncm::ProgramId){++remote.external;*out=22;return remote.failure;}
Result UnregisterExternalCode(ncm::ProgramId){++remote.unregister;return remote.failure;}
Result HasLaunchedBootProgram(bool*out,ncm::ProgramId){*out=true;return remote.failure;}
Result SetEnabledProgramVerification(bool value){++remote.verification;remote.enabled=value;return remote.failure;}
}
Result PinProgram(PinId*out,const ncm::ProgramLocation&,const cfg::OverrideStatus&){++local.pin;*out={local.next_pin};return local.failure;}
Result UnpinProgram(PinId p){++local.unpin;local.last_pin=p.value;return local.failure;}
Result GetProgramLocationAndOverrideStatusFromPinId(ncm::ProgramLocation*out,cfg::OverrideStatus*,PinId p){local.last_pin=p.value;*out={{0x010013c00e930000},5};return {};}
Result CreateProcess(os::NativeHandle*out,PinId p,const ncm::ProgramLocation&,const cfg::OverrideStatus&,const char*,const ArgumentStore::Entry*,u32,os::NativeHandle,const ProgramAttributes&){++local.create;local.last_pin=p.value;*out=11;return local.failure;}
Result GetProgramInfo(ProgramInfo*out,cfg::OverrideStatus*,const ncm::ProgramLocation&loc,const char*,const ProgramAttributes&){++local.info;*out={loc.program_id,11};return local.failure;}
Result GetProcessModuleInfo(u32*count,ModuleInfo*out,size_t cap,os::ProcessId p){++local.modules;if(p.value==999)return ResultNotPinned();if(R_FAILED(local.failure))return local.failure;*count=cap?1:0;if(cap)*out={11};return {};}
bool HasLaunchedBootProgram(ncm::ProgramId p){return p.value==1;}
void SetEnabledProgramVerification(bool value){++local.verification;local.enabled=value;}
Result GetProgramPath(char*out,size_t,const ncm::ProgramLocation&,const ProgramAttributes&){*out=0;return {};}
Result RedirectProgramPath(char*,size_t,const ncm::ProgramLocation&){return {};}
Result TryResolveUsbProgramAlias(ncm::ProgramLocation*,const ncm::ProgramLocation&){return {12};}
void RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage,const ncm::ProgramLocation&,Result={}){}
void ClearUsbExternalCodeVerification(ncm::ProgramId){}
nexus_routing::Pins<0x80> g_pins;
std::array<ncm::ProgramId,0x10> g_external_programs{};
}
namespace fssystem {
Result CreateExternalCode(os::NativeHandle*out,ncm::ProgramId){++ldr::local.external;*out=11;return ldr::local.failure;}
void DestroyExternalCode(ncm::ProgramId){++ldr::local.unregister;}
}
namespace ldr {
'''
suffix=r'''
}}
using namespace ams;using namespace ams::ldr;
int main(){
 LoaderService service;const cfg::OverrideStatus status{};const ProgramAttributes attrs{};
 ncm::ProgramLocation game{{0x010013c00e930000},5},pcv{{0x010000000000001a},3},external{{0x05004e584d470000},0};
 // HOC clock targets and custom boot sysmodules never enter the USB frontend.
 for(u64 id:{0x010000000000001aull,0x0100000000000010ull,0x420000000007e51aull,0x0000000000534c56ull,0x00FF0000636C6BFFull,0x010000000000bd00ull}) {
   ncm::ProgramLocation boot{{id},0}; ProgramInfo info{};auto before=local.info;
   assert(service.GetProgramInfo(&info,nullptr,boot,attrs).value==0 && info.marker==22 && local.info==before);
 }
 // Nexus's existing system-module and HOME patches must not be lost by delegation.
 for(u64 id:{0x0100000000000006ull,0x010000000000000full,0x010000000000001full,0x0100000000000023ull,0x0100000000000025ull,0x0100000000000033ull}) {
   for(u8 storage:{0,3}) {ProgramInfo info{};auto before=remote.info;assert(service.GetProgramInfo(&info,nullptr,{{id},storage},attrs).value==0 && info.marker==11 && remote.info==before);}
 }
 for(u64 id:{0x0100000000001000ull,0x010000000000100aull})assert(UseFrontend({{id},3}));
 for(u64 id:{0x010000000000001aull,0x0100000000000010ull})assert(!UseFrontend({{id},3}));
 PinId gp{},bp{};assert(service.PinProgram(&gp,game,status).value==0);assert(service.PinProgram(&bp,pcv,status).value==0);
 assert(gp.value && bp.value && gp.value!=bp.value && local.pin==1 && remote.pin==1);
 os::NativeHandle handle=99;assert(service.CreateProcess(&handle,gp,0,7,attrs).value==0 && handle==11 && local.last_pin==UINT64_MAX);
 assert(service.CreateProcess(&handle,bp,0,7,attrs).value==0 && handle==22 && remote.last_pin==UINT64_MAX);
 remote.failure={50};assert(service.UnpinProgram(bp).value==50 && g_pins.Find(bp.value));remote.failure={};assert(service.UnpinProgram(bp).value==0 && !g_pins.Find(bp.value));
 assert(service.CreateProcess(&handle,bp,0,7,attrs).value==9 && handle==0);
 assert(service.UnpinProgram(gp).value==0);PinId newer{};assert(service.PinProgram(&newer,game,status).value==0 && newer.value!=gp.value);
 // SaltyNX's two-module query and debugger's zero-capacity query use the actual owning loader.
 ModuleInfo modules[2]{};u32 count=999;assert(service.GetProcessModuleInfo(&count,modules,2,{1}).value==0 && count==1 && modules[0].marker==11 && remote.modules==0);
 assert(service.GetProcessModuleInfo(&count,modules,2,{999}).value==0 && count==1 && modules[0].marker==22 && remote.modules==1);
 assert(service.GetProcessModuleInfo(&count,modules,0,{1}).value==0 && count==0);
 local.failure={51};assert(service.GetProcessModuleInfo(&count,modules,2,{1}).value==51 && remote.modules==1);local.failure={};
 // External-code homebrew keeps its loader while pinned, including after unregister.
 assert(!UseFrontend(external));assert(service.RegisterExternalCode(&handle,external.program_id).value==0 && UseFrontend(external));
 PinId ep{};assert(service.PinProgram(&ep,external,status).value==0);service.UnregisterExternalCode(external.program_id);assert(UseFrontend(external));
 assert(service.UnpinProgram(ep).value==0 && !UseFrontend(external));
 assert(service.RegisterExternalCode(&handle,pcv.program_id).value==0 && remote.external==1);service.UnregisterExternalCode(pcv.program_id);assert(remote.unregister==1);
 bool launched=false;service.HasLaunchedBootProgram(&launched,{1});assert(launched);service.HasLaunchedBootProgram(&launched,{2});assert(launched);
 assert(service.SetEnabledProgramVerification(false).value==0 && !local.enabled && !remote.enabled);
 remote.failure={52};assert(service.SetEnabledProgramVerification(true).value==52 && !local.enabled);remote.failure={};
 // Full table fails before either loader allocates an inner pin; no eviction.
 while(g_pins.HasRoom())assert(g_pins.Add(123,nexus_routing::Owner::Backend));
 int before=local.pin;PinId rejected{99};assert(service.PinProgram(&rejected,game,status).value==10 && rejected.value==0 && local.pin==before);
 puts("PASS production Loader proxy: boot routing, opaque pins, retries, debugger/SaltyNX, external-code lifetime and capacity.");
}
'''
report={'hardware_tested':False,'scope':'Production method bodies with instrumented IPC stubs','runs':[]}
with tempfile.TemporaryDirectory() as d:
 p=Path(d);src=p/'frontend.cpp';src.write_text(prefix+helpers+'\nclass LoaderService { public:\n'+decls+'\n};\n'+'\n'.join(methods)+suffix)
 for opt in ('-O0','-O2'):
  binary=p/('frontend'+opt[-1]);subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-I'+str(S/'libraries/libstratosphere/include'),str(src),'-o',str(binary)],check=True)
  run=subprocess.run([str(binary)],timeout=30,check=True,capture_output=True,text=True);print(run.stdout.strip());report['runs'].append({'optimization':opt,'output':run.stdout.strip()})
