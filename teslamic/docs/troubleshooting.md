# Troubleshooting

## Separate the layers

1. **Physical/role:** correct data-capable cable and device-mode USB port; UDC exists.
2. **Enumeration:** host completes `SET_CONFIGURATION`.
3. **Class binding:** audio, HID, and storage drivers bind.
4. **Application:** Bluetooth audio actively reaches the gadget PCM.

## Pi-side checks

```bash
sudo ./teslamic/verify.sh
cat /sys/class/udc/*/state
cat /sys/class/udc/*/current_speed
/usr/local/sbin/teslamic-compose status
cat /proc/asound/cards
systemctl status bluetooth bluealsa teslamic-bluealsa sentryusb-archive
journalctl -b -k | grep -Ei 'usb|dwc|teslamic|stall|error'
```

`configured` is necessary but not sufficient. On Linux hosts, verify `bConfigurationValue`, `bNumInterfaces`, and interface nodes. Reading VID/PID with `lsusb` alone does not prove configuration.

## Signature: `can't set config #1, error -32`

This means the gadget stalled `SET_CONFIGURATION`. For the original module, the cause was a stale high-speed endpoint address after `usb_ep_autoconfig()`. Confirm the installed source contains both HS address synchronization assignments, rebuild for the running kernel, install under `/lib/modules/$(uname -r)/extra/`, run `depmod`, and reboot.

## Audio exists but is silent

- Confirm the phone is connected as an A2DP source.
- Start playback; an idle source legitimately reports not running and leaves the gadget PCM closed.
- Check bridge logs:

  ```bash
  journalctl -u teslamic-bluealsa.service -u bluealsa.service -b
  ```

- Confirm the bridge targets the dynamically discovered `TeslaMic_Gadget` card, not a fixed card number.
- Keep generic `bluealsa-aplay.service` disabled.

## Pairing fails repeatedly

Do not queue commands into an interactive agent while an authorization prompt is pending. Use `teslamic-pair`, which opens a bounded D-Bus agent and closes exposure automatically.

## Storage disappears

Run `teslamic-compose status`. `teslamic.usb0` must be linked before `mass_storage.0`. Confirm every original LUN file path remains populated. Unbind before changing links. If uncertain, roll back to original SentryUSB rather than mounting a backing file simultaneously on Pi and host.

## Kernel update

A module built for an old kernel will not load into a new one. Re-run `teslamic/install.sh` after installing matching headers, then reboot and verify. Never treat a release `.ko` as universal.
