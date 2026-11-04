// SPDX-License-Identifier: GPL-2.0-only
/* Persistent low-volume checkpoints for the Blu Vivo 5R diagnostic boots. */

#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/vivo5r_checkpoint.h>

#define VIVO5R_CHECKPOINT_PHYS	0x43f10000ULL
#define VIVO5R_CHECKPOINT_SIZE	0x1000
#define VIVO5R_CHECKPOINT_MAGIC	0x56504350U /* VPCP */
#define VIVO5R_CHECKPOINT_VERSION	1
#define VIVO5R_CHECKPOINT_SLOT_SIZE	64
#define VIVO5R_CHECKPOINT_SLOTS	2

struct vivo5r_checkpoint_record {
	__le32 magic;
	__le16 version;
	__le16 size;
	__le32 sequence;
	__le32 stage;
	__le32 value;
	__le32 crc;
	u8 reserved[VIVO5R_CHECKPOINT_SLOT_SIZE - 24];
} __packed;

static void __iomem *vivo5r_checkpoint_base;
static DEFINE_RAW_SPINLOCK(vivo5r_checkpoint_lock);
static u32 vivo5r_checkpoint_sequence = 1;
static unsigned int vivo5r_checkpoint_slot;

static u32 vivo5r_checkpoint_crc(const struct vivo5r_checkpoint_record *record)
{
	return crc32_le(~0, (const u8 *)record,
			offsetof(struct vivo5r_checkpoint_record, crc));
}

static bool vivo5r_checkpoint_valid(const struct vivo5r_checkpoint_record *record)
{
	return le32_to_cpu(record->magic) == VIVO5R_CHECKPOINT_MAGIC &&
		le16_to_cpu(record->version) == VIVO5R_CHECKPOINT_VERSION &&
		le16_to_cpu(record->size) == sizeof(*record) &&
		le32_to_cpu(record->crc) == vivo5r_checkpoint_crc(record);
}

static bool vivo5r_checkpoint_read(unsigned int slot,
					struct vivo5r_checkpoint_record *record)
{
	memcpy_fromio(record,
			      vivo5r_checkpoint_base + slot * VIVO5R_CHECKPOINT_SLOT_SIZE,
			      sizeof(*record));
	return vivo5r_checkpoint_valid(record);
}

static void vivo5r_checkpoint_write_locked(u32 stage, u32 value)
{
	struct vivo5r_checkpoint_record record = {
		.magic = cpu_to_le32(VIVO5R_CHECKPOINT_MAGIC),
		.version = cpu_to_le16(VIVO5R_CHECKPOINT_VERSION),
		.size = cpu_to_le16(sizeof(record)),
		.sequence = cpu_to_le32(vivo5r_checkpoint_sequence),
		.stage = cpu_to_le32(stage),
		.value = cpu_to_le32(value),
	};
	void __iomem *slot = vivo5r_checkpoint_base +
		vivo5r_checkpoint_slot * VIVO5R_CHECKPOINT_SLOT_SIZE;
	u32 crc = vivo5r_checkpoint_crc(&record);

	iowrite32(0, slot + offsetof(struct vivo5r_checkpoint_record, crc));
	wmb();
	memcpy_toio(slot, &record,
			offsetof(struct vivo5r_checkpoint_record, crc));
	wmb();
	iowrite32(crc, slot + offsetof(struct vivo5r_checkpoint_record, crc));

	vivo5r_checkpoint_sequence++;
	vivo5r_checkpoint_slot ^= 1;
}

void vivo5r_checkpoint(u32 stage, u32 value)
{
	unsigned long flags;

	if (!vivo5r_checkpoint_base)
		return;

	raw_spin_lock_irqsave(&vivo5r_checkpoint_lock, flags);
	vivo5r_checkpoint_write_locked(stage, value);
	raw_spin_unlock_irqrestore(&vivo5r_checkpoint_lock, flags);
}
EXPORT_SYMBOL_GPL(vivo5r_checkpoint);

static int __init vivo5r_checkpoint_init(void)
{
	struct vivo5r_checkpoint_record first, second;
	const struct vivo5r_checkpoint_record *previous = NULL;

	vivo5r_checkpoint_base = ioremap(VIVO5R_CHECKPOINT_PHYS,
					 VIVO5R_CHECKPOINT_SIZE);
	if (!vivo5r_checkpoint_base) {
		pr_emerg("Vivo5R checkpoint: ioremap failed\n");
		return -ENOMEM;
	}

	if (vivo5r_checkpoint_read(0, &first))
		previous = &first;
	if (vivo5r_checkpoint_read(1, &second) &&
	    (!previous || (s32)(le32_to_cpu(second.sequence) -
				le32_to_cpu(previous->sequence)) > 0))
		previous = &second;

	if (previous) {
		vivo5r_checkpoint_sequence = le32_to_cpu(previous->sequence) + 1;
		vivo5r_checkpoint_slot = (le32_to_cpu(previous->sequence) + 1) & 1;
		pr_emerg("Vivo5R checkpoint: previous seq=%u stage=0x%04x value=0x%08x\n",
			le32_to_cpu(previous->sequence),
			le32_to_cpu(previous->stage),
			le32_to_cpu(previous->value));
	} else {
		pr_emerg("Vivo5R checkpoint: no valid previous record\n");
	}

	vivo5r_checkpoint(VIVO5R_CP_BOOT, 0);
	return 0;
}
early_initcall(vivo5r_checkpoint_init);
