/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

#define CurrentEL_EL3		(3 << 2)

SYM_FUNC_START(mt6753_secondary_entry)
	/* Send 'S' (0x53) to UART0 (0x11005000) for instant proof of entry */
	mov	x1, #0x5000
	movk	x1, #0x1100, lsl #16
	mov	w2, #0x53
	str	w2, [x1]

	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL3
	b.ne	1f

	/* Send '3' (0x33): Confirmed EL3, configuring SCR_EL3 and dropping to EL2 */
	mov	w2, #0x33
	str	w2, [x1]

	/* If entered at EL3 from ATF, drop cleanly to Non-Secure EL2h */
	mov	x0, #0x531		// SCR_EL3: NS=1, HCE=1, SMD=1, RW=1
	msr	scr_el3, x0
	isb
	mov	x0, #0x3c9		// SPSR_EL3: EL2h, DAIF masked (0x3c9)
	msr	spsr_el3, x0
	adr_l	x0, secondary_entry	// Target in Non-Secure world
	msr	elr_el3, x0
	isb
	eret

1:
	/* Send 'N' (0x4e): Already Non-Secure */
	mov	w2, #0x4e
	str	w2, [x1]
	b	secondary_entry
SYM_FUNC_END(mt6753_secondary_entry)
