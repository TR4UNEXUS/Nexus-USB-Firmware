# Nexus USB Firmware

USB storage, game-content routing, registration, database and Loader integration for **Nexus Atmosphere 1.12.2 HS3R1**, with **Switch system version 23.5.0** support. This repository also contains **Nexus USB Content 1.3.0**, the drive information and safe-eject overlay.

## What's included

- An independent Nexus Loader, so USB hooks no longer need to be merged into HOC. The current HOC test pack uses unmodified upstream HOC 2.6.1.
- The HS3R1 boot fix for the single filesystem-session conflict between Loaders, with production-method regression tests.
- The updated blue/purple USB overlay: drive details, free/total space, usage bar, install route, safe eject and reconnect. Its UI library and tests are included.
- Earlier N4R2 compressed game-data and USB-to-USB install fixes, console-side map backups, paged mapping, sleep/wake recovery and explicit USB installation commands. SD and internal-storage installs keep their selected destination.

## How it fits together

USB support spans several Atmosphere modules. This is a source overlay on the exact [Nexus Atmosphere](https://github.com/TR4UNEXUS/Nexus-Atmosphere) commit recorded in `base.json`, not a standalone USB-driver KIP. The preparation tool checks out that base and applies the source here. Both HS3R1 packs need the matching core and separate Nexus Loader.

| Folder | Purpose |
| --- | --- |
| `overlay/libraries/libstratosphere/source/usb` | USB transport, storage, filesystems and safe eject |
| `overlay/stratosphere/ams_mitm/source/usb_mitm` | Shared USB service and content routing |
| `overlay/libraries/libstratosphere/source/ncm` | Registration, content database and mapping backups |
| `overlay/stratosphere/nexus_loader` | Independent USB-aware Loader and filesystem proxy |
| `overlay/stratosphere/loader` | Original Loader, replaceable by compatible upstream HOC |
| `overlay/stratosphere/sm` | Service routing between Loaders |
| `overlay/companions/usb-content-overlay` | Nexus USB Content 1.3.0 source and tests |
| `overlay/companions/horizon-oc-monitor-reference/lib/libultrahand` | Overlay UI dependency and licences |
| `overlay/tests/nexus-integration` | Host regression tests |
| `optional` | Historical combined HOC and HS2 source; not used by HS3R1 |

Library namespaces are included in full so headers and implementations stay together. The pinned base supplies the remaining core, build scripts, version support and original Atmosphere splash.

## Build and contribute

See [BUILDING.md](BUILDING.md), [CHANGELOG.md](CHANGELOG.md) and [CONTRIBUTING.md](CONTRIBUTING.md). **Help building and improving Nexus is welcome**, including USB compatibility, performance, testing, build tools and documentation.

The firmware cross-build and host checks passed. HS3R1 remains a test build pending full console validation. No claim is made that every game, drive or system module works.

For downloads, visit [Nexus USB HATS releases](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases). [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source) is maintained separately; these packs include Manager 0.2.24.

## Licence

This code builds on Atmosphère. Original copyright notices and third-party licences are retained; see [LICENSE](LICENSE) and the notices alongside each component. The overlay's UI libraries keep their own licences. Private keys, console data and generated builds are not included.
