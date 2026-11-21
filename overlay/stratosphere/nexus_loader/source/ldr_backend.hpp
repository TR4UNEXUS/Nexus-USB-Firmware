// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stratosphere.hpp>
namespace ams::ldr::backend {
    Result GetProgramInfo(ProgramInfo *,cfg::OverrideStatus *,const ncm::ProgramLocation &,const ProgramAttributes &);
    Result PinProgram(PinId *,const ncm::ProgramLocation &,const cfg::OverrideStatus &);
    Result UnpinProgram(PinId);
    Result CreateProcess(os::NativeHandle *,PinId,u32,os::NativeHandle,const ProgramAttributes &);
    Result SetProgramArgument(ncm::ProgramId,const void *,size_t);
    Result FlushArguments();
    Result GetProcessModuleInfo(u32 *,ModuleInfo *,size_t,os::ProcessId);
    Result RegisterExternalCode(os::NativeHandle *,ncm::ProgramId);
    Result UnregisterExternalCode(ncm::ProgramId);
    Result HasLaunchedBootProgram(bool *,ncm::ProgramId);
    Result SetEnabledProgramVerification(bool);
}
