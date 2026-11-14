# Building the USB integration

This snapshot targets Nexus Atmosphere 1.12.2 N1 and Switch system version 23.5.0. Install the toolchain described in [Nexus Atmosphere's build instructions](https://github.com/TR4UNEXUS/Nexus-Atmosphere/blob/c246c2ee3866c4897cb5741f1cbf5026a1000582/BUILDING.md). Use Linux or WSL with Git and Python 3. The release firmware used GCC 15.2.0 from devkitPro for both ARM toolchains.

```sh
git clone https://github.com/TR4UNEXUS/Nexus-USB-Firmware.git
cd Nexus-USB-Firmware
python3 tools/prepare.py
cd build/Atmosphere
export DEVKITPRO=/opt/devkitpro
JOBS=2 sh scripts/build-nexus.sh
```

The preparation step checks out the exact commit in `base.json`, then copies this repository's `overlay` and `optional` source over it. It refuses to replace an existing destination. Keep local changes in these source folders or in your own development branch; the tool never cleans a working tree for you. Use `--destination <new-directory>` for another prepared tree, or `--source-repository <local-Nexus-Atmosphere-clone>` to reuse an existing clone that contains the pinned commit.

The default build uses the USB-only Loader. After building the core, build the matching HOC variant from the same prepared tree:

```sh
JOBS=2 sh scripts/build-nexus.sh hoc
```

The core output is `fusee/out/nintendo_nx_arm_armv4t/release/package3`. The optional HOC output is `optional/loader-hoc-2.6.0/out/nintendo_nx_arm64_armv8a/release/loader.kip`. The HOC source has generic defaults; personal overclock settings are not included. The older `optional/loader-no-hoc` and `optional/loader-hoc` folders preserve the HS2 separation experiment and are not the current release's Loader selection.

Run the host checks from the prepared tree:

```sh
python3 tests/nexus-integration/run_u2_registration_tests.py
python3 tests/nexus-integration/run_u1_metadata_routing_tests.py
python3 tests/nexus-integration/run_u1_ownership_tests.py
python3 tests/nexus-integration/run_db2_paged_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py --out build/host-tests/sleep
```

The host checks use instrumented backends. They do not replace Switch testing. This source update matches the published Nexus Atmosphere revision; it does not claim a new clean cross-build or a new hardware validation run.

Update `base.json` deliberately when rebasing onto a newer core. Keep Loader, service and database changes together. Use the matching Nexus HOC Loader when assembling an HOC pack; an unrelated HOC KIP can replace the USB hooks. See the prepared tree's `BUILDING.md` for the separate Hekate, overlay and other files needed for a complete pack. This repository produces source builds, not a complete HATS ZIP.
