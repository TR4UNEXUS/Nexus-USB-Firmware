// SPDX-License-Identifier: GPL-2.0-or-later
#include <stratosphere/fssystem/fssystem_usb_signature_policy.hpp>
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using ams::fssystem::AcceptNcaHeaderSignature;
    // Header signatures are enforced for every title ID, with or without the
    // legacy USB flag: an invalid signature is always rejected.
    constexpr std::array<uint64_t, 15> ids{
        0, 0x00FFFFFFFFFFFFFF, 0x0100000000000000, 0x010000000000000D,
        0x0100000000010000, 0x01007EF00011E000, 0x01FFFFFFFFFFFFFF,
        0x0500000000000000, 0x05004E584D470000, 0x0530E68C0A899000,
        0x05FFFFFFFFFFFFFF, 0x0600000000000000, 0x10FFFFFFFFFFFFFF,
        0x0200000000000000, UINT64_MAX};
    for (const auto id : ids) {
        assert(!AcceptNcaHeaderSignature(false, true, id));
        assert(!AcceptNcaHeaderSignature(false, false, id));
        assert(AcceptNcaHeaderSignature(true, false, id));
        assert(AcceptNcaHeaderSignature(true, true, id));
    }
    std::puts("PASS NCA header signature policy: invalid signatures are always rejected");
}
