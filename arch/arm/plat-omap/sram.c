/*
 * linux/arch/arm/plat-omap/sram.c
 *
 * OMAP SRAM detection and management
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

#include <asm/tlb.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include <asm/mach/map.h>

#include <asm/arch/sram.h>
#include <asm/arch/board.h>
#include <asm-arm/cacheflush.h>

#define OMAP1_SRAM_PA		0x20000000
#define OMAP1_SRAM_VA		0xd7000000
#define OMAP2_SRAM_PA		0x40200000
#define OMAP2_SRAM_PUB_PA	0x4020f800
#define OMAP2_SRAM_VA		0xd7000000
#define OMAP2_SRAM_PUB_VA	0xd7000800
#define OMAP3_SRAM_PA		0x40200000
#define OMAP3_SRAM_VA		0xd7000000
#define OMAP3_SRAM_PUB_PA	0x40208000
#define OMAP3_SRAM_PUB_VA	0xd7008000

#if defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
#define SRAM_BOOTLOADER_SZ	0x00
#else
#define SRAM_BOOTLOADER_SZ	0x80
#endif

#if defined(CONFIG_ARCH_OMAP34XX)
#define VA_REQINFOPERM0		IO_ADDRESS(0x68012848)
#define VA_READPERM0		IO_ADDRESS(0x68012850)
#define VA_WRITEPERM0		IO_ADDRESS(0x68012858)
#define VA_ADDR_MATCH2		IO_ADDRESS(0x68012880)
#define VA_SMS_RG_ATT0		IO_ADDRESS(0x6C000048)
#define VA_CONTROL_STAT		IO_ADDRESS(0x480022F0)
#else
#define VA_REQINFOPERM0		IO_ADDRESS(0x68005048)
#define VA_READPERM0		IO_ADDRESS(0x68005050)
#define VA_WRITEPERM0		IO_ADDRESS(0x68005058)
#define VA_CONTROL_STAT		IO_ADDRESS(0x480002F8)
#endif
#define GP_DEVICE		0x300
#define TYPE_MASK		0x700

#define ROUND_DOWN(value,boundary)	((value) & (~((boundary)-1)))

static unsigned long omap_sram_start;
static unsigned long omap_sram_base;
static unsigned long omap_sram_size;
static unsigned long omap_sram_ceil;

int	omap_fb_sram_plane = -1;
int	omap_fb_sram_valid;

/* Depending on the target RAMFS firewall setup, the public usable amount of
 * SRAM varies.  The default accessable size for all device types is 2k. A GP
 * device allows ARM11 but not other initators for full size. This
 * functionality seems ok until some nice security API happens.
 */
static int is_sram_locked(void)
{
	int type = 0;

	if ((cpu_is_omap242x()) || (cpu_is_omap34xx()))
		type = __raw_readl(VA_CONTROL_STAT) & TYPE_MASK;

	if (type == GP_DEVICE) {
		/* RAMFW: R/W access to all initators for all qualifier sets */
		if (cpu_is_omap242x()) {
			__raw_writel(0xFF, VA_REQINFOPERM0); /* all q-vects */
			__raw_writel(0xCFDE, VA_READPERM0);  /* all i-read */
			__raw_writel(0xCFDE, VA_WRITEPERM0); /* all i-write */
		}
		if (cpu_is_omap34xx()) {
			__raw_writel(0xFFFF, VA_REQINFOPERM0); /* all q-vects */
			__raw_writel(0xFFFF, VA_READPERM0);  /* all i-read */
			__raw_writel(0xFFFF, VA_WRITEPERM0); /* all i-write */
#if defined(CONFIG_ARCH_OMAP34XX)
			__raw_writel(0x0, VA_ADDR_MATCH2);
			__raw_writel(0xFFFFFFFF, VA_SMS_RG_ATT0);
#endif
		}
		return 0;
	} else
		return 1; /* assume locked with no PPA or security driver */
}

static int get_fb_sram_conf(unsigned long start_avail, unsigned size_avail,
			      unsigned long *start, int *plane_idx)
{
	const struct omap_fbmem_config *fbmem_conf;
	unsigned long size = 0;
	int i;

	i = 0;
	*start = 0;
	*plane_idx = -1;
	while ((fbmem_conf = omap_get_nr_config(OMAP_TAG_FBMEM,
				struct omap_fbmem_config, i)) != NULL) {
		u32 paddr, end;

		paddr = fbmem_conf->start;
		end = fbmem_conf->start + fbmem_conf->size;
		if (paddr > omap_sram_start &&
		    paddr < omap_sram_start + omap_sram_size) {
			if (*plane_idx != -1 || paddr < start_avail ||
			    paddr == end ||
			    end > start_avail + size_avail) {
				printk(KERN_ERR "invalid FB SRAM configuration");
				*start = 0;
				return -1;
			}
			*plane_idx = i;
			*start = fbmem_conf->start;
			size = fbmem_conf->size;
		}
		i++;
	}

	if (*plane_idx >= 0)
		pr_info("Reserving %lu bytes SRAM frame buffer "
			"for plane %d\n", size, *plane_idx);

	return 0;
}

/*
 * The amount of SRAM depends on the core type.
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 */
void __init omap_detect_sram(void)
{
	unsigned long fb_sram_start;

	if (cpu_is_omap24xx()) {
		if (is_sram_locked()) {
			omap_sram_base = OMAP2_SRAM_PUB_VA;
			omap_sram_start = OMAP2_SRAM_PUB_PA;
			omap_sram_size = 0x800; /* 2K */
		} else {
			omap_sram_base = OMAP2_SRAM_VA;
			omap_sram_start = OMAP2_SRAM_PA;
			if (cpu_is_omap242x())
				omap_sram_size = 0xa0000; /* 640K */
			else if (cpu_is_omap243x())
				omap_sram_size = 0x10000; /* 64K */
		}
	} else {
		if (cpu_is_omap34xx()) {
			if (is_sram_locked()) {
				omap_sram_base = OMAP3_SRAM_PUB_VA;
				omap_sram_start = OMAP3_SRAM_PUB_PA;
				omap_sram_size = 0x8000; /* 32K */
			} else {
				omap_sram_base = OMAP3_SRAM_VA;
				omap_sram_start = OMAP3_SRAM_PA;
				omap_sram_size = 0x10000; /* 64K */
			}
		} else	{
			omap_sram_base = OMAP1_SRAM_VA;
			omap_sram_start = OMAP1_SRAM_PA;

			if (cpu_is_omap730())
				omap_sram_size = 0x32000;	/* 200K */
			else if (cpu_is_omap15xx())
				omap_sram_size = 0x30000;	/* 192K */
			else if (cpu_is_omap1610() || cpu_is_omap1621() ||
		     	cpu_is_omap1710())
				omap_sram_size = 0x4000;	/* 16K */
			else if (cpu_is_omap1611())
				omap_sram_size = 0x3e800;	/* 250K */
			else {
				printk(KERN_ERR "Could not detect SRAM size\n");
				omap_sram_size = 0x4000;
			}
		}
	
	}
	if (get_fb_sram_conf(omap_sram_start + SRAM_BOOTLOADER_SZ,
			    omap_sram_size - SRAM_BOOTLOADER_SZ,
			    &fb_sram_start, &omap_fb_sram_plane) == 0)
		omap_fb_sram_valid = 1;
	if (omap_fb_sram_valid && omap_fb_sram_plane >= 0)
		omap_sram_size -= omap_sram_start + omap_sram_size -
				  fb_sram_start;
	omap_sram_ceil = omap_sram_base + omap_sram_size;
}

static struct map_desc omap_sram_io_desc[] __initdata = {
	{	/* .length gets filled in at runtime */
		.virtual	= OMAP1_SRAM_VA,
		.pfn		= __phys_to_pfn(OMAP1_SRAM_PA),
		.type		= MT_MEMORY_SO_EXE
	}
};

/*
 * Note that we cannot use ioremap for SRAM, as clock init needs SRAM early.
 */
void __init omap_map_sram(void)
{
	unsigned long base;

	if (omap_sram_size == 0)
		return;

	if (cpu_is_omap24xx()) {
		omap_sram_io_desc[0].virtual = OMAP2_SRAM_VA;

		base = OMAP2_SRAM_PA;
		base = ROUND_DOWN(base, PAGE_SIZE);
		omap_sram_io_desc[0].pfn = __phys_to_pfn(base);
	}
	
	if (cpu_is_omap34xx()) {
		omap_sram_io_desc[0].virtual = OMAP3_SRAM_VA;
		base = OMAP3_SRAM_PA;
		base = ROUND_DOWN(base, PAGE_SIZE);
		omap_sram_io_desc[0].pfn = __phys_to_pfn(base);
	}

	omap_sram_io_desc[0].length = 1024 * 1024;	/* Use section desc */
	iotable_init(omap_sram_io_desc, ARRAY_SIZE(omap_sram_io_desc));

	printk(KERN_INFO "SRAM: Mapped pa 0x%08lx to va 0x%08lx size: 0x%lx\n",
	__pfn_to_phys(omap_sram_io_desc[0].pfn),
	omap_sram_io_desc[0].virtual,
	       omap_sram_io_desc[0].length);

	/*
	 * Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but since we're called from map_io(), we
	 * must do it here.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset((void *)omap_sram_base + SRAM_BOOTLOADER_SZ, 0,
	       omap_sram_size - SRAM_BOOTLOADER_SZ);
}

void * omap_sram_push(void * start, unsigned long size)
{
	if (size > (omap_sram_ceil - (omap_sram_base + SRAM_BOOTLOADER_SZ))) {
		printk(KERN_ERR "Not enough space in SRAM\n");
		return NULL;
	}

	omap_sram_ceil -= size;
	omap_sram_ceil = ROUND_DOWN(omap_sram_ceil, sizeof(void *));
	memcpy((void *)omap_sram_ceil, start, size);

	return (void *)omap_sram_ceil;
}

static void omap_sram_error(void)
{
	panic("Uninitialized SRAM function\n");
}

#ifdef CONFIG_ARCH_OMAP1

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	if (!_omap_sram_reprogram_clock)
		omap_sram_error();

	return _omap_sram_reprogram_clock(dpllctl, ckctl);
}

int __init omap1_sram_init(void)
{
	_omap_sram_reprogram_clock = omap_sram_push(sram_reprogram_clock,
						    sram_reprogram_clock_sz);

	return 0;
}

#else
#define omap1_sram_init()	do {} while (0)
#endif

#if defined (CONFIG_ARCH_OMAP2) ||  defined (CONFIG_ARCH_OMAP3)

static void (*_omap2_sram_ddr_init)(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
			      u32 base_cs, u32 force_unlock);

void omap2_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
		   u32 base_cs, u32 force_unlock)
{
	if (!_omap2_sram_ddr_init)
		omap_sram_error();

	return _omap2_sram_ddr_init(slow_dll_ctrl, fast_dll_ctrl,
				    base_cs, force_unlock);
}

static void (*_omap2_sram_reprogram_sdrc)(u32 perf_level, u32 dll_val,
					  u32 mem_type);

void omap2_sram_reprogram_sdrc(u32 perf_level, u32 dll_val, u32 mem_type)
{
	if (!_omap2_sram_reprogram_sdrc)
		omap_sram_error();

	return _omap2_sram_reprogram_sdrc(perf_level, dll_val, mem_type);
}

static u32 (*_omap2_set_prcm)(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass);

u32 omap2_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass)
{
	if (!_omap2_set_prcm)
		omap_sram_error();

	return _omap2_set_prcm(dpll_ctrl_val, sdrc_rfr_val, bypass);
}

static u32 (*_omap3_configure_core_dpll) (
			u32 m2, u32 unlock_dll, u32 f, u32 inc,
			u32 sdrc_rfr_ctrl_0, u32 sdrc_actim_ctrl_a_0,
			u32 sdrc_actim_ctrl_b_0, u32 sdrc_mr_0,
			u32 sdrc_rfr_ctrl_1, u32 sdrc_actim_ctrl_a_1,
			u32 sdrc_actim_ctrl_b_1, u32 sdrc_mr_1);

u32 omap3_configure_core_dpll(
			u32 m2, u32 unlock_dll, u32 f, u32 inc,
			u32 sdrc_rfr_ctrl_0, u32 sdrc_actim_ctrl_a_0,
			u32 sdrc_actim_ctrl_b_0, u32 sdrc_mr_0,
			u32 sdrc_rfr_ctrl_1, u32 sdrc_actim_ctrl_a_1,
			u32 sdrc_actim_ctrl_b_1, u32 sdrc_mr_1)
{
	if (!_omap3_configure_core_dpll)
		omap_sram_error();

	return _omap3_configure_core_dpll(
			m2, unlock_dll, f, inc,
			sdrc_rfr_ctrl_0, sdrc_actim_ctrl_a_0,
			sdrc_actim_ctrl_b_0, sdrc_mr_0,
			sdrc_rfr_ctrl_1, sdrc_actim_ctrl_a_1,
			sdrc_actim_ctrl_b_1, sdrc_mr_1);
}

static u32 (*_omap3_sram_reprogram_gpmc)(u32 perf_level);

u32 omap3_sram_reprogram_gpmc(u32 perf_level)
{
	if (!_omap3_sram_reprogram_gpmc)
		omap_sram_error();

	return _omap3_sram_reprogram_gpmc(perf_level);
}

int __init omap2_sram_init(void)
{
	_omap2_sram_ddr_init = omap_sram_push(sram_ddr_init, sram_ddr_init_sz);

	_omap2_sram_reprogram_sdrc = omap_sram_push(sram_reprogram_sdrc,
						    sram_reprogram_sdrc_sz);

	if (cpu_is_omap24xx()) {
		_omap2_set_prcm = omap_sram_push(sram_set_prcm, sram_set_prcm_sz);
	} else {
#if defined (CONFIG_ARCH_OMAP34XX)  
		_omap3_configure_core_dpll = 
			omap_sram_push( omap3_sram_configure_core_dpll,
					omap3_sram_configure_core_dpll_sz);
		_omap3_sram_reprogram_gpmc = 
			omap_sram_push( sram_reprogram_gpmc,
					sram_reprogram_gpmc_sz);
#endif
	}
	return 0;
}
#else
#define omap2_sram_init()	do {} while (0)
#endif

int __init omap_sram_init(void)
{
	omap_detect_sram();
	omap_map_sram();

	if ( (!cpu_is_omap24xx()) && (!cpu_is_omap34xx()) )
		omap1_sram_init();
	else
		omap2_sram_init();

	return 0;
}

void restore_sram_functions(void)
{
#ifdef CONFIG_CORE_OFF
	omap_sram_ceil = omap_sram_base + omap_sram_size;
	_omap2_sram_ddr_init = omap_sram_push(sram_ddr_init, sram_ddr_init_sz);
	_omap2_sram_reprogram_sdrc = omap_sram_push(sram_reprogram_sdrc,
			sram_reprogram_sdrc_sz);

	if (cpu_is_omap24xx()) {
		_omap2_set_prcm = omap_sram_push(sram_set_prcm,
				sram_set_prcm_sz);
	} else {
#if defined(CONFIG_ARCH_OMAP34XX)
		_omap3_configure_core_dpll =
			omap_sram_push( omap3_sram_configure_core_dpll,
					omap3_sram_configure_core_dpll_sz);
		_omap3_sram_reprogram_gpmc = 
			omap_sram_push( sram_reprogram_gpmc,
					sram_reprogram_gpmc_sz);
#endif
	}
#endif
}
