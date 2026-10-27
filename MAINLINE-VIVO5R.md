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
drivers, pinctrl, power-domain definitions, the MTK SD/MMC host, the MTK
watchdog, MT65xx I2C and MTU3 USB support. It does not currently contain a
Vivo 5R board DTS or an MT6753 DTS. The closest device-tree references are
MT6755 and MT6795, but their register maps and boards must not be copied
without checking them against the vendor DTS and hardware logs.

The vendor DTS identifies the first bring-up facts below:

| Function | Vendor evidence | Mainline status |
|---|---|---|
| RAM | `0x40000000`, size `0xc0000000` (3 GiB) | Board DTS to write |
| UART | `apuart3`, vendor console `ttyMT3` | Need an upstream-compatible UART node/driver path |
| eMMC | `mmc0` at vendor MSDC address `0x11230000` | Use upstream `mtk-sd` after clock/reset/pinctrl port |
| USB | Vendor USB block at `0x11200000` | Defer until serial and storage boot |
| Touch | FT8716 on I2C1, reset GPIO62, EINT9 | Defer; use standard FocalTech binding later |
| Display | FT8716 DSI panel and RT4801 bias | Defer; not required for headless Debian |
| Wireless | Vendor combo/firmware stack | Defer; first network path may be USB |

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

No upstream kernel image has been compiled, packaged, booted or flashed from
this checkout yet.
