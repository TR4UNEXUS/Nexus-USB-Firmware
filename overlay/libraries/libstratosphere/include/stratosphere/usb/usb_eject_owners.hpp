// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stratosphere/usb/usb_eject.hpp>
#include <cstddef>
#include <cstring>

namespace ams::usb::eject {
    // kind is a string literal. Paths passed by callers can be stack buffers.
    struct Origin {
        const char *kind;
        const char *path{};
        uint64_t pid{}, title{};
    };
    struct Owner {
        const char *kind{};
        uint64_t pid{}, title{}, generation{}, opened_ms{};
        char path[96]{};
    };
    struct OwnerLink {
        Owner owner{};
        OwnerLink *next{}, **previous{};
    };
    struct Attempt {
        const char *check{"accepted"};
        uint32_t query_result{};
        uint64_t pid{}, title{}, pin{};
        Status status{};
        Owner owners[16]{};
        size_t shown{};
    };
    // Mutated with the gate count under the same storage mutex. No allocation
    // or filesystem/IPC calls occur inside the lock.
    class Owners {
        OwnerLink *first{};
    public:
        void Add(OwnerLink &link, Origin origin, uint64_t generation, uint64_t now) {
            auto &o = link.owner;
            o.kind = origin.kind;
            o.pid = origin.pid;
            o.title = origin.title;
            o.generation = generation;
            o.opened_ms = now;
            const char *path = origin.path ? origin.path : "";
            const size_t length = std::strlen(path);
            size_t n = 0;
            if (length >= sizeof(o.path)) {
                path += length - (sizeof(o.path) - 4);
                std::memcpy(o.path, "...", 3);
                n = 3;
            }
            while (*path) {
                const auto c = static_cast<unsigned char>(*path++);
                o.path[n++] = c < 32 || c == 127 ? '?' : static_cast<char>(c);
            }
            o.path[n] = 0;
            link.next = first;
            link.previous = &first;
            if (first) first->previous = &link.next;
            first = &link;
        }
        void Remove(OwnerLink &link) {
            *link.previous = link.next;
            if (link.next) link.next->previous = link.previous;
            link.previous = nullptr;
            link.next = nullptr;
        }
        void Snapshot(Attempt &out, Status status) const {
            out.status = status;
            out.shown = 0;
            for (auto *p = first; p && out.shown < sizeof(out.owners)/sizeof(out.owners[0]); p = p->next) {
                if (p->owner.generation == status.generation) out.owners[out.shown++] = p->owner;
            }
        }
    };
}
