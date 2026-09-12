# Validation record

## Source and build provenance

The final working Pi source was captured before packaging. Its SHA-256 is:

```text
493f4396592c01b831923b418698f84f401be5fd382c1d2652f82d3a191ef732
```

Its clean Linux `6.18.39+rpt-rpi-2712` build matched the deployed module:

```text
726f89ffcbcca8d24e73f786a78eeb44718e63bdaf781a225c1b3ab3f32b0187
```

Independent review then identified an unbounded host-supplied EP0 OUT length. The
repository source adds descriptor-sized UAC/HID bounds; its SHA-256 is:

```text
addf5e2f0946f387056488308ce87d9f5f1345c1b318becd41c54d0dd16b76dd
```

A clean target-kernel build of that hardened source produced:

```text
f03cbb130ca635966d80955da9691cd09084734efe538324ba03430cfbe26db8
```

with aarch64 vermagic `6.18.39+rpt-rpi-2712 SMP preempt mod_unload modversions`.
The same target build completed with `W=1` and no compiler warnings.
The hardened revision was built but deliberately not loaded into the working Pi
while packaging. Runtime host/vehicle results below therefore describe the
captured working source; the hardened revision still requires a controlled
rebind/reboot validation before publication. Different kernels may legitimately
produce different module hashes.

## Validation matrix

| Test | Result |
|---|---|
| Static TeslaMic descriptor tests | Pass |
| Clean target-kernel module build | Pass |
| Configfs function creation/link | Pass |
| Linux host `SET_CONFIGURATION` | Pass |
| Linux host audio + HID driver binding | Pass |
| Composite mode, five interfaces | Pass |
| Four SentryUSB mass-storage LUNs | Pass |
| macOS audio input and storage | Pass |
| Bluetooth A2DP source to USB capture | Pass |
| Physical disconnect/reboot recreation | Pass |
| Services active after reboot | Pass |
| 2018 Model X Intel MCU | Pending in-vehicle validation |

## Live verifier result

The packaged `verify.sh` was run read-only against the working deployment and confirmed the module, TeslaMic and mass-storage links, USB identity, bound/configured UDC, Bluetooth and BlueALSA services, disabled generic player, and gadget ALSA PCM.
