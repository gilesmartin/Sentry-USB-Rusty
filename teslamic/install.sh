#!/bin/bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=/
DRY_RUN=0
ACTIVATE=0
usage() { echo "usage: $0 [--dry-run] [--activate] [--root DIR]"; }
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --activate) ACTIVATE=1 ;;
        --root) shift; ROOT=${1:?--root requires a directory} ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done
ROOT=${ROOT%/}; [ -n "$ROOT" ] || ROOT=/
case "$ROOT" in /*) ;; *) echo "--root directory must be absolute" >&2; exit 2 ;; esac
rpath() { [ "$ROOT" = / ] && printf '%s' "$1" || printf '%s%s' "$ROOT" "$1"; }

[ "$ROOT" != / ] || [ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
STATE=$(rpath /var/lib/sentryusb-teslamic)
BACKUP_ROOT=$(rpath /var/backups/sentryusb-teslamic)
ACTIVE="$STATE/active-backup"

if [ "$DRY_RUN" = 1 ]; then
    echo "Would preflight, build the current-kernel module, back up SentryUSB integration files, install services/scripts, and enable boot units."
    exit 0
fi

if [ "$ROOT" = / ]; then
    for cmd in make install modprobe depmod python3 systemctl bluetoothctl bluealsa bluealsa-aplay sentryusb; do
        command -v "$cmd" >/dev/null || { echo "missing prerequisite: $cmd" >&2; exit 1; }
    done
    python3 -c 'import dbus; from gi.repository import GLib' 2>/dev/null || {
        echo "missing Python Bluetooth dependencies: install python3-dbus and python3-gi" >&2
        exit 1
    }
    KERNEL=$(uname -r)
    [ -d "/lib/modules/$KERNEL/build" ] || {
        echo "missing matching headers: /lib/modules/$KERNEL/build" >&2
        echo "Install the headers for $KERNEL, then retry." >&2
        exit 1
    }
    [ -d /sys/kernel/config/usb_gadget ] || { echo "configfs USB gadget tree is unavailable" >&2; exit 1; }
    compgen -G '/sys/class/udc/*' >/dev/null || { echo "no USB device controller found" >&2; exit 1; }
    if ! touch /etc/.teslamic-write-test; then
        echo "root filesystem is not writable; remount it rw before installation" >&2
        exit 1
    fi
    if ! rm -f /etc/.teslamic-write-test; then
        echo "could not remove root-filesystem write test" >&2
        exit 1
    fi
fi

mkdir -p "$STATE" "$BACKUP_ROOT"
if [ ! -s "$ACTIVE" ]; then
    stamp=$(date -u +%Y%m%dT%H%M%SZ)
    backup="$BACKUP_ROOT/$stamp"
    mkdir -p "$backup/files"
    manifest="$backup/manifest.tsv"
    : >"$manifest"
    for path in \
        /root/bin/enable_gadget.sh \
        /root/bin/disable_gadget.sh \
        /usr/local/sbin/teslamic-compose \
        /usr/local/sbin/teslamic-bluealsa-bridge \
        /usr/local/sbin/teslamic-pair \
        /usr/local/sbin/teslamic-devices \
        /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh \
        /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh \
        /etc/systemd/system/teslamic-bluealsa.service \
        /etc/systemd/system/bluealsa.service.d/teslamic.conf \
        /etc/modules-load.d/teslamic-gadget.conf \
        /etc/teslamic-gadget.conf \
        /etc/teslamic-gadget.serial \
        /etc/bluetooth/main.conf; do
        actual=$(rpath "$path")
        rel=${path#/}
        if [ -e "$actual" ] || [ -L "$actual" ]; then
            mkdir -p "$backup/files/$(dirname "$rel")"
            cp -a "$actual" "$backup/files/$rel"
            printf 'present\t%s\n' "$path" >>"$manifest"
        else
            printf 'missing\t%s\n' "$path" >>"$manifest"
        fi
    done
    if [ "$ROOT" = / ]; then
        path="/lib/modules/$KERNEL/extra/usb_f_teslamic.ko"
        actual="$path"
        rel=${path#/}
        if [ -e "$actual" ]; then
            mkdir -p "$backup/files/$(dirname "$rel")"
            cp -a "$actual" "$backup/files/$rel"
            printf 'present\t%s\n' "$path" >>"$manifest"
        else
            printf 'missing\t%s\n' "$path" >>"$manifest"
        fi
    fi
    service_states="$backup/service-states.tsv"
    : >"$service_states"
    if [ "$ROOT" = / ]; then
        for service in bluetooth.service bluealsa.service bluealsa-aplay.service teslamic-bluealsa.service sentryusb-archive.service; do
            state=$(systemctl is-enabled "$service" 2>/dev/null || true)
            printf '%s\t%s\n' "$service" "${state:-not-found}" >>"$service_states"
        done
    fi
    printf '%s\n' "$backup" >"$ACTIVE"
else
    backup=$(cat "$ACTIVE")
    manifest="$backup/manifest.tsv"
    service_states="$backup/service-states.tsv"
    [ -s "$manifest" ] || { echo "active backup manifest missing: $manifest" >&2; exit 1; }
fi

install -d "$(rpath /usr/local/libexec/sentryusb-teslamic)"
if [ ! -e "$(rpath /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh)" ]; then
    if grep -q 'teslamic-compose' "$(rpath /root/bin/enable_gadget.sh)"; then
        cat >"$(rpath /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh)" <<'ORIGINAL'
#!/bin/bash -eu
sentryusb gadget enable "$@"
ORIGINAL
    else
        cp -a "$(rpath /root/bin/enable_gadget.sh)" "$(rpath /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh)"
    fi
    if grep -q 'teslamic-compose' "$(rpath /root/bin/disable_gadget.sh)"; then
        cat >"$(rpath /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh)" <<'ORIGINAL'
#!/bin/bash -eu
sentryusb gadget disable "$@"
ORIGINAL
    else
        cp -a "$(rpath /root/bin/disable_gadget.sh)" "$(rpath /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh)"
    fi
    chmod 0755 \
        "$(rpath /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh)" \
        "$(rpath /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh)"
fi

install -Dm755 "$HERE/scripts/teslamic-compose" "$(rpath /usr/local/sbin/teslamic-compose)"
install -Dm755 "$HERE/scripts/teslamic-bluealsa-bridge" "$(rpath /usr/local/sbin/teslamic-bluealsa-bridge)"
install -Dm755 "$HERE/scripts/teslamic-pair" "$(rpath /usr/local/sbin/teslamic-pair)"
install -Dm755 "$HERE/scripts/teslamic-devices" "$(rpath /usr/local/sbin/teslamic-devices)"
install -Dm644 "$HERE/systemd/teslamic-bluealsa.service" "$(rpath /etc/systemd/system/teslamic-bluealsa.service)"
install -Dm644 "$HERE/systemd/bluealsa.service.d/teslamic.conf" "$(rpath /etc/systemd/system/bluealsa.service.d/teslamic.conf)"
[ -e "$(rpath /etc/teslamic-gadget.conf)" ] || install -Dm644 "$HERE/config/teslamic-gadget.conf.example" "$(rpath /etc/teslamic-gadget.conf)"
install -d "$(rpath /etc/modules-load.d)" "$(rpath /root/bin)"
printf 'usb_f_teslamic\n' >"$(rpath /etc/modules-load.d/teslamic-gadget.conf)"

cat >"$(rpath /root/bin/enable_gadget.sh)" <<'WRAPPER'
#!/bin/bash -eu
/usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh "$@"
/usr/local/sbin/teslamic-compose enable
WRAPPER
cat >"$(rpath /root/bin/disable_gadget.sh)" <<'WRAPPER'
#!/bin/bash -eu
/usr/local/sbin/teslamic-compose prepare-disable
exec /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh "$@"
WRAPPER
chmod 0755 "$(rpath /root/bin/enable_gadget.sh)" "$(rpath /root/bin/disable_gadget.sh)"

MAIN_CONF=$(rpath /etc/bluetooth/main.conf)
if [ -f "$MAIN_CONF" ]; then
    if grep -q '^#AutoEnable=true' "$MAIN_CONF"; then
        sed -i 's/^#AutoEnable=true/AutoEnable=true/' "$MAIN_CONF"
    elif ! grep -q '^AutoEnable=true' "$MAIN_CONF"; then
        printf '\n[Policy]\nAutoEnable=true\n' >>"$MAIN_CONF"
    fi
fi

if [ "$ROOT" = / ]; then
    make -C "$HERE/kernel" clean all
    install -Dm644 "$HERE/kernel/usb_f_teslamic.ko" "/lib/modules/$KERNEL/extra/usb_f_teslamic.ko"
    depmod -a "$KERNEL"
    systemctl daemon-reload
    systemctl disable --now bluealsa-aplay.service 2>/dev/null || true
    systemctl enable bluetooth.service bluealsa.service teslamic-bluealsa.service sentryusb-archive.service
    if [ "$ACTIVATE" = 1 ]; then
        systemctl restart bluetooth.service bluealsa.service teslamic-bluealsa.service
        /root/bin/disable_gadget.sh || true
        /root/bin/enable_gadget.sh
        "$HERE/verify.sh"
    else
        echo "Installed. Reboot to load the module and recreate the composite gadget."
    fi
fi
printf 'Backup and rollback manifest: %s\n' "$manifest"
