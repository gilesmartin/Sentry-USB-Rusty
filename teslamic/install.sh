#!/bin/bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=/
DRY_RUN=0
ACTIVATE=0
READ_ONLY_ROOT=0
usage() { echo "usage: $0 [--dry-run] [--activate] [--read-only-root] [--root DIR]"; }
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --activate) ACTIVATE=1 ;;
        --read-only-root) READ_ONLY_ROOT=1 ;;
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

if [ "$READ_ONLY_ROOT" = 1 ]; then
    picker_source="$HERE/../pi-gen-sources/00-sentryusb-tweaks/files/sentryusb-pick-binary"
    [ -f "$picker_source" ] || { echo "missing read-only-safe SentryUSB binary picker" >&2; exit 1; }
    if [ "$ROOT" = / ] && ! systemctl cat systemd-resolved.service >/dev/null 2>&1; then
        echo "systemd-resolved is required for read-only resolver state" >&2
        exit 1
    fi
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
    manifest_new="$manifest.new"
    rm -f "$manifest_new"
    : >"$manifest_new"
    for path in \
        /root/bin/enable_gadget.sh \
        /root/bin/disable_gadget.sh \
        /usr/local/sbin/teslamic-compose \
        /usr/local/sbin/teslamic-bluealsa-bridge \
        /usr/local/sbin/teslamic-pair \
        /usr/local/sbin/teslamic-devices \
        /usr/local/sbin/sentryusb-readonly-root \
        /usr/local/libexec/sentryusb-teslamic/teslamic-ensure-integration \
        /usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh \
        /usr/local/libexec/sentryusb-teslamic/original-disable-gadget.sh \
        /etc/systemd/system/teslamic-bluealsa.service \
        /etc/systemd/system/sentryusb-teslamic-guard.service \
        /etc/systemd/system/sentryusb-archive.service.d/teslamic.conf \
        /etc/systemd/system/bluealsa.service.d/teslamic.conf \
        /etc/modules-load.d/teslamic-gadget.conf \
        /etc/teslamic-gadget.conf \
        /etc/teslamic-gadget.serial \
        /etc/fstab \
        /boot/firmware/cmdline.txt \
        /root/sentryusb.conf \
        /etc/resolv.conf \
        /usr/local/bin/sentryusb-pick-binary \
        /etc/bluetooth/main.conf; do
        actual=$(rpath "$path")
        rel=${path#/}
        if [ -e "$actual" ] || [ -L "$actual" ]; then
            mkdir -p "$backup/files/$(dirname "$rel")"
            cp -a "$actual" "$backup/files/$rel"
            printf 'present\t%s\n' "$path" >>"$manifest_new"
        else
            printf 'missing\t%s\n' "$path" >>"$manifest_new"
        fi
    done
    if [ "$ROOT" = / ]; then
        path="/lib/modules/$KERNEL/extra/usb_f_teslamic.ko"
        actual="$path"
        rel=${path#/}
        if [ -e "$actual" ]; then
            mkdir -p "$backup/files/$(dirname "$rel")"
            cp -a "$actual" "$backup/files/$rel"
            printf 'present\t%s\n' "$path" >>"$manifest_new"
        else
            printf 'missing\t%s\n' "$path" >>"$manifest_new"
        fi
    fi
    service_states="$backup/service-states.tsv"
    service_states_new="$service_states.new"
    rm -f "$service_states_new"
    : >"$service_states_new"
    if [ "$ROOT" = / ]; then
        for service in bluetooth.service bluealsa.service bluealsa-aplay.service teslamic-bluealsa.service sentryusb-teslamic-guard.service sentryusb-archive.service; do
            state=$(systemctl is-enabled "$service" 2>/dev/null || true)
            printf '%s\t%s\n' "$service" "${state:-not-found}" >>"$service_states_new"
        done
    fi
    mv "$manifest_new" "$manifest"
    mv "$service_states_new" "$service_states"
    active_new="$ACTIVE.new"
    printf '%s\n' "$backup" >"$active_new"
    mv "$active_new" "$ACTIVE"
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
install -Dm755 "$HERE/scripts/sentryusb-readonly-root" "$(rpath /usr/local/sbin/sentryusb-readonly-root)"
install -Dm755 "$HERE/scripts/teslamic-ensure-integration" "$(rpath /usr/local/libexec/sentryusb-teslamic/teslamic-ensure-integration)"
install -Dm644 "$HERE/systemd/teslamic-bluealsa.service" "$(rpath /etc/systemd/system/teslamic-bluealsa.service)"
install -Dm644 "$HERE/systemd/sentryusb-teslamic-guard.service" "$(rpath /etc/systemd/system/sentryusb-teslamic-guard.service)"
install -Dm644 "$HERE/systemd/sentryusb-archive.service.d/teslamic.conf" "$(rpath /etc/systemd/system/sentryusb-archive.service.d/teslamic.conf)"
install -Dm644 "$HERE/systemd/bluealsa.service.d/teslamic.conf" "$(rpath /etc/systemd/system/bluealsa.service.d/teslamic.conf)"
[ -e "$(rpath /etc/teslamic-gadget.conf)" ] || install -Dm644 "$HERE/config/teslamic-gadget.conf.example" "$(rpath /etc/teslamic-gadget.conf)"
install -d "$(rpath /etc/modules-load.d)" "$(rpath /root/bin)"
printf 'usb_f_teslamic\n' >"$(rpath /etc/modules-load.d/teslamic-gadget.conf)"

"$HERE/scripts/teslamic-ensure-integration" --root "$ROOT"

MAIN_CONF=$(rpath /etc/bluetooth/main.conf)
if [ -f "$MAIN_CONF" ]; then
    if grep -q '^#AutoEnable=true' "$MAIN_CONF"; then
        sed -i 's/^#AutoEnable=true/AutoEnable=true/' "$MAIN_CONF"
    elif ! grep -q '^AutoEnable=true' "$MAIN_CONF"; then
        printf '\n[Policy]\nAutoEnable=true\n' >>"$MAIN_CONF"
    fi
fi

if [ "$READ_ONLY_ROOT" = 1 ]; then
    install -Dm755 "$picker_source" "$(rpath /usr/local/bin/sentryusb-pick-binary)"
    "$HERE/scripts/sentryusb-readonly-root" enable --root "$ROOT"
fi

if [ "$ROOT" = / ]; then
    make -C "$HERE/kernel" clean all
    install -Dm644 "$HERE/kernel/usb_f_teslamic.ko" "/lib/modules/$KERNEL/extra/usb_f_teslamic.ko"
    depmod -a "$KERNEL"
    systemctl daemon-reload
    systemctl disable --now bluealsa-aplay.service 2>/dev/null || true
    systemctl enable bluetooth.service bluealsa.service teslamic-bluealsa.service sentryusb-teslamic-guard.service sentryusb-archive.service
    systemctl start sentryusb-teslamic-guard.service
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
