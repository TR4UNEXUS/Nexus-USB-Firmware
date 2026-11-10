// SPDX-License-Identifier: GPL-2.0-or-later
#include <stratosphere/fssystem/fssystem_usb_signature_policy.hpp>
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using ams::fssystem::AcceptNcaHeaderSignature;
    // Actual failed launcher IDs, Manager's native ID, and the boundaries of
    // the 05 range emitted by Manager's forwarder builder.
    constexpr std::array<uint64_t, 7> homebrew{
        0x0530E68C0A899000, 0x05C57FEBDFDE1000, 0x05004E584D470000,
        0x0500000000000000, 0x05FFFFFFFFFFF000,
        0x05FFFFFFFFFFFFFF, 0x05DD1C9FCAB06000};
    for (const auto id : homebrew) {
        assert(AcceptNcaHeaderSignature(false, true, id));
        assert(!AcceptNcaHeaderSignature(false, false, id));
    }
    constexpr std::array<uint64_t, 3> applications{
        0x0100000000010000, 0x01007EF00011E000, 0x01FFFFFFFFFFFFFF};
    for (const auto id : applications) {
        assert(AcceptNcaHeaderSignature(false, true, id));
        assert(!AcceptNcaHeaderSignature(false, false, id));
    }
    constexpr std::array<uint64_t, 11> rejected{
        0, 0x00FFFFFFFFFFFFFF, 0x0100000000000000,
        0x010000000000000D, 0x0100000000001000, 0x010000000000FFFF,
        0x0200000000000000, 0x04FFFFFFFFFFFFFF,
        0x0600000000000000, 0x10FFFFFFFFFFFFFF, UINT64_MAX};
    for (const auto id : rejected) {
        assert(!AcceptNcaHeaderSignature(false, true, id));
        assert(!AcceptNcaHeaderSignature(false, false, id));
        assert(AcceptNcaHeaderSignature(true, false, id));
        assert(AcceptNcaHeaderSignature(true, true, id));
    }
    std::puts("PASS USB homebrew signature policy: failed IDs, range boundaries, opt-in and system exclusions");
}
