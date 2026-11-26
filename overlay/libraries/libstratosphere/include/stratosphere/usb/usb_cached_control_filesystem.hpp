// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stratosphere/usb/usb_filesystem.hpp>

namespace ams::usb::filesystem {
    enum class ReadLease { OpenFile, CachedControl };
    Result CreateReadOnlyFileSystem(std::unique_ptr<fs::fsa::IFileSystem> *out, Service *service, ReadLease lease,
                                    eject::Origin reader = {"read_only_file"});
}
