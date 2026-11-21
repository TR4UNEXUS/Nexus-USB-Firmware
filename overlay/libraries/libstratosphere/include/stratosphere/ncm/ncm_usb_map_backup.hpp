/* Copyright (c) Atmosphere-NX contributors. SPDX-License-Identifier:
 * GPL-2.0-only */
#pragma once
#include <stratosphere.hpp>
namespace ams::ncm {
struct UsbRoutingBackupStatus {
  u32 magic, version, flags, last_result;
  u64 sequence, bytes;
};
static_assert(sizeof(UsbRoutingBackupStatus) == 32);
// Private commands 65034 query, 65035 checkpoint, 65036 read. Restore is
// performed by Manager's existing checked journal/rollback workflow.
Result QueryUsbRoutingBackup(UsbRoutingBackupStatus *out);
Result ReadUsbRoutingBackup(void *out, size_t size, u64 sequence, u64 offset);
Result CheckpointUsbRoutingBackup();
Result CreateUsbRoutingBackup(); // serialized with NCM map mutations
void RequestUsbRoutingBackup();
void RunUsbRoutingBackupWorker(); // NCM's existing idle main thread
} // namespace ams::ncm
