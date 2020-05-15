/*
 *
 * Copyright (c) 2016-2017 Micron Technology, Inc.
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
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_ETRON		0xD5

struct etron_spinand_info {
	char *name;
	u8 dev_id;
	struct nand_memory_organization memorg;
	struct nand_ecc_req eccreq;
	unsigned int rw_mode;
};

#define ETRON_SPI_NAND_INFO(nm, did, mo, er, rwm)			\
	{								\
		.name = (nm),						\
		.dev_id = (did),					\
		.memorg = mo,						\
		.eccreq = er,						\
		.rw_mode = (rwm)					\
	}

static const struct etron_spinand_info etron_spinand_table[] = {
	ETRON_SPI_NAND_INFO("ETNORxxxx", 0x11,
			     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
			     NAND_ECCREQ(8, 512),
			     SPINAND_RW_COMMON),
};

static int etron_spinand_get_dummy(struct spinand_device *spinand,
				    struct spinand_op *op)
{
	u8 opcode = op->cmd;

	switch (opcode) {
	case SPINAND_CMD_READ_FROM_CACHE:
	case SPINAND_CMD_READ_FROM_CACHE_FAST:
	case SPINAND_CMD_READ_FROM_CACHE_X2:
	case SPINAND_CMD_READ_FROM_CACHE_DUAL_IO:
	case SPINAND_CMD_READ_FROM_CACHE_X4:
	case SPINAND_CMD_READ_ID:
		return 1;

	case SPINAND_CMD_READ_FROM_CACHE_QUAD_IO:
		return 2;

	default:
		return 0;
	}
}

/**
 * etron_spinand_scan_id_table - scan SPI NAND info in id table
 * @spinand: SPI NAND device structure
 * @id: point to manufacture id and device id
 * Description:
 *   If found in id table, config device with table information.
 */
static bool etron_spinand_scan_id_table(struct spinand_device *spinand,
					 u8 dev_id)
{
	struct mtd_info *mtd = spinand_to_mtd(spinand);
	struct nand_device *nand = mtd_to_nanddev(mtd);
	struct etron_spinand_info *item;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(etron_spinand_table); i++) {
		item = (struct etron_spinand_info *)etron_spinand_table + i;
		if (dev_id != item->dev_id)
			continue;

		nand->memorg = item->memorg;
		nand->eccreq = item->eccreq;
		spinand->rw_mode = item->rw_mode;

		return true;
	}

	return false;
}

/**
 * etron_spinand_detect - initialize device related part in spinand_device
 * struct if it is Micron device.
 * @spinand: SPI NAND device structure
 */
static bool etron_spinand_detect(struct spinand_device *spinand)
{
	u8 *id = spinand->id.data;

	/*
	 * Micron SPI NAND read ID need a dummy byte,
	 * so the first byte in raw_id is dummy.
	 */
	if (id[1] != SPINAND_MFR_ETRON)
		return false;

	return etron_spinand_scan_id_table(spinand, id[2]);
}

/**
 * etron_spinand_prepare_op - Fix address for cache operation.
 * @spinand: SPI NAND device structure
 * @op: pointer to spinand_op struct
 * @page: page address
 * @column: column address
 */
static void etron_spinand_adjust_cache_op(struct spinand_device *spinand,
					   const struct nand_page_io_req *req,
					   struct spinand_op *op)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	unsigned int shift;

	op->n_addr= 2;
	op->addr[0] = op->addr[1];
	op->addr[1] = op->addr[2];
	op->addr[2] = 0;
	op->dummy_bytes = etron_spinand_get_dummy(spinand, op);
}

static const struct spinand_manufacturer_ops etron_spinand_manuf_ops = {
	.detect = etron_spinand_detect,
	.adjust_cache_op = etron_spinand_adjust_cache_op,
};

const struct spinand_manufacturer etron_spinand_manufacturer = {
	.id = SPINAND_MFR_ETRON,
	.name = "Etron",
	.ops = &etron_spinand_manuf_ops,
};
