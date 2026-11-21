/* Copyright (c) Atmosphere-NX contributors. SPDX-License-Identifier:
 * GPL-2.0-only */
#pragma once
#include "ncm_usb_limits.hpp"
#include "ncm_usb_paged_set.hpp"
#include <stratosphere.hpp>

namespace ams::ncm {
class UsbLocationIo {
    struct Header {
        u32 magic, version, count, reserved;
    };
    static constexpr u32 Magic = 0x44425355;
    char primary[128]{}, backup[136]{}, paths[3][160]{};
    ContentId cache[64]{};
    u32 cached_begin{}, cached_count{};
    unsigned cached_file{};
    bool ephemeral{}, empty{};
    Result error{};
    bool Rc(Result r) {
        if (R_FAILED(r)) {
            error = r;
            return false;
        }
        return true;
    }
    bool Remove(const char *p) {
        const auto r = fs::DeleteFile(p);
        return fs::ResultPathNotFound::Includes(r) || Rc(r);
    }
    [[maybe_unused]] bool Same(const char *a, const char *b) {
        fs::FileHandle x{}, y{};
        if (!Rc(fs::OpenFile(&x, a, fs::OpenMode_Read)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(x); };
        if (!Rc(fs::OpenFile(&y, b, fs::OpenMode_Read)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(y); };
        s64 xs{}, ys{};
        if (!Rc(fs::GetFileSize(&xs, x)) || !Rc(fs::GetFileSize(&ys, y)))
            return false;
        if (xs != ys)
            return Invalid();
        u8 xb[1024], yb[1024];
        for (s64 off = 0; off < xs; off += sizeof(xb)) {
            const auto n = std::min<s64>(sizeof(xb), xs - off);
            size_t xn{}, yn{};
            if (!Rc(fs::ReadFile(&xn, x, off, xb, n)) || !Rc(fs::ReadFile(&yn, y, off, yb, n)))
                return false;
            if (xn != size_t(n) || yn != size_t(n) || std::memcmp(xb, yb, n))
                return Invalid();
        }
        return true;
    }

  public:
    UsbLocationIo(const char *path, bool temporary = false) : ephemeral(temporary) {
        util::SNPrintf(primary, sizeof(primary), "%s", path);
        util::SNPrintf(backup, sizeof(backup), "%s.bak", path);
        util::SNPrintf(paths[1], sizeof(paths[1]), "%s.sort1", path);
        util::SNPrintf(paths[2], sizeof(paths[2]), "%s.sort2", path);
    }
    Result Error() const { return R_FAILED(error) ? error : ncm::ResultInvalidOperation(); }
    bool Invalid() {
        error = ncm::ResultInvalidOperation();
        return false;
    }
    bool Full() {
        error = ncm::ResultBufferInsufficient();
        return false;
    }
    bool Missing() const { return empty; }
    bool ResetBoot() { return Remove(primary) && Remove(backup); }
    bool Begin(u32 &count, u32 limit) {
        error = ResultSuccess();
        cached_count = 0;
        count = 0;
        empty = false;
        util::SNPrintf(paths[0], sizeof(paths[0]), "%s", primary);
        fs::FileHandle f{};
        auto r = fs::OpenFile(&f, paths[0], fs::OpenMode_Read);
        if (fs::ResultPathNotFound::Includes(r)) {
            util::SNPrintf(paths[0], sizeof(paths[0]), "%s", backup);
            r = fs::OpenFile(&f, paths[0], fs::OpenMode_Read);
        }
        if (fs::ResultPathNotFound::Includes(r)) {
            empty = true;
            return true;
        }
        if (!Rc(r))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        Header h{};
        s64 bytes{};
        size_t got{};
        if (!Rc(fs::GetFileSize(&bytes, f)) || !Rc(fs::ReadFile(&got, f, 0, &h, sizeof(h))))
            return false;
        if (got != sizeof(h) || h.magic != Magic || h.version != 1 || h.reserved || h.count > limit ||
            bytes != s64(sizeof(h)) + s64(h.count) * s64(sizeof(ContentId)))
            return Invalid();
        count = h.count;
        return true;
    }
    bool Read(unsigned slot, u32 index, ContentId *out, u32 n) {
        if (!n)
            return true;
        if (slot > 2 || (!out && n) || (slot == 0 && empty))
            return Invalid();
        if (slot == cached_file && index >= cached_begin && index - cached_begin <= cached_count &&
            n <= cached_count - (index - cached_begin)) {
            std::memcpy(out, cache + index - cached_begin, n * sizeof(ContentId));
            return true;
        }
        fs::FileHandle f{};
        if (!Rc(fs::OpenFile(&f, paths[slot], fs::OpenMode_Read)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        size_t got{};
        const size_t bytes = size_t(n) * sizeof(ContentId);
        if (!Rc(fs::ReadFile(&got, f, sizeof(Header) + u64(index) * sizeof(ContentId), out, bytes)))
            return false;
        if (got != bytes)
            return Invalid();
        if (n <= 64) {
            std::memcpy(cache, out, bytes);
            cached_begin = index;
            cached_count = n;
            cached_file = slot;
        }
        return true;
    }
    bool Create(unsigned slot, u32 count) {
        cached_count = 0;
        if (slot < 1 || slot > 2)
            return Invalid();
        if (!Remove(paths[slot]))
            return false;
        if (!Rc(fs::CreateFile(paths[slot], sizeof(Header) + u64(count) * sizeof(ContentId))))
            return false;
        fs::FileHandle f{};
        if (!Rc(fs::OpenFile(&f, paths[slot], fs::OpenMode_Write)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        const Header h{Magic, 1, count, 0};
        return Rc(fs::WriteFile(f, 0, &h, sizeof(h), fs::WriteOption::Flush));
    }
    bool Write(unsigned slot, u32 index, const ContentId *data, u32 n) {
        cached_count = 0;
        if (slot < 1 || slot > 2)
            return Invalid();
        fs::FileHandle f{};
        if (!Rc(fs::OpenFile(&f, paths[slot], fs::OpenMode_Write)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        // This handle is closed at the end of each page write. Flushing a
        // different handle later in Publish cannot satisfy FileAccessor's
        // flush-before-close contract for this one.
        return Rc(fs::WriteFile(f, sizeof(Header) + u64(index) * sizeof(ContentId), data, size_t(n) * sizeof(ContentId),
                                fs::WriteOption::Flush));
    }
    bool Publish(unsigned slot, u32 count) {
        AMS_UNUSED(count);
        cached_count = 0;
        fs::FileHandle f{};
        if (!Rc(fs::OpenFile(&f, paths[slot], fs::OpenMode_Write)))
            return false;
        const auto flushed = fs::FlushFile(f);
        fs::CloseFile(f);
        if (!Rc(flushed))
            return false;
        // A repair transaction must not be interleaved with publication.
        fs::FileHandle pending{};
        const auto pending_result =
            fs::OpenFile(&pending, "#ncm-usb-map:/atmosphere/usb_content-repair.pending", fs::OpenMode_Read);
        if (R_SUCCEEDED(pending_result)) {
            fs::CloseFile(pending);
            return Invalid();
        }
        if (!fs::ResultPathNotFound::Includes(pending_result))
            return Rc(pending_result);
        // Preserve both the primary and sole backup when a previous rename was
        // interrupted. Never delete the only known-good file to free its name.
        auto r = fs::OpenFile(&f, primary, fs::OpenMode_Read);
        bool moved = false;
        if (R_SUCCEEDED(r)) {
            fs::CloseFile(f);
            if (!Remove(backup) || !Rc(fs::RenameFile(primary, backup)))
                return false;
            moved = true;
        } else if (!fs::ResultPathNotFound::Includes(r))
            return Rc(r);
        // The other work file may be a merge source. Compare the completed file
        // by hashing before/after rename; no additional full-size RAM buffer.
        u8 before[32]{}, after[32]{};
        if (!Hash(paths[slot], before)) {
            if (moved)
                AMS_UNUSED(fs::RenameFile(backup, primary));
            return false;
        }
        r = fs::RenameFile(paths[slot], primary);
        if (R_FAILED(r)) {
            if (moved)
                AMS_UNUSED(fs::RenameFile(backup, primary));
            return Rc(r);
        }
        if (!Hash(primary, after) || std::memcmp(before, after, sizeof(before)))
            return Invalid();
        util::SNPrintf(paths[0], sizeof(paths[0]), "%s", primary);
        empty = false;
        if (ephemeral)
            AMS_UNUSED(Remove(backup));
        return true;
    }
    bool Hash(const char *path, u8 *out) {
        fs::FileHandle f{};
        if (!Rc(fs::OpenFile(&f, path, fs::OpenMode_Read)))
            return false;
        ON_SCOPE_EXIT { fs::CloseFile(f); };
        s64 size{};
        if (!Rc(fs::GetFileSize(&size, f)) || size < 0)
            return false;
        u8 buffer[1024];
        crypto::Sha256Generator h;
        h.Initialize();
        for (s64 off = 0; off < size; off += sizeof(buffer)) {
            const auto n = std::min<s64>(sizeof(buffer), size - off);
            size_t got{};
            if (!Rc(fs::ReadFile(&got, f, off, buffer, n)))
                return false;
            if (got != size_t(n))
                return Invalid();
            h.Update(buffer, n);
        }
        h.GetHash(out, 32);
        return true;
    }
};
struct UsbLocationStore {
    UsbLocationIo live_io{"#ncm-usb-map:/atmosphere/usb_content/content_locations.bin"};
    UsbLocationIo fresh_io{"#ncm-usb-map:/atmosphere/usb_content/registered-this-boot.tmp", true};
    usb_map::PagedSet<UsbLocationIo, ContentId> live{live_io}, fresh{fresh_io};
    bool initialized{};
    Result Initialize() {
        if (initialized)
            R_SUCCEED();
        R_TRY(fs::EnsureDirectory("#ncm-usb-map:/atmosphere/usb_content"));
        R_UNLESS(fresh_io.ResetBoot() && fresh.Load(usb_limits::Content), fresh_io.Error());
        initialized = true;
        R_SUCCEED();
    }
    Result Load(bool force = false) {
        R_TRY(Initialize());
        if (!force && live.Ready())
            R_SUCCEED();
        R_UNLESS(live.Load(usb_limits::Content), live_io.Error());
        R_SUCCEED();
    }
    Result NewMedia() {
        R_TRY(Initialize());
        R_UNLESS(fresh_io.ResetBoot() && fresh.Load(usb_limits::Content), fresh_io.Error());
        R_RETURN(Load(true));
    }
    Result Change(ContentId key, bool add) {
        R_TRY(Load(true));
        R_UNLESS(live.Change(key, add, usb_limits::Content), live_io.Error());
        R_UNLESS(fresh.Change(key, add, usb_limits::Content), fresh_io.Error());
        R_SUCCEED();
    }
    bool Has(ContentId key, bool this_boot = false) {
        if (R_FAILED(Load()))
            return false;
        u32 pos{};
        bool found{};
        if (!live.Find(key, pos, found) || !found)
            return false;
        if (this_boot && (!fresh.Find(key, pos, found) || !found))
            return false;
        return true;
    }
    Result List(ContentId *out, size_t capacity, size_t offset, size_t *written) {
        R_TRY(Load());
        *written = 0;
        R_SUCCEED_IF(offset >= live.Count());
        u32 n{};
        R_UNLESS(live.List(out, std::min<size_t>(capacity, usb_limits::Content), offset, n), live_io.Error());
        *written = n;
        R_SUCCEED();
    }
};
} // namespace ams::ncm
