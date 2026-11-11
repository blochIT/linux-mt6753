// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6753 SMP secondary CPU bringup driver (mt-boot)
 *
 * Secondary cores in the MT6753 are power-gated by MTCMOS switches in the
 * System Power Manager (SPM). They also require writing the secondary entry
 * address to the BootROM boot address register in INFRACFG_AO, followed by
 * an SMC call (MTK_SIP_KERNEL_CPU_RST_B) to deassert the CPU reset.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/psci.h>
#include <linux/arm-smccc.h>
#include <asm/cpu_ops.h>
#include <asm/smp_plat.h>
#include <asm/sections.h>
#include <asm/memory.h>
#include <asm/cacheflush.h>

extern void secondary_entry(void);

#define INFRACFG_AO_PHYS		0x10001000ULL
#define BOOTROM_BOOT_ADDR_OFFSET	0x800

#define SPM_PHYS			0x10006000ULL
#define SPM_POWERON_CONFIG_SET		0x000
#define SPM_PWR_STATUS			0x60c
#define SPM_PWR_STATUS_2ND		0x610
#define SPM_PROJECT_CODE		0x0b16

/* PWR_CON bit flags */
#define PWR_RST_B			BIT(0)
#define PWR_ISO				BIT(1)
#define PWR_ON				BIT(2)
#define PWR_ON_2ND			BIT(3)
#define PWR_CLK_DIS			BIT(4)
#define SRAM_CKISO			BIT(5)
#define SRAM_ISOINT_B			BIT(6)

/* Cluster 0 L1_PDN bits */
#define L1_PDN				BIT(0)
#define L1_PDN_ACK			BIT(8)

#define ATF_CPU_JUMP_TABLE_PHYS		0x43018340ULL
#define ATF_CPU_FLAG_PHYS		0x430127a0ULL
#define MTK_SIP_KERNEL_MCUSYS_WRITE	0x82000201UL
#define MTK_SIP_KERNEL_CPU_RST_B	0x82000213UL

static void mt6753_smc_write(u32 phys_addr, u32 val)
{
	struct arm_smccc_res res;
	arm_smccc_smc(MTK_SIP_KERNEL_MCUSYS_WRITE, phys_addr, val, 0,
		      0, 0, 0, 0, &res);
}

static int mt6753_cpu_init(unsigned int cpu)
{
	pr_emerg("mt-boot: CPU%u init\n", cpu);
	return 0;
}

static int mt6753_cpu_prepare(unsigned int cpu)
{
	u64 entry_phys = __pa_symbol(secondary_entry);
	u64 target_phys = ATF_CPU_JUMP_TABLE_PHYS + (cpu * 64);
	volatile u64 *table_entry;
	volatile u32 *atf_flag;

	/* 1. Clear ACINACTM on MP0_AXI_CONFIG for Cluster 0 coherency */
	if (cpu < 4)
		mt6753_smc_write(0x1020002c, 0);

	/* 2. Clear ATF spin flag at 0x430127a0 so core won't branch to wfi loop */
	atf_flag = (volatile u32 *)phys_to_virt(ATF_CPU_FLAG_PHYS);
	*atf_flag = 0;
	dcache_clean_inval_poc((unsigned long)atf_flag, (unsigned long)(atf_flag + 1));

	/* 3. Write physical address of secondary_entry into ATF CPU jump table in DRAM */
	table_entry = (volatile u64 *)phys_to_virt(target_phys);
	*table_entry = entry_phys;
	dcache_clean_inval_poc((unsigned long)table_entry,
			       (unsigned long)(table_entry + 1));
	dmb(sy);

	pr_emerg("mt-boot: set ATF jump table [CPU%u] at 0x%llx = 0x%08llx\n",
		 cpu, target_phys, entry_phys);
	return 0;
}

static int mt6753_power_on_cluster0_core(void __iomem *spm, unsigned int cpu)
{
	u32 pwr_con_offset, l1_pdn_offset, pwr_mask;
	u32 val;
	int ret;

	switch (cpu) {
	case 1:
		pwr_con_offset = 0x218;
		l1_pdn_offset = 0x264;
		pwr_mask = BIT(12);
		break;
	case 2:
		pwr_con_offset = 0x21c;
		l1_pdn_offset = 0x26c;
		pwr_mask = BIT(11);
		break;
	case 3:
		pwr_con_offset = 0x220;
		l1_pdn_offset = 0x274;
		pwr_mask = BIT(10);
		break;
	default:
		return -EINVAL;
	}

	/* 1. Set PWR_ON */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | PWR_ON, spm + pwr_con_offset);
	udelay(2);

	/* 2. Set PWR_ON_2ND */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | PWR_ON_2ND, spm + pwr_con_offset);

	/* 3. Wait for power status */
	ret = readl_relaxed_poll_timeout_atomic(spm + SPM_PWR_STATUS, val,
						(val & pwr_mask) == pwr_mask,
						1, 50000);
	if (ret)
		pr_emerg("mt-boot: CPU%u power on timeout (status=0x%08x)\n",
			 cpu, val);

	/* 4. Clear PWR_ISO */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val & ~PWR_ISO, spm + pwr_con_offset);

	/* 5. Clear L1_PDN */
	val = readl_relaxed(spm + l1_pdn_offset);
	writel_relaxed(val & ~L1_PDN, spm + l1_pdn_offset);

	/* 6. Wait for L1_PDN_ACK clear */
	ret = readl_relaxed_poll_timeout_atomic(spm + l1_pdn_offset, val,
						!(val & L1_PDN_ACK),
						1, 50000);
	if (ret)
		pr_emerg("mt-boot: CPU%u L1 PDN ACK timeout\n", cpu);

	udelay(2);

	/* 7. Configure SRAM isolation, clock, and release reset */
	val = readl_relaxed(spm + pwr_con_offset);
	val |= SRAM_ISOINT_B;
	val &= ~SRAM_CKISO;
	val &= ~PWR_CLK_DIS;
	val |= PWR_RST_B;
	writel_relaxed(val, spm + pwr_con_offset);

	return 0;
}

static int mt6753_power_on_cluster1_top(void __iomem *spm)
{
	u32 val;
	int ret;

	/* If Cluster 1 Top is already powered on, nothing to do */
	val = readl_relaxed(spm + SPM_PWR_STATUS);
	if (val & BIT(16))
		return 0;

	pr_emerg("mt-boot: powering on Cluster 1 Top (CPUSYS1)...\n");

	/* 1. Set PWR_ON */
	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val | PWR_ON, spm + 0x2b0);
	dsb(sy);
	udelay(2);

	/* 2. Set PWR_ON_2ND */
	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val | PWR_ON_2ND, spm + 0x2b0);
	dsb(sy);

	/* 3. Wait for both PWR_STATUS and PWR_STATUS_2ND bit 16 */
	ret = readl_relaxed_poll_timeout_atomic(spm + SPM_PWR_STATUS, val,
						(val & BIT(16)), 1, 50000);
	if (ret)
		pr_emerg("mt-boot: Cluster 1 Top power on timeout (0x%08x)\n", val);

	readl_relaxed_poll_timeout_atomic(spm + SPM_PWR_STATUS_2ND, val,
					  (val & BIT(16)), 1, 50000);

	/* 4. Clear PWR_ISO */
	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val & ~PWR_ISO, spm + 0x2b0);
	dsb(sy);

	/* 5. Clear L2 cache power down (bit 2 then bit 0) */
	val = readl_relaxed(spm + 0x2b8);
	writel_relaxed(val & ~BIT(2), spm + 0x2b8);
	dsb(sy);

	val = readl_relaxed(spm + 0x2b8);
	writel_relaxed(val & ~BIT(0), spm + 0x2b8);
	dsb(sy);

	readl_relaxed_poll_timeout_atomic(spm + 0x2b8, val,
					  !(val & BIT(0)), 1, 50000);
	udelay(2);

	/* 6. SRAM isolation and clock */
	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val | SRAM_ISOINT_B, spm + 0x2b0);
	dsb(sy);
	udelay(2);

	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val & ~SRAM_CKISO, spm + 0x2b0);
	dsb(sy);

	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val & ~PWR_CLK_DIS, spm + 0x2b0);
	dsb(sy);

	val = readl_relaxed(spm + 0x2b0);
	writel_relaxed(val | PWR_RST_B, spm + 0x2b0);
	dsb(sy);

	return 0;
}

static int mt6753_power_on_cluster1_core(void __iomem *spm, unsigned int cpu)
{
	u32 pwr_con_offset, pwr_mask, core_idx;
	u32 val;
	int ret;

	mt6753_power_on_cluster1_top(spm);

	switch (cpu) {
	case 4:
		pwr_con_offset = 0x2a0;
		pwr_mask = BIT(20);
		core_idx = 0;
		break;
	case 5:
		pwr_con_offset = 0x2a4;
		pwr_mask = BIT(19);
		core_idx = 1;
		break;
	case 6:
		pwr_con_offset = 0x2a8;
		pwr_mask = BIT(18);
		core_idx = 2;
		break;
	case 7:
		pwr_con_offset = 0x2ac;
		pwr_mask = BIT(17);
		core_idx = 3;
		break;
	default:
		return -EINVAL;
	}

	/* 1. Set PWR_ON */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | PWR_ON, spm + pwr_con_offset);
	dsb(sy);
	udelay(2);

	/* 2. Set PWR_ON_2ND */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | PWR_ON_2ND, spm + pwr_con_offset);
	dsb(sy);

	/* 3. Wait for power status */
	ret = readl_relaxed_poll_timeout_atomic(spm + SPM_PWR_STATUS, val,
						(val & pwr_mask) == pwr_mask,
						1, 50000);
	if (ret)
		pr_emerg("mt-boot: CPU%u power on timeout (status=0x%08x)\n",
			 cpu, val);

	readl_relaxed_poll_timeout_atomic(spm + SPM_PWR_STATUS_2ND, val,
					  (val & pwr_mask) == pwr_mask,
					  1, 50000);

	/* 4. Clear PWR_ISO */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val & ~PWR_ISO, spm + pwr_con_offset);
	dsb(sy);

	/* 5. Clear L1 power down on Cluster 1 (0x2b4) */
	val = readl_relaxed(spm + 0x2b4);
	writel_relaxed(val & ~BIT(core_idx + 4), spm + 0x2b4);
	dsb(sy);

	val = readl_relaxed(spm + 0x2b4);
	writel_relaxed(val & ~BIT(core_idx), spm + 0x2b4);
	dsb(sy);

	ret = readl_relaxed_poll_timeout_atomic(spm + 0x2b4, val,
						!(val & BIT(core_idx + 16)),
						1, 50000);
	if (ret)
		pr_emerg("mt-boot: CPU%u L1 PDN ACK timeout\n", cpu);

	udelay(2);

	/* 6. Configure SRAM isolation, clock, and release core reset */
	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | SRAM_ISOINT_B, spm + pwr_con_offset);
	dsb(sy);
	udelay(2);

	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val & ~SRAM_CKISO, spm + pwr_con_offset);
	dsb(sy);

	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val & ~PWR_CLK_DIS, spm + pwr_con_offset);
	dsb(sy);

	val = readl_relaxed(spm + pwr_con_offset);
	writel_relaxed(val | PWR_RST_B, spm + pwr_con_offset);
	dsb(sy);

	return 0;
}

static int mt6753_cpu_boot(unsigned int cpu)
{
	void __iomem *spm;
	struct arm_smccc_res res;
	int ret;

	if (cpu == 0 || cpu >= 8)
		return -EINVAL;

	spm = ioremap(SPM_PHYS, 0x1000);
	if (!spm) {
		pr_emerg("mt-boot: failed to ioremap SPM\n");
		return -ENOMEM;
	}

	/* Unlock SPM register writes */
	writel_relaxed((SPM_PROJECT_CODE << 16) | 1U,
		       spm + SPM_POWERON_CONFIG_SET);

	/* 1. If starting Cluster 1, power on Cluster 1 Top (CPUSYS1) FIRST */
	if (cpu >= 4) {
		ret = mt6753_power_on_cluster1_top(spm);
		if (ret) {
			iounmap(spm);
			return ret;
		}
	}

	/* 2. Invoke standard PSCI cpu_on with cluster hardware already powered */
	if (psci_ops.cpu_on) {
		ret = psci_ops.cpu_on(cpu_logical_map(cpu),
				      __pa_symbol(secondary_entry));
		pr_emerg("mt-boot: PSCI cpu_on(CPU%u) returned %d\n", cpu, ret);
		if (ret < 0 && ret != -EEXIST) {
			iounmap(spm);
			return ret;
		}
	} else {
		pr_emerg("mt-boot: psci_ops.cpu_on is NULL\n");
	}

	/* 3. Power on the individual core */
	pr_emerg("mt-boot: powering on CPU%u via MTCMOS...\n", cpu);

	if (cpu < 4)
		ret = mt6753_power_on_cluster0_core(spm, cpu);
	else
		ret = mt6753_power_on_cluster1_core(spm, cpu);

	iounmap(spm);

	if (ret)
		return ret;

	if (cpu < 4) {
		pr_emerg("mt-boot: releasing reset for CPU%u via SMC...\n", cpu);
		arm_smccc_smc(MTK_SIP_KERNEL_CPU_RST_B, cpu, 0, 0, 0, 0, 0, 0, &res);
		pr_emerg("mt-boot: SMC returned %ld\n", (long)res.a0);
	}

	/* Send event to wake up CPU from holding pen / WFE */
	dmb(sy);
	sev();

	return 0;
}

const struct cpu_operations mt6753_smp_ops = {
	.name		= "mt-boot",
	.cpu_init	= mt6753_cpu_init,
	.cpu_prepare	= mt6753_cpu_prepare,
	.cpu_boot	= mt6753_cpu_boot,
};
