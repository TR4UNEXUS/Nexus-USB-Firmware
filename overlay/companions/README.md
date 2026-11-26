# Nexus companion source

Nexus USB Content 1.3.3 displays drive capacity, install routing, safe eject and reconnect. The three NX1 editions share its menu and notification assets.

Run the following from the Atmosphere source root:

```sh
sh companions/usb-content-overlay/notifications/prepare-library.sh
make -C companions/usb-content-overlay -j4
python3 companions/usb-content-overlay/tests/run.py
python3 tests/nexus-integration/run_hs3_frontend_tests.py
python3 tests/nexus-integration/run_eject_owner_tests.py
python3 tests/nexus-integration/run_usb_notification_tests.py
```

From `companions/usb-content-overlay/out/`, copy both `switch/` and `config/` to the SD root. All three editions use the four `ams-usb*.rgba` badges under `config/ultrahand/assets/notifications/`. The HATS packs supply Ultrahand 2.5.4, its notification sound and the enabled notification defaults. The user's Ultrahand mute setting applies.
