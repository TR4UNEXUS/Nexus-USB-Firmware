"""Run the real SM registration/lookup entry points with a fake service registry."""
from pathlib import Path
import subprocess,tempfile,json
W=Path(__file__).resolve().parent;S=W.parents[1]
def extract(t,signature):
 a=t.index(signature);b=t.index('{',a)+1;depth=1
 while depth:depth+=(t[b]=='{')-(t[b]=='}');b+=1
 return t[a:b]
t=(S/'stratosphere/sm/source/impl/sm_service_manager.cpp').read_text()
functions=['u64 ServiceValue(', 'ServiceName ServiceFromValue(', 'bool HasNexusLoaderProcess(', 'ServiceName ResolveLoaderService(', 'bool IsNexusLoaderOwner(', 'void ResumeServiceAndAliases(', 'Result RegisterServiceImpl(', 'Result HasService(', 'Result WaitService(', 'Result GetServiceHandle(']
body='\n'.join(extract(t,fn) for fn in functions)
prefix=r'''
#include <array>
#include <map>
#include <set>
#include <mutex>
#include <cstring>
#include <memory>
#include <cassert>
#include <cstdio>
#include "stratosphere/ldr/ldr_nexus_routing.hpp"
using u64=std::uint64_t;using u8=std::uint8_t;
struct Result {int value;};
#define R_FAILED(x) ((x).value!=0)
#define R_TRY(x) do{auto rc=(x);if(R_FAILED(rc))return rc;}while(0)
#define R_UNLESS(c,r) do{if(!(c))return(r);}while(0)
#define R_SUCCEED() return Result{0}
#define R_TRY_CATCH(x) do{auto rc=(x);if(R_FAILED(rc))
#define R_CONVERT(a,b) if(rc.value==(a()).value)return(b);
#define R_END_TRY_CATCH if(R_FAILED(rc))return rc;}while(0)
namespace ams {
namespace os {using NativeHandle=int;using ProcessId=u64;}
namespace svc {Result ResultOutOfSessions(){return {7};}}
namespace tipc {Result ResultRequestDeferred(){return {1};}}
namespace sm {
struct ServiceName {u64 value;auto operator<=>(const ServiceName&)const=default;};
Result ResultNotAllowed(){return {2};}Result ResultInvalidClient(){return {3};}Result ResultAlreadyRegistered(){return {4};}Result ResultOutOfServices(){return {5};}Result ResultOutOfSessions(){return {6};}
namespace impl {
std::recursive_mutex g_mutex;
struct ProcessInfo {u64 process_id;struct {u64 value;} program_id;size_t access_control_size;u8 access_control[1];};
std::array<ProcessInfo,8> g_process_list{};
struct MitmInfo {bool waiting_ack;};
struct ServiceInfo{ServiceName name;u64 owner_process_id;int port_h;size_t max_sessions;bool is_light;MitmInfo *mitm;};
std::array<ServiceInfo,16> services{};
std::set<u64> resumes,future;u64 acl_checked{},connected{};bool deny_acl=false;int native_fs_connections=0;
bool IsInitialProcess(u64 p){return p>=1 && p<=10;}
ProcessInfo*GetProcessInfo(u64 p){for(auto &v:g_process_list)if(v.process_id==p)return &v;return nullptr;}
ServiceInfo*GetServiceInfo(ServiceName n){for(auto &s:services)if(s.name==n)return &s;return nullptr;}
bool HasServiceInfo(ServiceName n){return GetServiceInfo(n)!=nullptr;}
ServiceInfo*GetFreeServiceInfo(){for(auto &s:services)if(!s.name.value)return &s;return nullptr;}
MitmInfo*GetMitmInfo(ServiceInfo*s){return s->mitm;}
Result ValidateServiceName(ServiceName n){return {n.value?0:10};}
struct AccessControlEntry{AccessControlEntry(const void*,size_t){}};
Result ValidateAccessControl(AccessControlEntry,ServiceName n,bool,bool){acl_checked=n.value;return {deny_acl?2:0};}
Result CreatePortImpl(int*out,int*port,size_t,bool,ServiceName&){static int next=100;*out=next++;*port=next++;return {0};}
void TriggerResume(ServiceName n){resumes.insert(n.value);}
bool ShouldDeferForInit(ServiceName){return false;}
bool HasFutureMitmDeclaration(ServiceName n){return future.count(n.value);}
Result GetServiceHandleImpl(int*out,ServiceInfo*s,u64){connected=s->name.value;if(connected==ldr::nexus_routing::FileSystemService && native_fs_connections++)return svc::ResultOutOfSessions();*out=s->port_h;return {0};}
'''
suffix=r'''
}}}
using namespace ams;using namespace ams::sm;using namespace ams::sm::impl;using namespace ams::ldr::nexus_routing;
int main(){
 int h=0;bool available=false;
 g_process_list[0]={1,{0x0100000000000001},0,{}};g_process_list[1]={20,{0x0000000000534c56},0,{}};
 for(const auto &r:Services) {
  assert(RegisterServiceImpl(&h,1,{r.public_name},3,false).value==0);
  assert(GetServiceHandle(&h,20,{r.public_name}).value==0 && connected==r.public_name && acl_checked==r.public_name);
  assert(HasService(&available,{r.public_name}).value==0 && available);
 }
 // Initial-process registration happens before PM opens ldr:pm. Frontend not yet ready: defer, never hand out backend.
 g_process_list[2]={10,{ProgramId},0,{}};
 // Realistic FS constraint: the native Loader port accepts just ONE session.
 // HOC arriving first must wait for the proxy without consuming that slot.
 assert(RegisterServiceImpl(&h,2,{FileSystemService},1,false).value==0);
 assert(GetServiceHandle(&h,1,{FileSystemService}).value==1 && native_fs_connections==0);
 assert(GetServiceHandle(&h,10,{FileSystemService}).value==0 && native_fs_connections==1 && connected==FileSystemService);
 assert(GetServiceHandle(&h,1,{FileSystemService}).value==1 && native_fs_connections==1);
 resumes.clear();assert(RegisterServiceImpl(&h,10,{FileSystemFrontend},2,false).value==0);
 assert(resumes.count(FileSystemService));
 assert(GetServiceHandle(&h,1,{FileSystemService}).value==0 && connected==FileSystemFrontend && native_fs_connections==1);
 assert(HasService(&available,{FileSystemService}).value==0 && available);
 deny_acl=true;assert(GetServiceHandle(&h,20,{FileSystemService}).value==2);deny_acl=false;
 for(const auto &r:Services) {
  assert(GetServiceHandle(&h,20,{r.public_name}).value==1);
  assert(HasService(&available,{r.public_name}).value==0 && !available && WaitService({r.public_name}).value==1);
  resumes.clear();assert(RegisterServiceImpl(&h,10,{r.frontend},3,false).value==0);
  assert(resumes.count(r.frontend) && resumes.count(r.public_name));
  assert(GetServiceHandle(&h,20,{r.public_name}).value==0 && connected==r.frontend && acl_checked==r.public_name);
  assert(WaitService({r.public_name}).value==0);
  assert(GetServiceHandle(&h,10,{r.backend_alias}).value==0 && connected==r.public_name);
  assert(GetServiceHandle(&h,1,{r.backend_alias}).value==2); // ordinary initial Loader is not allowed this private alias
  assert(GetServiceHandle(&h,20,{r.backend_alias}).value==2);
  deny_acl=true;assert(GetServiceHandle(&h,20,{r.public_name}).value==2 && acl_checked==r.public_name);deny_acl=false;
  future.insert(r.frontend);assert(GetServiceHandle(&h,20,{r.public_name}).value==1);future.clear();
  MitmInfo pending{true};GetServiceInfo({r.frontend})->mitm=&pending;assert(GetServiceHandle(&h,20,{r.public_name}).value==1);GetServiceInfo({r.frontend})->mitm=nullptr;
  resumes.clear();ResumeServiceAndAliases({r.public_name});assert(resumes.count(r.backend_alias));
 }
 // Unrelated sysmodule services retain their names and ACLs.
 for(const char *name:{"btdrv","nvdrv","fsp-srv","saltysd"}) {
  const u64 n=Name(name);assert(RegisterServiceImpl(&h,1,{n},3,false).value==0);
  assert(GetServiceHandle(&h,20,{n}).value==0 && connected==n && acl_checked==n);
 }
 // A similarly named regular sysmodule cannot impersonate the privileged initial loader.
 g_process_list[2]={30,{ProgramId},0,{}};assert(!HasNexusLoaderProcess());assert(GetServiceHandle(&h,30,{Name("nxb:pm")}).value==2);
 native_fs_connections=0;assert(GetServiceHandle(&h,1,{FileSystemService}).value==0 && connected==FileSystemService);
 puts("PASS production SM: one native FS Loader session, proxy startup waits, absent/present startup, public ACLs and private aliases.");
}
'''
report={'hardware_tested':False,'scope':'Production SM entry points; stubbed kernel ports and ACL evaluation','runs':[]}
with tempfile.TemporaryDirectory() as d:
 p=Path(d);src=p/'sm.cpp';src.write_text(prefix+body+suffix)
 for opt in ('-O0','-O2'):
  binary=p/('sm'+opt[-1]);subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-I'+str(S/'libraries/libstratosphere/include'),str(src),'-o',str(binary)],check=True)
  r=subprocess.run([str(binary)],timeout=30,check=True,capture_output=True,text=True);print(r.stdout.strip());report['runs'].append({'optimization':opt,'output':r.stdout.strip()})
for fn in ('InstallMitm','UninstallMitm','DeclareFutureMitm','ClearFutureMitm','AcknowledgeMitmSession'):
 part=extract(t,'Result '+fn+'(');assert 'service=ResolveLoaderService(service);' in part
 if 'ValidateAccessControl' in part:assert part.index('ValidateAccessControl')<part.index('service=ResolveLoaderService')
report['mitm_public_names_preserved']=True
print('PASS: public MITM names retained; production HS3R1 service tests passed.')
