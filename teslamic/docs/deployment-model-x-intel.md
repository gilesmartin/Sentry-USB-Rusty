# Deployment: 2018 Model X with Intel MCU

This runbook applies the validated SentryUSB TeslaMic package to the separate SentryUSB installation intended for a **2018 Model X with Intel processor/MCU**.

## Compatibility boundary

The Intel MCU is on the USB-host side. The Pi still builds the module for its own Linux kernel; do not copy a `.ko` from another Pi unless kernel release, architecture, and vermagic match exactly. Use the source installer on the second Pi.

USB Audio Class 1 is broadly supported, but vehicle firmware, region, and Caraoke behavior can differ. This deployment therefore requires final in-vehicle validation and should not be described as proven for the 2018 Model X until that test passes.

## Preflight on the second SentryUSB

1. Update/back up SentryUSB using its normal process.
2. Confirm ordinary SentryUSB storage works in the Model X before adding TeslaMic.
3. Record:

   ```bash
   uname -r
   cat /proc/device-tree/model
   ls /sys/class/udc
   systemctl is-enabled sentryusb-archive.service
   ```

4. Confirm matching headers exist:

   ```bash
   test -d "/lib/modules/$(uname -r)/build"
   ```

5. Ensure root is writable for installation. Ordinary installation keeps it
   writable. Use the explicit `--read-only-root` option only after mapping
   Model X-specific NetworkManager, modem, hotspot, and SSH state to
   persistent storage.
6. Copy or clone the repository onto the Pi. Do not transfer Bluetooth pairing databases, `/etc/machine-id`, generated USB serials, SSH keys, or a module built on the first Pi.

## Install

```bash
cd Sentry-USB-Rusty
sudo ./teslamic/install.sh --dry-run
sudo ./teslamic/install.sh
sudo reboot
sudo ./teslamic/verify.sh
```

For an already-audited appliance, substitute `sudo ./teslamic/install.sh
--read-only-root`. Do not use that shortcut as proof that modem/hotspot state
is persistent; reboot-test those services separately.

The installer builds against the second Pi's kernel, generates that host's USB serial on first gadget composition, and preserves its own original SentryUSB wrappers.

## Bench validation before the car

Use a Linux or macOS USB host and confirm:

- USB host completes configuration.
- TeslaMic audio input appears.
- All expected SentryUSB volumes appear.
- A paired phone can stream audio into a recording application.
- Disconnect/reboot restores the same state.

## In-vehicle validation

1. Eject every SentryUSB volume from the bench host.
2. Power down/disconnect cleanly; reconnect the Pi to the Model X data-capable USB port.
3. Allow the Pi to boot fully.
4. Confirm dashcam/storage behavior first.
5. Confirm the Tesla microphone/Caraoke source appears.
6. Pair one phone with `sudo teslamic-pair`, select SentryUSB as its audio output, and play known audio.
7. Verify audio in the vehicle and make a short Sentry recording.
8. Power-cycle the vehicle USB connection and repeat.

If storage works but audio does not, preserve storage and collect evidence before changing descriptors. If the USB device never configures, use the rollback procedure rather than repeatedly modifying the live gadget.
