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
    "systemd/teslamic-bluealsa.service",
    "systemd/bluealsa.service.d/teslamic.conf",
    "install.sh", "uninstall.sh", "verify.sh",
    "docs/architecture.md", "docs/deployment-model-x-intel.md",
    "docs/history-and-debugging.md", "docs/pairing.md",
    "docs/troubleshooting.md", "docs/upgrade-and-rollback.md",
]

class TeslaMicPackageTests(unittest.TestCase):
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
