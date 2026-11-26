// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "usb_eject_protocol.hpp"

namespace eject_view {
// A progressing heartbeat distinguishes live status from a previous boot's
// file with a similar uptime. Freshness alone is insufficient.
class Status {
    nexus_eject::Snapshot previous{};
    bool live{}, available{};
public:
    nexus_eject::Snapshot value{};
    void Missing() { available=false; }
    bool Observe(const nexus_eject::Snapshot& next, uint64_t now) {
        if (!next.boot || next.sample_ms>now || now-next.sample_ms>3000) {
            Missing(); live=false; previous={}; return false;
        }
        if (previous.boot!=next.boot || next.sample_ms<previous.sample_ms) live=false;
        else if (next.sample_ms>previous.sample_ms) live=true;
        previous=next; value=next; available=true;
        return Fresh(now);
    }
    bool Fresh(uint64_t now) const {
        return available && live && value.sample_ms<=now && now-value.sample_ms<=3000;
    }
    bool Safe(uint64_t now) const {
        return Fresh(now) && nexus_eject::Safe(value,value.boot,value.request);
    }
    bool CanEject(uint64_t now) const {
        // The firmware checks current ownership on confirmation. Its last
        // sample can include a game that has since exited.
        return Fresh(now) && value.phase==0 && value.ready && value.generation;
    }
    bool CanResume(uint64_t now) const { return Safe(now); }
};
}
