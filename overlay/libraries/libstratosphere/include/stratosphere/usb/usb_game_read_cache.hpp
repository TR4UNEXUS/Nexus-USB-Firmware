/* Copyright (c) Atmosphere-NX contributors. SPDX-License-Identifier:
 * GPL-2.0-only */
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ams::usb::game_read {
inline std::atomic<std::uint64_t> hits{}, misses{}, bytes_saved{},
    invalidations{};
// Read-only NCA requests only; bulk installer/benchmark reads bypass this.
// The volume mutex protects contents. A media generation alone is insufficient
// across retained-handle wake: Clear is mandatory on suspend and all mutations.
class Cache {
  static constexpr std::size_t Slots = 4, Bytes = 16384, PathBytes = 0x301;
  struct Entry {
    char path[PathBytes]{};
    std::uint8_t data[Bytes]{};
    std::uint64_t generation{}, offset{}, stamp{};
    std::size_t size{};
  };
  Entry entries[Slots]{};
  std::uint64_t clock{};

public:
  static constexpr std::size_t ReadAheadBytes = Bytes;
  void Clear() {
    for (auto &e : entries)
      e.size = 0;
    invalidations.fetch_add(1, std::memory_order_relaxed);
  }
  bool Read(const char *path, std::uint64_t generation, std::uint64_t offset,
            void *out, std::size_t size) {
    if (!path || !out || !generation || !size || size > Bytes)
      return false;
    for (auto &e : entries)
      if (e.size && e.generation == generation &&
          std::strcmp(e.path, path) == 0 && offset >= e.offset &&
          offset - e.offset <= e.size && size <= e.size - (offset - e.offset)) {
        std::memcpy(out, e.data + offset - e.offset, size);
        e.stamp = ++clock;
        hits.fetch_add(1, std::memory_order_relaxed);
        bytes_saved.fetch_add(size, std::memory_order_relaxed);
        return true;
      }
    misses.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  template <class Read>
  std::uint32_t Fill(const char *path, std::uint64_t generation,
                     std::uint64_t offset, std::uint64_t file_size, void *out,
                     std::size_t requested, Read read) {
    // Caller validates request and returns its platform error on bypass.
    if (!path || !out || !generation || !requested || requested > Bytes ||
        std::strlen(path) >= PathBytes || offset > file_size ||
        requested > file_size - offset)
      return UINT32_MAX;
    Entry *e = &entries[0];
    for (auto &candidate : entries)
      if (!candidate.size || candidate.stamp < e->stamp)
        e = &candidate;
    e->size = 0;
    const auto count = std::min<std::uint64_t>(Bytes, file_size - offset);
    const auto result = read(offset, e->data, count);
    if (result)
      return result;
    std::strcpy(e->path, path);
    e->generation = generation;
    e->offset = offset;
    e->stamp = ++clock;
    e->size = count;
    std::memcpy(out, e->data, requested);
    return 0;
  }
};
} // namespace ams::usb::game_read
