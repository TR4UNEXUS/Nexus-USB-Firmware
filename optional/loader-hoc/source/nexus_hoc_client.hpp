// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stratosphere.hpp>
#include "nexus_hoc_protocol.hpp"

namespace ams::ldr::nexus_oc {
Result EnsureAvailable();
Result Patch(u32 kind, const u8 module_id[32], uintptr_t target_address,
             void *mapped_image, size_t module_size, size_t image_size);
Result Commit();
void InvalidatePending();
}
