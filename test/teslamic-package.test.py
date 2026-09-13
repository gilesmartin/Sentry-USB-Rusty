#!/usr/bin/env python3
import pathlib
import py_compile
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TM = ROOT / "teslamic"

REQUIRED = [
    "README.md", "NOTICE", "config/teslamic-gadget.conf.example",
    "kernel/Makefile", "kernel/usb_f_teslamic.c", "kernel/u_audio.h",
    "kernel/uac_common.h", "kernel/tests/test_descriptors.py",
    "scripts/teslamic-compose", "scripts/teslamic-bluealsa-bridge",
    "scripts/teslamic-pair", "scripts/teslamic-devices",
    "scripts/teslamic-ensure-integration",
    "scripts/sentryusb-readonly-root",
    "systemd/teslamic-bluealsa.service",
    "systemd/sentryusb-teslamic-guard.service",
    "systemd/sentryusb-archive.service.d/teslamic.conf",
    "systemd/bluealsa.service.d/teslamic.conf",
    "install.sh", "uninstall.sh", "verify.sh",
    "docs/architecture.md", "docs/deployment-model-x-intel.md",
    "docs/history-and-debugging.md", "docs/pairing.md",
    "docs/troubleshooting.md", "docs/upgrade-and-rollback.md",
]

class TeslaMicPackageTests(unittest.TestCase):
    def test_rsync_monitors_tolerate_mobile_tailscale_transitions(self):
        archive = (ROOT / "run/rsync_archive/archive-clips.sh").read_text()
        music = (ROOT / "run/rsync_archive/copy-music.sh").read_text()

        normal = archive.split("else", 1)[1]
        self.assertIn("MONITOR_MISSES=20", normal)
        self.assertIn("MONITOR_TIMEOUT=20", normal)
        self.assertIn(
            "export ARCHIVE_PING_TIMEOUT=4 ARCHIVE_SSH_TIMEOUT=8", normal
        )
        self.assertNotIn("--bwlimit", archive)
        self.assertIn(
            'MONITOR_SERVER="${ARCHIVE_SERVER:-$RSYNC_SERVER}"', archive
        )
        self.assertIn(
            'archive-is-reachable.sh "$MONITOR_SERVER"', archive
        )
        self.assertNotIn(
            'archive-is-reachable.sh "$ARCHIVE_SERVER"', archive
        )

        self.assertIn("MONITOR_MISSES=20", music)
        self.assertIn("MONITOR_TIMEOUT=20", music)
        self.assertIn(
            "export ARCHIVE_PING_TIMEOUT=4 ARCHIVE_SSH_TIMEOUT=8", music
        )
        self.assertIn('timeout "$MONITOR_TIMEOUT"', music)
        self.assertNotIn("--bwlimit", music)

    def test_required_package_files_exist(self):
        missing = [p for p in REQUIRED if not (TM / p).is_file()]
        self.assertEqual(missing, [])

    def test_shell_scripts_parse(self):
        scripts = [TM / "install.sh", TM / "uninstall.sh", TM / "verify.sh"]
        scripts += [p for p in (TM / "scripts").glob("*") if p.is_file()]
        for script in scripts:
            if script.name == "teslamic-pair":
                continue
            subprocess.run(["bash", "-n", str(script)], check=True)

    def test_python_pairing_helper_compiles(self):
        py_compile.compile(str(TM / "scripts/teslamic-pair"), doraise=True)

    def test_descriptor_regressions_are_preserved(self):
        src = (TM / "kernel/usb_f_teslamic.c").read_text()
        self.assertIn("tm_as_in_ep_hs.bEndpointAddress = tm_as_in_ep.bEndpointAddress", src)
        self.assertIn("tm_kbd_ep_hs.bEndpointAddress = tm_kbd_ep.bEndpointAddress", src)
        self.assertIn("bool as_ep_enabled, kbd_ep_enabled", src)
        self.assertIn("if (tm->as_ep_enabled)", src)
        self.assertIn("if (tm->kbd_ep_enabled)", src)
        self.assertIn("if (length > max_length)", src)
        self.assertIn("tm_queue_ep0_out(f, w_length, 3,", src)
        self.assertIn("tm_queue_ep0_out(f, w_length, max_length,", src)
        self.assertIn("if (tm->as_ep_enabled)\n\t\t\t\treturn 0;", src)
        self.assertIn("ret = u_audio_start_playback(&tm->audio)", src)
        self.assertIn("usb_ep_disable(tm->audio.in_ep);\n\t\t\t\treturn ret;", src)

    def test_compose_is_host_agnostic_and_configurable(self):
        text = (TM / "scripts/teslamic-compose").read_text()
        self.assertNotIn("1000480000.usb", text)
        self.assertIn("/etc/teslamic-gadget.conf", text)
        self.assertIn("TESLAMIC_UDC_CLASS:-/sys/class/udc", text)
        self.assertIn('"$UDC_CLASS"/*', text)
        self.assertIn("TeslaMic IF0-3, storage IF4", text)
        bridge = (TM / "scripts/teslamic-bluealsa-bridge").read_text()
        self.assertNotIn("1000480000.usb", bridge)
        self.assertIn("TESLAMIC_GADGET_ROOT", bridge)
        self.assertIn("TESLAMIC_UDC_CLASS", bridge)

    def test_pairing_is_bounded_and_closes_exposure(self):
        text = (TM / "scripts/teslamic-pair").read_text()
        self.assertIn("DEFAULT_TIMEOUT = 180", text)
        self.assertIn('"Discoverable", False', text)
        self.assertIn('"Pairable", False', text)
        self.assertIn("AUDIO_UUIDS", text)
        self.assertIn('"Trusted", dbus.Boolean(True)', text)
        self.assertIn("initially_paired", text)
        self.assertIn("adapter_device_prefix", text)
        self.assertIn("audio_authorized", text)
        self.assertIn("trust_if_ready", text)
        self.assertNotIn('return "0000"', text)
        unit = (TM / "systemd/teslamic-bluealsa.service").read_text()
        self.assertIn("bluetoothctl discoverable off", unit)
        self.assertIn("bluetoothctl pairable off", unit)
        verify = (TM / "verify.sh").read_text()
        self.assertIn("Discoverable: no", verify)
        self.assertIn("Pairable: no", verify)

    def test_installer_is_reversible_and_never_installs_a_prebuilt_module(self):
        install = (TM / "install.sh").read_text()
        uninstall = (TM / "uninstall.sh").read_text()
        self.assertIn("/var/backups/sentryusb-teslamic", install)
        self.assertIn("make", install)
        self.assertIn("depmod", install)
        self.assertIn('make -C "$HERE/kernel" clean all', install)
        self.assertEqual(list((TM / "kernel").glob("*.ko")), [])
        self.assertIn("manifest", install)
        self.assertIn("manifest", uninstall)
        self.assertIn("service-states.tsv", install)
        self.assertIn("service-states.tsv", uninstall)
        self.assertIn("systemctl start sentryusb-teslamic-guard.service", install)
        archive_dropin = (TM / "systemd/sentryusb-archive.service.d/teslamic.conf").read_text()
        self.assertIn("Requires=sentryusb-teslamic-guard.service", archive_dropin)
        self.assertIn("After=sentryusb-teslamic-guard.service", archive_dropin)
        self.assertNotIn("systemctl enable bluealsa-aplay.service", uninstall)
        self.assertNotIn('rm -f "/lib/modules/$KERNEL/extra/usb_f_teslamic.ko"', uninstall)

    def test_staged_install_and_uninstall_restore_original_wrappers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "root/bin").mkdir(parents=True)
            (root / "etc/bluetooth").mkdir(parents=True)
            enable = root / "root/bin/enable_gadget.sh"
            disable = root / "root/bin/disable_gadget.sh"
            enable.write_text("#!/bin/sh\necho original-enable\n")
            disable.write_text("#!/bin/sh\necho original-disable\n")
            (root / "etc/bluetooth/main.conf").write_text("[Policy]\n#AutoEnable=true\n")
            original_enable = enable.read_bytes()
            original_disable = disable.read_bytes()

            subprocess.run([str(TM / "install.sh"), "--root", tmp], check=True)
            self.assertIn("sentryusb-teslamic/original-enable", enable.read_text())
            self.assertTrue((root / "var/lib/sentryusb-teslamic/active-backup").is_file())
            self.assertTrue((root / "usr/local/sbin/teslamic-compose").is_file())
            self.assertIn("AutoEnable=true", (root / "etc/bluetooth/main.conf").read_text())
            # Reinstall must retain the first pre-TeslaMic backup, not back up wrappers.
            active_before = (root / "var/lib/sentryusb-teslamic/active-backup").read_text()
            subprocess.run([str(TM / "install.sh"), "--root", tmp], check=True)
            self.assertEqual((root / "var/lib/sentryusb-teslamic/active-backup").read_text(), active_before)

            subprocess.run([str(TM / "uninstall.sh"), "--root", tmp], check=True)
            self.assertEqual(enable.read_bytes(), original_enable)
            self.assertEqual(disable.read_bytes(), original_disable)
            self.assertFalse((root / "usr/local/sbin/teslamic-compose").exists())

    def test_readonly_mode_is_explicit_and_fully_reversible(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for directory in (
                "root/bin", "etc/bluetooth", "boot/firmware",
                "etc/NetworkManager/conf.d", "etc/systemd/system",
                "usr/local/bin", "var/lib/tailscale", "var/cache/tailscale",
                "var/lib/bluetooth", "var/lib/systemd", "mutable",
            ):
                (root / directory).mkdir(parents=True, exist_ok=True)
            originals = {
                "root/bin/enable_gadget.sh": "#!/bin/sh\necho enable\n",
                "root/bin/disable_gadget.sh": "#!/bin/sh\necho disable\n",
                "etc/bluetooth/main.conf": "[Policy]\n#AutoEnable=true\n",
                "etc/fstab": "PARTUUID=test / ext4 defaults,noatime,rw 0 1\n",
                "boot/firmware/cmdline.txt": "console=tty1 root=PARTUUID=test rootwait rw\n",
                "root/sentryusb.conf": "export SKIP_READONLY=true\n",
                "etc/resolv.conf": "nameserver 192.0.2.1\n",
                "usr/local/bin/sentryusb-pick-binary": "#!/bin/sh\necho old-picker\n",
                "var/lib/tailscale/tailscaled.state": "test-state\n",
                "var/lib/systemd/random-seed": "test-seed\n",
            }
            for relative, content in originals.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)

            subprocess.run(
                [str(TM / "install.sh"), "--read-only-root", "--root", tmp],
                check=True,
            )
            installed_helper = root / "usr/local/sbin/sentryusb-readonly-root"
            subprocess.run(
                [str(installed_helper), "enable", "--root", tmp], check=True
            )
            picker = (root / "usr/local/bin/sentryusb-pick-binary").read_text()
            self.assertIn("Avoid touching an already-correct link", picker)
            self.assertIn("mount -o remount,rw /", installed_helper.read_text())
            self.assertIn(
                "mount -o remount,rw /boot/firmware", installed_helper.read_text()
            )
            fstab = (root / "etc/fstab").read_text()
            self.assertEqual(fstab.count("/mutable/.tailscale /var/lib/tailscale"), 1)
            self.assertRegex(fstab, r"PARTUUID=test\s+/\s+ext4\s+defaults,noatime,ro")
            self.assertIn("/mutable/.tailscale /var/lib/tailscale", fstab)
            self.assertIn("/mutable/.tailscale-cache /var/cache/tailscale", fstab)
            self.assertIn("/mutable/.bluetooth /var/lib/bluetooth", fstab)
            self.assertIn("/mutable/.systemd /var/lib/systemd", fstab)
            self.assertIn("TESLAMIC_PRESERVE_WRITABLE_ROOT=0", (root / "etc/teslamic-gadget.conf").read_text())
            self.assertTrue((root / "etc/resolv.conf").is_symlink())
            self.assertEqual(
                (root / "etc/resolv.conf").readlink(),
                pathlib.Path("../run/systemd/resolve/stub-resolv.conf"),
            )
            self.assertTrue((root / "etc/systemd/system/cloud-init-local.service").is_symlink())
            self.assertTrue((root / "mutable/.tailscale/tailscaled.state").is_file())

            subprocess.run([str(TM / "uninstall.sh"), "--root", tmp], check=True)
            for relative, content in originals.items():
                self.assertEqual((root / relative).read_text(), content, relative)
            self.assertFalse((root / "etc/systemd/system/cloud-init-local.service").exists())
            self.assertFalse((root / "etc/systemd/system/tailscaled.service.d/20-readonly-state.conf").exists())

    def test_readonly_enable_failure_rolls_back_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for relative in ("etc", "root"):
                (root / relative).mkdir(parents=True, exist_ok=True)
            (root / "etc/fstab").write_text(
                "PARTUUID=test / ext4 defaults,noatime,rw 0 1\n"
            )
            (root / "root/sentryusb.conf").write_text(
                "export SKIP_READONLY=true\n"
            )
            (root / "etc/teslamic-gadget.conf").write_text(
                "TESLAMIC_PRESERVE_WRITABLE_ROOT=1\n"
            )
            cmdline = root / "boot/firmware/cmdline.txt"
            cmdline.parent.mkdir(parents=True)
            cmdline.write_bytes(b"console=tty1 rw \xff\n")
            with self.assertRaises(subprocess.CalledProcessError):
                subprocess.run(
                    [
                        str(TM / "scripts/sentryusb-readonly-root"),
                        "enable",
                        "--root",
                        tmp,
                    ],
                    check=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            self.assertEqual(
                (root / "etc/fstab").read_text(),
                "PARTUUID=test / ext4 defaults,noatime,rw 0 1\n",
            )
            self.assertEqual(
                (root / "root/sentryusb.conf").read_text(),
                "export SKIP_READONLY=true\n",
            )
            self.assertFalse(
                (root / "var/lib/sentryusb-teslamic/readonly-backup/manifest.tsv").exists()
            )

    def test_install_migrates_existing_ad_hoc_teslamic_wrapper_without_recursion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "root/bin").mkdir(parents=True)
            (root / "etc/bluetooth").mkdir(parents=True)
            (root / "root/bin/enable_gadget.sh").write_text(
                '#!/bin/bash -eu\nsentryusb gadget enable "$@"\n/usr/local/sbin/teslamic-compose enable\n')
            (root / "root/bin/disable_gadget.sh").write_text(
                '#!/bin/bash -eu\n/usr/local/sbin/teslamic-compose prepare-disable\nsentryusb gadget disable "$@"\n')
            (root / "etc/bluetooth/main.conf").write_text("[Policy]\nAutoEnable=true\n")
            subprocess.run([str(TM / "install.sh"), "--root", tmp], check=True)
            preserved = (root / "usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh").read_text()
            self.assertIn("sentryusb gadget enable", preserved)
            self.assertNotIn("teslamic-compose", preserved)

    def test_guard_repairs_setup_overwrite_and_preserves_writable_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "root/bin").mkdir(parents=True)
            (root / "etc/bluetooth").mkdir(parents=True)
            (root / "boot/firmware").mkdir(parents=True)
            (root / "root/bin/enable_gadget.sh").write_text(
                '#!/bin/bash -eu\nsentryusb gadget enable "$@"\n'
            )
            (root / "root/bin/disable_gadget.sh").write_text(
                '#!/bin/bash -eu\nsentryusb gadget disable "$@"\n'
            )
            (root / "etc/bluetooth/main.conf").write_text("[Policy]\nAutoEnable=true\n")
            (root / "etc/fstab").write_text(
                "PARTUUID=test / ext4 defaults,noatime,rw 0 1\n"
            )
            (root / "boot/firmware/cmdline.txt").write_text(
                "console=tty1 root=PARTUUID=test rootwait rw\n"
            )
            (root / "root/sentryusb.conf").write_text(
                "export ARCHIVE_SYSTEM=none\nexport SKIP_READONLY=true\n"
            )

            subprocess.run([str(TM / "install.sh"), "--root", tmp], check=True)

            # Reproduce what a full browser setup did on the live Pi.
            (root / "root/bin/enable_gadget.sh").write_text(
                '#!/bin/bash -eu\nsentryusb gadget enable --new-stock "$@"\n'
            )
            (root / "root/bin/disable_gadget.sh").write_text(
                '#!/bin/bash -eu\nsentryusb gadget disable --new-stock "$@"\n'
            )
            (root / "etc/fstab").write_text(
                "PARTUUID=test / ext4 defaults,noatime,ro 0 1\n"
            )
            (root / "boot/firmware/cmdline.txt").write_text(
                "console=tty1 root=PARTUUID=test rootwait ro\n"
            )
            (root / "root/sentryusb.conf").write_text("export ARCHIVE_SYSTEM=rsync\n")

            subprocess.run(
                [str(TM / "scripts/teslamic-ensure-integration"), "--root", tmp],
                check=True,
            )

            self.assertIn("teslamic-compose enable", (root / "root/bin/enable_gadget.sh").read_text())
            self.assertIn("Managed by sentryusb-teslamic", (root / "root/bin/enable_gadget.sh").read_text())
            self.assertIn("teslamic-compose prepare-disable", (root / "root/bin/disable_gadget.sh").read_text())
            self.assertIn(
                "enable --new-stock",
                (root / "usr/local/libexec/sentryusb-teslamic/original-enable-gadget.sh").read_text(),
            )
            self.assertIn("export SKIP_READONLY=true", (root / "root/sentryusb.conf").read_text())
            self.assertIn("defaults,noatime,rw", (root / "etc/fstab").read_text())
            self.assertNotIn("defaults,noatime,ro", (root / "etc/fstab").read_text())
            cmdline = (root / "boot/firmware/cmdline.txt").read_text().split()
            self.assertIn("rw", cmdline)
            self.assertNotIn("ro", cmdline)

    def test_public_tree_contains_no_deployment_identifiers_or_secrets(self):
        forbidden = [
            re.compile(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"),
            re.compile(r"sentryusb\.vpn", re.I),
            re.compile(r"pve-(?:mac|pri)", re.I),
            re.compile(r"10\.0\.[0-9]+\.[0-9]+"),
            re.compile(r"BEGIN (?:OPENSSH|RSA|EC) PRIVATE KEY"),
            re.compile(r"62A0CB4A43C7E730"),
        ]
        hits = []
        for path in TM.rglob("*"):
            if not path.is_file() or "__pycache__" in path.parts:
                continue
            text = path.read_text(errors="ignore")
            for rx in forbidden:
                if rx.search(text):
                    hits.append(f"{path.relative_to(ROOT)}: {rx.pattern}")
        self.assertEqual(hits, [])

    def test_docs_cover_both_vehicles_and_known_failed_path(self):
        docs = "\n".join(p.read_text() for p in (TM / "docs").glob("*.md"))
        self.assertIn("2018 Model X", docs)
        self.assertIn("Intel", docs)
        self.assertIn("Raspberry Pi 5", docs)
        self.assertIn("FunctionFS", docs)
        self.assertIn("error -32", docs)
        self.assertIn("mass storage", docs.lower())
        self.assertIn("in-vehicle validation", docs.lower())

if __name__ == "__main__":
    unittest.main()
