/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

namespace ams::mitm::usb_storage::impl {
    // Unsigned NCA headers are never accepted from USB. Content on the drive
    // must pass the same signature checks as SD and NAND content.
    constexpr bool UseEmbeddedUsbHeaderCompatibility(bool content_enabled) {
        static_cast<void>(content_enabled);
        return false;
    }
}
