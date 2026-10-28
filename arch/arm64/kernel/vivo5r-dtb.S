/* SPDX-License-Identifier: GPL-2.0-only */
/* The path is supplied by arch/arm64/kernel/Makefile. */

  .section .rodata, "a"
  .balign 8
  .global __mt6753_vivo5r_dtb_start
__mt6753_vivo5r_dtb_start:
  .incbin VIVO5R_DTB_PATH
  .global __mt6753_vivo5r_dtb_end
__mt6753_vivo5r_dtb_end:

  .section .note.GNU-stack,"",%progbits
