// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stratosphere.hpp>
namespace ams::usb::pause_diagnostics {
enum class Stage { FileRead, FileWrite, VolumeWriteWait };
struct Scope {
    Scope(Stage, std::size_t) {}
    void Finish(Result) {}
};
}
