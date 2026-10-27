# Blu Vivo 5R mainline bring-up

This tree is an independent checkout of upstream Linux for the Blu Vivo 5R
(MediaTek MT6753). The Android/vendor 3.18 tree remains the hardware
reference and is not merged into this history.

## Upstream baseline

- Source: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
- Revision: `d135ab453a7400a34930bdda1ef4bdc8acfac80e`
- Date: 2026-10-26
- Local checkout: `/home/admin/linux-mainline`
- Vendor reference: `/home/admin/vivo5r-mainline-src-ft8716`, tag
  `baseline-vendor-3.18-functional-20261027`

## Current support assessment

Upstream contains reusable MediaTek MT6735-family pieces, including clock
drivers, pinctrl, power-domain definitions and the MTK watchdog. It also has
generic MTK SD/MMC, MT65xx I2C and MTU3 USB frameworks, but the current tree
does not provide MT6753 compatible data for the SD/MMC driver or a board DTS.
The MT6577 UART and I2C compatibles may provide reusable fallback paths, but
that must be validated against the MT6753 register map. The closest complete
device-tree references are MT6755 and MT6795; their register maps and boards
must not be copied without checking them against the vendor DTS and hardware
logs.

The vendor DTS identifies the first bring-up facts below:

| Function | Vendor evidence | Mainline status |
|---|---|---|
| RAM | `0x40000000`, size `0xc0000000` (3 GiB) | Included in minimal board DTS |
| UART | `apuart3`, vendor console `ttyMT3` | Minimal DTS uses the upstream MT6577-compatible 8250 path at `0x11005000`, IRQ 94 |
| eMMC | `mmc0` at vendor MSDC address `0x11230000` | Requires a new MT6753-compatible data path in upstream `mtk-sd` |
| USB | Vendor USB block at `0x11200000` | Defer until serial and storage boot |
| Touch | FT8716 on I2C1, reset GPIO62, EINT9 | Defer; use standard FocalTech binding later |
| Display | FT8716 DSI panel and RT4801 bias | Defer; not required for headless Debian |
| Wireless | Vendor combo/firmware stack | Defer; first network path may be USB |

The vendor SoC DTS is not a drop-in mainline DTS: it uses vendor-only
compatibles such as `mediatek,mt6735-gic`, `mediatek,mt6735-uart` and
`mediatek,mt6753-mmc`, plus vendor clock and pinctrl bindings. These need to
be translated one subsystem at a time; changing only the compatible strings
would be unsafe.

## Bring-up order

1. Produce a minimal upstream ARM64 configuration and device tree that can
   reach the UART.
2. Add clocks, pinctrl, GIC/timer and watchdog only as required by the early
   boot log.
3. Enable eMMC and boot a read-only initramfs before touching Android system
   partitions.
4. Add a Debian minimal root filesystem and USB/network administration.
5. Port regulators, thermal management, battery and suspend/resume.
6. Port display, touch and capacitive keys after the headless server path is
   stable.

An ARM64 `tinyconfig`-based generic image was compiled successfully with the
minimal fragment at `configs/vivo5r-mainline-minimal.fragment`. Its output is
kept outside the source tree at
`/home/admin/linux-mainline-build-tiny/arch/arm64/boot/Image.gz`.
The fragment explicitly selects the conservative `ARM64_VA_BITS_39` and
`ARM64_PA_BITS_48` configuration for the MT6753 first-stage bring-up; the
upstream default VA52/PA52/LPA2 combination is not used on this pre-LPA2
Cortex-A53 target.

## Minimal MT6753 board DTS

`arch/arm64/boot/dts/mediatek/mt6753-vivo5r.dts` is the first board
description. It deliberately contains only the eight Cortex-A53 CPUs, PSCI,
ARM timer, 3 GiB memory, the MediaTek system-interrupt polarity block, a
GIC-400 description using the vendor register locations, and `apuart3` at
`0x11005000`. It is registered in the MediaTek ARM64 DTS Makefile.

The DTB compiles with the current upstream tree and the existing tinyconfig:

- DTB: `/home/admin/linux-mainline-build-tiny/arch/arm64/boot/dts/mediatek/mt6753-vivo5r.dtb`
- Size: 2258 bytes
- SHA-256: `d8b84ceb489f53d2f0f5f68a66a771df15378d319d2a9c0050805ab0c90f6506`
- `Image.gz` remains 1536527 bytes with SHA-256
  `1bd35b36683ccf883a00cefcc9b3d3d55fe0b19c90e2f95c5e165ec6edb0fe4c`

This is a build/static-validation milestone, not a bootable phone image yet.
The two-region GIC description and the reuse of the MT6577 sysirq binding are
explicit hypotheses based on the vendor register map. They must be tested by
UART before adding eMMC, clocks, pinctrl, USB or a Debian root filesystem.
