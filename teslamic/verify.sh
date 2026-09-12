#!/bin/bash
set -euo pipefail
failed=0
ok() { printf 'PASS: %s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*" >&2; failed=1; }
check_active() { systemctl is-active --quiet "$1" && ok "$1 active" || bad "$1 inactive"; }
command -v modinfo >/dev/null && modinfo usb_f_teslamic >/dev/null 2>&1 && ok "usb_f_teslamic installed" || bad "usb_f_teslamic missing"
G=/sys/kernel/config/usb_gadget/sentryusb
[ -d "$G/functions/teslamic.usb0" ] && ok "TeslaMic function exists" || bad "TeslaMic function missing"
[ -L "$G/configs/c.1/teslamic.usb0" ] && ok "TeslaMic linked before bind" || bad "TeslaMic link missing"
[ -L "$G/configs/c.1/mass_storage.0" ] && ok "mass storage linked" || bad "mass storage link missing"
[ "$(cat "$G/idVendor" 2>/dev/null)" = 0x1235 ] && [ "$(cat "$G/idProduct" 2>/dev/null)" = 0x0002 ] && ok "TeslaMic USB identity" || bad "unexpected USB identity"
udc=$(cat "$G/UDC" 2>/dev/null || true)
[ -n "$udc" ] && ok "UDC bound: $udc" || bad "UDC unbound"
[ -z "$udc" ] || { [ "$(cat "/sys/class/udc/$udc/state")" = configured ] && ok "host configured gadget" || bad "host has not configured gadget"; }
check_active bluetooth.service
check_active bluealsa.service
check_active teslamic-bluealsa.service
systemctl is-enabled --quiet bluealsa-aplay.service && bad "generic bluealsa-aplay must be disabled" || ok "generic bluealsa-aplay disabled"
BT_SHOW=$(bluetoothctl show 2>/dev/null || true)
grep -q 'Discoverable: no' <<<"$BT_SHOW" && ok "Bluetooth discoverability closed" || bad "Bluetooth remains discoverable"
grep -q 'Pairable: no' <<<"$BT_SHOW" && ok "Bluetooth pairability closed" || bad "Bluetooth remains pairable"
aplay -l 2>/dev/null | grep -q TeslaMic_Gadget && ok "gadget ALSA PCM exists" || bad "TeslaMic_Gadget ALSA PCM missing"
exit "$failed"
