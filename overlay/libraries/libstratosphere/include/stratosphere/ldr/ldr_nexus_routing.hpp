// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace ams::ldr::nexus_routing {
    inline constexpr std::uint64_t ProgramId = 0x01004E584C440000ull;
    constexpr std::uint64_t Name(const char *s) {
        std::uint64_t result=0;
        for (unsigned i=0; i<8 && s[i]; ++i) result |= std::uint64_t(static_cast<unsigned char>(s[i])) << (8*i);
        return result;
    }
    struct ServiceRoute { std::uint64_t public_name, frontend, backend_alias; };
    inline constexpr std::array Services{
        ServiceRoute{Name("ldr:pm"),Name("nxl:pm"),Name("nxb:pm")},
        ServiceRoute{Name("ldr:shel"),Name("nxl:shel"),Name("nxb:shel")},
        ServiceRoute{Name("ldr:dmnt"),Name("nxl:dmnt"),Name("nxb:dmnt")}
    };
    inline constexpr auto FileSystemService=Name("fsp-ldr");
    inline constexpr auto FileSystemFrontend=Name("nxl:fs");
    constexpr bool IsBackendAlias(std::uint64_t name) {
        for (const auto &r:Services) if (name==r.backend_alias) return true;
        return false;
    }
    constexpr std::uint64_t Lookup(std::uint64_t name,bool present,bool native_loader=false) {
        if (name==FileSystemService && present && !native_loader) return FileSystemFrontend;
        for (const auto &r:Services) {
            if (name==r.public_name && present) return r.frontend;
            if (name==r.backend_alias) return r.public_name;
        }
        return name;
    }
    constexpr std::uint64_t WakeAlias(std::uint64_t registered) {
        if (registered==FileSystemFrontend) return FileSystemService;
        for (const auto &r:Services) {
            if (registered==r.frontend) return r.public_name;
            if (registered==r.public_name) return r.backend_alias;
        }
        return 0;
    }
    // These are the modules with embedded Nexus compatibility tables. Keep
    // those tables in the independent loader, including when boot launches
    // them with StorageId::None. This does not enable USB resolution for them.
    constexpr bool HasNexusSystemPatches(std::uint64_t program) {
        switch (program) {
            case 0x0100000000000006ull: // USB
            case 0x010000000000000Full: // NIFM
            case 0x010000000000001Full: // NS
            case 0x0100000000000023ull: // AM
            case 0x0100000000000025ull: // NIM
            case 0x0100000000000033ull: // ES
                return true;
            default: return false;
        }
    }
    // None=0, Host=1, GameCard=2, System=3, User=4, SD=5. System-storage
    // applets include HOME/web: they also need the existing Nexus patches.
    // Ordinary boot modules, including HOC's PCV/PTM targets, use the backend.
    constexpr bool UseFrontend(std::uint64_t program,bool system_program,std::uint8_t storage) {
        return HasNexusSystemPatches(program) || (!system_program && storage>=1 && storage<=5);
    }
    enum class Owner : std::uint8_t { Frontend, Backend };
    struct Pin {
        std::uint64_t external{}, internal{}, program{};
        Owner owner{};
        std::uint8_t storage{};
    };
    template<std::size_t Capacity> class Pins {
        std::array<Pin,Capacity> entries{};
        std::uint64_t next{};
      public:
        bool HasRoom() const {
            if (next==std::numeric_limits<std::uint64_t>::max()) return false;
            for (const auto &p:entries) if (!p.external) return true;
            return false;
        }
        std::uint64_t Add(std::uint64_t internal,Owner owner,std::uint64_t program=0,std::uint8_t storage=0) {
            if (!internal || !HasRoom()) return 0;
            for (auto &p:entries) if (!p.external) { p={++next,internal,program,owner,storage}; return p.external; }
            return 0;
        }
        const Pin *Find(std::uint64_t external) const {
            if (!external) return nullptr;
            for (const auto &p:entries) if (p.external==external) return &p;
            return nullptr;
        }
        bool HasFrontendProgram(std::uint64_t program,std::uint8_t storage) const {
            for (const auto &p:entries)
                if (p.external && p.owner==Owner::Frontend && p.program==program && p.storage==storage) return true;
            return false;
        }
        bool Remove(std::uint64_t external) {
            if (!external) return false;
            for (auto &p:entries) if (p.external==external) { p={}; return true; }
            return false;
        }
    };
}
