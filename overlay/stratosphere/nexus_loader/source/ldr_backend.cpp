// SPDX-License-Identifier: GPL-2.0-only
#include "ldr_backend.hpp"

namespace ams::ldr::backend {
    namespace {
        // The server dispatches on one thread. Each backend port has a separate,
        // persistent session; there is no ams:usb call on this path.
        Service g_pm{}, g_shell{}, g_debug{};
        Result Connect(Service *service,const char *name) {
            if (serviceIsActive(service)) R_SUCCEED();
            os::NativeHandle handle;
            R_TRY(sm::GetServiceHandle(std::addressof(handle),sm::ServiceName::Encode(name)));
            serviceCreate(service,handle);
            R_SUCCEED();
        }
    }
    Result GetProgramInfo(ProgramInfo *out,cfg::OverrideStatus *status,const ncm::ProgramLocation &location,const ProgramAttributes &attributes) {
        // Copy request data before nested IPC reuses the request TLS buffer.
        const struct { ProgramAttributes attributes; u16 pad1; u32 pad2; ncm::ProgramLocation location; } in{attributes,0,0,location};
        static_assert(sizeof(in)==0x18);
        R_TRY(Connect(&g_pm,"nxb:pm"));
        ProgramInfo info{}; cfg::OverrideStatus override_status{};
        R_TRY(serviceDispatchInOut(&g_pm,65001,in,override_status,
            .buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcPointer|SfBufferAttr_FixedSize},
            .buffers={{&info,sizeof(info)}}));
        *out=info; if (status) *status=override_status;
        R_SUCCEED();
    }
    Result PinProgram(PinId *out,const ncm::ProgramLocation &location,const cfg::OverrideStatus &status) {
        const struct { ncm::ProgramLocation location; cfg::OverrideStatus status; } in{location,status};
        R_TRY(Connect(&g_pm,"nxb:pm"));
        u64 id{}; R_TRY(serviceDispatchInOut(&g_pm,65002,in,id)); *out={id}; R_SUCCEED();
    }
    Result UnpinProgram(PinId id) {
        R_TRY(Connect(&g_pm,"nxb:pm")); R_RETURN(serviceDispatchIn(&g_pm,3,id.value));
    }
    Result CreateProcess(os::NativeHandle *out,PinId pin,u32 flags,os::NativeHandle resource,const ProgramAttributes &attributes) {
        const struct { ProgramAttributes attributes; u16 pad; u32 flags; u64 pin; } in{attributes,0,flags,pin.value};
        static_assert(sizeof(in)==0x10);
        R_TRY(Connect(&g_pm,"nxb:pm"));
        os::NativeHandle handle=os::InvalidNativeHandle;
        const Result result=serviceDispatchIn(&g_pm,0,in,.in_num_handles=1,.in_handles={resource},
            .out_handle_attrs={SfOutHandleAttr_HipcMove},.out_handles=&handle);
        if (R_FAILED(result)) {
            if (handle!=os::InvalidNativeHandle) R_DISCARD(svc::CloseHandle(handle));
            R_RETURN(result);
        }
        *out=handle; R_SUCCEED();
    }
    Result SetProgramArgument(ncm::ProgramId id,const void *argument,size_t size) {
        R_TRY(Connect(&g_shell,"nxb:shel"));
        R_RETURN(serviceDispatchIn(&g_shell,0,id.value,.buffer_attrs={SfBufferAttr_In|SfBufferAttr_HipcPointer},.buffers={{argument,size}}));
    }
    Result FlushArguments() {
        R_TRY(Connect(&g_shell,"nxb:shel")); R_RETURN(serviceDispatch(&g_shell,1));
    }
    Result GetProcessModuleInfo(u32 *count,ModuleInfo *out,size_t max_count,os::ProcessId pid) {
        R_TRY(Connect(&g_debug,"nxb:dmnt"));
        // Do not use the frontend server's TLS-backed pointer buffer as the
        // destination of nested IPC. Nintendo Loader has at most 32 NSOs.
        std::array<ModuleInfo,0x20> modules{}; s32 got{};
        const size_t capacity=std::min(max_count,modules.size());
        R_TRY(serviceDispatchInOut(&g_debug,2,pid.value,got,.buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcPointer},.buffers={{modules.data(),capacity*sizeof(ModuleInfo)}}));
        R_UNLESS(got>=0 && static_cast<size_t>(got)<=capacity,ldr::ResultInvalidSize());
        if (got) std::memcpy(out,modules.data(),got*sizeof(ModuleInfo));
        *count=got;
        R_SUCCEED();
    }
    Result RegisterExternalCode(os::NativeHandle *out,ncm::ProgramId id) {
        R_TRY(Connect(&g_shell,"nxb:shel"));
        os::NativeHandle handle=os::InvalidNativeHandle;
        const Result result=serviceDispatchIn(&g_shell,65000,id.value,.out_handle_attrs={SfOutHandleAttr_HipcMove},.out_handles=&handle);
        if (R_FAILED(result)) {
            if (handle!=os::InvalidNativeHandle) R_DISCARD(svc::CloseHandle(handle));
            R_RETURN(result);
        }
        *out=handle;
        R_SUCCEED();
    }
    Result UnregisterExternalCode(ncm::ProgramId id) {
        R_TRY(Connect(&g_shell,"nxb:shel")); R_RETURN(serviceDispatchIn(&g_shell,65001,id.value));
    }
    Result HasLaunchedBootProgram(bool *out,ncm::ProgramId id) {
        R_TRY(Connect(&g_pm,"nxb:pm")); u8 value{};
        R_TRY(serviceDispatchInOut(&g_pm,65000,id.value,value)); *out=(value&1)!=0; R_SUCCEED();
    }
    Result SetEnabledProgramVerification(bool enabled) {
        R_TRY(Connect(&g_pm,"nxb:pm")); const u8 value=enabled;
        R_RETURN(serviceDispatchIn(&g_pm,4,value));
    }
}
