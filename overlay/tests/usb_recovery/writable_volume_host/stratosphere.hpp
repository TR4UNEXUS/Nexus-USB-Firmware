// SPDX-License-Identifier: GPL-2.0-or-later
// Host-only platform scaffolding. The test compiles production WritableVolume
// and production FatFs unchanged; only the OS/device shell is substituted.
#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
namespace ams {
class Result {
    uint32_t value;
public:
    constexpr Result(uint32_t v = 0) : value(v) {}
    constexpr uint32_t GetValue() const { return value; }
};
constexpr Result ResultSuccess() { return {}; }
namespace util {
template<typename T> constexpr bool IsPowerOfTwo(T n) { return n && !(n & (n - 1)); }
template<typename T, std::size_t N> constexpr std::size_t size(const T (&)[N]) { return N; }
}
namespace usb {
#define TEST_RESULT(name, value) inline Result name() { return Result(value); }
TEST_RESULT(ResultFileNotFound, 1)
TEST_RESULT(ResultInvalidParameter, 2)
TEST_RESULT(ResultAlreadyInitialized, 3)
TEST_RESULT(ResultNotInitialized, 4)
TEST_RESULT(ResultOperationDenied, 5)
TEST_RESULT(ResultMemAllocFailure, 6)
TEST_RESULT(ResultResourceBusy, 7)
TEST_RESULT(ResultTransactionError, 8)
TEST_RESULT(ResultUnsupportedFilesystem, 9)
TEST_RESULT(ResultInternalStateError, 10)
TEST_RESULT(ResultCorruptedFilesystem, 11)
#undef TEST_RESULT
namespace mass_storage {
struct Geometry { u32 block_size; u64 block_count; };
class Device {
public:
    Geometry geometry{};
    bool IsInitialized() const { return true; }
    const Geometry &GetGeometry() const { return geometry; }
};
}
namespace volume {
constexpr inline std::size_t MaxFileNameLength = 255;
enum class PartitionStyle : u8 { Mbr, Gpt, SuperFloppy };
struct Partition { u64 first_lba; u64 sector_count; u8 type; bool bootable; PartitionStyle style; char name[37]; };
struct FileInfo { u64 size; u32 first_cluster; bool directory; };
struct DirectoryEntryInfo { char name[MaxFileNameLength + 1]; u64 size; bool directory; };
class WritableVolume {
    struct Impl;
    Impl *m_impl{};
public:
    ~WritableVolume();
    Result Initialize(mass_storage::Device *, const Partition &);
    Result Finalize();
    bool IsInitialized() const { return m_impl != nullptr; }
    const Partition &GetPartition() const;
    Result CreateDirectory(const char *);
    Result CreateFile(const char *, u64);
    Result ResizeFile(const char *, u64);
    Result DeleteFile(const char *);
    Result RenameFile(const char *, const char *);
    Result GetFileInfo(const char *, FileInfo *);
    Result ReadFile(const char *, u64, void *, std::size_t, std::size_t *);
    Result WriteFile(const char *, u64, const void *, std::size_t, std::size_t *);
    Result ReadDirectory(const char *, DirectoryEntryInfo *, std::size_t, std::size_t, bool, bool, std::size_t *);
    Result GetSpaceSize(u64 *, u64 *);
    Result Flush();
};
}
}
}
#define R_SUCCEEDED(rc) ((rc).GetValue() == 0)
#define R_FAILED(rc) (!R_SUCCEEDED(rc))
#define R_RETURN(rc) return (rc)
#define R_SUCCEED() return ams::ResultSuccess()
#define R_UNLESS(test, rc) do { if (!(test)) return (rc); } while (0)
#define R_TRY(expression) do { const ams::Result test_rc = (expression); if (R_FAILED(test_rc)) return test_rc; } while (0)
#define AMS_ASSERT(test) assert(test)
