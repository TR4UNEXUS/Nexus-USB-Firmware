/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stratosphere.hpp>
namespace ams::ncm {
// FS denies a second writer while this file handle is open. The file is kept
// between boots; the handle, not its existence, owns the lease. A crashed
// process therefore cannot leave a permanent lock marker behind.
class UsbMapWriteLease {
    fs::FileHandle file{};
    bool opened{};

  public:
    ~UsbMapWriteLease() {
        if (opened)
            fs::CloseFile(file);
    }
    Result Open(const char *mount) {
        char path[128]{};
        util::SNPrintf(path, sizeof(path), "%s:/atmosphere/usb_content-write.lock", mount);
        const auto create = fs::CreateFile(path, 0);
        R_UNLESS(R_SUCCEEDED(create) || fs::ResultPathAlreadyExists::Includes(create), create);
        R_TRY(fs::OpenFile(&file, path, fs::OpenMode_Write));
        opened = true;
        R_SUCCEED();
    }
};
} // namespace ams::ncm
