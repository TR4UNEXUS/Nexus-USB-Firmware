# Building the USB integration

This source targets Nexus Atmosphere 1.12.2 HS3R1 and Switch system version 23.5.0. Install the Linux/WSL toolchain described in the [pinned core's build guide](https://github.com/TR4UNEXUS/Nexus-Atmosphere/blob/21084836f4b7e70f01026090cd6bf33fe7a7ea83/BUILDING.md): devkitPro devkitA64/devkitARM, libnx, deko3d, Switch tools, Make, Git and Python 3. The release used GCC 15.2.0 for both ARM toolchains.

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
make -C companions/usb-content-overlay -j4
```

Preparation checks out the full commit in `base.json`, then copies `overlay` and the historical `optional` source. It refuses an existing destination. Use `--destination <new-directory>` for another tree or `--source-repository <local-Nexus-Atmosphere-clone>` to use a local clone containing the pinned commit. Make changes in this repository's source folders or a development branch; preparation does not clean working trees.

The core target also builds the independent Nexus Loader. Package `fusee/out/nintendo_nx_arm_armv4t/release/package3` as `atmosphere/package3` and `stratosphere/nexus_loader/out/nintendo_nx_arm64_armv8a/release/nexus_loader.kip` as `atmosphere/kips/nexus-loader.kip`. Copy the overlay's `usb-content-status.ovl` to `switch/.overlays/`.

Both HOC and No-HOC need the same matching Nexus core and Loader. HOC is now separate: the HOC test pack adds the unmodified upstream HOC 2.6.1 release and its supporting files. Do not build an old combined HOC Loader from `optional`. Keep the Nexus boot entry when updating HOC and use a version compatible with your system firmware. See the prepared core's `BUILDING.md` for exact Hekate entries and dependency versions.

Boot through **Hekate → Launch** with `pkg3=atmosphere/package3` and `kip1=atmosphere/kips/nexus-loader.kip`. HOC additionally uses `kip1=atmosphere/kips/hoc.kip` and `secmon=atmosphere/exosphere.bin`. Direct Fusee boot does not load the external Nexus Loader in these test packs.

Run the new host checks from the prepared tree:

```sh
python3 tests/nexus-integration/run_hs3_frontend_tests.py
python3 tests/nexus-integration/run_hs3_sm_tests.py
python3 companions/usb-content-overlay/tests/run.py
python3 tests/nexus-integration/run_compressed_read_tests.py
```

The core guide lists the existing registration, mapping and sleep tests. G++ with address/undefined-behaviour sanitizers is required. These checks exercise production logic with instrumented services; they do not replace console testing of boot, game launches, wake or physical USB removal.

The public overlay Makefile was cross-built and the new portable tests passed. The firmware source matches the audited HS3R1 build inputs, apart from documented public branding/resource paths and whitespace cleanup. A full clean rebuild of this public checkout is not claimed. No generated firmware or personal data is published here. The source build does not assemble a complete HATS ZIP; see the pinned core guide for the other pack components.
