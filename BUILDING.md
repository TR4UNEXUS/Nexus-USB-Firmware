# Building the USB integration

This source targets Nexus Atmosphere 1.12.2 NX1 and Switch system version 23.5.0. Install the Linux/WSL toolchain described in the [pinned core's build guide](https://github.com/TR4UNEXUS/Nexus-Atmosphere/blob/941fda1145e3c181e1b7a59edb9c8b16e8be846e/BUILDING.md): devkitPro devkitA64/devkitARM, libnx, deko3d, Switch tools, Make, Git and Python 3. The release used GCC 15.2.0 for both ARM toolchains.

```sh
git clone https://github.com/TR4UNEXUS/Nexus-USB-Firmware.git
cd Nexus-USB-Firmware
python3 tools/prepare.py
cd build/Atmosphere
export DEVKITPRO=/opt/devkitpro
export DEVKITA64="$DEVKITPRO/devkitA64"
export LIBNX="$DEVKITPRO/libnx"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
JOBS=4 sh scripts/build-nexus.sh
sh companions/usb-content-overlay/notifications/prepare-library.sh
make -C companions/usb-content-overlay -j4
```

Preparation checks out the full commit in `base.json`, then copies `overlay` and the historical `optional` source. It refuses an existing destination. Use `--destination <new-directory>` for another tree or `--source-repository <local-Nexus-Atmosphere-clone>` to use a local clone containing the pinned commit. Make changes in this repository's source folders or a development branch; preparation does not clean working trees.

The core target also builds the independent Nexus Loader. Package `fusee/out/nintendo_nx_arm_armv4t/release/package3` as `atmosphere/package3` and `stratosphere/nexus_loader/out/nintendo_nx_arm64_armv8a/release/nexus_loader.kip` as `atmosphere/kips/nexus-loader.kip`. Install the overlay build's `out/switch/` and `out/config/` on the SD root. The config folder contains the notification artwork.

Both HOC and No-HOC need the same matching Nexus core and Loader. HOC is now separate: the HOC pack adds the unmodified upstream HOC 2.6.1 release and its supporting files. Do not build an old combined HOC Loader from `optional`. Keep the Nexus boot entry when updating HOC and use a version compatible with your system firmware. See the prepared core's `BUILDING.md` for exact Hekate entries and dependency versions.

The three NX1 packs load these files through their CFW (emuMMC) and CFW (sysMMC) entries:

```ini
pkg3=atmosphere/package3
pkg3kip1skip=ams_mitm
kip1=atmosphere/kips/nexus-loader.kip
kip1=bootloader/nexus-eject/ams-mitm.kip
```

The replacement service comes from `stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip`. Both HOC editions keep their existing HOC and exosphere entries. The stock entry uses its original configuration.

Run the new host checks from the prepared tree:

```sh
python3 tests/nexus-integration/run_hs3_frontend_tests.py
python3 tests/nexus-integration/run_hs3_sm_tests.py
python3 companions/usb-content-overlay/tests/run.py
python3 tests/nexus-integration/run_eject_owner_tests.py
python3 tests/nexus-integration/run_usb_notification_tests.py
python3 tests/nexus-integration/run_compressed_read_tests.py
```

The core guide lists the existing registration, mapping and sleep tests. G++ with address/undefined-behaviour sanitizers is required. These checks exercise production logic with instrumented services; they do not replace console testing of boot, game launches, wake or physical USB removal.

The public overlay Makefile was cross-built and the new portable tests passed. The firmware source matches the NX1 USB build inputs, apart from documented public branding/resource paths and whitespace cleanup. A full clean rebuild of this public checkout is not claimed. No generated firmware or personal data is published here. The source build does not assemble a complete HATS ZIP; see the pinned core guide for the other pack components.

## Regression checks and themed variant

From the prepared tree, run:

```sh
python3 tests/nexus-integration/run_homebrew_override_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py
python3 tests/nexus-integration/run_usb_map_backup_tests.py
g++ -std=c++20 -O2 -fsanitize=undefined -I libraries/libstratosphere/include tests/nexus-integration/usb_report_completion_tests.cpp -o /tmp/nexus-completion-test
/tmp/nexus-completion-test
```

Map-backup tests also need OpenSSL development headers. The successful same-drive wake path remains in place; failed-wake rediscovery deliberately invalidates stale handles. These host checks do not replace a physical console run.

For the themed HOC appearance, copy `variants/nexus-themed/` over the prepared source root before building. The themed boot module includes the Nexus JPEG and the TJpgDec decoder. The three pack variants use the same USB changes; HOC and its supported system versions remain independent requirements.
