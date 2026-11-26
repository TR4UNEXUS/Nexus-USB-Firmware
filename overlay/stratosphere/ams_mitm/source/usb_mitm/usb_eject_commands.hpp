// SPDX-License-Identifier: GPL-2.0-or-later
// Included in usbmitm_module.cpp's private namespace. The existing monitor is
// the only caller. Never run PM/NS/Loader IPC from the storage worker.
constexpr const char* EjectCommandPath="/logs/usb_eject_command.bin";
constexpr const char* EjectStatusPath="/logs/usb_eject_status.txt";
constexpr const char* EjectTempPath="/logs/usb_eject_status.txt.tmp";
u64 g_eject_boot{},g_eject_ack{};
u32 g_eject_ack_result{};

bool UsbLaunchInUse(usb::eject::Attempt &attempt) {
    // A failed Loader unbind used to hold this lease until another USB game
    // launched. Retire it only if PM confirms the owner has exited. Never
    // hold the binding lock across IPC or clear a replacement in the same slot.
    for (auto &entry : g_process_contents) {
        u64 pin{}, pid{};
        {
            std::scoped_lock lock(g_process_content_mutex);
            if (!entry.pin_id || !entry.snapshot) continue;
            pin = entry.pin_id;
            pid = entry.process_id;
        }
        u64 program{};
        if (!pid) {
            attempt.check = "loader_binding_has_no_pid";
            attempt.pin = pin;
            return true;
        }
        const auto rc = ::pminfoGetProgramId(&program, pid);
        if (rc != MAKERESULT(15,1)) {
            attempt.check = rc ? "loader_binding_process_query_failed" : "loader_binding_process_alive";
            attempt.pid = pid;
            attempt.pin = pin;
            attempt.title = program;
            attempt.query_result = rc;
            return true;
        }
        std::scoped_lock lock(g_process_content_mutex);
        if (entry.pin_id == pin && entry.process_id == pid) entry = {};
        else if (entry.pin_id) {
            attempt.check = "loader_binding_replaced_during_query";
            attempt.pin = entry.pin_id;
            attempt.pid = entry.process_id;
            return true;
        }
    }
    // USB-backed homebrew now holds a storage lease from Arm through exit.
    // An idle hbloader PID watch has no USB content attached.
    return false;
}

bool IsHomeNxMenuProcess(u64 pid, usb::eject::Attempt &attempt) {
    // Album can host an installer as well as HOME. A folder or a log on the
    // SD card alone is insufficient: match the actual process's main module.
    FsFile file{};
    auto fail = [&](const char *reason, u32 rc = 0) {
        attempt.check = reason;
        attempt.pid = pid;
        attempt.query_result = rc;
        return false;
    };
    const auto opened = mitm::fs::OpenSdFile(&file, "/switch/SwitchU/bin/menu/main", ams::fs::OpenMode_Read);
    if (R_FAILED(opened)) return fail("album_menu_file_open_failed", opened.GetValue());
    u8 header[0x60]{};
    u64 size{};
    const auto rc = fsFileRead(&file, 0, header, sizeof(header), FsReadOption_None, &size);
    fsFileClose(&file);
    if (R_FAILED(rc)) return fail("album_menu_header_read_failed", rc);
    if (size != sizeof(header) || std::memcmp(header, "NSO0", 4) != 0) return fail("album_menu_header_invalid");
    const auto *id = header + 0x40;
    const u8 empty[0x20]{};
    if (std::memcmp(id, empty, sizeof(empty)) == 0) return fail("album_menu_build_id_empty");
    const auto init = ::ldrDmntInitialize();
    if (R_FAILED(init)) return fail("album_loader_connection_failed", init);
    ON_SCOPE_EXIT { ::ldrDmntExit(); };
    LoaderModuleInfo modules[0x20]{};
    s32 count{};
    const auto result = ::ldrDmntGetProcessModuleInfo(pid, modules, util::size(modules), &count);
    if (R_FAILED(result)) return fail("album_module_query_failed", result);
    if (count <= 0 || static_cast<size_t>(count) > util::size(modules)) return fail("album_module_count_invalid");
    for (s32 i = 0; i < count; ++i) {
        if (std::memcmp(modules[i].build_id, id, sizeof(empty)) == 0) return true;
    }
    return fail("album_process_does_not_match_cardsnx");
}

bool ApplicationOrAlbumInUse(usb::eject::Attempt &attempt) {
    // Existing pm:dmnt client, queried only for a user eject request and with
    // no storage/FS/launch lock held. Unknown/failed queries refuse removal.
    u64 pid{};
    const auto app=::pmdmntGetApplicationProcessId(&pid);
    if (app != MAKERESULT(15,1)) {
        attempt.check = app ? "application_process_query_failed" : "application_still_open";
        attempt.query_result = app;
        attempt.pid = pid;
        return true;
    }
    const auto album=::pmdmntGetProcessId(&pid,ncm::SystemAppletId::PhotoViewer.value);
    if (album == MAKERESULT(15,1)) return false;
    if (R_FAILED(album)) {
        attempt.check = "album_process_query_failed";
        attempt.query_result = album;
        return true;
    }
    return !IsHomeNxMenuProcess(pid, attempt);
}

// Only the monitor accesses these buffers. Capture under the gate lock, then
// format and flush the physical SD file after releasing every USB lock.
char g_eject_report[8192]{};
size_t g_eject_report_size{};
bool g_eject_report_denied{};

void FormatEjectAttempt(const usb::eject::Attempt &attempt, const nexus_eject::Request &request, u64 now) {
    g_eject_report_size = 0;
    const auto &status = attempt.status;
    auto append = [&](const char *format, auto... args) {
        const auto space = sizeof(g_eject_report) - g_eject_report_size;
        const int written = util::SNPrintf(g_eject_report + g_eject_report_size, space, format, args...);
        if (written < 0 || static_cast<size_t>(written) >= space) return false;
        g_eject_report_size += static_cast<size_t>(written);
        return true;
    };
    g_eject_report_denied = g_eject_ack_result != 0;
    auto wide = [](u64 value) { return static_cast<unsigned long long>(value); };
    if (!append("NX1 safe eject admission v1\nboot=%llu request=%llu uptime_ms=%llu requested_generation=%llu\n"
                "decision=%s check=%s result=0x%08X query_result=0x%08X pid=%llu title=%016llX pin=%llu\n"
                "observed_generation=%llu phase_before=%u stage_before=%u handles=%u shown=%u omitted=%u\n",
                wide(g_eject_boot), wide(request.nonce), wide(now), wide(request.generation),
                g_eject_report_denied ? "refused" : "accepted", attempt.check, g_eject_ack_result,
                attempt.query_result, wide(attempt.pid), wide(attempt.title), wide(attempt.pin), wide(status.generation),
                static_cast<u32>(status.phase), static_cast<u32>(status.stage), status.handles,
                static_cast<u32>(attempt.shown), status.handles - static_cast<u32>(attempt.shown))) return;
    for (size_t i = 0; i < attempt.shown; ++i) {
        const auto &o = attempt.owners[i];
        if (!append("handle[%u] owner=%s pid=%llu title=%016llX opened_ms=%llu path=%s\n",
                    static_cast<u32>(i + 1), o.kind, wide(o.pid), wide(o.title), wide(o.opened_ms), o.path[0] ? o.path : "-")) {
            g_eject_report_size = 0;
            return;
        }
    }
}

void ApplyEjectCommand() {
    const u64 now=os::GetSystemTick().ToTimeSpan().GetMilliSeconds();
    if(!g_eject_boot) {
        // A request left by a previous boot must never eject today's drive.
        static_cast<void>(mitm::fs::DeleteAtmosphereSdFile(EjectCommandPath));
        g_eject_boot=static_cast<u64>(::armGetSystemTick());
        if(!g_eject_boot) g_eject_boot=1;
        return;
    }
    FsFile file{};
    if(R_FAILED(mitm::fs::OpenAtmosphereSdFile(&file,EjectCommandPath,ams::fs::OpenMode_Read))) return;
    nexus_eject::Request request{};u64 read{};s64 size{};
    const Result size_rc=fsFileGetSize(&file,&size);
    const Result rc=fsFileRead(&file,0,&request,sizeof(request),FsReadOption_None,&read);
    fsFileClose(&file);
    const auto deleted=mitm::fs::DeleteAtmosphereSdFile(EjectCommandPath);
    if(R_FAILED(deleted) || R_FAILED(size_rc) || R_FAILED(rc) || size!=sizeof(request) || read!=sizeof(request)) return;
    if(!nexus_eject::Valid(request,g_eject_boot,now) || request.nonce==g_eject_ack) return;
    g_eject_ack=request.nonce;
    if(request.action==nexus_eject::Resume) {
        const auto snapshot=g_storage_service.GetEjectStatus();
        g_eject_ack_result=request.generation==snapshot.generation
            ? g_storage_service.ResumeAfterEject().GetValue() : usb::ResultMediaChanged().GetValue();
        return;
    }
    // The panel also refuses running application/Album processes. Backend
    // ownership checks independently protect USB launch/virtual-SD lifetimes.
    usb::eject::Attempt attempt{};
    if (UsbLaunchInUse(attempt) || ApplicationOrAlbumInUse(attempt)) {
        g_storage_service.SnapshotEjectOwners(attempt);
        g_eject_ack_result = usb::ResultResourceBusy().GetValue();
    } else {
        g_eject_ack_result = g_storage_service.RequestEject(request.nonce, request.generation, attempt).GetValue();
    }
    FormatEjectAttempt(attempt, request, now);
}

Result PublishEjectStatus() {
    if(!g_eject_boot) R_THROW(usb::ResultNotInitialized());
    if (g_eject_report_size) {
        const char *path = g_eject_report_denied ? "/logs/usb_eject_last_blocked.txt" : "/logs/usb_eject_last_accepted.txt";
        if (R_SUCCEEDED(PublishStatusSnapshot(g_eject_report, g_eject_report_size, path, "/logs/usb_eject_attempt.tmp"))) {
            g_eject_report_size = 0;
        }
    }
    const auto e=g_storage_service.GetEjectStatus();
    usb::storage::Status s{};R_TRY(g_storage_service.GetStatus(&s));
    const u64 now=os::GetSystemTick().ToTimeSpan().GetMilliSeconds();
    char text[512]{};
    const int n=util::SNPrintf(text,sizeof(text),"NXEJ1 %llu %llu %llu %llu %llu %u %u %u %u %u %u\n",
        static_cast<unsigned long long>(g_eject_boot),static_cast<unsigned long long>(now),
        static_cast<unsigned long long>(e.generation),static_cast<unsigned long long>(e.request),
        static_cast<unsigned long long>(g_eject_ack),g_eject_ack_result,static_cast<u32>(e.phase),
        static_cast<u32>(e.stage),e.result,e.handles,s.state==usb::storage::State::Ready?1u:0u);
    R_UNLESS(n>0 && static_cast<size_t>(n)<sizeof(text),usb::ResultInvalidParameter());
    static_cast<void>(mitm::fs::DeleteAtmosphereSdFile(EjectTempPath));
    R_TRY(mitm::fs::CreateAtmosphereSdFile(EjectTempPath,0,ams::fs::CreateOption_None));
    {
        FsFile f{};R_TRY(mitm::fs::OpenAtmosphereSdFile(&f,EjectTempPath,ams::fs::OpenMode_ReadWrite));
        ON_SCOPE_EXIT {fsFileClose(&f);};
        R_TRY(fsFileSetSize(&f,n));R_TRY(fsFileWrite(&f,0,text,n,FsWriteOption_Flush));
    }
    char from[ams::fs::EntryNameLengthMax+1]{},to[ams::fs::EntryNameLengthMax+1]{};
    util::SNPrintf(from,sizeof(from),"%s:/atmosphere%s",ams::fs::impl::SdCardFileSystemMountName,EjectTempPath);
    util::SNPrintf(to,sizeof(to),"%s:/atmosphere%s",ams::fs::impl::SdCardFileSystemMountName,EjectStatusPath);
    static_cast<void>(ams::fs::DeleteFile(to));
    R_TRY(ams::fs::RenameFile(from,to));
    static u64 last_announced_request{};
    const nexus_eject::Snapshot snapshot{g_eject_boot,now,e.generation,e.request,g_eject_ack,
        g_eject_ack_result,static_cast<u32>(e.phase),static_cast<u32>(e.stage),e.result,e.handles,
        s.state==usb::storage::State::Ready ? 1u : 0u};
    if (e.request != last_announced_request && nexus_eject::Safe(snapshot, g_eject_boot, e.request)) {
        if (WriteUltrahandNotification("USB safely ejected", "You can unplug the drive now.", "ams-usb-safe"))
            last_announced_request = e.request;
    }
    R_SUCCEED();
}
