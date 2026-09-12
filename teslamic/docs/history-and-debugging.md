# Development and debugging history

This records the reproducible technical path, including failed approaches.

## 1. Requirements and inventory

The target was a Raspberry Pi 5 already running SentryUSB mass storage. The Pi exposes one usable device-mode UDC, so TeslaMic and mass storage cannot be independently bound gadgets; they must share one configfs gadget.

The reference TeslaMic descriptor topology was taken from the MIT-licensed TeslAux project. It combines UAC1 audio with keyboard HID and an endpoint-less vendor HID interface.

## 2. Bluetooth audio path

BlueALSA was selected for a headless A2DP sink. The generic `bluealsa-aplay.service` was disabled because it did not target the gadget PCM. A dedicated bridge dynamically finds `TeslaMic_Gadget`, waits for USB configuration, and invokes `bluealsa-aplay` for that card.

## 3. FunctionFS prototype (unsupported)

A FunctionFS prototype compiled and passed static byte tests, but the kernel rejected its class-specific audio descriptors with `EINVAL`. FunctionFS does not accept this arbitrary Audio/HID descriptor layout. That prototype is not included as a production path.

## 4. Custom configfs kernel function

An out-of-tree GPL module was built against the Pi's exact running kernel and its matching headers. It uses the kernel `u_audio` helper and implements the TeslaMic control requests and descriptors.

Compilation alone did not prove enumeration. Both macOS and an independent Linux host could read VID/PID and strings but failed the host `SET_CONFIGURATION` request. Linux logged:

```text
can't set config #1, error -32
```

No per-interface nodes or class drivers appeared. Removing mass storage produced the same failure, ruling out mass storage as the root cause and confirming this was not macOS-specific.

## 5. Root cause

Function-graph ftrace showed `reset_config` immediately after the custom function's `set_alt`, and `config_ep_by_speed()` returned `-EIO` for the high-speed endpoint.

`usb_ep_autoconfig()` had updated only the full-speed descriptor's endpoint address. The separate high-speed descriptors retained requested addresses that no longer matched the endpoints actually allocated by the UDC. At high speed, `config_ep_by_speed()` could not match the endpoint, `set_alt()` failed, and libcomposite stalled `SET_CONFIGURATION`.

The working fix copies both resolved addresses before `usb_assign_descriptors()`:

```c
tm_as_in_ep_hs.bEndpointAddress = tm_as_in_ep.bEndpointAddress;
tm_kbd_ep_hs.bEndpointAddress = tm_kbd_ep.bEndpointAddress;
```

A second fix tracks whether the audio and keyboard endpoints are enabled before stopping or disabling them. The initial alt-setting sequence had attempted teardown before first enable, which could also abort configuration.

## 6. Validation

After rebuilding, installing the new `.ko` into `/lib/modules/$(uname -r)/extra/`, running `depmod`, and recreating the gadget:

- Linux completed `SET_CONFIGURATION`.
- TeslaMic-only mode exposed four interfaces and bound HID/audio drivers.
- Composite mode exposed five interfaces.
- All four SentryUSB LUNs appeared with their original sizes and labels.
- macOS showed both TeslaMic audio and storage.
- Bluetooth audio traversed A2DP -> BlueALSA -> gadget PCM -> USB host.
- A full physical disconnect/reboot recreated the composite gadget and services automatically.

## 7. Pairing lesson

Commands must not be queued into `bluetoothctl` before the agent is ready. An incoming yes/no authorization prompt can consume the next queued command as its answer and reject pairing. The packaged helper uses a BlueZ D-Bus agent, accepts only required audio services during a bounded window, trusts the bonded identity, and always closes pairability/discoverability.

## 8. Pre-publication hardening review

The exact working source was preserved before packaging. Independent review of
the public package then found issues that did not appear during normal host use:

1. Host-supplied EP0 OUT lengths were not bounded. UAC and HID writes are now
   rejected when they exceed the corresponding descriptor-defined payload.
2. The pairing helper could promote unrelated BlueZ state changes. It now
   considers only devices newly paired on the selected adapter, rejects legacy
   fixed-PIN pairing, and waits for allowlisted A2DP/AVRCP authorization before
   marking the device trusted.
3. Repeated audio `SET_INTERFACE alt=1` requests could re-enable an active
   endpoint, and playback-start failure did not disable the endpoint. Both paths
   are now idempotent/fail-safe.
4. The initial bridge helper inherited the development Pi's UDC path. The
   package now reads the bound UDC from the configured gadget and UDC class.
5. Pairability was observed enabled after later service activity. The package
   closes pairability/discoverability when its bridge starts and verifies both.

The hardened module was rebuilt against the target Pi kernel with `W=1` and no
warnings. It was not silently loaded over the accepted working module during
packaging; `validation.md` records the remaining controlled runtime test gate.
