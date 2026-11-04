// SPDX-License-Identifier: GPL-2.0-only
/* Low-volume checkpoints using the MT6753 TOPRGU non-reset register. */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/vivo5r_checkpoint.h>

#define VIVO5R_WDT_BASE_PHYS	0x10212000ULL
#define VIVO5R_WDT_NONRST_REG2	0x24

/* Vendor LK uses bits 0..2 for rpmbpk/recovery/bootloader flags. */
#define VIVO5R_SCRATCH_FLAGS_MASK	0x7U
#define VIVO5R_SCRATCH_MAGIC		0x80000000U
#define VIVO5R_SCRATCH_STAGE_SHIFT	15
#define VIVO5R_SCRATCH_VALUE_SHIFT	3
#define VIVO5R_SCRATCH_VALUE_MASK	0xfffU

static void __iomem *vivo5r_wdt_base;

static u32 vivo5r_scratch_encode(u32 stage, u32 value, u32 flags)
{
	return VIVO5R_SCRATCH_MAGIC |
		(stage << VIVO5R_SCRATCH_STAGE_SHIFT) |
		((value & VIVO5R_SCRATCH_VALUE_MASK) << VIVO5R_SCRATCH_VALUE_SHIFT) |
		(flags & VIVO5R_SCRATCH_FLAGS_MASK);
}

static bool vivo5r_scratch_decode(u32 record, u32 *stage, u32 *value)
{
	if (!(record & VIVO5R_SCRATCH_MAGIC))
		return false;

	*stage = (record >> VIVO5R_SCRATCH_STAGE_SHIFT) & 0xffffU;
	*value = (record >> VIVO5R_SCRATCH_VALUE_SHIFT) &
		VIVO5R_SCRATCH_VALUE_MASK;
	return true;
}

void vivo5r_checkpoint(u32 stage, u32 value)
{
	u32 scratch_reg;
	u32 flags;

	if (!vivo5r_wdt_base)
		return;

	scratch_reg = readl(vivo5r_wdt_base + VIVO5R_WDT_NONRST_REG2);
	flags = scratch_reg & VIVO5R_SCRATCH_FLAGS_MASK;
	writel(vivo5r_scratch_encode(stage, value, flags),
	       vivo5r_wdt_base + VIVO5R_WDT_NONRST_REG2);
	pr_emerg("V5R:%04x:%03x\n", stage & 0xffffU,
		 value & VIVO5R_SCRATCH_VALUE_MASK);
}
EXPORT_SYMBOL_GPL(vivo5r_checkpoint);

static int __init vivo5r_checkpoint_init(void)
{
	u32 scratch_reg;
	u32 stage;
	u32 value;

	vivo5r_wdt_base = ioremap(VIVO5R_WDT_BASE_PHYS, 0x1000);
	if (!vivo5r_wdt_base) {
		pr_emerg("Vivo5R scratch: ioremap failed\n");
		return -ENOMEM;
	}

	scratch_reg = readl(vivo5r_wdt_base + VIVO5R_WDT_NONRST_REG2);
	if (vivo5r_scratch_decode(scratch_reg, &stage, &value))
		pr_emerg("Vivo5R scratch: previous stage=0x%04x value=0x%03x flags=0x%x\n",
			 stage, value, scratch_reg & VIVO5R_SCRATCH_FLAGS_MASK);
	else
		pr_emerg("Vivo5R scratch: empty reg=0x%08x\n", scratch_reg);

	vivo5r_checkpoint(VIVO5R_CP_BOOT, 0);
	return 0;
}
early_initcall(vivo5r_checkpoint_init);
