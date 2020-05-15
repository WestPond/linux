/*
 * Copyright (c) 2017 Free Electrons
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 *	Boris Brezillon <boris.brezillon@free-electrons.com>
 *	Peter Pan <peterpandong@micron.com>
 */

#define pr_fmt(fmt)	"nand-bbt: " fmt

#include <linux/mtd/nand.h>
#include <linux/slab.h>
#include <linux/mtd/spinand.h>

int nanddev_bbt_init(struct nand_device *nand)
{
	unsigned int nwords = nanddev_neraseblocks(nand);

	nand->bbt.cache = kzalloc(nwords, GFP_KERNEL);
	if (!nand->bbt.cache)
		return -ENOMEM;
	memset(nand->bbt.cache,0,nwords);
	return 0;
}
EXPORT_SYMBOL_GPL(nanddev_bbt_init);

void nanddev_bbt_cleanup(struct nand_device *nand)
{
	kfree(nand->bbt.cache);
}
EXPORT_SYMBOL_GPL(nanddev_bbt_cleanup);

int nanddev_bbt_update(struct nand_device *nand)
{
	return 0;
}
EXPORT_SYMBOL_GPL(nanddev_bbt_update);

int nanddev_bbt_get_block_status(const struct nand_device *nand,
				 unsigned int entry)
{
	unsigned char *pos = nand->bbt.cache + entry;
	unsigned long status;

	if (entry >= nanddev_neraseblocks(nand)){
		return -ERANGE;
		}

	status = pos[0];


	return status & 0xff;
}
EXPORT_SYMBOL_GPL(nanddev_bbt_get_block_status);

int nanddev_bbt_set_block_status(struct nand_device *nand, unsigned int entry,
				 enum nand_bbt_block_status status)
{
	unsigned char *pos = nand->bbt.cache + entry;;

	if (entry >= nanddev_neraseblocks(nand)){
		return -ERANGE;
		}

	pos[0] = status & 0xff;

	return 0;
}
EXPORT_SYMBOL_GPL(nanddev_bbt_set_block_status);
