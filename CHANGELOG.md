# Changes

## 1.12.2 HS3R1 — 21 November 2026 (test)

- Moved Nexus USB game-loading hooks to a separate Loader. The stock Loader remains replaceable by unmodified upstream HOC; both packs use the same Nexus core and USB Loader.
- Fixed the HS3 boot conflict where two Loaders competed for a single filesystem service session. Nexus owns the native session and serves backend filesystem requests on a dedicated thread.
- Preserved service access checks, external-code handling, opaque Loader pins and debugger/module queries used by tools such as SaltyNX. Hardware compatibility still needs testing.
- Added Nexus USB Content 1.3.0 source, its UI dependency and host tests. The revised menu shows drive details and free/used/total space, with safe eject and reconnect. Drive information no longer depends on the eject heartbeat; bottom diagnostics are removed.
- Retained the N4R2 compressed-read, USB install, mapping-backup and wake-recovery fixes.
- HOC test pack references unchanged upstream HOC 2.6.1. No-HOC omits HOC. Both require the supplied Hekate Launch setup and include Manager 0.2.24.

Firmware builds and host checks passed. These remain test packs pending full on-console validation.

## 1.12.2 N4R2 — 20 November 2026

- Fixed compressed game-data reads crossing a full buffer. The shared reader no longer advances past a block, addressing the Silksong launch failure and the same path in other games.
- Fixed pending USB mapping writes during content-storage shutdown and backup failure unwinding, addressing the USB-to-USB install abort.
- Expanded mapping capacity to 4,096 title rows and 262,144 content entries with paged storage. These are bounded limits, not unlimited storage.
- Added automatic checked routing-index backups in a dedicated console system save, including existing valid maps at startup. Game data, tickets and saves are not included. Manager can preview restoration of missing links.
- Added a bounded small-read cache with invalidation on writes, disconnects and wake, and improved read access while writes are queued. Hardware speed gains have not been measured.
- Improved XCI inspection for trimmed files and optional leading key areas. This does not mount an XCI as a cartridge.
- Included the N2 boot-module isolation and N3 cheat-service error handling changes, and the current matching HOC 2.6.0 Loader source.
- Standard packs use the original Atmosphere splash and official Hekate appearance while retaining the Nexus build version.

The no-HOC N4R2 firmware and Manager 0.2.24 passed the reported Switch test, including Silksong gameplay. The HOC variant was rebuilt and binary-checked but still needs its own console test. Host regression tests cover the repaired compressed-read path; they do not establish that every game, cheat or system module works.

## Nexus Atmosphere 1.12.2 N1 / Switch 23.5.0

- Updated the pinned core to Nexus Atmosphere 1.12.2 N1, with basic Switch 23.5.0 support.
- Refreshed the USB service, storage library, content database, Loader and tests to match that source revision.
- Retained the USB write improvements, SL7 sleep/wake recovery, paged database and fixes that keep SD and internal-storage installs on their selected destination.
- Added the matching HOC 2.6.0 Loader source alongside the default USB-only Loader.
- Updated preparation to include optional Loader changes, and documented both build variants.

This is a source update. Ready-to-use firmware packs are available from [Nexus USB HATS releases](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases).
