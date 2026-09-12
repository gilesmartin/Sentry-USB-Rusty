# Architecture

## Data path

```text
phone/tablet A2DP source
  -> BlueZ + BlueALSA A2DP sink
  -> bluealsa-aplay (dedicated bridge)
  -> TeslaMic_Gadget ALSA playback PCM
  -> u_audio isochronous USB IN endpoint
  -> vehicle USB microphone capture
```

The generic `bluealsa-aplay.service` is disabled. `teslamic-bluealsa.service` starts one dedicated bridge that discovers the ALSA card dynamically and waits until the UDC reports `configured`, avoiding continuous overruns while no USB host is present.

## Composite gadget

SentryUSB first creates `mass_storage.0` and its LUNs. The wrapper then unbinds the UDC, loads `usb_f_teslamic`, and relinks functions in this order:

1. `teslamic.usb0` -> interfaces 0-3
2. `mass_storage.0` -> interface 4
3. bind the UDC

Interface allocation order matters. TeslaMic provides UAC1 audio control/streaming, keyboard HID, and a vendor HID interface matching the reference descriptors. Storage remains owned by SentryUSB.

## Boot lifecycle

- `/etc/modules-load.d/teslamic-gadget.conf` loads the kernel function.
- `sentryusb-archive.service`/archiveloop invokes `/root/bin/enable_gadget.sh`.
- The installed wrapper calls the preserved original SentryUSB enable operation, then `teslamic-compose enable`.
- `bluetooth.service` starts the adapter; BlueZ `AutoEnable=true` keeps it powered after boot.
- `bluealsa.service` advertises A2DP sink/source profiles.
- `teslamic-bluealsa.service` waits for the configured gadget and launches the dedicated bridge.

Configfs objects are ephemeral; successful runtime setup without this boot path will disappear on reboot.

## Failure safety

`teslamic-compose` traps enable failures and restores a mass-storage-only gadget when possible. The installer keeps an original-file backup manifest. The uninstaller restores those files rather than trying to reconstruct them.
