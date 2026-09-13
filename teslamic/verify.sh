#!/bin/bash
set -euo pipefail
failed=0
ok() { printf 'PASS: %s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*" >&2; failed=1; }
check_active() {
    if systemctl is-active --quiet "$1"; then ok "$1 active"; else bad "$1 inactive"; fi
}
if command -v modinfo >/dev/null && modinfo usb_f_teslamic >/dev/null 2>&1; then
    ok "usb_f_teslamic installed"
else
    bad "usb_f_teslamic missing"
fi
G=/sys/kernel/config/usb_gadget/sentryusb
if [ -d "$G/functions/teslamic.usb0" ]; then ok "TeslaMic function exists"; else bad "TeslaMic function missing"; fi
if [ -L "$G/configs/c.1/teslamic.usb0" ]; then ok "TeslaMic linked before bind"; else bad "TeslaMic link missing"; fi
if [ -L "$G/configs/c.1/mass_storage.0" ]; then ok "mass storage linked"; else bad "mass storage link missing"; fi
if [ "$(cat "$G/idVendor" 2>/dev/null)" = 0x1235 ] && [ "$(cat "$G/idProduct" 2>/dev/null)" = 0x0002 ]; then
    ok "TeslaMic USB identity"
else
    bad "unexpected USB identity"
fi
udc=$(cat "$G/UDC" 2>/dev/null || true)
if [ -n "$udc" ]; then
    ok "UDC bound: $udc"
    if [ "$(cat "/sys/class/udc/$udc/state")" = configured ]; then
        ok "host configured gadget"
    else
        bad "host has not configured gadget"
    fi
else
    bad "UDC unbound"
fi
check_active bluetooth.service
check_active bluealsa.service
check_active teslamic-bluealsa.service
check_active sentryusb-teslamic-guard.service
if grep -q 'sentryusb-teslamic/original-enable-gadget.sh' /root/bin/enable_gadget.sh 2>/dev/null \
    && grep -q 'sentryusb-teslamic/original-disable-gadget.sh' /root/bin/disable_gadget.sh 2>/dev/null; then
    ok "TeslaMic gadget wrappers installed"
else
    bad "TeslaMic gadget wrappers were replaced"
fi
if grep -qx 'export SKIP_READONLY=true' /root/sentryusb.conf 2>/dev/null \
    && findmnt -no OPTIONS / | tr ',' '\n' | grep -qx rw; then
    ok "writable-root safeguard active"
else
    bad "writable-root safeguard missing"
fi
if systemctl is-enabled --quiet bluealsa-aplay.service; then
    bad "generic bluealsa-aplay must be disabled"
else
    ok "generic bluealsa-aplay disabled"
fi
BT_SHOW=$(bluetoothctl show 2>/dev/null || true)
if grep -q 'Discoverable: no' <<<"$BT_SHOW"; then ok "Bluetooth discoverability closed"; else bad "Bluetooth remains discoverable"; fi
if grep -q 'Pairable: no' <<<"$BT_SHOW"; then ok "Bluetooth pairability closed"; else bad "Bluetooth remains pairable"; fi
if aplay -l 2>/dev/null | grep -q TeslaMic_Gadget; then ok "gadget ALSA PCM exists"; else bad "TeslaMic_Gadget ALSA PCM missing"; fi
exit "$failed"
