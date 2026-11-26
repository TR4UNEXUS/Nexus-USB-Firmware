#pragma once

#include <cstddef>
#include <cstdint>

namespace ams::boot::splash {
    bool Draw(const std::uint8_t *jpeg, std::size_t size, std::uint32_t *surface,
              std::size_t column_stride, std::size_t surface_pixels);
}
