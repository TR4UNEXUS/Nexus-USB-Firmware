# Nexus USB Firmware

The USB integration source used by Nexus Atmosphere: USB storage, filesystem routing, game-content registration, the content database and Loader hooks.

This repository is for working on USB support without navigating the entire firmware tree. It includes the U2 SD registration fix, explicit USB installation commands, the write-performance work, the paged database and the SL7 sleep/wake recovery changes.

The current source matches **Nexus Atmosphere 1.12.2 N1**, with basic support for **Switch system version 23.5.0**. The USB-only Loader is the default; the matching HOC 2.6.0 Loader is included as an optional build. See [the changelog](CHANGELOG.md).

## How it fits together

USB support is integrated into several Atmosphere modules. This is an overlay on a pinned [Nexus Atmosphere](https://github.com/TR4UNEXUS/Nexus-Atmosphere) revision, not an independently installable KIP. `base.json` records that revision. The preparation tool checks it out and applies the included source before you build.

| Folder | Purpose |
| --- | --- |
| `overlay/libraries/libstratosphere/source/usb` | USB transport, storage and filesystem code |
| `overlay/stratosphere/ams_mitm/source/usb_mitm` | Shared USB service and content routing |
| `overlay/libraries/libstratosphere/source/ncm` | Content metadata, registration and database changes |
| `overlay/stratosphere/loader` | Game-loading integration |
| `overlay/tests/nexus-integration` | Host regression tests |
| `optional/loader-hoc-2.6.0` | Current HOC 2.6.0 Loader with Nexus USB support |
| `optional/loader-no-hoc`, `optional/loader-hoc` | Historical HS2 Loader alternatives |

The library namespace is included in full so its headers and implementation stay together. The base repository supplies the rest of Atmosphere, including the 23.5.0 version support, Nexus splash resources and build scripts. Preparation applies both `overlay` and `optional` so changes to either are included in the build tree.

## Build and contribute

See [BUILDING.md](BUILDING.md) and [CONTRIBUTING.md](CONTRIBUTING.md). Help with USB compatibility, testing, performance and build tooling is welcome.

For ready-to-use downloads, visit [Nexus USB HATS releases](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases). The [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source) contains the companion app.

## Licence

This code builds on Atmosphère. Original copyright notices and third-party licences are retained; see [LICENSE](LICENSE) and the notices in the source files.
