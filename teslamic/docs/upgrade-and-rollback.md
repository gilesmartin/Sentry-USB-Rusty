# Upgrade and rollback

## Backups

The first install records each replaced or newly created path in:

```text
/var/backups/sentryusb-teslamic/<UTC timestamp>/manifest.tsv
```

`/var/lib/sentryusb-teslamic/active-backup` points to that original backup. Re-running the installer updates package files but deliberately retains the first pre-TeslaMic baseline.

The same backup directory also contains `service-states.tsv`, recording whether
Bluetooth, BlueALSA, the generic player, the TeslaMic bridge, and SentryUSB's
archive service were enabled before installation. Rollback restores those
enablement states instead of guessing defaults.

Backed-up integration includes the SentryUSB enable/disable wrappers, TeslaMic scripts and units, BlueALSA override, module-load configuration, Bluetooth configuration, and local TeslaMic configuration/serial if present.

## SentryUSB OTA updates

An upstream update may replace `/root/bin/enable_gadget.sh` and `disable_gadget.sh`. Recommended sequence:

1. eject all storage and stop playback;
2. run `teslamic/uninstall.sh` and reboot;
3. apply/verify the SentryUSB update;
4. install matching headers for the new kernel;
5. re-run `teslamic/install.sh` and reboot;
6. run `teslamic/verify.sh` and perform a host audio/storage test.

For an eventual upstream merge, SentryUSB's native gadget builder should gain an optional TeslaMic composition mode so wrapper replacement is unnecessary. This package intentionally keeps that invasive core change separate from the reproducible add-on.

## Rollback

```bash
sudo ./teslamic/uninstall.sh --dry-run
sudo ./teslamic/uninstall.sh
sudo reboot
```

The uninstaller disables the bridge, prepares the gadget for SentryUSB teardown, restores each path according to the manifest, removes the current-kernel module, runs `depmod`, and requests a reboot. It does not delete Bluetooth pairings or disk images.

After reboot, confirm ordinary SentryUSB mass storage works before deleting the backup directory.
