// SPDX-License-Identifier: GPL-2.0-only
#include "nexus_hoc_client.hpp"

namespace ams::ldr::nexus_oc {
namespace {
    Service g_helper{};
    nexus_hoc::Descriptor g_descriptor{};
    nexus_hoc::PatchRequest g_pending_request{};
    nexus_hoc::PatchReply g_pending_reply{};
    u64 g_next_sequence = 1;
    bool g_admitted = false, g_pending = false, g_failed = false;
    // IPC cannot use the SharedCode mapping of the target process. Two distinct
    // normal BSS mappings also avoid overlapping input/output IPC descriptors.
    alignas(nexus_hoc::PageSize) u8 g_source[nexus_hoc::MaxImageBytes];
    alignas(nexus_hoc::PageSize) u8 g_destination[nexus_hoc::MaxImageBytes];

    Result Fail(Result result) {
        g_failed = true;
        g_pending = false;
        g_pending_request = {};
        g_pending_reply = {};
        if (serviceIsActive(&g_helper)) serviceClose(&g_helper);
        R_RETURN(result);
    }
    void Hash(u8 (&out)[32], const void *data, size_t size) {
        crypto::GenerateSha256(out, sizeof(out), data, size);
    }
}

Result EnsureAvailable() {
    R_UNLESS(!g_failed, ldr::ResultInternalError());
    if (g_admitted) R_SUCCEED();
    const auto firmware = static_cast<u32>(hos::GetVersion());
    if (!nexus_hoc::SupportedFirmware(firmware)) return Fail(ldr::ResultInvalidNso());

    // A kernel named-port lookup never queues an SM deferred GetService request.
    // The connection budget does not imply a timeout for subsequent sync IPC.
    const auto started = os::GetSystemTick();
    os::NativeHandle handle = os::InvalidNativeHandle;
    while (true) {
        const Result rc = svc::ConnectToNamedPort(&handle, nexus_hoc::ServiceName);
        if (R_SUCCEEDED(rc)) break;
        if (!svc::ResultNotFound::Includes(rc)) return Fail(rc);
        if ((os::GetSystemTick() - started).ToTimeSpan().GetMilliSeconds() >= 5000)
            return Fail(svc::ResultTimedOut());
        os::SleepThread(TimeSpan::FromMilliSeconds(10));
    }
    serviceCreate(&g_helper, handle);
    const nexus_hoc::HelloRequest request{nexus_hoc::Magic, nexus_hoc::Abi,
        sizeof(nexus_hoc::HelloRequest), firmware, 1, 0};
    nexus_hoc::Descriptor descriptor{};
    Handle helper_process = INVALID_HANDLE;
    const nexus_hoc::AuthenticatedInput<nexus_hoc::HelloRequest> input{0, request};
    const Result rc = serviceDispatchInOut(&g_helper, nexus_hoc::HelloCommand, input, descriptor,
        .in_send_pid = true, .in_num_handles = 1, .in_handles = {CUR_PROCESS_HANDLE},
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &helper_process);
    ON_SCOPE_EXIT { if (helper_process != INVALID_HANDLE) R_DISCARD(svc::CloseHandle(helper_process)); };
    if (R_FAILED(rc)) return Fail(rc);
    if (helper_process == INVALID_HANDLE) return Fail(ldr::ResultInternalError());
    // Resolve through the kernel directly; PM is waiting for Loader here.
    u64 helper_program_id = 0;
    const Result identity_rc = svc::GetInfo(&helper_program_id, svc::InfoType_ProgramId, helper_process, 0);
    if (R_FAILED(identity_rc)) return Fail(identity_rc);
    if (helper_program_id != nexus_hoc::HelperProgramId || !nexus_hoc::ValidDescriptor(descriptor, firmware))
        return Fail(ldr::ResultInternalError());
    g_descriptor = descriptor;
    g_admitted = true;
    R_SUCCEED();
}

Result Patch(u32 kind, const u8 module_id[32], uintptr_t target_address,
             void *mapped_image, size_t module_size, size_t image_size) {
    R_TRY(EnsureAvailable());
    if (g_pending || mapped_image == nullptr || image_size == 0 || image_size > sizeof(g_source))
        return Fail(ldr::ResultInvalidNso());
    nexus_hoc::PatchRequest request{};
    request.magic = nexus_hoc::Magic; request.abi = nexus_hoc::Abi; request.size = sizeof(request);
    request.kind = kind; request.generation = g_descriptor.generation; request.sequence = g_next_sequence;
    request.program_id = kind == nexus_hoc::Pcv ? nexus_hoc::PcvProgramId : nexus_hoc::PtmProgramId;
    request.target_address = target_address; request.module_size = module_size; request.image_size = image_size;
    std::memcpy(request.module_id, module_id, sizeof(request.module_id));
    // No target-image writes until a complete matching reply and output hash.
    std::memcpy(g_source, mapped_image, image_size);
    std::memset(g_destination, 0, image_size);
    Hash(request.input_hash, g_source, image_size);
    if (!nexus_hoc::ValidPatch(request, g_descriptor.generation, image_size, image_size))
        return Fail(ldr::ResultInvalidNso());
    nexus_hoc::PatchReply reply{};
    const nexus_hoc::AuthenticatedInput<nexus_hoc::PatchRequest> input{0, request};
    const Result rc = serviceDispatchInOut(&g_helper, nexus_hoc::PatchCommand, input, reply,
        .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias, SfBufferAttr_Out | SfBufferAttr_HipcMapAlias},
        .buffers = {{g_source, image_size}, {g_destination, image_size}},
        .in_send_pid = true, .in_num_handles = 1, .in_handles = {CUR_PROCESS_HANDLE});
    if (R_FAILED(rc)) return Fail(rc);
    u8 actual_hash[32];
    Hash(actual_hash, g_destination, image_size);
    if (!nexus_hoc::ValidReply(reply, request, nexus_hoc::Prepared)
        || std::memcmp(actual_hash, reply.output_hash, sizeof(actual_hash)) != 0)
        return Fail(ldr::ResultInvalidNso());
    std::memcpy(mapped_image, g_destination, image_size);
    g_pending_request = request; g_pending_reply = reply; g_pending = true;
    ++g_next_sequence;
    R_SUCCEED();
}

Result Commit() {
    R_UNLESS(!g_failed && g_pending, ldr::ResultInternalError());
    nexus_hoc::CommitRequest request{nexus_hoc::Magic, nexus_hoc::Abi, sizeof(request),
        g_pending_request.kind, g_pending_request.generation, g_pending_request.sequence, {}};
    std::memcpy(request.output_hash, g_pending_reply.output_hash, sizeof(request.output_hash));
    nexus_hoc::PatchReply reply{};
    const nexus_hoc::AuthenticatedInput<nexus_hoc::CommitRequest> input{0, request};
    const Result rc = serviceDispatchInOut(&g_helper, nexus_hoc::CommitCommand, input, reply,
        .in_send_pid = true, .in_num_handles = 1, .in_handles = {CUR_PROCESS_HANDLE});
    if (R_FAILED(rc)) return Fail(rc);
    if (!nexus_hoc::ValidReply(reply, g_pending_request, nexus_hoc::Committed)
        || std::memcmp(reply.output_hash, g_pending_reply.output_hash, sizeof(reply.output_hash)) != 0)
        return Fail(ldr::ResultInternalError());
    g_pending = false; g_pending_request = {}; g_pending_reply = {};
    R_SUCCEED();
}

void InvalidatePending() {
    // A failed process load after preparation cannot be retried as stock OC:
    // the OC engine may already have performed regulator writes.
    if (g_pending) R_DISCARD(Fail(ldr::ResultInternalError()));
}
}
