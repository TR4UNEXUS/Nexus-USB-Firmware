// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace ams::os {
inline thread_local uint64_t host_now_ns{};
inline thread_local uint64_t host_clock_reads{};
inline void AdvanceHostClock(uint64_t ns) { host_now_ns += ns; }
struct Span {
    uint64_t value{};
    int64_t GetMicroSeconds() const { return static_cast<int64_t>(value / 1000); }
    int64_t GetNanoSeconds() const { return static_cast<int64_t>(value); }
};
struct Tick { uint64_t value{}; Span ToTimeSpan() const { return {value}; } };
inline Tick operator-(Tick a, Tick b) { return {a.value - b.value}; }
inline Tick GetSystemTick() { ++host_clock_reads; return {host_now_ns}; }
}
