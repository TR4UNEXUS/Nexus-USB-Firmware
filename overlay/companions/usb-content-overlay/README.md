# Nexus USB Content 1.3.0

An Ultrahand/Tesla overlay for Nexus USB storage. Copy
`switch/.overlays/usb-content-status.ovl` onto the SD card, replacing the old
overlay. It works with the safe-eject protocol already present in N4R2 and
HS3R1, in either HOC or non-HOC packs. It does not start another USB driver.

Close games, Album/homebrew and transfers, then open Nexus USB Content from
the HOME menu. Select **Safely eject USB**, press A to confirm, and leave the
drive connected until **Safe to unplug** appears. The Switch can remain on.
Choose **Reconnect USB** to use the drive again without restarting the Switch.

The firmware refuses eject while it has active users, closes admission to new
operations, flushes the filesystem, unmounts it, synchronises the device cache,
and releases the USB connection. This overlay reports success only after a
live, matching completion. A stale file, timeout, failure or missing firmware
support never means it is safe to unplug. If flushing fails, keep the drive
connected and shut down fully before removal.

The menu uses a consistent dark blue design with blue/purple accents, compact
outlined controls, a clear selection highlight and Switch button glyphs in the
footer. It is a revised design, not a pixel-for-pixel restoration of 1.1.0.
Drive information and a space-used bar appear above Install route, Safe eject
and Refresh status. Free space and total capacity have separate aligned values.
The drive panel shows
the name, device/filesystem, free space, used/total space and access mode. It is
independent of the safe-eject heartbeat, so waiting for eject status no longer
hides the drive information. Missing/old storage status is labelled as last
reported data. A full drive correctly shows zero free space.

Technical diagnostics are no longer shown. Coloured notifications
are supplied by the firmware and existing Ultrahand configuration, unchanged
by this overlay. Selecting an install target is separate from ejecting.

Build with devkitPro/devkitA64 and libnx: `make -j4`. The adjacent
`horizon-oc-monitor-reference/lib/libultrahand` supplies the UI library. The
protocol header is copied byte-for-byte from the matching Nexus firmware.

The 1.3.0 build and host tests passed, including actual button callbacks and
request-file handling. Safe removal/reconnect still needs a physical Switch
test. Host tests do not reproduce USB hardware or Horizon services. The layout
preview is rendered from production drawing code with sample data and a
substitute font; it is not a console screenshot.
