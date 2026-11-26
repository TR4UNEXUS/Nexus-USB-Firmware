# Nexus USB Firmware

USB storage, game-content routing, registration, database and Loader integration for **Nexus Atmosphere 1.12.2 NX1**, with **Switch system version 23.5.0** support. This repository also contains **Nexus USB Content 1.3.3**, the drive information and safe-eject overlay.

## What's included

- Safe eject releases the temporary USB game archive after closing a title. USB Content 1.3.3 displays distinct notifications for connection, safe eject and unexpected removal. Wait for **Safe to unplug** before disconnecting the drive.

- Independent Nexus Loader and separate upstream HOC support.
- Existing same-drive sleep/wake recovery, plus fresh drive detection after a failed wake. Recovery revokes old handles and does not replay uncertain writes.
- Checked console-save backups of USB mappings, recovery of invalid archives from validated current maps, and clearer backup failure details.
- Read-only current USB report snapshots, completion-wait recovery and bounded failure timing records.
- Album and hold-R homebrew launching, Nextendo-related DNS/mod paths, compressed game-data support, explicit USB install routing and paged mappings.
- Nexus USB Content 1.3.3: drive details, capacity bar, install route, safe eject and reconnect.

## How it fits together

USB support spans several Atmosphere modules. This is a source overlay on the exact [Nexus Atmosphere](https://github.com/TR4UNEXUS/Nexus-Atmosphere) commit recorded in `base.json`, not a standalone USB-driver KIP. The preparation tool checks out that base and applies the source here. All three NX1 packs need the matching core and separate Nexus Loader.

| Folder | Purpose |
| --- | --- |
| `overlay/libraries/libstratosphere/source/usb` | USB transport, storage, filesystems and safe eject |
| `overlay/stratosphere/ams_mitm/source/usb_mitm` | Shared USB service and content routing |
| `overlay/libraries/libstratosphere/source/ncm` | Registration, content database and mapping backups |
| `overlay/stratosphere/nexus_loader` | Independent USB-aware Loader and filesystem proxy |
| `overlay/stratosphere/loader` | Original Loader, replaceable by compatible upstream HOC |
| `overlay/stratosphere/sm` | Service routing between Loaders |
| `overlay/companions/usb-content-overlay` | Nexus USB Content 1.3.3 source and tests |
| `overlay/companions/horizon-oc-monitor-reference/lib/libultrahand` | Historical UI library and licences |
| `overlay/tests/nexus-integration` | Host regression tests |
| `optional` | Historical combined HOC and HS2 source; not used by NX1 |

Library namespaces are included in full so headers and implementations stay together. The pinned base supplies the remaining core, build scripts and version support. Standard builds retain the original Atmosphere splash; `overlay/variants/nexus-themed` supplies the compressed Nexus splash variant.

## Build and contribute

See [BUILDING.md](BUILDING.md), [CHANGELOG.md](CHANGELOG.md) and [CONTRIBUTING.md](CONTRIBUTING.md). **Help building and improving Nexus is welcome**, including USB compatibility, performance, testing, build tools and documentation.

The firmware cross-build and host checks passed. Safe eject after closing a USB game was confirmed on a Switch. No claim is made that every game, drive or system module works.

For downloads, visit [Nexus USB HATS releases](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases). [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source) is maintained separately; these packs include Manager 0.2.40.

## Support Nexus

If Nexus has been useful to you, consider [buying me a coffee](https://www.buymeacoffee.com/TechRepairs4u). Your support gives me more time to build new features and test fixes. Thanks for supporting my work!

<a href="https://www.buymeacoffee.com/TechRepairs4u"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" width="217" height="60"></a>

## Licence

This code builds on Atmosphère. Original copyright notices and third-party licences are retained; see [LICENSE](LICENSE) and the notices alongside each component. The overlay's UI libraries keep their own licences. Private keys, console data and generated builds are not included.
