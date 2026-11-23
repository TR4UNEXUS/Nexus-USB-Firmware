/* Copyright (c) Atmosphere-NX contributors. SPDX-License-Identifier:
 * GPL-2.0-only */
#include "ncm_usb_limits.hpp"
#include <stratosphere.hpp>
#include <stratosphere/ncm/ncm_usb_map_backup.hpp>

namespace ams::fs::impl {
Result ReadSaveDataFileSystemExtraData(SaveDataExtraData *, SaveDataSpaceId, SaveDataId);
}
namespace ams::ncm {
namespace {
constexpr char Mount[] = "#nx-usb-backup", Marker[] = "#nx-usb-backup:/owner.bin";
constexpr fs::SystemSaveDataId SaveId{0x80584E5553420001ULL};
constexpr u64 OwnerId = 0x0100000000000002ULL; // NCM
constexpr char Owner[] = "Nexus USB routing archive v1";
constexpr char Archives[2][32] = {"#nx-usb-backup:/maps-a.bin", "#nx-usb-backup:/maps-b.bin"};
constexpr char Paths[3][96] = {"#ncm-usb-map:/atmosphere/usb_content/title_map.bin",
                               "#ncm-usb-map:/atmosphere/usb_content/title_content_map.bin",
                               "#ncm-usb-map:/atmosphere/usb_content/content_locations.bin"};
constexpr char Pending[] = "#ncm-usb-map:/atmosphere/usb_content-repair.pending";
constexpr u32 Magic = 0x3442584E;
struct Header {
    u32 magic, version, header_size, mask;
    u64 sequence, sizes[3];
    u8 hashes[3][32], header_hash[32];
};
static_assert(sizeof(Header) == 176);
constinit os::SdkMutex mutex;
u32 last_result{};
UsbRoutingBackupDiagnostic diagnostic{0x3344584e, 1, 0, 0, 0xffffffff, 0, 0, 0, 0};
void Step(u32 stage, u32 index=0xffffffff, u64 expected=0, u64 actual=0) {
    diagnostic.stage=stage; diagnostic.index=index;
    diagnostic.expected=expected; diagnostic.actual=actual;
}
std::atomic<u64> requested{1}; // First boot imports the existing SD maps.

struct SaveMount {
    bool mounted{};
    ~SaveMount() {
        if (mounted)
            fs::Unmount(Mount);
    }
    Result Open(bool create) {
        bool created = false;
        auto rc = fs::MountSystemSaveData(Mount, fs::SaveDataSpaceId::System, SaveId);
        if (fs::ResultTargetNotFound::Includes(rc) && create) {
            R_TRY(fs::CreateSystemSaveData(fs::SaveDataSpaceId::System, SaveId, OwnerId, 16_MB, 16_MB, 0));
            created = true;
            rc = fs::MountSystemSaveData(Mount, fs::SaveDataSpaceId::System, SaveId);
        }
        R_TRY(rc);
        mounted = true;
        // Power loss can occur between creation and the first marker commit.
        // Recover only our empty, correctly owned save; never adopt/format data.
        fs::DirectoryEntryType marker_type{};
        const auto marker_result = fs::GetEntryType(&marker_type, Marker);
        if (!created && create && fs::ResultPathNotFound::Includes(marker_result)) {
            FsSaveDataInfoReader reader{};
            R_TRY(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_System));
            ON_SCOPE_EXIT { fsSaveDataInfoReaderClose(&reader); };
            bool owned = false;
            for (;;) {
                FsSaveDataInfo info{};
                s64 count{};
                R_TRY(fsSaveDataInfoReaderRead(&reader, &info, 1, &count));
                if (!count)
                    break;
                if (info.system_save_data_id != SaveId)
                    continue;
                fs::SaveDataExtraData extra{};
                R_TRY(fs::impl::ReadSaveDataFileSystemExtraData(&extra, fs::SaveDataSpaceId::System,
                                                                fs::SaveDataId{info.save_data_id}));
                R_UNLESS(extra.owner_id == OwnerId && extra.available_size == 16_MB && extra.journal_size == 16_MB,
                         ncm::ResultInvalidOperation());
                owned = true;
                break;
            }
            R_UNLESS(owned, ncm::ResultInvalidOperation());
            fs::DirectoryHandle dir{};
            R_TRY(fs::OpenDirectory(&dir, "#nx-usb-backup:/", fs::OpenDirectoryMode_All));
            s64 count{};
            const auto result = fs::GetDirectoryEntryCount(&count, dir);
            fs::CloseDirectory(dir);
            R_TRY(result);
            R_UNLESS(count == 0, ncm::ResultInvalidOperation());
            created = true;
        }
        if (created) {
            R_TRY(fs::CreateFile(Marker, sizeof(Owner)));
            fs::FileHandle f{};
            R_TRY(fs::OpenFile(&f, Marker, fs::OpenMode_Write));
            const auto result = fs::WriteFile(f, 0, Owner, sizeof(Owner), fs::WriteOption::Flush);
            fs::CloseFile(f);
            R_TRY(result);
            R_TRY(fs::CommitSaveData(Mount));
        }
        // Refuse an unrelated/corrupt save with the same ID. Never format it.
        fs::FileHandle f{};
        R_TRY(fs::OpenFile(&f, Marker, fs::OpenMode_Read));
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        char marker[sizeof(Owner)]{};
        s64 bytes{};
        size_t got{};
        R_TRY(fs::GetFileSize(&bytes, f));
        R_TRY(fs::ReadFile(&got, f, 0, marker, sizeof(marker)));
        R_UNLESS(bytes == sizeof(Owner) && got == sizeof(Owner) && !std::memcmp(marker, Owner, sizeof(Owner)),
                 ncm::ResultInvalidOperation());
        R_SUCCEED();
    }
};
Result HashRange(fs::FileHandle f, u64 offset, u64 size, u8 *out) {
    u8 b[1024];
    crypto::Sha256Generator h;
    h.Initialize();
    for (u64 done = 0; done < size;) {
        const size_t n = std::min<u64>(sizeof(b), size - done);
        size_t got{};
        R_TRY(fs::ReadFile(&got, f, offset + done, b, n));
        R_UNLESS(got == n, ncm::ResultInvalidOperation());
        h.Update(b, n);
        done += n;
    }
    h.GetHash(out, 32);
    R_SUCCEED();
}
u64 Total(const Header &h) { return sizeof(h) + h.sizes[0] + h.sizes[1] + h.sizes[2]; }
void Seal(Header &h) { crypto::GenerateSha256(h.header_hash, sizeof(h.header_hash), &h, offsetof(Header, header_hash)); }
bool Valid(const Header &h) {
    if (h.magic != Magic || h.version != 1 || h.header_size != sizeof(h) || h.mask != 7 || !h.sequence)
        return false;
    for (unsigned i = 0; i < 3; ++i)
        if (h.sizes[i] < 16 || h.sizes[i] > 16 + u64(i == 2 ? usb_limits::Content : usb_limits::Titles) * (i == 1 ? 56 : 16))
            return false;
    auto copy = h;
    Seal(copy);
    return !std::memcmp(copy.header_hash, h.header_hash, 32);
}
Result ReadHeader(unsigned slot, Header *h, bool verify) {
    fs::FileHandle f{};
    R_TRY(fs::OpenFile(&f, Archives[slot], fs::OpenMode_Read));
    ON_SCOPE_EXIT { fs::CloseFile(f); };
    size_t got{};
    s64 bytes{};
    R_TRY(fs::GetFileSize(&bytes, f));
    R_TRY(fs::ReadFile(&got, f, 0, h, sizeof(*h)));
    R_UNLESS(got == sizeof(*h) && Valid(*h) && bytes >= 0 && u64(bytes) == Total(*h), ncm::ResultInvalidOperation());
    if (verify) {
        u64 offset = sizeof(*h);
        for (unsigned i = 0; i < 3; ++i) {
            u8 digest[32];
            R_TRY(HashRange(f, offset, h->sizes[i], digest));
            R_UNLESS(!std::memcmp(digest, h->hashes[i], 32), ncm::ResultInvalidOperation());
            offset += h->sizes[i];
        }
    }
    R_SUCCEED();
}
Result Latest(Header *out, unsigned *slot, bool verify) {
    Header a{}, b{};
    const auto ar = ReadHeader(0, &a, verify), br = ReadHeader(1, &b, verify);
    // A read or device error is not evidence that an archive is corrupt.
    // Preserve it and return the original error instead of overwriting it.
    for (const auto rc : {ar,br})
        R_UNLESS(R_SUCCEEDED(rc) || fs::ResultPathNotFound::Includes(rc) ||
                 ncm::ResultInvalidOperation::Includes(rc), rc);
    if (R_FAILED(ar) && R_FAILED(br)) {
        if (fs::ResultPathNotFound::Includes(ar) && fs::ResultPathNotFound::Includes(br))
            R_RETURN(fs::ResultPathNotFound());
        R_RETURN(ncm::ResultInvalidOperation());
    }
    *slot = R_SUCCEEDED(br) && (R_FAILED(ar) || b.sequence > a.sequence) ? 1 : 0;
    *out = *slot ? b : a;
    R_SUCCEED();
}
Result NoPending() {
    Step(1);
    for (const auto *path : {Pending, "#ncm-usb-map:/atmosphere/usb-import.pending"}) {
        fs::FileHandle f{};
        const auto r = fs::OpenFile(&f, path, fs::OpenMode_Read);
        if (R_SUCCEEDED(r)) {
            fs::CloseFile(f);
            R_RETURN(ncm::ResultInvalidOperation());
        }
        R_UNLESS(fs::ResultPathNotFound::Includes(r), r);
    }
    R_SUCCEED();
}
Result Snapshot(Header *h) {
    R_TRY(NoPending());
    *h = {Magic, 1, sizeof(Header), 7, 1, {}, {}, {}};
    for (unsigned i = 0; i < 3; ++i) {
        fs::FileHandle f{};
        Step(2,i);
        R_TRY(fs::OpenFile(&f, Paths[i], fs::OpenMode_Read));
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        s64 size{};
        R_TRY(fs::GetFileSize(&size, f));
        Step(3,i,16,size>=0?u64(size):UINT64_MAX);
        R_UNLESS(size >= 16, ncm::ResultInvalidOperation());
        h->sizes[i] = size;
        u32 fields[4]{};
        size_t got{};
        R_TRY(fs::ReadFile(&got, f, 0, fields, sizeof(fields)));
        constexpr u32 magics[]{0x54425355, 0x43425355, 0x44425355};
        const u32 stride = i == 1 ? (fields[1] == 1 ? 40 : 56) : 16;
        Step(3,i,16+u64(fields[2])*stride,u64(size));
        R_UNLESS(
            got == sizeof(fields) && fields[0] == magics[i] && !fields[3] && (fields[1] == 1 || (i == 1 && fields[1] == 2)) &&
                fields[2] <= (i == 2 ? usb_limits::Content : usb_limits::Titles) && u64(size) == 16 + u64(fields[2]) * stride,
            ncm::ResultInvalidOperation());
        Step(4,i,u64(size));
        R_TRY(HashRange(f, 0, size, h->hashes[i]));
    }
    Seal(*h);
    R_UNLESS(Valid(*h), ncm::ResultInvalidOperation());
    R_RETURN(NoPending());
}
Result Checkpoint() {
    Header next{};
    R_TRY(Snapshot(&next));
    SaveMount mount;
    Step(5);
    R_TRY(mount.Open(true));
    Header prior{};
    unsigned slot{};
    Step(6);
    const auto existing = Latest(&prior, &slot, true);
    if (R_SUCCEEDED(existing)) {
        if (!std::memcmp(prior.sizes, next.sizes, sizeof(next.sizes)) &&
            !std::memcmp(prior.hashes, next.hashes, sizeof(next.hashes)))
            R_SUCCEED();
        R_UNLESS(prior.sequence != UINT64_MAX, ncm::ResultInvalidOperation());
        next.sequence = prior.sequence + 1;
        slot ^= 1;
    } else {
        if (!fs::ResultPathNotFound::Includes(existing)) {
            // An interrupted/corrupt archive must not permanently block new
            // backups. Only validated current maps and our owned save reach
            // here. Preserve rejected bytes and never swallow a read/I/O error.
            Header a{},b{};
            const auto ar=ReadHeader(0,&a,true), br=ReadHeader(1,&b,true);
            const auto rejected=[](Result r) { return fs::ResultPathNotFound::Includes(r) || ncm::ResultInvalidOperation::Includes(r); };
            R_UNLESS(rejected(ar) && rejected(br), existing);
            Step(7);
            if (!fs::ResultPathNotFound::Includes(ar)) {
                // Rename is inside the same uncommitted save transaction.
                // If a retained rejected copy exists, refuse to overwrite it.
                R_TRY(fs::RenameFile(Archives[0],"#nx-usb-backup:/maps-a.rejected.bin"));
            }
        }
        slot = 0;
    }
    Seal(next);
    Step(8,slot,Total(next));
    const auto removed = fs::DeleteFile(Archives[slot]);
    R_UNLESS(R_SUCCEEDED(removed) || fs::ResultPathNotFound::Includes(removed), removed);
    R_TRY(fs::CreateFile(Archives[slot], Total(next)));
    {
        fs::FileHandle output{};
        R_TRY(fs::OpenFile(&output, Archives[slot], fs::OpenMode_Write));
        bool output_flushed = false;
        ON_SCOPE_EXIT {
            // An input/read/validation error can leave successful buffered
            // writes on this handle. CloseFile would abort NCM in that state.
            // Flush the handle before closing, but never commit a failed save
            // transaction. Unmount still discards the uncommitted revision.
            if (!output_flushed)
                AMS_UNUSED(fs::FlushFile(output));
            fs::CloseFile(output);
        };
        R_TRY(fs::WriteFile(output, 0, &next, sizeof(next), fs::WriteOption::None));
        u64 offset = sizeof(next);
        u8 buffer[1024];
        for (unsigned i = 0; i < 3; ++i) {
            fs::FileHandle input{};
            R_TRY(fs::OpenFile(&input, Paths[i], fs::OpenMode_Read));
            ON_SCOPE_EXIT { fs::CloseFile(input); };
            for (u64 done = 0; done < next.sizes[i];) {
                size_t got{};
                const auto n = std::min<u64>(sizeof(buffer), next.sizes[i] - done);
                R_TRY(fs::ReadFile(&got, input, done, buffer, n));
                R_UNLESS(got == n, ncm::ResultInvalidOperation());
                R_TRY(fs::WriteFile(output, offset + done, buffer, n, fs::WriteOption::None));
                done += n;
            }
            offset += next.sizes[i];
        }
        R_TRY(fs::FlushFile(output));
        output_flushed = true;
    }
    Header check{}, current{};
    Step(9,slot);
    R_TRY(ReadHeader(slot, &check, true));
    R_TRY(Snapshot(&current));
    Step(10);
    R_UNLESS(!std::memcmp(next.sizes, current.sizes, sizeof(next.sizes)) &&
                 !std::memcmp(next.hashes, current.hashes, sizeof(next.hashes)),
             ncm::ResultInvalidOperation());
    Step(11);
    R_TRY(fs::CommitSaveData(Mount));
    Step(12,slot);
    // Closing on every outcome discards uncommitted changes after any failure.
    R_RETURN(ReadHeader(slot, &check, true));
}
} // namespace
Result CheckpointUsbRoutingBackup() {
    std::scoped_lock lock(mutex);
    ++diagnostic.attempts;
    const auto r = Checkpoint();
    last_result = r.GetValue();
    diagnostic.result = last_result;
    if (R_SUCCEEDED(r)) Step(0);
    R_RETURN(r);
}
Result QueryUsbRoutingBackupDiagnostic(UsbRoutingBackupDiagnostic *out) {
    R_UNLESS(out, fs::ResultInvalidArgument());
    std::scoped_lock lock(mutex); *out=diagnostic; R_SUCCEED();
}
void RecordUsbRoutingBackupPreflightFailure(u32 stage, Result result) {
    std::scoped_lock lock(mutex);
    ++diagnostic.attempts; Step(stage);
    diagnostic.result=last_result=result.GetValue();
}
Result QueryUsbRoutingBackup(UsbRoutingBackupStatus *out) {
    R_UNLESS(out, fs::ResultInvalidArgument());
    std::scoped_lock lock(mutex);
    *out = {Magic, 1, 0, last_result, 0, 0};
    SaveMount mount;
    const auto rc = mount.Open(false);
    if (fs::ResultTargetNotFound::Includes(rc))
        R_SUCCEED();
    R_TRY(rc);
    Header h{};
    unsigned slot{};
    const auto r = Latest(&h, &slot, true);
    if (fs::ResultPathNotFound::Includes(r))
        R_SUCCEED();
    R_TRY(r);
    out->flags = 1;
    out->sequence = h.sequence;
    out->bytes = Total(h);
    R_SUCCEED();
}
Result ReadUsbRoutingBackup(void *out, size_t size, u64 sequence, u64 offset) {
    R_UNLESS((out || !size) && size <= 0x10000 && sequence, fs::ResultInvalidArgument());
    std::scoped_lock lock(mutex);
    SaveMount mount;
    R_TRY(mount.Open(false));
    Header h{};
    unsigned slot{};
    // Query may have selected the older valid revision after a damaged latest
    // archive. Bind reads to that exact revision, never select "latest" again.
    bool found = false;
    for (unsigned candidate = 0; candidate < 2; ++candidate) {
        Header current{};
        if (R_SUCCEEDED(ReadHeader(candidate, &current, false)) && current.sequence == sequence) {
            h = current;
            slot = candidate;
            found = true;
            break;
        }
    }
    R_UNLESS(found, ncm::ResultInvalidOperation());
    R_UNLESS(h.sequence == sequence && offset <= Total(h) && size <= Total(h) - offset, ncm::ResultInvalidOperation());
    fs::FileHandle f{};
    R_TRY(fs::OpenFile(&f, Archives[slot], fs::OpenMode_Read));
    ON_SCOPE_EXIT { fs::CloseFile(f); };
    size_t got{};
    R_TRY(fs::ReadFile(&got, f, offset, out, size));
    R_UNLESS(got == size, ncm::ResultInvalidOperation());
    R_SUCCEED();
}
void RequestUsbRoutingBackup() { requested.fetch_add(1, std::memory_order_relaxed); }
void RunUsbRoutingBackupWorker() {
    u64 completed{}, previous{};
    unsigned quiet{}, periodic{};
    for (;;) {
        os::SleepThread(TimeSpan::FromSeconds(2));
        const auto revision = requested.load(std::memory_order_relaxed);
        if (revision != previous) {
            previous = revision;
            quiet = 0;
            continue;
        }
        if (++quiet < 2)
            continue;
        // The periodic pass catches checked edits made by Manager or the USB
        // alias publisher in another process. Unchanged snapshots do not write
        // NAND. No USB service calls or per-game files enter this worker.
        if (revision == completed && ++periodic < 15)
            continue;
        const auto result = CreateUsbRoutingBackup();
        if (R_FAILED(result)) {
            std::scoped_lock lock(mutex);
            last_result = result.GetValue();
        }
        if (R_SUCCEEDED(result))
            completed = revision;
        periodic = 0;
        quiet = 0;
        if (R_FAILED(result))
            os::SleepThread(TimeSpan::FromSeconds(24));
    }
}
} // namespace ams::ncm
