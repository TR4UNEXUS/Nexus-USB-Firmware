/*
 * Copyright (c) AtmosphÃ¨re-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */
#include "ncm_usb_limits.hpp"
#include "ncm_usb_location_store.hpp"
#include <stratosphere.hpp>
#include <stratosphere/ncm/ncm_usb_map_backup.hpp>
#include <stratosphere/ncm/ncm_usb_map_write_lease.hpp>
#include <stratosphere/ncm/ncm_usb_title_map.hpp>

namespace ams::ncm {

namespace {

constexpr const char TitleMapMountName[] = "#ncm-usb-map";
constexpr const char TitleMapDirectory[] = "#ncm-usb-map:/atmosphere/usb_content";
constexpr const char TitleMapPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_map.bin";
constexpr const char TitleMapTempPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_map.bin.tmp";
constexpr const char TitleMapBackupPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_map.bin.bak";
constexpr const char TitleContentMapPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_content_map.bin";
constexpr const char TitleContentMapTempPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_content_map.bin.tmp";
constexpr const char TitleContentMapBackupPath[] = "#ncm-usb-map:/atmosphere/usb_content/title_content_map.bin.bak";
constexpr const char RepairPendingPath[] = "#ncm-usb-map:/atmosphere/usb_content-repair.pending";

constexpr u32 TitleMapMagic = 0x54425355; /* "USBT" */
constexpr u32 TitleMapVersion = 1;
constexpr u32 TitleMapMaxEntries = usb_limits::Titles;
constexpr u32 TitleContentMapMagic = 0x43425355; /* "USBC" */
constexpr u32 TitleContentMapVersion = 2;
constexpr u32 TitleContentMapMaxEntries = usb_limits::Titles;

struct TitleMapHeader {
    u32 magic;
    u32 version;
    u32 count;
    u32 reserved;
};

struct TitleMapEntry {
    u64 program_id;
    u64 alias_program_id;
};

struct TitleContentMapHeader {
    u32 magic;
    u32 version;
    u32 count;
    u32 reserved;
};

struct LegacyTitleContentMapEntry {
    u64 program_id;
    ContentId control_content_id;
    ContentId program_content_id;
};

struct TitleContentMapEntry {
    u64 program_id;
    ContentId meta_content_id;
    ContentId control_content_id;
    ContentId program_content_id;
};

static_assert(sizeof(TitleMapHeader) == 0x10);
static_assert(sizeof(TitleMapEntry) == 0x10);
static_assert(sizeof(TitleContentMapHeader) == 0x10);
static_assert(sizeof(LegacyTitleContentMapEntry) == 0x28);
static_assert(sizeof(TitleContentMapEntry) == 0x38);

constinit os::SdkMutex g_title_map_mutex;
constinit os::SdkMutex g_program_id_cache_mutex;
constinit bool g_title_map_storage_mounted;

/* All callers hold g_title_map_mutex. Keep the map scratch space off
 * NCM's 16 KiB IPC stack. Scratch snapshots are freed after each call. */
TitleMapEntry *g_title_map_scratch = nullptr;
TitleContentMapEntry *g_title_content_map_scratch = nullptr;

/* Launch-time GetProgramId must not enter fsp-srv or ams:usb. Keep its
 * reverse map in RAM, but do not reserve another fixed 18 KiB in NCM's
 * KIP: R87-R89 all failed the hardware boot test after that static
 * growth. These buffers are allocated only when the post-mount status
 * handoff primes them, or when an install commits a durable map. */
TitleMapEntry *g_program_id_title_cache = nullptr;
TitleContentMapEntry *g_program_id_content_cache = nullptr;
u32 g_program_id_title_cache_count = 0;
u32 g_program_id_content_cache_count = 0;
bool g_program_id_title_cache_valid = false;
bool g_program_id_content_cache_valid = false;
u64 g_program_id_cache_media_generation = 0;

UsbLocationStore *g_locations{};
u32 g_title_scratch_capacity{}, g_content_scratch_capacity{};
u32 g_title_cache_capacity{}, g_content_cache_capacity{};
template <class T> bool ReserveMap(T *&p, u32 &capacity, u32 needed) {
    if (needed <= capacity)
        return true;
    const u32 target = std::min<u32>(TitleMapMaxEntries, std::max<u32>(64, needed));
    if (needed > target)
        return false;
    auto *next = new (std::nothrow) T[target]{};
    if (!next)
        return false;
    delete[] p;
    p = next;
    capacity = target;
    return true;
}

Result EnsureTitleMapStorageMounted() {
    R_SUCCEED_IF(g_title_map_storage_mounted);
    R_TRY(fs::MountSdCard(TitleMapMountName));
    g_title_map_storage_mounted = true;
    R_SUCCEED();
}

Result CheckNoPendingRepair() {
    R_TRY(EnsureTitleMapStorageMounted());
    fs::FileHandle file;
    const Result result = fs::OpenFile(std::addressof(file), RepairPendingPath, fs::OpenMode_Read);
    if (R_SUCCEEDED(result)) {
        fs::CloseFile(file);
        R_RETURN(ncm::ResultInvalidOperation());
    }
    R_UNLESS(fs::ResultPathNotFound::Includes(result), result);
    R_SUCCEED();
}

Result OpenMapForRead(fs::FileHandle *out, const char *path, const char *backup) {
    const Result result = fs::OpenFile(out, path, fs::OpenMode_Read);
    if (fs::ResultPathNotFound::Includes(result)) {
        R_RETURN(fs::OpenFile(out, backup, fs::OpenMode_Read));
    }
    R_RETURN(result);
}

Result VerifyMapBytes(const char *path, const void *header, size_t header_size, const void *entries, size_t entries_size) {
    fs::FileHandle file;
    R_TRY(fs::OpenFile(std::addressof(file), path, fs::OpenMode_Read));
    ON_SCOPE_EXIT { fs::CloseFile(file); };
    s64 size = 0;
    R_TRY(fs::GetFileSize(std::addressof(size), file));
    R_UNLESS(size == static_cast<s64>(header_size + entries_size), ncm::ResultInvalidOperation());
    u8 buffer[256];
    size_t offset = 0;
    for (u32 part = 0; part < 2; ++part) {
        const auto *data = static_cast<const u8 *>(part == 0 ? header : entries);
        const size_t length = part == 0 ? header_size : entries_size;
        for (size_t done = 0; done < length;) {
            const size_t count = std::min(sizeof(buffer), length - done);
            size_t read = 0;
            R_TRY(fs::ReadFile(std::addressof(read), file, offset, buffer, count));
            R_UNLESS(read == count && std::memcmp(buffer, data + done, count) == 0, ncm::ResultInvalidOperation());
            done += count;
            offset += count;
        }
    }
    R_SUCCEED();
}

Result PublishMap(const char *path, const char *temp, const char *backup, const void *header, size_t header_size,
                  const void *entries, size_t entries_size) {
    R_TRY(CheckNoPendingRepair());
    R_TRY(fs::EnsureDirectory(TitleMapDirectory));
    const Result remove_temp = fs::DeleteFile(temp);
    R_UNLESS(R_SUCCEEDED(remove_temp) || fs::ResultPathNotFound::Includes(remove_temp), remove_temp);
    R_TRY(fs::CreateFile(temp, header_size + entries_size));
    {
        fs::FileHandle file;
        R_TRY(fs::OpenFile(std::addressof(file), temp, fs::OpenMode_Write));
        ON_SCOPE_EXIT { fs::CloseFile(file); };
        R_TRY(fs::WriteFile(file, 0, header, header_size, fs::WriteOption::Flush));
        if (entries_size != 0) {
            R_TRY(fs::WriteFile(file, header_size, entries, entries_size, fs::WriteOption::Flush));
        }
    }
    R_TRY(VerifyMapBytes(temp, header, header_size, entries, entries_size));
    R_TRY(CheckNoPendingRepair());
    fs::FileHandle previous;
    const Result open_result = fs::OpenFile(std::addressof(previous), path, fs::OpenMode_Read);
    bool moved_previous = false;
    if (R_SUCCEEDED(open_result)) {
        fs::CloseFile(previous);
        const Result remove_backup = fs::DeleteFile(backup);
        R_UNLESS(R_SUCCEEDED(remove_backup) || fs::ResultPathNotFound::Includes(remove_backup), remove_backup);
        R_TRY(fs::RenameFile(path, backup));
        moved_previous = true;
    } else {
        R_UNLESS(fs::ResultPathNotFound::Includes(open_result), open_result);
    }
    const Result publish = fs::RenameFile(temp, path);
    if (R_FAILED(publish)) {
        if (moved_previous) {
            AMS_UNUSED(fs::RenameFile(backup, path));
        }
        R_RETURN(publish);
    }
    // A failed readback is not success. Preserve both files for recovery.
    R_RETURN(VerifyMapBytes(path, header, header_size, entries, entries_size));
}

// Transfers an already validated scratch snapshot into the RAM-only
// lookup cache. No third full-size copy is allocated during growth.
void UpdateProgramIdTitleCache(const TitleMapEntry *entries, u32 count) {
    AMS_ASSERT(entries == g_title_map_scratch);
    AMS_UNUSED(entries);
    std::scoped_lock lock(g_program_id_cache_mutex);
    std::swap(g_program_id_title_cache, g_title_map_scratch);
    std::swap(g_title_cache_capacity, g_title_scratch_capacity);
    g_program_id_title_cache_count = count;
    g_program_id_title_cache_valid = true;
}
void UpdateProgramIdContentCache(const TitleContentMapEntry *entries, u32 count) {
    AMS_ASSERT(entries == g_title_content_map_scratch);
    AMS_UNUSED(entries);
    std::scoped_lock lock(g_program_id_cache_mutex);
    std::swap(g_program_id_content_cache, g_title_content_map_scratch);
    std::swap(g_content_cache_capacity, g_content_scratch_capacity);
    g_program_id_content_cache_count = count;
    g_program_id_content_cache_valid = true;
}
struct ScratchScope {
    ~ScratchScope() {
        delete[] g_title_map_scratch;
        g_title_map_scratch = nullptr;
        g_title_scratch_capacity = 0;
        delete[] g_title_content_map_scratch;
        g_title_content_map_scratch = nullptr;
        g_content_scratch_capacity = 0;
    }
};

bool ReadTitleMap(TitleMapEntry *&out_entries, u32 *out_count, bool allow_missing = false) {
    if (!out_entries && !ReserveMap(out_entries, g_title_scratch_capacity, 64))
        return false;
    if (!out_entries) {
        *out_count = 0;
        return false;
    }
    AMS_ASSERT(out_entries != nullptr);
    AMS_ASSERT(out_count != nullptr);

    *out_count = 0;

    if (R_FAILED(EnsureTitleMapStorageMounted())) {
        return false;
    }

    fs::FileHandle file;
    const Result open_result = OpenMapForRead(std::addressof(file), TitleMapPath, TitleMapBackupPath);
    if (R_FAILED(open_result)) {
        return allow_missing && fs::ResultPathNotFound::Includes(open_result);
    }
    ON_SCOPE_EXIT { fs::CloseFile(file); };

    s64 file_size = 0;
    if (R_FAILED(fs::GetFileSize(std::addressof(file_size), file))) {
        return false;
    }

    TitleMapHeader header{};
    size_t read_size = 0;
    if (file_size < static_cast<s64>(sizeof(header)) ||
        R_FAILED(fs::ReadFile(std::addressof(read_size), file, 0, std::addressof(header), sizeof(header))) ||
        read_size != sizeof(header) || header.magic != TitleMapMagic || header.version != TitleMapVersion ||
        header.reserved != 0 || header.count > TitleMapMaxEntries) {
        return false;
    }

    const s64 expected_size = static_cast<s64>(sizeof(header)) + static_cast<s64>(header.count) * sizeof(TitleMapEntry);
    if (file_size != expected_size) {
        return false;
    }

    if (!ReserveMap(out_entries, g_title_scratch_capacity, std::min<u32>(TitleMapMaxEntries, header.count + 1)))
        return false;
    if (header.count > 0) {
        read_size = 0;
        if (R_FAILED(fs::ReadFile(std::addressof(read_size), file, sizeof(header), out_entries,
                                  static_cast<size_t>(header.count) * sizeof(TitleMapEntry))) ||
            read_size != static_cast<size_t>(header.count) * sizeof(TitleMapEntry)) {
            return false;
        }
    }

    for (u32 i = 0; i < header.count; ++i) {
        if (out_entries[i].program_id == 0) {
            return false;
        }
        for (u32 j = 0; j < i; ++j) {
            if (out_entries[i].program_id == out_entries[j].program_id) {
                return false;
            }
        }
    }
    *out_count = header.count;
    return true;
}

Result WriteTitleMap(const TitleMapEntry *entries, u32 count) {
    AMS_ASSERT(entries != nullptr || count == 0);
    R_UNLESS(count <= TitleMapMaxEntries, ncm::ResultBufferInsufficient());

    const TitleMapHeader header{TitleMapMagic, TitleMapVersion, count, 0};
    // Preserve last-known-good routing on failure: invalidating it would
    // send unrelated cached IDs into Host GetProgramId's USB fallback.
    // An idempotent retry refreshes RAM from the validated disk snapshot.
    R_TRY(PublishMap(TitleMapPath, TitleMapTempPath, TitleMapBackupPath, std::addressof(header), sizeof(header), entries,
                     static_cast<size_t>(count) * sizeof(TitleMapEntry)));
    UpdateProgramIdTitleCache(entries, count);
    RequestUsbRoutingBackup();
    R_SUCCEED();
}

bool ReadTitleContentMap(TitleContentMapEntry *&out_entries, u32 *out_count, bool allow_missing = false) {
    if (!out_entries && !ReserveMap(out_entries, g_content_scratch_capacity, 64))
        return false;
    if (!out_entries) {
        *out_count = 0;
        return false;
    }
    AMS_ASSERT(out_entries != nullptr);
    AMS_ASSERT(out_count != nullptr);

    *out_count = 0;

    if (R_FAILED(EnsureTitleMapStorageMounted())) {
        return false;
    }

    fs::FileHandle file;
    const Result open_result = OpenMapForRead(std::addressof(file), TitleContentMapPath, TitleContentMapBackupPath);
    if (R_FAILED(open_result)) {
        return allow_missing && fs::ResultPathNotFound::Includes(open_result);
    }
    ON_SCOPE_EXIT { fs::CloseFile(file); };

    s64 file_size = 0;
    if (R_FAILED(fs::GetFileSize(std::addressof(file_size), file))) {
        return false;
    }

    TitleContentMapHeader header{};
    size_t read_size = 0;
    if (file_size < static_cast<s64>(sizeof(header)) ||
        R_FAILED(fs::ReadFile(std::addressof(read_size), file, 0, std::addressof(header), sizeof(header))) ||
        read_size != sizeof(header) || header.magic != TitleContentMapMagic ||
        (header.version != 1 && header.version != TitleContentMapVersion) || header.reserved != 0 ||
        header.count > TitleContentMapMaxEntries) {
        return false;
    }

    const size_t entry_size = header.version == 1 ? sizeof(LegacyTitleContentMapEntry) : sizeof(TitleContentMapEntry);
    const s64 expected_size = static_cast<s64>(sizeof(header)) + static_cast<s64>(header.count) * entry_size;
    if (file_size != expected_size) {
        return false;
    }

    if (!ReserveMap(out_entries, g_content_scratch_capacity, std::min<u32>(TitleContentMapMaxEntries, header.count + 1)))
        return false;
    if (header.count > 0) {
        read_size = 0;
        if (header.version == 1) {
            for (u32 i = 0; i < header.count; ++i) {
                LegacyTitleContentMapEntry legacy{};
                if (R_FAILED(fs::ReadFile(std::addressof(read_size), file, sizeof(header) + i * sizeof(legacy),
                                          std::addressof(legacy), sizeof(legacy))) ||
                    read_size != sizeof(legacy)) {
                    return false;
                }
                out_entries[i] = {
                    .program_id = legacy.program_id,
                    .meta_content_id = InvalidContentId,
                    .control_content_id = legacy.control_content_id,
                    .program_content_id = legacy.program_content_id,
                };
            }
        } else {
            const size_t entries_size = static_cast<size_t>(header.count) * sizeof(TitleContentMapEntry);
            if (R_FAILED(fs::ReadFile(std::addressof(read_size), file, sizeof(header), out_entries, entries_size)) ||
                read_size != entries_size) {
                return false;
            }
        }
    }

    for (u32 i = 0; i < header.count; ++i) {
        if (out_entries[i].program_id == 0) {
            return false;
        }
        for (u32 j = 0; j < i; ++j) {
            if (out_entries[i].program_id == out_entries[j].program_id) {
                return false;
            }
        }
    }
    *out_count = header.count;
    return true;
}

Result PrimeProgramIdCache() {
    u32 titles{}, contents{};
    R_UNLESS(ReadTitleMap(g_title_map_scratch, &titles) && ReadTitleContentMap(g_title_content_map_scratch, &contents),
             ncm::ResultContentNotFound());
    // Both disk snapshots are complete before replacing either cache.
    std::scoped_lock lock(g_program_id_cache_mutex);
    std::swap(g_program_id_title_cache, g_title_map_scratch);
    std::swap(g_title_cache_capacity, g_title_scratch_capacity);
    std::swap(g_program_id_content_cache, g_title_content_map_scratch);
    std::swap(g_content_cache_capacity, g_content_scratch_capacity);
    g_program_id_title_cache_count = titles;
    g_program_id_content_cache_count = contents;
    g_program_id_title_cache_valid = true;
    g_program_id_content_cache_valid = true;
    R_SUCCEED();
}

Result WriteTitleContentMap(const TitleContentMapEntry *entries, u32 count) {
    AMS_ASSERT(entries != nullptr || count == 0);
    R_UNLESS(count <= TitleContentMapMaxEntries, ncm::ResultBufferInsufficient());

    const size_t entries_size = static_cast<size_t>(count) * sizeof(TitleContentMapEntry);
    const TitleContentMapHeader header{TitleContentMapMagic, TitleContentMapVersion, count, 0};
    // A failed publication does not validate new IDs or revoke the
    // prior routing snapshot. Only successful revalidation replaces it.
    R_TRY(PublishMap(TitleContentMapPath, TitleContentMapTempPath, TitleContentMapBackupPath, std::addressof(header),
                     sizeof(header), entries, entries_size));
    UpdateProgramIdContentCache(entries, count);
    RequestUsbRoutingBackup();
    R_SUCCEED();
}

} // namespace

namespace {

Result MigrateLegacyLocations() {
    if (!g_locations->live_io.Missing())
        R_SUCCEED();
    u32 rows{};
    R_UNLESS(ReadTitleContentMap(g_title_content_map_scratch, &rows, true), ncm::ResultInvalidOperation());
    if (!rows)
        R_SUCCEED();
    const auto key = [&](u32 i) {
        const auto &row = g_title_content_map_scratch[i / 3];
        return i % 3 == 0 ? row.meta_content_id : i % 3 == 1 ? row.control_content_id : row.program_content_id;
    };
    const auto unique = [&](u32 i) {
        const auto id = key(i);
        if (id == InvalidContentId)
            return false;
        for (u32 j = 0; j < i; ++j)
            if (key(j) == id)
                return false;
        return true;
    };
    u32 count{};
    for (u32 i = 0; i < rows * 3; ++i)
        if (unique(i))
            ++count;
    auto &io = g_locations->live_io;
    R_TRY(CheckNoPendingRepair());
    R_UNLESS(io.Create(1, count), io.Error());
    ContentId page[64]{};
    u32 n{}, done{};
    for (u32 i = 0; i < rows * 3; ++i)
        if (unique(i)) {
            page[n++] = key(i);
            if (n == 64) {
                R_UNLESS(io.Write(1, done, page, n), io.Error());
                done += n;
                n = 0;
            }
        }
    if (n)
        R_UNLESS(io.Write(1, done, page, n), io.Error());
    R_UNLESS(io.Publish(1, count), io.Error());
    R_RETURN(g_locations->Load(true));
}
Result LoadContentLocations() {
    R_TRY(EnsureTitleMapStorageMounted());
    if (!g_locations)
        g_locations = new (std::nothrow) UsbLocationStore;
    R_UNLESS(g_locations, fs::ResultAllocationMemoryFailed());
    R_TRY(g_locations->Load());
    R_RETURN(MigrateLegacyLocations());
}
} // namespace
Result EnsureUsbContentLocationsLoaded() {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_RETURN(LoadContentLocations());
}

Result RefreshUsbProgramIdCache(u64 media_generation) {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    {
        std::scoped_lock cache_lk(g_program_id_cache_mutex);
        R_SUCCEED_IF(g_program_id_title_cache_valid && g_program_id_content_cache_valid &&
                     g_program_id_cache_media_generation == media_generation);
    }

    R_TRY(PrimeProgramIdCache());
    // Registration evidence belongs to this connected medium only.
    if (g_locations && g_program_id_cache_media_generation && g_program_id_cache_media_generation != media_generation)
        R_TRY(g_locations->NewMedia());
    {
        std::scoped_lock cache_lk(g_program_id_cache_mutex);
        g_program_id_cache_media_generation = media_generation;
    }
    R_SUCCEED();
}

Result CreateUsbRoutingBackup() {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());
    UsbMapWriteLease lease;
    R_TRY(lease.Open(TitleMapMountName));
    u32 titles{}, contents{};
    R_UNLESS(ReadTitleMap(g_title_map_scratch, &titles) && ReadTitleContentMap(g_title_content_map_scratch, &contents),
             ncm::ResultInvalidOperation());
    R_TRY(LoadContentLocations());
    R_TRY(g_locations->Load(true));
    // A partial multi-file publication must never displace the last valid
    // archive. Validate complete base-title routes before snapshotting.
    for (u32 i = 0; i < titles; ++i) {
        const auto title = g_title_map_scratch[i].program_id;
        bool matched = false;
        for (u32 j = 0; j < contents; ++j)
            if (g_title_content_map_scratch[j].program_id == title) {
                const auto &entry = g_title_content_map_scratch[j];
                R_UNLESS(entry.meta_content_id != InvalidContentId && entry.control_content_id != InvalidContentId &&
                             entry.program_content_id != InvalidContentId,
                         ncm::ResultInvalidOperation());
                for (const auto &id : {entry.meta_content_id, entry.control_content_id, entry.program_content_id})
                    R_UNLESS(g_locations->Has(id), ncm::ResultInvalidOperation());
                matched = true;
                break;
            }
        R_UNLESS(matched, ncm::ResultInvalidOperation());
    }
    R_RETURN(CheckpointUsbRoutingBackup());
}

Result RegisterUsbContentLocation(ContentId content_id) {
    R_UNLESS(content_id != InvalidContentId, ncm::ResultInvalidOperation());
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());
    R_TRY(LoadContentLocations());
    const auto result = g_locations->Change(content_id, true);
    if (R_SUCCEEDED(result))
        RequestUsbRoutingBackup();
    R_RETURN(result);
}
Result UnregisterUsbContentLocation(ContentId content_id) {
    R_UNLESS(content_id != InvalidContentId, ncm::ResultInvalidOperation());
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());
    R_TRY(LoadContentLocations());
    const auto result = g_locations->Change(content_id, false);
    if (R_SUCCEEDED(result))
        RequestUsbRoutingBackup();
    R_RETURN(result);
}
bool IsUsbContentLocationRegistered(ContentId content_id) {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    return R_SUCCEEDED(LoadContentLocations()) && g_locations->Has(content_id);
}
bool WasUsbContentRegisteredThisBoot(ContentId content_id) {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    return R_SUCCEEDED(LoadContentLocations()) && g_locations->Has(content_id, true);
}
Result ListUsbContentLocations(ContentId *out, size_t capacity, size_t offset, size_t *out_count) {
    R_UNLESS(out_count && (out || !capacity), ncm::ResultInvalidOperation());
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(LoadContentLocations());
    R_RETURN(g_locations->List(out, capacity, offset, out_count));
}

bool IsUsbTitleMapped(ProgramId program_id) {
    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;

    auto &entries = g_title_map_scratch;
    u32 count = 0;
    if (!ReadTitleMap(entries, std::addressof(count))) {
        return false;
    }

    for (u32 i = 0; i < count; i++) {
        if (entries[i].program_id == program_id.value) {
            return true;
        }
    }

    return false;
}

Result ResolveUsbTitleAlias(ProgramId *out_program_id, ProgramId alias_program_id) {
    R_UNLESS(out_program_id != nullptr && alias_program_id.value != 0, ncm::ResultInvalidOperation());
    *out_program_id = {};
    /* Only an opaque, non-system ID may be looked up as an alias. The
     * second u64 is written only after a validated USB Control NCA has
     * identified the canonical application, so this is an exact durable
     * association rather than the old single-title fallback. */
    R_UNLESS(!IsApplicationId(alias_program_id) && !IsSystemProgramId(alias_program_id), ncm::ResultContentNotFound());

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    auto &entries = g_title_map_scratch;
    u32 count = 0;
    R_UNLESS(ReadTitleMap(entries, std::addressof(count)), fs::ResultPathNotFound());

    for (u32 i = 0; i < count; ++i) {
        const auto &entry = entries[i];
        const ProgramId program_id{entry.program_id};
        if (entry.alias_program_id == alias_program_id.value && IsApplicationId(program_id) && !IsSystemProgramId(program_id) &&
            program_id != alias_program_id) {
            *out_program_id = program_id;
            R_SUCCEED();
        }
    }

    R_RETURN(ncm::ResultContentNotFound());
}

Result ListUsbTitles(ProgramId *out_program_ids, size_t max_program_ids, u32 *out_count) {
    R_UNLESS(out_count != nullptr, fs::ResultInvalidArgument());
    R_UNLESS(out_program_ids != nullptr || max_program_ids == 0, fs::ResultInvalidArgument());

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;

    auto &entries = g_title_map_scratch;
    u32 count = 0;
    R_UNLESS(ReadTitleMap(entries, std::addressof(count)), fs::ResultPathNotFound());

    const u32 written = std::min<u32>(count, static_cast<u32>(std::min<size_t>(max_program_ids, TitleMapMaxEntries)));
    for (u32 i = 0; i < written; i++) {
        out_program_ids[i] = ProgramId{entries[i].program_id};
    }
    *out_count = written;
    R_SUCCEED();
}

Result RegisterUsbTitle(ProgramId program_id) {
    R_UNLESS(program_id.value != 0, fs::ResultInvalidArgument());

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());
    UsbMapWriteLease lease;
    R_TRY(lease.Open(TitleMapMountName));

    auto &entries = g_title_map_scratch;
    u32 count = 0;
    R_UNLESS(ReadTitleMap(entries, std::addressof(count), true), ncm::ResultInvalidOperation());

    for (u32 i = 0; i < count; i++) {
        if (entries[i].program_id == program_id.value) {
            // An idempotent retry or external repair can leave disk newer
            // than RAM. Publish the already validated snapshot as well.
            UpdateProgramIdTitleCache(entries, count);
            R_SUCCEED();
        }
    }

    R_UNLESS(count < TitleMapMaxEntries, ncm::ResultBufferInsufficient());
    entries[count++] = {.program_id = program_id.value, .alias_program_id = 0};
    R_RETURN(WriteTitleMap(entries, count));
}

Result RegisterUsbTitleContent(ProgramId program_id, const ContentId &meta_content_id, const ContentId &control_content_id,
                               const ContentId &program_content_id) {
    R_UNLESS(program_id.value != 0, fs::ResultInvalidArgument());
    R_UNLESS(meta_content_id != InvalidContentId && control_content_id != InvalidContentId &&
                 program_content_id != InvalidContentId,
             fs::ResultInvalidArgument());

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());

    auto &entries = g_title_content_map_scratch;
    u32 count = 0;
    R_UNLESS(ReadTitleContentMap(entries, std::addressof(count), true), ncm::ResultInvalidOperation());

    for (u32 i = 0; i < count; i++) {
        if (entries[i].program_id == program_id.value) {
            if (entries[i].meta_content_id == meta_content_id && entries[i].control_content_id == control_content_id &&
                entries[i].program_content_id == program_content_id) {
                UpdateProgramIdContentCache(entries, count);
                R_SUCCEED();
            }

            entries[i].meta_content_id = meta_content_id;
            entries[i].control_content_id = control_content_id;
            entries[i].program_content_id = program_content_id;
            R_RETURN(WriteTitleContentMap(entries, count));
        }
    }

    R_UNLESS(count < TitleContentMapMaxEntries, ncm::ResultBufferInsufficient());
    entries[count++] = {
        .program_id = program_id.value,
        .meta_content_id = meta_content_id,
        .control_content_id = control_content_id,
        .program_content_id = program_content_id,
    };
    R_RETURN(WriteTitleContentMap(entries, count));
}

Result GetUsbTitleContentIds(ProgramId program_id, ContentId *out_meta_content_id, ContentId *out_control_content_id,
                             ContentId *out_program_content_id) {
    R_UNLESS(program_id.value != 0 && out_meta_content_id != nullptr && out_control_content_id != nullptr &&
                 out_program_content_id != nullptr,
             fs::ResultInvalidArgument());
    *out_meta_content_id = InvalidContentId;
    *out_control_content_id = InvalidContentId;
    *out_program_content_id = InvalidContentId;

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;

    auto &entries = g_title_content_map_scratch;
    u32 count = 0;
    R_UNLESS(ReadTitleContentMap(entries, std::addressof(count)), ncm::ResultContentNotFound());

    for (u32 i = 0; i < count; i++) {
        if (entries[i].program_id == program_id.value) {
            *out_meta_content_id = entries[i].meta_content_id;
            *out_control_content_id = entries[i].control_content_id;
            *out_program_content_id = entries[i].program_content_id;
            R_SUCCEED();
        }
    }

    R_RETURN(ncm::ResultContentNotFound());
}

Result ResolveUsbContentProgramId(ProgramId *out_program_id, const ContentId &content_id) {
    R_UNLESS(out_program_id != nullptr && content_id != InvalidContentId, fs::ResultInvalidArgument());
    *out_program_id = {};

    std::scoped_lock lk(g_program_id_cache_mutex);
    R_UNLESS(g_program_id_title_cache_valid && g_program_id_content_cache_valid, ncm::ResultContentNotFound());

    ProgramId resolved_program_id{};
    bool found = false;
    for (u32 i = 0; i < g_program_id_content_cache_count; ++i) {
        const auto &content_entry = g_program_id_content_cache[i];
        if (content_entry.meta_content_id != content_id && content_entry.control_content_id != content_id &&
            content_entry.program_content_id != content_id) {
            continue;
        }

        const TitleMapEntry *title_entry = nullptr;
        for (u32 j = 0; j < g_program_id_title_cache_count; ++j) {
            if (g_program_id_title_cache[j].program_id == content_entry.program_id) {
                title_entry = g_program_id_title_cache + j;
                break;
            }
        }
        if (title_entry == nullptr) {
            continue;
        }

        /* A validated Control NCA may have persisted its opaque header
         * ProgramId as an alias. Otherwise the canonical application id
         * is also the NCA ProgramId for an ordinary installed title. */
        const ProgramId candidate{title_entry->alias_program_id != 0 ? title_entry->alias_program_id : title_entry->program_id};
        R_UNLESS(candidate.value != 0, ncm::ResultContentNotFound());
        R_UNLESS(!found || candidate == resolved_program_id, ncm::ResultContentNotFound());
        resolved_program_id = candidate;
        found = true;
    }

    R_UNLESS(found, ncm::ResultContentNotFound());
    *out_program_id = resolved_program_id;
    R_SUCCEED();
}

Result UnregisterUsbTitle(ProgramId program_id) {
    R_UNLESS(program_id.value != 0, fs::ResultInvalidArgument());

    std::scoped_lock lk(g_title_map_mutex);
    ScratchScope scratch_scope;
    R_TRY(CheckNoPendingRepair());
    UsbMapWriteLease lease;
    R_TRY(lease.Open(TitleMapMountName));

    {
        auto &entries = g_title_map_scratch;
        u32 count = 0;
        R_UNLESS(ReadTitleMap(entries, std::addressof(count), true), ncm::ResultInvalidOperation());
        {
            u32 write_count = 0;
            bool changed = false;
            for (u32 i = 0; i < count; i++) {
                auto entry = entries[i];
                if (entry.program_id == program_id.value) {
                    /* This title is now stock-backed; remove its USB row. */
                    changed = true;
                    continue;
                }
                if (entry.alias_program_id == program_id.value) {
                    /* A real installed SD title owns this opaque ID now.
                     * Clear only the stale alias while retaining the
                     * canonical USB application's own mapping. */
                    entry.alias_program_id = 0;
                    changed = true;
                }
                entries[write_count++] = entry;
            }
            if (changed) {
                R_TRY(WriteTitleMap(entries, write_count));
            } else {
                UpdateProgramIdTitleCache(entries, count);
            }
        }
    }

    {
        auto &entries = g_title_content_map_scratch;
        u32 count = 0;
        R_UNLESS(ReadTitleContentMap(entries, std::addressof(count), true), ncm::ResultInvalidOperation());
        {
            u32 write_count = 0;
            for (u32 i = 0; i < count; i++) {
                if (entries[i].program_id != program_id.value) {
                    entries[write_count++] = entries[i];
                }
            }
            if (write_count != count) {
                R_TRY(WriteTitleContentMap(entries, write_count));
            } else {
                UpdateProgramIdContentCache(entries, count);
            }
        }
    }

    R_SUCCEED();
}

} // namespace ams::ncm
