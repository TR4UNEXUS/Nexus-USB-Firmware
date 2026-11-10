# Building the USB integration

Install the toolchain described in [Nexus Atmosphere's build instructions](https://github.com/TR4UNEXUS/Nexus-Atmosphere/blob/main/BUILDING.md). Use Linux or WSL with Git and Python 3.

```sh
git clone https://github.com/TR4UNEXUS/Nexus-USB-Firmware.git
cd Nexus-USB-Firmware
python3 tools/prepare.py
cd build/Atmosphere
export DEVKITPRO=/opt/devkitpro
export DEVKITA64="$DEVKITPRO/devkitA64"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITARM/bin:$PATH"
make -j2 nx_release
```

The preparation step checks out the exact commit in `base.json`, then copies this repository's overlay over it. It refuses to replace an existing destination. Keep local changes in the overlay or in your own development branch; the tool never cleans a working tree for you.

Run the host checks from the prepared tree:

```sh
python3 tests/nexus-integration/run_u2_registration_tests.py
python3 tests/nexus-integration/run_u1_metadata_routing_tests.py
python3 tests/nexus-integration/run_db2_paged_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py --out build/host-tests/sleep
```

The host checks use instrumented backends. They do not replace Switch testing. The initial source publication does not claim a new clean cross-build or a new hardware validation run.

Update `base.json` deliberately when rebasing onto a newer core. Keep Loader, service and database changes together. A plain upstream HOC KIP must not replace the matching separated HOC helper; see the optional HOC notes in the base repository.
