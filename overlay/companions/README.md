# Nexus companion source

`usb-content-overlay` is Nexus USB Content 1.3.0, the drive information and safe-eject menu included in both HS3R1 packs. It includes its production UI, status parser, eject protocol and host tests.

`horizon-oc-monitor-reference/lib/libultrahand` contains the UI library used by this overlay, with its original licences. This directory name reflects the existing build layout; it does not add an HOC runtime dependency. No HOC binary is compiled into the overlay.

Build with devkitPro/devkitA64 and libnx installed, using `make -C companions/usb-content-overlay -j4`. Set DEVKITPRO, DEVKITA64 and LIBNX as described in the main build guide. The result is `companions/usb-content-overlay/usb-content-status.ovl`.

Run `python3 companions/usb-content-overlay/tests/run.py` on Linux/WSL with G++ and the address/undefined-behaviour sanitizers. These tests exercise actual callbacks with host stubs; they do not prove physical USB removal is safe on every drive.
