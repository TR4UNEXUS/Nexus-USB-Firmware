/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stratosphere.hpp>
#include "boot_boot_reason.hpp"
#include "boot_display.hpp"
#include "boot_splash_screen.hpp"
#include "boot_splash_codec.hpp"
#include "boot_splash_zstd.hpp"

namespace ams::boot {

    namespace {

/* Include splash screen into anonymous namespace. */
/* TODO: Compile-time switch for splash_screen_text.hpp? */
#include "boot_splash_zstd.inc"
        /* Decode directly into the existing display buffer; never use the 20 KiB boot heap. */
        constinit u8 g_splash_scratch[SplashScratchSize] = {};
        alignas(8) constinit u8 g_splash_workspace[splash::ZstdWorkspaceSize] = {};

    }

    void ShowSplashScreen() {
        const auto boot_reason = GetBootReason();
        if (boot_reason == spl::BootReason_AcOk || boot_reason == spl::BootReason_RtcAlarm2) {
            return;
        }

        InitializeDisplay();
        /* A damaged embedded splash must not prevent the console from booting. */
        if (ShowDecodedSplash([](u32 *pixels, size_t stride, size_t capacity) {
            return splash::Decode(pixels, SplashScreenW * SplashScreenH, SplashScreenW,
                g_splash_scratch, SplashScratchSize, SplashCompressed, sizeof(SplashCompressed),
                SplashBlocks, sizeof(SplashBlocks)/sizeof(SplashBlocks[0]),
                splash::ZstdDecompressor{g_splash_workspace, sizeof(g_splash_workspace),
                    g_splash_scratch, sizeof(g_splash_scratch), SplashScratchSize}, stride, capacity);
        })) {
            os::SleepThread(TimeSpan::FromSeconds(2));
        }
        FinalizeDisplay();
    }

}
