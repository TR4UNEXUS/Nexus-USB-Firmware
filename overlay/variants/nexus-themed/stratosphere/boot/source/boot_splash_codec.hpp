/* Lossless CNX splash reconstruction. Distributed under GPL-2.0 with Atmosphere. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ams::boot::splash {
    struct Block {
        std::size_t offset, size, first, count;
        std::uint8_t channel;
    };

    /* The LZ4 callback must enforce both input and output bounds. No heap or I/O. */
    template<typename Decompress>
    bool Decode(std::uint32_t *pixels, std::size_t pixel_count, std::size_t width,
                std::uint8_t *scratch, std::size_t scratch_size,
                const std::uint8_t *data, std::size_t data_size,
                const Block *blocks, std::size_t block_count, Decompress decompress,
                std::size_t column_stride = 0, std::size_t capacity = 0) {
        if (!pixels || !scratch || !data || !blocks || !width || !pixel_count ||
            pixel_count % width || pixel_count > std::numeric_limits<std::size_t>::max()/4 ||
            !block_count || block_count % 4) return false;
        // A rotated display surface avoids a second full-screen allocation.
        // The ordinary row-major decoder remains the default for other callers.
        if (column_stride && (column_stride < pixel_count / width ||
            width > std::numeric_limits<std::size_t>::max() / column_stride ||
            capacity < width * column_stride ||
            capacity > std::numeric_limits<std::size_t>::max() / 4)) return false;
        const auto index = [width, column_stride](std::size_t pixel) {
            return column_stride ? (width - 1 - pixel % width) * column_stride + pixel / width : pixel;
        };
        auto *out = reinterpret_cast<std::uint8_t *>(pixels);
        std::size_t expected_first = 0, expected_offset = 0;
        for (std::size_t group = 0; group < block_count; group += 4) {
            const auto count = blocks[group].count;
            if (!count || count % width || count > scratch_size ||
                count > pixel_count-expected_first || count > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const auto &block = blocks[group+channel];
                if (block.first != expected_first || block.count != count || block.channel != channel ||
                    block.offset != expected_offset || block.offset > data_size || !block.size ||
                    block.size > data_size-block.offset || block.size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
                if (decompress(scratch, count, data+block.offset, block.size) != static_cast<int>(count)) return false;
                for (std::size_t j = 0; j < count; ++j) {
                    const auto pixel = expected_first+j;
                    const unsigned left = pixel % width ? out[index(pixel-1)*4+channel] : 0;
                    const unsigned above = pixel >= width ? out[index(pixel-width)*4+channel] : 0;
                    out[index(pixel)*4+channel] = static_cast<std::uint8_t>(scratch[j] + (left+above)/2);
                }
                expected_offset += block.size;
            }
            expected_first += count;
        }
        if (expected_first != pixel_count || expected_offset != data_size) return false;
        /* Restore B and R only after prediction has consumed the transformed */
        /* prior rows. No extra buffer, allocation or filesystem access is needed. */
        for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
            out[index(pixel)*4] = static_cast<std::uint8_t>(out[index(pixel)*4] + out[index(pixel)*4+1]);
            out[index(pixel)*4+2] = static_cast<std::uint8_t>(out[index(pixel)*4+2] + out[index(pixel)*4+1]);
        }
        return true;
    }
}
