// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include <common.h>
#include <config.h>
#include <dm.h>
#include <env.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/libfdt.h>

#define MT7622_TOPRGUSTRAP_PAR			0x10212060
#define MT7622_BOOT_SEQ_MASK			0x18
#define MT7622_BOOT_SEQ_SHIFT			3
#define MT7622_BOOT_SEQ_NOR_EMMC_SDXC		0x0
#define MT7622_BOOT_SEQ_SPI_NAND_EMMC_SDXC	0x1
#define MT7622_BOOT_SEQ_NAND_EMMC_SDXC		0x2
#define MT7622_BOOT_SEQ_SDXC_EMMC_NAND		0x3

#define MT7622_GPIO_MODE0			0x10211300
#define MT7622_GPIO_NAND_MODE_MASK		0x00f00000
#define MT7622_GPIO_NAND_MODE_SHIFT		20
#define MT7622_GPIO_NAND_MODE_EMMC		0x2
#define MT7622_GPIO_RGMII_MODE_MASK		0x0000f000
#define MT7622_GPIO_RGMII_MODE_SHIFT		12
#define MT7622_GPIO_RGMII_MODE_SDCX		0x2
#define MT7622_GPIO_SPI_MODE_MASK		0x00000f00
#define MT7622_GPIO_SPI_MODE_SHIFT		8
#define MT7622_GPIO_SPI_MODE_NAND		0x2

#define MT7622_MSDC_INT				0x1124000C
#define MT7622_MSDC_INT_BD_CS_ERR		0x200

DECLARE_GLOBAL_DATA_PTR;

static int	gpio_mode0;
static int	msdc_int;

int board_init(void)
{
	/*
	 * Save content of GPIO_MODE0 as left behind by the BootROM.
	 * Also grab MSDC1 INT status to see if BootROM has been reading
	 * from SD card.
	 * Together this will allow to infer the device used for booting.
	 */
	gpio_mode0 = readl(MT7622_GPIO_MODE0);
	msdc_int = readl(MT7622_MSDC_INT);
	return 0;
}

int board_late_init(void)
{
	gd->env_valid = 1; //to load environment variable from persistent store
	env_relocate();
	return 0;
}

int ft_system_setup(void *blob, struct bd_info *bd)
{
	bool pinctrl_set_mmc = false;
	bool pinctrl_set_snfi = false;
	bool pinctrl_set_emmc = false;
	bool msdc_bd_cs_err = false;

	const u32 *media_handle_p;
	int chosen, len, ret;
	const char *media;
	u32 media_handle, strap;

	if ((gpio_mode0 & MT7622_GPIO_RGMII_MODE_MASK) >>
	    MT7622_GPIO_RGMII_MODE_SHIFT == MT7622_GPIO_RGMII_MODE_SDCX)
		pinctrl_set_mmc = true;

	if ((gpio_mode0 & MT7622_GPIO_SPI_MODE_MASK) >>
	    MT7622_GPIO_SPI_MODE_SHIFT == MT7622_GPIO_SPI_MODE_NAND)
		pinctrl_set_snfi = true;

	if ((gpio_mode0 & MT7622_GPIO_NAND_MODE_MASK) >>
	    MT7622_GPIO_NAND_MODE_SHIFT == MT7622_GPIO_NAND_MODE_EMMC)
		pinctrl_set_emmc = true;

	if (msdc_int & MT7622_MSDC_INT_BD_CS_ERR)
		msdc_bd_cs_err = true;

	strap = readl(MT7622_TOPRGUSTRAP_PAR);
	strap &= MT7622_BOOT_SEQ_MASK;
	strap >>= MT7622_BOOT_SEQ_SHIFT;
	switch (strap) {
	case MT7622_BOOT_SEQ_NOR_EMMC_SDXC:
		if (!pinctrl_set_emmc)
			media = "rootdisk-nor";
		else if (pinctrl_set_mmc)
			media = "rootdisk-emmc";
		else
			media = "rootdisk-sd";
		break
		;;
	case MT7622_BOOT_SEQ_SPI_NAND_EMMC_SDXC:
		if (pinctrl_set_snfi)
			media = "rootdisk-snfi";
		else if (pinctrl_set_emmc)
			media = "rootdisk-emmc";
		else
			media = "rootdisk-sd";
		break
		;;
	case MT7622_BOOT_SEQ_NAND_EMMC_SDXC:
	case MT7622_BOOT_SEQ_SDXC_EMMC_NAND:
		if (!pinctrl_set_emmc && pinctrl_set_mmc)
			media = "rootdisk-nand";
		else if (pinctrl_set_emmc)
			media = "rootdisk-emmc";
		else
			media = "rootdisk-sd";
		break
		;;
	}

	chosen = fdt_path_offset(blob, "/chosen");
	if (chosen <= 0)
		return 0;

	media_handle_p = fdt_getprop(blob, chosen, media, &len);
	if (media_handle_p <= 0 || len != 4)
		return 0;

	media_handle = *media_handle_p;
	ret = fdt_setprop(blob, chosen, "rootdisk", &media_handle, sizeof(media_handle));
	if (ret) {
		printf("cannot set media phandle %s as rootdisk /chosen node\n", media);
		return ret;
	}

	printf("set /chosen/rootdisk to bootrom media: %s (phandle 0x%08x)\n", media, fdt32_to_cpu(media_handle));

	return 0;
}
