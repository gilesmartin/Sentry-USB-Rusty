# SentryUSB TeslaMic + Bluetooth bridge

This optional SentryUSB component adds a TeslaMic-compatible USB Audio/HID function before the existing mass-storage function. A paired phone or tablet can stream A2DP audio through BlueALSA into the gadget-side ALSA playback PCM; the vehicle receives it as USB microphone input while SentryUSB storage remains available.

## Validated configuration

- Raspberry Pi 5 running SentryUSB
- Linux 6.18 Raspberry Pi kernel with matching build headers
- High-speed composite USB gadget: TeslaMic interfaces 0-3, mass storage interface 4
- Four existing SentryUSB LUNs retained
- Bluetooth A2DP source -> BlueALSA -> `TeslaMic_Gadget` -> USB capture
- Linux host and macOS host enumeration
- Physical disconnect, reboot, automatic gadget recreation, storage, and audio

`kernel/` contains the captured working source plus the review-driven EP0 bounds.
The hardened source builds successfully against the validated Pi kernel but has
not yet replaced the working live module. See `docs/validation.md` for both sets
of hashes and the remaining controlled runtime-validation gate. Compiled `.ko`
files are deliberately not committed because they are kernel-specific.

## Install on an existing SentryUSB Pi

1. Back up SentryUSB and eject its USB volumes from the vehicle/host.
2. Ensure `/lib/modules/$(uname -r)/build` exists and the root filesystem is writable.
3. Install prerequisites if absent: compiler/make, matching kernel headers, `bluez-alsa-utils`, `python3-dbus`, and `python3-gi`.
4. Run:

   ```bash
   sudo ./teslamic/install.sh --dry-run
   sudo ./teslamic/install.sh
   sudo reboot
   sudo ./teslamic/verify.sh
   ```

The installer creates one timestamped backup and manifest under `/var/backups/sentryusb-teslamic/`. It preserves the original SentryUSB gadget wrappers and does not modify disk-image contents.

## Pair an audio source

```bash
sudo teslamic-pair
```

The default pairing window is 180 seconds and closes automatically. List or manage devices with:

```bash
teslamic-devices list
sudo teslamic-devices disconnect DEVICE_ADDRESS
sudo teslamic-devices remove DEVICE_ADDRESS
```

Only one source should stream at a time; the bridge uses `--single-audio`.

## Roll back

```bash
sudo ./teslamic/uninstall.sh --dry-run
sudo ./teslamic/uninstall.sh
sudo reboot
```

See `docs/upgrade-and-rollback.md` before a SentryUSB OTA update.

## Vehicle compatibility

The working deployment was validated end-to-end with macOS and its target vehicle. `docs/deployment-model-x-intel.md` documents applying the same package to a separate **2018 Model X with Intel MCU**, including the required in-vehicle validation. The car's infotainment processor does not change how the Pi module is compiled, but vehicle software may differ in USB-audio behavior; do not skip that test.

## Documentation

- `docs/architecture.md` — components, lifecycle, and interface order
- `docs/history-and-debugging.md` — complete investigation and root cause
- `docs/deployment-model-x-intel.md` — second-car installation runbook
- `docs/pairing.md` — secure bounded Bluetooth pairing
- `docs/wifi-profile.md` — adding the car Wi-Fi profile
- `docs/troubleshooting.md` — USB, audio, Bluetooth, and boot diagnosis
- `docs/validation.md` — tested source/build hashes and validation matrix
- `docs/upgrade-and-rollback.md` — backups, SentryUSB updates, and removal

## Status

This is an optional feature package suitable for a SentryUSB feature branch or pull request. It does not claim universal Tesla compatibility. The original FunctionFS prototype is not a supported install path.
