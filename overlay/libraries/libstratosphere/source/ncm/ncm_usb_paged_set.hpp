/* Copyright (c) Atmosphere-NX contributors. SPDX-License-Identifier:
 * GPL-2.0-only */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace ams::ncm::usb_map {
// A sorted on-disk set with bounded scratch memory. IO owns paths, file
// handles, exact reads, durable replacement and error reporting. No USB IPC is
// used. Existing v1 unsorted archives are normalized in disposable work files
// only. Publishing a changed set retains the existing wire format and old
// archive.
template <class IO, class Key, std::size_t PageEntries = 64> class PagedSet {
  IO &io;
  std::uint32_t count{};
  unsigned active{};
  bool ready{};
  static int Compare(const Key &a, const Key &b) {
    return std::memcmp(&a, &b, sizeof(Key));
  }
  static bool Zero(const Key &v) {
    Key z{};
    return Compare(v, z) == 0;
  }

public:
  explicit PagedSet(IO &backend) : io(backend) {}
  std::uint32_t Count() const { return count; }
  bool Ready() const { return ready; }
  void Invalidate() { ready = false; }
  bool Load(std::uint32_t limit) {
    ready = false;
    count = 0;
    active = 0;
    if (!io.Begin(count, limit))
      return false;
    std::array<Key, PageEntries> page{};
    Key previous{};
    bool have_previous = false, sorted = true;
    for (std::uint32_t start = 0; start < count;) {
      const auto n = std::min<std::uint32_t>(PageEntries, count - start);
      if (!io.Read(0, start, page.data(), n))
        return false;
      for (std::uint32_t i = 0; i < n; ++i) {
        if (Zero(page[i]))
          return io.Invalid();
        if (have_previous && Compare(previous, page[i]) >= 0)
          sorted = false;
        previous = page[i];
        have_previous = true;
      }
      start += n;
    }
    if (sorted) {
      ready = true;
      return true;
    }
    if (!io.Create(1, count))
      return false;
    for (std::uint32_t start = 0; start < count;) {
      const auto n = std::min<std::uint32_t>(PageEntries, count - start);
      if (!io.Read(0, start, page.data(), n))
        return false;
      std::sort(page.begin(), page.begin() + n,
                [](const Key &a, const Key &b) { return Compare(a, b) < 0; });
      for (std::uint32_t i = 1; i < n; ++i)
        if (Compare(page[i - 1], page[i]) == 0)
          return io.Invalid();
      if (!io.Write(1, start, page.data(), n))
        return false;
      start += n;
    }
    active = 1;
    for (std::uint64_t width = PageEntries; width < count; width *= 2) {
      const unsigned next = active == 1 ? 2 : 1;
      if (!io.Create(next, count))
        return false;
      for (std::uint64_t begin = 0; begin < count; begin += width * 2) {
        const auto middle = std::min<std::uint64_t>(count, begin + width);
        const auto end = std::min<std::uint64_t>(count, begin + width * 2);
        std::uint64_t l = begin, r = middle, out = begin;
        std::array<Key, PageEntries> left{}, right{};
        std::size_t ln = 0, li = 0, rn = 0, ri = 0, used = 0;
        while (l < middle || r < end) {
          if (l < middle && li == ln) {
            ln = std::min<std::uint64_t>(PageEntries, middle - l);
            li = 0;
            if (!io.Read(active, l, left.data(), ln))
              return false;
          }
          if (r < end && ri == rn) {
            rn = std::min<std::uint64_t>(PageEntries, end - r);
            ri = 0;
            if (!io.Read(active, r, right.data(), rn))
              return false;
          }
          if (l < middle && r < end && Compare(left[li], right[ri]) == 0)
            return io.Invalid();
          const bool take_left =
              r == end || (l < middle && Compare(left[li], right[ri]) < 0);
          if (take_left) {
            page[used++] = left[li++];
            ++l;
          } else {
            page[used++] = right[ri++];
            ++r;
          }
          if (used == PageEntries || l + r == middle + end) {
            if (!io.Write(next, out, page.data(), used))
              return false;
            out += used;
            used = 0;
          }
        }
      }
      active = next;
    }
    ready = true;
    return true;
  }
  bool Find(const Key &key, std::uint32_t &position, bool &found) {
    found = false;
    position = 0;
    if (!ready || Zero(key))
      return io.Invalid();
    std::uint32_t lo = 0, hi = count;
    Key item{};
    while (lo < hi) {
      const auto mid = lo + (hi - lo) / 2;
      if (!io.Read(active, mid, &item, 1))
        return false;
      if (Compare(item, key) < 0)
        lo = mid + 1;
      else
        hi = mid;
    }
    position = lo;
    if (lo < count) {
      if (!io.Read(active, lo, &item, 1))
        return false;
      found = Compare(item, key) == 0;
    }
    return true;
  }
  bool List(Key *out, std::uint32_t capacity, std::uint32_t offset,
            std::uint32_t &written) {
    written = 0;
    if (!ready || (!out && capacity))
      return io.Invalid();
    if (offset >= count)
      return true;
    written = std::min(capacity, count - offset);
    return io.Read(active, offset, out, written);
  }
  bool Change(const Key &key, bool add, std::uint32_t limit) {
    std::uint32_t position{};
    bool found{};
    if (!Find(key, position, found))
      return false;
    if (found == add)
      return true;
    if (add && count >= limit)
      return io.Full();
    const auto total = add ? count + 1 : count - 1;
    const unsigned next = active == 1 ? 2 : 1;
    if (!io.Create(next, total))
      return false;
    std::array<Key, PageEntries> page{};
    auto copy = [&](std::uint32_t begin, std::uint32_t end,
                    std::uint32_t output) {
      while (begin < end) {
        const auto n = std::min<std::uint32_t>(PageEntries, end - begin);
        if (!io.Read(active, begin, page.data(), n) ||
            !io.Write(next, output, page.data(), n))
          return false;
        begin += n;
        output += n;
      }
      return true;
    };
    if (!copy(0, position, 0))
      return false;
    if (add && !io.Write(next, position, &key, 1))
      return false;
    if (!copy(position + (add ? 0 : 1), count, position + (add ? 1 : 0)))
      return false;
    // IO must verify the replacement and return false on ambiguous commit.
    // A failed publication forces a reload instead of guessing a rollback.
    if (!io.Publish(next, total)) {
      ready = false;
      return false;
    }
    count = total;
    active = 0;
    return true;
  }
};
} // namespace ams::ncm::usb_map
