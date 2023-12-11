// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/buffer_head.h>

#include "exfat_fs.h"

#if BITS_PER_LONG == 32
#define __le_long __le32
#define lel_to_cpu(A) le32_to_cpu(A)
#define cpu_to_lel(A) cpu_to_le32(A)
#elif BITS_PER_LONG == 64
#define __le_long __le64
#define lel_to_cpu(A) le64_to_cpu(A)
#define cpu_to_lel(A) cpu_to_le64(A)
#else
#error "BITS_PER_LONG not 32 or 64"
#endif

/*
 *  Allocation Bitmap Management Functions
 */
static int exfat_allocate_bitmap(struct super_block *sb,
		struct exfat_dentry *ep)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long long map_size;
	unsigned int i, need_map_size;
	sector_t sector;

	sbi->map_clu = le32_to_cpu(ep->dentry.bitmap.start_clu);
	map_size = le64_to_cpu(ep->dentry.bitmap.size);
	need_map_size = ((EXFAT_DATA_CLUSTER_COUNT(sbi) - 1) / BITS_PER_BYTE)
		+ 1;
	if (need_map_size != map_size) {
		exfat_err(sb, "bogus allocation bitmap size(need : %u, cur : %lld)",
			  need_map_size, map_size);
		/*
		 * Only allowed when bogus allocation
		 * bitmap size is large
		 */
		if (need_map_size > map_size)
			return -EIO;
	}
	sbi->map_sectors = ((need_map_size - 1) >>
			(sb->s_blocksize_bits)) + 1;
	sbi->vol_amap = kmalloc_array(sbi->map_sectors,
				sizeof(struct buffer_head *), GFP_KERNEL);
	if (!sbi->vol_amap)
		return -ENOMEM;

	sector = exfat_cluster_to_sector(sbi, sbi->map_clu);
	for (i = 0; i < sbi->map_sectors; i++) {
		sbi->vol_amap[i] = sb_bread(sb, sector + i);
		if (!sbi->vol_amap[i]) {
			/* release all buffers and free vol_amap */
			int j = 0;

			while (j < i)
				brelse(sbi->vol_amap[j++]);

			kfree(sbi->vol_amap);
			sbi->vol_amap = NULL;
			return -EIO;
		}
	}

	return 0;
}

int exfat_load_bitmap(struct super_block *sb)
{
	unsigned int i, type;
	struct exfat_chain clu;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	exfat_chain_set(&clu, sbi->root_dir, 0, ALLOC_FAT_CHAIN);
	while (clu.dir != EXFAT_EOF_CLUSTER) {
		for (i = 0; i < sbi->dentries_per_clu; i++) {
			struct exfat_dentry *ep;
			struct buffer_head *bh;

			ep = exfat_get_dentry(sb, &clu, i, &bh, NULL);
			if (!ep)
				return -EIO;

			type = exfat_get_entry_type(ep);
			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_BITMAP)
				continue;
			if (ep->dentry.bitmap.flags == 0x0) {
				int err;

				err = exfat_allocate_bitmap(sb, ep);
				brelse(bh);
				return err;
			}
			brelse(bh);
		}

		if (exfat_get_next_cluster(sb, &clu.dir))
			return -EIO;
	}

	return -EINVAL;
}

void exfat_free_bitmap(struct exfat_sb_info *sbi)
{
	int i;

	for (i = 0; i < sbi->map_sectors; i++)
		__brelse(sbi->vol_amap[i]);

	kfree(sbi->vol_amap);
}

/*
 * If the value of "clu" is 0, it means cluster 2 which is the first cluster of
 * the cluster heap.
 */
int exfat_set_bitmap(struct inode *inode, unsigned int clu)
{
	int i, b;
	unsigned int ent_idx;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	WARN_ON(clu < EXFAT_FIRST_CLUSTER);
	ent_idx = CLUSTER_TO_BITMAP_ENT(clu);
	i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	b = BITMAP_OFFSET_BIT_IN_SECTOR(sb, ent_idx);

	set_bit_le(b, sbi->vol_amap[i]->b_data);
	exfat_update_bh(sbi->vol_amap[i], IS_DIRSYNC(inode));
	return 0;
}

/*
 * If the value of "clu" is 0, it means cluster 2 which is the first cluster of
 * the cluster heap.
 */
void exfat_clear_bitmap(struct inode *inode, unsigned int clu)
{
	int i, b;
	unsigned int ent_idx;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *opts = &sbi->options;

	WARN_ON(clu < EXFAT_FIRST_CLUSTER);
	ent_idx = CLUSTER_TO_BITMAP_ENT(clu);
	i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	b = BITMAP_OFFSET_BIT_IN_SECTOR(sb, ent_idx);

	clear_bit_le(b, sbi->vol_amap[i]->b_data);
	exfat_update_bh(sbi->vol_amap[i], IS_DIRSYNC(inode));

	if (opts->discard) {
		int ret_discard;

		ret_discard = sb_issue_discard(sb,
			exfat_cluster_to_sector(sbi, clu +
						EXFAT_RESERVED_CLUSTERS),
			(1 << sbi->sect_per_clus_bits), GFP_NOFS, 0);

		if (ret_discard == -EOPNOTSUPP) {
			exfat_err(sb, "discard not supported by device, disabling");
			opts->discard = 0;
		}
	}
}

/*
 * If the value of "clu" is 0, it means cluster 2 which is the first cluster of
 * the cluster heap.
 */
unsigned int exfat_find_free_bitmap(struct super_block *sb, unsigned int clu)
{
	unsigned int i, map_i, map_b, ent_idx;
	unsigned int clu_base, clu_free;
	unsigned long clu_bits, clu_mask;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	__le_long bitval;

	WARN_ON(clu < EXFAT_FIRST_CLUSTER);
	ent_idx = ALIGN_DOWN(CLUSTER_TO_BITMAP_ENT(clu), BITS_PER_LONG);
	clu_base = BITMAP_ENT_TO_CLUSTER(ent_idx);
	clu_mask = IGNORED_BITS_REMAINED(clu, clu_base);

	map_i = BITMAP_OFFSET_SECTOR_INDEX(sb, ent_idx);
	map_b = BITMAP_OFFSET_BYTE_IN_SECTOR(sb, ent_idx);

	for (i = EXFAT_FIRST_CLUSTER; i < sbi->num_clusters;
	     i += BITS_PER_LONG) {
		bitval = *(__le_long *)(sbi->vol_amap[map_i]->b_data + map_b);
		if (clu_mask > 0) {
			bitval |= cpu_to_lel(clu_mask);
			clu_mask = 0;
		}
		if (lel_to_cpu(bitval) != ULONG_MAX) {
			clu_bits = lel_to_cpu(bitval);
			clu_free = clu_base + ffz(clu_bits);
			if (clu_free < sbi->num_clusters)
				return clu_free;
		}
		clu_base += BITS_PER_LONG;
		map_b += sizeof(long);

		if (map_b >= sb->s_blocksize ||
		    clu_base >= sbi->num_clusters) {
			if (++map_i >= sbi->map_sectors) {
				clu_base = EXFAT_FIRST_CLUSTER;
				map_i = 0;
			}
			map_b = 0;
		}
	}

	return EXFAT_EOF_CLUSTER;
}

int exfat_count_used_clusters(struct super_block *sb, unsigned int *ret_count)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned int count = 0;
	unsigned int i, map_i = 0, map_b = 0;
	unsigned int total_clus = EXFAT_DATA_CLUSTER_COUNT(sbi);
	unsigned int last_mask = total_clus & (BITS_PER_LONG - 1);
	unsigned long *bitmap, clu_bits;

	total_clus &= ~last_mask;
	for (i = 0; i < total_clus; i += BITS_PER_LONG) {
		bitmap = (void *)(sbi->vol_amap[map_i]->b_data + map_b);
		count += hweight_long(*bitmap);
		map_b += sizeof(long);
		if (map_b >= (unsigned int)sb->s_blocksize) {
			map_i++;
			map_b = 0;
		}
	}

	if (last_mask) {
		bitmap = (void *)(sbi->vol_amap[map_i]->b_data + map_b);
		clu_bits = lel_to_cpu(*(__le_long *)bitmap);
		count += hweight_long(clu_bits & BITMAP_LAST_WORD_MASK(last_mask));
	}

	*ret_count = count;
	return 0;
}
