/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stratosphere.hpp>
#include "ldr_argument_store.hpp"
#include "ldr_content_management.hpp"
#include "ldr_development_manager.hpp"
#include "ldr_process_creation.hpp"
#include "ldr_launch_record.hpp"
#include "ldr_loader_service.hpp"
#include "ldr_backend.hpp"
#include <stratosphere/ldr/ldr_nexus_routing.hpp>

namespace ams::ldr {

    namespace {

        constinit ArgumentStore g_argument_store;
        // Opaque backend pins are mapped, not tagged: future HOC builds may use
        // all 64 bits. The table never evicts a live process or recycles an ID.
        nexus_routing::Pins<0x80> g_pins;
        std::array<ncm::ProgramId,0x10> g_external_programs{};
        // The loader processing a CardsNX Album launch must also own its external
        // filesystem. The loader service handles these requests on one thread.
        bool g_backend_album_registered = false;
        void SetBackendAlbumRegistration(bool registered) {
            // A pin from the previous registration must not consume a new
            // provider. Keep its owner so the caller can still release it.
            g_pins.InvalidateCreates(nexus_routing::Owner::Backend, ncm::SystemAppletId::PhotoViewer.value);
            g_backend_album_registered = registered;
        }
        bool HasExternalProgram(ncm::ProgramId id) {
            return id != ncm::InvalidProgramId && std::find(g_external_programs.begin(),g_external_programs.end(),id)!=g_external_programs.end();
        }
        void ForgetExternalProgram(ncm::ProgramId id) {
            for (auto &entry:g_external_programs) if (entry==id) entry=ncm::InvalidProgramId;
        }
        bool UseFrontend(const ncm::ProgramLocation &loc) {
            if (loc.program_id == ncm::SystemAppletId::PhotoViewer && g_backend_album_registered) {
                return false;
            }
            return nexus_routing::UseFrontend(loc.program_id.value,ncm::IsSystemProgramId(loc.program_id),loc.storage_id)
                || (!ncm::IsSystemProgramId(loc.program_id)
                    && (HasExternalProgram(loc.program_id)
                        || g_pins.HasFrontendProgram(loc.program_id.value,loc.storage_id)));
        }

        Result TryResolveUsbProgramAlias(ncm::ProgramLocation *out, const ncm::ProgramLocation &loc) {
            R_UNLESS(out != nullptr, ncm::ResultInvalidOperation());
            *out = loc;

            if constexpr (usb::content::UseFileSystemContentRoute) {
                /* ProgramLocation stays canonical in R97; there is no opaque
                 * Host alias to repair in Loader. */
                R_THROW(ncm::ResultContentNotFound());
            }

            /* This is deliberately a failure-only Loader fallback. PGL no
             * longer asks ams:usb about ordinary SD launches, and Loader calls
             * this helper only after stock path/meta loading has already
             * failed. This mirrors the replacement-Loader boundary observed
             * in SX OS without placing USB IPC in every homebrew launch. */
            const auto storage_id = static_cast<ncm::StorageId>(loc.storage_id);
            R_UNLESS((storage_id == ncm::StorageId::SdCard || storage_id == ncm::StorageId::Host)
                     && !ncm::IsSystemProgramId(loc.program_id)
                     && !ncm::IsApplicationId(loc.program_id),
                     ncm::ResultContentNotFound());

            ncm::ProgramId resolved_program_id{};
            /* Prefer the durable SD map. The previous implementation asked
             * ams:usb first, but that command only searched its in-memory
             * alias cache; after a reboot the cache could be empty even when
             * title_map.bin contained the validated association. This direct
             * lookup runs only after the stock Loader attempt has completed,
             * so it does not put USB IPC into the normal launch path. */
            Result alias_result = ncm::ResolveUsbTitleAlias(std::addressof(resolved_program_id), loc.program_id);
            if (R_FAILED(alias_result)) {
                /* Keep the runtime cache as a compatibility fallback for an
                 * alias learned during the current media generation but not
                 * persisted yet. */
                os::NativeHandle service_handle;
                R_TRY(sm::GetServiceHandle(std::addressof(service_handle), sm::ServiceName::Encode(usb::content::ServiceName)));

                Service usb_service{};
                serviceCreate(std::addressof(usb_service), service_handle);
                ON_SCOPE_EXIT { serviceClose(std::addressof(usb_service)); };

                alias_result = serviceDispatchInOut(std::addressof(usb_service), usb::content::CommandId_ResolveProgramIdAlias, loc.program_id, resolved_program_id);
            }
            R_TRY(alias_result);
            R_UNLESS(resolved_program_id != loc.program_id && !ncm::IsSystemProgramId(resolved_program_id), ncm::ResultContentNotFound());

            /* Match the PGL handoff: Host is the USB byte provider, while the
             * installed title's authoritative content-meta database remains
             * on the redirected SD storage. */
            const ncm::StorageId resolved_storage_id = storage_id == ncm::StorageId::Host
                ? ncm::StorageId::SdCard
                : storage_id;
            *out = ncm::ProgramLocation::Make(resolved_program_id, resolved_storage_id);
            R_SUCCEED();
        }

        bool IsValidProgramAttributes(const ldr::ProgramAttributes &attrs) {
            /* Check that the attributes are valid; on NX, this means Platform = NX + no content attrs. */
            if (attrs.platform != ncm::ContentMetaPlatform::Nx) {
                return false;
            }
            if (attrs.content_attributes != fs::ContentAttributes_None) {
                return false;
            }

            return true;
        }

    }


    Result LoaderService::CreateProcess(os::NativeHandle *out, PinId pin_id, u32 flags, os::NativeHandle resource_limit, const ProgramAttributes &attrs) {
        *out=os::InvalidNativeHandle;
        const auto *route=g_pins.Find(pin_id.value);
        R_UNLESS(route != nullptr && route->can_create, ldr::ResultNotPinned());
        if (route->owner == nexus_routing::Owner::Backend) {
            R_TRY(backend::CreateProcess(out, PinId{route->internal}, flags, resource_limit, attrs));
            if (route->program == ncm::SystemAppletId::PhotoViewer.value && g_backend_album_registered) {
                // Successful creation consumes the backend's external code session.
                SetBackendAlbumRegistration(false);
            }
            R_SUCCEED();
        }
        pin_id={route->internal};
        /* Check that the program attributes are valid. */
        R_UNLESS(IsValidProgramAttributes(attrs), ldr::ResultInvalidProgramAttributes());

        /* Get the location and override status. */
        ncm::ProgramLocation loc;
        cfg::OverrideStatus override_status;
        R_TRY(ldr::GetProgramLocationAndOverrideStatusFromPinId(std::addressof(loc), std::addressof(override_status), pin_id));
        // Path resolution may register a USB code archive before creation
        // fails. A successful creation already consumes that registration.
        ON_SCOPE_EXIT { ReleaseTemporaryUsbCode(loc.program_id); };

        /* Get the program path. */
        char path[fs::EntryNameLengthMax];
        RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderCreateProcessAttempt, loc);
        const Result path_result = GetProgramPath(path, sizeof(path), loc, attrs);
        RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderCreateProcessPathResult, loc, path_result);
        R_TRY(path_result);
        path[sizeof(path) - 1] = '\x00';

        /* Create the process. */
        const Result created=ldr::CreateProcess(out, pin_id, loc, override_status, path, g_argument_store.Get(loc.program_id), flags, resource_limit, attrs);
        if (R_SUCCEEDED(created)) ForgetExternalProgram(loc.program_id);
        R_RETURN(created);
    }

    Result LoaderService::GetProgramInfo(ProgramInfo *out, cfg::OverrideStatus *out_status, const ncm::ProgramLocation &loc, const ProgramAttributes &attrs) {
        if (!UseFrontend(loc)) R_RETURN(backend::GetProgramInfo(out,out_status,loc,attrs));
        /* Check that the program attributes are valid. */
        R_UNLESS(IsValidProgramAttributes(attrs), ldr::ResultInvalidProgramAttributes());

        /* The diagnostics and USB resolver perform nested service calls. Copy
         * the request out of loader's IPC message before any of those calls
         * can reuse the thread-local message buffer. */
        const ncm::ProgramLocation requested_location = loc;

        char path[fs::EntryNameLengthMax];
        cfg::OverrideStatus status;

        auto load_program_info = [&](const ncm::ProgramLocation &candidate_location) -> Result {
            // PM may query metadata again after launch, without a subsequent
            // CreateProcess. Close this lookup's archive after its mounts unwind.
            ON_SCOPE_EXIT { ReleaseTemporaryUsbCode(candidate_location.program_id); };
            std::memset(out, 0, sizeof(*out));
            std::memset(path, 0, sizeof(path));

            RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderGetProgramInfoAttempt, candidate_location);
            const Result path_result = GetProgramPath(path, sizeof(path), candidate_location, attrs);
            RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderGetProgramInfoPathResult, candidate_location, path_result);
            R_TRY(path_result);
            path[sizeof(path) - 1] = '\x00';

            R_RETURN(ldr::GetProgramInfo(out, std::addressof(status), candidate_location, path, attrs));
        };

        ncm::ProgramLocation effective_location = requested_location;
        Result program_info_result = load_program_info(effective_location);
        bool used_usb_alias_fallback = false;

        if (R_FAILED(program_info_result)) {
            /* The stock request has completed and released its FS call before
             * this IPC is made, avoiding an FS -> ams:usb -> Loader cycle. An
             * exact alias exists only after a validated USB Control NCA was
             * opened for the current media generation. */
            RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderAliasFallbackAttempt, requested_location, program_info_result);

            ncm::ProgramLocation resolved_location = requested_location;
            const Result alias_result = TryResolveUsbProgramAlias(std::addressof(resolved_location), requested_location);
            RecordUsbLoaderTraceForDiagnostics(usb::content::LoaderProbeStage::LoaderAliasFallbackResult,
                                               R_SUCCEEDED(alias_result) ? resolved_location : requested_location,
                                               alias_result);
            if (R_FAILED(alias_result)) {
                R_RETURN(program_info_result);
            }

            effective_location = resolved_location;
            used_usb_alias_fallback = true;
            program_info_result = load_program_info(effective_location);
        }
        R_TRY(program_info_result);

        if (!used_usb_alias_fallback && requested_location.program_id != out->program_id) {
            /* Redirect the program path. */
            const ncm::ProgramLocation new_loc = ncm::ProgramLocation::Make(out->program_id, static_cast<ncm::StorageId>(requested_location.storage_id));
            R_TRY(RedirectProgramPath(path, sizeof(path), new_loc));
        }

        /* Update arguments when NPDM or the USB alias recovered a different
         * real ProgramId for the process that PM will pin. */
        if (requested_location.program_id != out->program_id) {
            if (const auto *entry = g_argument_store.Get(requested_location.program_id); entry != nullptr) {
                R_TRY(g_argument_store.Set(out->program_id, entry->argument, entry->argument_size));
            }
        }

        /* If we should, set the output status. */
        if (out_status != nullptr) {
            *out_status = status;
        }

        R_SUCCEED();
    }

    Result LoaderService::PinProgram(PinId *out, const ncm::ProgramLocation &loc, const cfg::OverrideStatus &status) {
        *out = {};
        R_UNLESS(g_pins.HasRoom(),ldr::ResultMaxProcess());
        const bool local=UseFrontend(loc);
        const auto program=loc.program_id.value;
        const auto storage=loc.storage_id;
        PinId inner{};
        R_TRY(local ? ldr::PinProgram(&inner,loc,status) : backend::PinProgram(&inner,loc,status));
        const auto external=g_pins.Add(inner.value,local?nexus_routing::Owner::Frontend:nexus_routing::Owner::Backend,program,storage);
        if (!external) {
            R_DISCARD(local?ldr::UnpinProgram(inner):backend::UnpinProgram(inner));
            R_THROW(ldr::ResultMaxProcess());
        }
        *out={external}; R_SUCCEED();
    }

    Result LoaderService::UnpinProgram(PinId id) {
        const auto *route=g_pins.Find(id.value);
        R_UNLESS(route != nullptr,ldr::ResultNotPinned());
        const Result result=route->owner==nexus_routing::Owner::Frontend
            ? ldr::UnpinProgram(PinId{route->internal}) : backend::UnpinProgram(PinId{route->internal});
        if (R_SUCCEEDED(result)) g_pins.Remove(id.value);
        R_RETURN(result);
    }

    Result LoaderService::SetProgramArgument(ncm::ProgramId program_id, const void *argument, size_t size) {
        R_TRY(g_argument_store.Set(program_id,argument,size));
        const auto *entry=g_argument_store.Get(program_id);
        R_UNLESS(entry != nullptr,ldr::ResultArgumentOverflow());
        // Use the stable copied arguments, never the incoming TLS pointer.
        R_RETURN(backend::SetProgramArgument(program_id,entry->argument,entry->argument_size));
    }

    Result LoaderService::FlushArguments() {
        R_TRY(backend::FlushArguments());
        R_RETURN(g_argument_store.Flush());
    }

    Result LoaderService::GetProcessModuleInfo(u32 *out_count, ModuleInfo *out, size_t max_out_count, os::ProcessId process_id) {
        *out_count = 0;
        std::memset(out, 0, max_out_count * sizeof(*out));
        const Result local=ldr::GetProcessModuleInfo(out_count,out,max_out_count,process_id);
        if (!ldr::ResultNotPinned::Includes(local)) R_RETURN(local);
        R_RETURN(backend::GetProcessModuleInfo(out_count,out,max_out_count,process_id));
    }

    Result LoaderService::RegisterExternalCode(os::NativeHandle *out, ncm::ProgramId program_id) {
        if (ncm::IsSystemProgramId(program_id)) R_RETURN(backend::RegisterExternalCode(out,program_id));
        if (program_id == ncm::SystemAppletId::PhotoViewer) {
            *out = os::InvalidNativeHandle;
            R_UNLESS(!g_backend_album_registered && !HasExternalProgram(program_id), ncm::ResultInvalidOperation());
            const Result result = backend::RegisterExternalCode(out, program_id);
            if (R_SUCCEEDED(result)) {
                SetBackendAlbumRegistration(true);
                R_SUCCEED();
            }
            // Alternate loaders without external-code support keep the existing
            // local registration path. Propagate real registration failures.
            if (!sf::cmif::ResultUnknownCommandId::Includes(result) && !sf::ResultNotSupported::Includes(result)) {
                R_RETURN(result);
            }
        }
        auto *slot=static_cast<ncm::ProgramId *>(nullptr);
        for (auto &entry:g_external_programs) if (entry==program_id) slot=&entry;
        if (!slot) for (auto &entry:g_external_programs) if (entry==ncm::InvalidProgramId) { slot=&entry; break; }
        R_UNLESS(slot != nullptr,ldr::ResultMaxProcess());
        R_TRY(fssystem::CreateExternalCode(out,program_id)); *slot=program_id; R_SUCCEED();
    }

    void LoaderService::UnregisterExternalCode(ncm::ProgramId program_id) {
        if (ncm::IsSystemProgramId(program_id)) { R_DISCARD(backend::UnregisterExternalCode(program_id)); return; }
        if (program_id == ncm::SystemAppletId::PhotoViewer && g_backend_album_registered) {
            if (R_SUCCEEDED(backend::UnregisterExternalCode(program_id))) {
                SetBackendAlbumRegistration(false);
            }
            return;
        }
        ForgetExternalProgram(program_id);
        fssystem::DestroyExternalCode(program_id);
        ClearUsbExternalCodeVerification(program_id);
    }

    void LoaderService::HasLaunchedBootProgram(bool *out, ncm::ProgramId program_id) {
        *out = ldr::HasLaunchedBootProgram(program_id);
        if (!*out) R_DISCARD(backend::HasLaunchedBootProgram(out,program_id));
    }

    Result LoaderService::SetEnabledProgramVerification(bool enabled) {
        R_TRY(backend::SetEnabledProgramVerification(enabled));
        ldr::SetEnabledProgramVerification(enabled);
        R_SUCCEED();
    }

}
