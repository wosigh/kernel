/*
 * linux/arch/arm/mach-omap3pe/id.c
 *
 * Copyright (C) 2008-2009 Palm, Inc.
 *
 * Based on OMAP2/3 CPU identification code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/arch/control.h>

#include <asm/io.h>

unsigned int scalability_status;
static unsigned int omap_revision_legacy;
static unsigned int omap_revision;

unsigned int omap_rev(void)
{
	return omap_revision_legacy;
}
EXPORT_SYMBOL(omap_rev);

unsigned int omap3_rev(void)
{
	return omap_revision;
}
EXPORT_SYMBOL(omap3_rev);

/*----------------------------------------------------------------------------*/


#if defined(CONFIG_ARCH_OMAP34XX) || defined(CONFIG_ARCH_OMAP36XX)
#define TAP_BASE		io_p2v(0x4830A000)
#endif

#define OMAP_TAP_IDCODE		0x0204
#if defined(CONFIG_ARCH_OMAP34XX) || defined(CONFIG_ARCH_OMAP36XX)
#define OMAP_TAP_PROD_ID	0x0210
#else
#define OMAP_TAP_PROD_ID	0x0208
#endif

#define OMAP_TAP_DIE_ID_0	0x0218
#define OMAP_TAP_DIE_ID_1	0x021C
#define OMAP_TAP_DIE_ID_2	0x0220
#define OMAP_TAP_DIE_ID_3	0x0224

struct omap_id {
	u16	hawkeye;	/* Silicon type (Hawkeye id) */
	u8	dev;		/* Device revision */
	u32	type;		/* combined type id copied to system_rev */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .hawkeye = 0xb6d6, .dev = 0x0, .type = 0x34300000 },
	{ .hawkeye = 0xb7ae, .dev = 0x1, .type = 0x34301000 },
	{ .hawkeye = 0xb7ae, .dev = 0x2, .type = 0x34302000 },
	{ .hawkeye = 0xb7ae, .dev = 0x3, .type = 0x34303000 },
	{ .hawkeye = 0xb891, .dev = 0x0, .type = 0x36300000 }, 
	{ .hawkeye = 0xb891, .dev = 0x1, .type = 0x36301000 },
	{ .hawkeye = 0xb891, .dev = 0x2, .type = 0x36302000 },
};

static u32 __init read_tap_reg(int reg)
{
	unsigned int regval = 0;
	u32 cpuid;

	/* Reading the IDCODE register on 3430 ES1 results in a
	 * data abort as the register is not exposed on the OCP
	 * Hence reading the Cortex Rev
	 */
	cpuid = read_cpuid(CPUID_ID);

	/* If the processor type is Cortex-A8 and the revision is 0x0
	 * it means its Cortex r0p0 which is 3430 ES1
	 */
	if ((((cpuid >> 4) & 0xFFF) == 0xC08) && ((cpuid & 0xF) == 0x0)) {
		switch (reg) {
		/* lets return the value from TRM  */
		case OMAP_TAP_IDCODE  : regval = 0x0B6D602F; break;
		/* Making DevType as 0xF in ES1 to differ from ES2 */
		case OMAP_TAP_PROD_ID : regval = 0x000F00F0; break;
		case OMAP_TAP_DIE_ID_0: regval = 0x01000000; break;
		case OMAP_TAP_DIE_ID_1: regval = 0x1012d687; break;
		case OMAP_TAP_DIE_ID_2: regval = 0x00000000; break;
		case OMAP_TAP_DIE_ID_3: regval = 0x2d2c0000; break;
		}
	} else {
		regval = __raw_readl(TAP_BASE + reg);
	}

	return regval;

}


void __init omap_cpuinfo(void)
{
	u32 dev_rev ;
	
	if (cpu_is_omap36xx()) {
		pr_info("OMAP%04x", GET_OMAP3_TYPE);
		dev_rev  = get_omap3_rev();
		switch (dev_rev) {
			case 0:  printk("ES1.0"); break;
			case 1:  printk("ES1.1"); break;
			case 2:  printk("ES1.2"); break;
			default: 
				 printk("ES(0x%x)", dev_rev );
			break;
		}
	} else 	if (cpu_is_omap34xx()) {
		pr_info("OMAP%04x", GET_OMAP_TYPE);
		dev_rev  = get_sil_rev();
		switch (dev_rev) {
			case 0:  printk("ES1.0"); break;
			case 1:  printk("ES2.0"); break;
			case 2:  printk("ES2.1"); break;
			case 3:  printk("ES3.0"); break;
			case 4:  printk("ES3.1"); break;
			default: 
				 printk("ES(0x%x)", dev_rev );
			break;
		}
	} else {
		pr_info("OMAP(%08x)", omap_rev());
	}
	
	printk("\n");
}

void __init omap2_check_revision(void)
{
	int ctrl_status = 0;
	int i, j;
	u32 idcode;
	u32 prod_id;
	u16 hawkeye;
	u8  dev_type;
	u8  dev_rev;

	idcode   = read_tap_reg(OMAP_TAP_IDCODE);
	prod_id  = read_tap_reg(OMAP_TAP_PROD_ID);
	hawkeye  = (idcode >> 12) & 0xffff;
	dev_rev  = (idcode >> 28) & 0x0f;
	dev_type = (prod_id >> 16) & 0x0f;

#ifdef DEBUG
	printk(KERN_INFO "OMAP_TAP_IDCODE 0x%08x REV %i HAWKEYE 0x%04x MANF %03x\n",
		idcode, dev_rev, hawkeye, (idcode >> 1) & 0x7ff);
	printk(KERN_INFO "OMAP_TAP_DIE_ID_0: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_0));
	printk(KERN_INFO "OMAP_TAP_DIE_ID_1: 0x%08x DEV_REV: %i\n",
		read_tap_reg(OMAP_TAP_DIE_ID_1),
	       (read_tap_reg(OMAP_TAP_DIE_ID_1) >> 28) & 0xf);
	printk(KERN_INFO "OMAP_TAP_DIE_ID_2: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_2));
	printk(KERN_INFO "OMAP_TAP_DIE_ID_3: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_3));
	printk(KERN_INFO "OMAP_TAP_PROD_ID_0: 0x%08x DEV_TYPE: %i\n",
		prod_id, dev_type);
#endif

	/* Check hawkeye ids */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (hawkeye == omap_ids[i].hawkeye)
			break;
	}

	if (i == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP CPU id\n");
		return;
	}

	for (j = i; j < ARRAY_SIZE(omap_ids); j++) {
		if (dev_rev == omap_ids[j].dev)
			break;
	}

	if (j == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP device type. "
				"Handling it as OMAP%04x\n",
				omap_ids[i].type >> 16);
		j = i;
	}

	/*
	 * omap_revision encoding is as follows
	 * omap_revision & 0xff000000 -> Omap Class (24xx/34xx)
	 * omap_revision & 0xfff00000 -> Omap Sub Class (242x/343x)
	 * omap_revision & 0xffff0000 -> Omap type (2420/2422/2423/2430/3430/3630)
	 * omap_revision & 0x0000f000 -> Silicon revision (ES1, ES2 )
	 * omap_revision & 0x00000700 -> Device Type ( EMU/HS/GP/BAD )
	 * omap_revision & 0x0000003f -> sys_boot[0:5]
	 */
	/* Embedding the ES revision info in type field */
	omap_revision = omap_ids[j].type;

	ctrl_status = omap_ctrl_readl(OMAP343X_CONTROL_STATUS);
	omap_revision |= (ctrl_status & 0x3f);
	omap_revision |= (ctrl_status & 0x700);

	omap_revision_legacy = omap_revision;

#ifdef CONFIG_MACH_SIRLOIN_3630
	/* For SIRLOIN3630 devices give a fake legacy omap revision.
         * All 3630 macros will work with real (non-legacy) revision
         */
	if (0x34304332 != omap_revision) {
		printk("omap_revision is 0x%08x, patching legacy revision to 0x34304332\n", 
		        omap_revision );
		omap_revision_legacy = 0x34304332;
	}
#endif

#ifdef CONFIG_ARCH_OMAP34XX
	/* Reading the scalability status register for versions
	 * greater than 3430 ES1 so as as to identify whether running
	 * in 3410 or 3420 mode
	 */
	if(is_sil_rev_greater_than(OMAP3430_REV_ES1_0)) {
		scalability_status =
			omap_ctrl_readl(OMAP343X_CONTROL_SCALABLE_OMAP_STATUS) & 0xFFFF;
		if (((scalability_status >> 8) & 0x3) == 0x2)
		       scalability_status |= OMAP3410;
		else if (((scalability_status >> 8) & 0x3) == 0x1)
			scalability_status |= OMAP3420;
	}
#endif

	/* show omap cpuinfo */
	omap_cpuinfo();
	
}

