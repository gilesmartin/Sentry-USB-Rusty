#!/bin/bash
set -euo pipefail
ROOT=/
DRY_RUN=0
usage() { echo "usage: $0 [--dry-run] [--root DIR]"; }
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --root) shift; ROOT=${1:?--root requires a directory} ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done
ROOT=${ROOT%/}; [ -n "$ROOT" ] || ROOT=/
case "$ROOT" in /*) ;; *) echo "--root directory must be absolute" >&2; exit 2 ;; esac
rpath() { [ "$ROOT" = / ] && printf '%s' "$1" || printf '%s%s' "$ROOT" "$1"; }
STATE=$(rpath /var/lib/sentryusb-teslamic)
ACTIVE="$STATE/active-backup"
[ -s "$ACTIVE" ] || { echo "no active TeslaMic installation manifest" >&2; exit 1; }
backup=$(cat "$ACTIVE")
manifest="$backup/manifest.tsv"
service_states="$backup/service-states.tsv"
[ -s "$manifest" ] || { echo "manifest missing: $manifest" >&2; exit 1; }
if [ "$DRY_RUN" = 1 ]; then echo "Would restore files from $manifest and disable TeslaMic services."; exit 0; fi
[ "$ROOT" != / ] || [ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
if [ "$ROOT" = / ]; then
    systemctl disable --now teslamic-bluealsa.service 2>/dev/null || true
    /usr/local/sbin/teslamic-compose prepare-disable 2>/dev/null || true
fi
while IFS=$'\t' read -r state path; do
    actual=$(rpath "$path")
    rel=${path#/}
    rm -rf "$actual"
    if [ "$state" = present ]; then
        mkdir -p "$(dirname "$actual")"
        cp -a "$backup/files/$rel" "$actual"
    fi
done <"$manifest"
if [ "$ROOT" = / ]; then
    KERNEL=$(uname -r)
    # The manifest already restored or removed the module as appropriate.
    depmod -a "$KERNEL"
    systemctl daemon-reload
    if [ -s "$service_states" ]; then
        while IFS=$'\t' read -r service state; do
            case "$state" in
                enabled|enabled-runtime|linked|linked-runtime)
                    systemctl enable "$service" 2>/dev/null || true ;;
                masked|masked-runtime)
                    systemctl mask "$service" 2>/dev/null || true ;;
                *)
                    systemctl disable "$service" 2>/dev/null || true ;;
            esac
        done <"$service_states"
    fi
    echo "Rollback restored. Reboot to return to the original SentryUSB gadget."
fi
rm -f "$ACTIVE"
