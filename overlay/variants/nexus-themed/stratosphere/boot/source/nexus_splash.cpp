#include "nexus_splash.hpp"
#include "jpeg/tjpgd.h"
#include <cstring>

namespace ams::boot::splash {
    namespace {
        constexpr unsigned Width = 1280, Height = 720;
        // ShowSplashScreen runs once on the boot thread. Keep scratch off its stack.
        alignas(16) std::uint8_t workspace[4096];

        struct Surface {
            const std::uint8_t *next;
            std::size_t remaining;
            std::uint32_t *pixels;
            std::size_t stride;
        };

        std::size_t Read(JDEC *decoder, std::uint8_t *destination, std::size_t amount) {
            auto &s = *static_cast<Surface *>(decoder->device);
            if (amount > s.remaining) amount = s.remaining;
            if (destination) std::memcpy(destination, s.next, amount);
            s.next += amount;
            s.remaining -= amount;
            return amount;
        }

        int DrawTile(JDEC *decoder, void *tile, JRECT *rect) {
            auto &s = *static_cast<Surface *>(decoder->device);
            if (rect->right >= Width || rect->bottom >= Height ||
                rect->left > rect->right || rect->top > rect->bottom) return 0;
            const auto *rgb = static_cast<const std::uint8_t *>(tile);
            for (unsigned y = rect->top; y <= rect->bottom; ++y) {
                for (unsigned x = rect->left; x <= rect->right; ++x, rgb += 3) {
                    s.pixels[(Width - 1 - x) * s.stride + y] =
                        0xff000000u | rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
                }
            }
            return 1;
        }
    }

    bool Draw(const std::uint8_t *jpeg, std::size_t size, std::uint32_t *surface,
              std::size_t column_stride, std::size_t surface_pixels) {
        if (!jpeg || !size || !surface || column_stride < Height ||
            column_stride > surface_pixels / Width) return false;
        Surface s{jpeg, size, surface, column_stride};
        JDEC decoder{};
        if (jd_prepare(&decoder, Read, workspace, sizeof(workspace), &s) != JDR_OK ||
            decoder.width != Width || decoder.height != Height) return false;
        return jd_decomp(&decoder, DrawTile, 0) == JDR_OK;
    }
}
