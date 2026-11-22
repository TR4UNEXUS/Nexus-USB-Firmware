/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cstdint>

namespace ams::fssystem {
    /* NCA header signatures are always enforced, exactly as upstream
     * Atmosphere does. The parameters are kept only so existing callers
     * compile; no storage location or title range can opt out. */
    constexpr bool AcceptNcaHeaderSignature(bool valid, bool allow_unsigned_usb_application, uint64_t program_id) {
        static_cast<void>(allow_unsigned_usb_application);
        static_cast<void>(program_id);
        return valid;
    }
}
