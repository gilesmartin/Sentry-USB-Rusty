#!/usr/bin/env python3
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "usb_f_teslamic.c"
if not SRC.exists():
    SRC = HERE.parent / "usb_f_teslamic.c"
text = SRC.read_text() if SRC.exists() else ""

def c_array(name):
    m = re.search(r"static\s+(?:const\s+)?u8\s+" + re.escape(name) + r"\s*\[\s*\]\s*(?:__maybe_unused\s*)?=\s*\{(?P<body>.*?)\};", text, re.S)
    assert m, f"missing u8 array {name}"
    vals = []
    for tok in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", re.sub(r"/\*.*?\*/|//.*", "", m.group('body'), flags=re.S)):
        vals.append(int(tok, 0) & 0xff)
    return bytes(vals)

def test_exact_ac_descriptors():
    assert c_array("tm_ac_header") == bytes.fromhex("09240100012f000101")
    assert c_array("tm_ac_input_terminal") == bytes.fromhex("0c2402040102000203000000")
    assert c_array("tm_ac_feature_unit") == bytes.fromhex("0a240605040101020200")
    assert c_array("tm_ac_selector_unit") == bytes.fromhex("07240506010500")
    assert c_array("tm_ac_output_terminal") == bytes.fromhex("092403070101000600")

def test_exact_as_descriptors():
    assert c_array("tm_as_general") == bytes.fromhex("07240107010100")
    assert c_array("tm_as_format_type_i") == bytes.fromhex("0e2402010202100244ac0080bb00")
    assert c_array("tm_as_iso_endpoint") == bytes.fromhex("07250101000000")

def test_exact_hid_report_descriptors_and_feature():
    assert c_array("tm_hid_keyboard_report") == bytes.fromhex(
        "05010906a101050719e029e71500250175019508810295017508810195057501"
        "050819012905910295017503910195067508150026a400050719002aa4008100c0")
    assert c_array("tm_hid_vendor_report") == bytes.fromhex(
        "0600ff0aaa55a101150026ff007508960001090181029600010901910295080901b102c0")
    assert c_array("tm_if3_feature_report") == bytes.fromhex("0001000303000800")

def test_expected_static_descriptor_shapes():
    assert c_array("tm_hid_keyboard_desc")[0:9] == bytes.fromhex("092101020001224100")
    assert c_array("tm_hid_vendor_desc")[0:9] == bytes.fromhex("092101020001222400")
    # Endpoint templates: preferred addresses are 0x84 audio and 0x81 keyboard,
    # but bind may let usb_ep_autoconfig patch them for controllers that cannot.
    assert c_array("tm_as_in_ep_desc") == bytes.fromhex("09058409c000010000")
    assert c_array("tm_hid_keyboard_ep_desc") == bytes.fromhex("07058103400001")

if __name__ == "__main__":
    tests = [obj for name, obj in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print(f"ok {test.__name__}")
