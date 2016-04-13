/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c/i2c-qup.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/memory.h>
#include <linux/regulator/cpr-regulator.h>
#include <linux/regulator/fan53555.h>
#include <linux/regulator/onsemi-ncp6335d.h>
#include <linux/regulator/qpnp-regulator.h>
#include <linux/msm_tsens.h>
#include <asm/mach/map.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/board.h>
#include <mach/msm_bus.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#include <mach/restart.h>
#ifdef CONFIG_ION_MSM
#include <mach/ion.h>
#endif
#ifdef CONFIG_ANDROID_PERSISTENT_RAM
#include <linux/persistent_ram.h>
#endif
#include <linux/memblock.h>
#include <mach/msm_memtypes.h>
#include <mach/socinfo.h>
#include <mach/board.h>
#include <mach/clk-provider.h>
#include <mach/msm_smd.h>
#include <mach/rpm-smd.h>
#include <mach/rpm-regulator-smd.h>
#include <mach/msm_smem.h>
#include <linux/msm_thermal.h>
#include <linux/mmc/host.h>

#include "board-dt.h"
#include "clock.h"
#include "platsmp.h"
#include "spm.h"
#include "pm.h"
#include "modem_notifier.h"
#include "spm-regulator.h"

static struct memtype_reserve msm8226_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

#ifdef CONFIG_ANDROID_RAM_CONSOLE
static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
};
#endif

#ifdef CONFIG_PERSISTENT_TRACER
static struct platform_device persistent_trace_device = {
	.name = "persistent_trace",
	.id = -1,
};
#endif

#ifdef CONFIG_ANDROID_PERSISTENT_RAM
static struct persistent_ram_descriptor pram_descs[] __initdata = {
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	{
		.name = "ram_console",
		.size = 0,
	},
#endif
#ifdef CONFIG_PERSISTENT_TRACER
	{
		.name = "persistent_trace",
		.size = 0,
	},
#endif
};

static struct persistent_ram persist_ram __initdata = {
	.descs = pram_descs,
	.num_descs = ARRAY_SIZE(pram_descs),
	.start = 0,
	.size = 0
};

static int __init add_persist_ram_devices(void)
{
	int ret, i;
	struct memtype_reserve * mt = NULL;
	phys_addr_t base;
	int size;
	int pram_size = get_persist_ram_size(0);

	base = get_persist_ram_info(0, &size);
	if (!pram_size) {
		pr_info("%s: pram_size=0x%x, base=0x%lx \n", __func__, pram_size, (long)base);
		return 0;
	}
	if (!base) {
		mt = &reserve_info->memtype_reserve_table[MEMTYPE_EBI1];
		if (!mt->start) {
			pr_err("%s: EBI1 addr = NULL \n", __func__);
			return -ENODEV;
		}
		size = pram_size;
		base = mt->start - size - PAGE_SIZE;
	}
	if (size <= 0)
		return -EINVAL;

	persist_ram.start = base;
	persist_ram.size = size;	
	for (i=0; i < persist_ram.num_descs; i++) {
		persist_ram.descs[i].size = (phys_addr_t)(size / persist_ram.num_descs);
	}
	pr_info("PERSIST RAM CONSOLE START ADDR : 0x%x\n", persist_ram.start);
	if (!mt)
		persist_ram.start++;
	ret = persistent_ram_early_init(&persist_ram);
	if (!mt)
		persist_ram.start--;
	if (ret)
		pr_err("%s: failed to initialize persistent ram at 0x%x \n", __func__, persist_ram.start);
	return ret;
}

#endif

static int msm8226_paddr_to_memtype(unsigned int paddr)
{
	return MEMTYPE_EBI1;
}

static struct of_dev_auxdata msm_hsic_host_adata[] = {
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	{}
};

static struct of_dev_auxdata msm8226_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	OF_DEV_AUXDATA("qcom,hsic-smsc-hub", 0, "msm_smsc_hub",
			msm_hsic_host_adata),

	{}
};

static struct of_dev_auxdata msm8226_auxdata_lookup_swap[] __initdata = {
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	OF_DEV_AUXDATA("qcom,hsic-smsc-hub", 0, "msm_smsc_hub",
			msm_hsic_host_adata),

	{}
};

static struct reserve_info msm8226_reserve_info __initdata = {
	.memtype_reserve_table = msm8226_reserve_table,
	.paddr_to_memtype = msm8226_paddr_to_memtype,
};

static void __init msm8226_early_memory(void)
{
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_hole, msm8226_reserve_table);
}

static void __init msm8226_reserve(void)
{
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_reserve, msm8226_reserve_table);
#ifdef CONFIG_ANDROID_PERSISTENT_RAM
	add_persist_ram_devices();
#endif
	msm_reserve();
}

static void __init add_persistent_device(void)
{
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	platform_device_register(&ram_console_device);
#endif
#ifdef CONFIG_PERSISTENT_TRACER
	platform_device_register(&persistent_trace_device);
#endif
}

/*
 * Used to satisfy dependencies for devices that need to be
 * run early or in a particular order. Most likely your device doesn't fall
 * into this category, and thus the driver should not be added here. The
 * EPROBE_DEFER can satisfy most dependency problems.
 */
void __init msm8226_add_drivers(void)
{
	msm_smem_init();
	msm_init_modem_notifier_list();
	msm_smd_init();
	msm_rpm_driver_init();
	msm_spm_device_init();
	msm_pm_sleep_status_init();
	rpm_regulator_smd_driver_init();
	qpnp_regulator_init();
	spm_regulator_init();
	if (of_board_is_rumi())
		msm_clock_init(&msm8226_rumi_clock_init_data);
	else
		msm_clock_init(&msm8226_clock_init_data);
	msm_bus_fabric_init_driver();
	qup_i2c_init_driver();
	ncp6335d_regulator_init();
	fan53555_regulator_init();
	cpr_regulator_init();
	tsens_tm_init_driver();
	msm_thermal_device_init();
	add_persistent_device();
}

void __init msm8226_init(void)
{
	struct of_dev_auxdata *adata = msm8226_auxdata_lookup;

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

	if (swap_sdcc) {
		int i;
		struct clk_lookup * clk = msm8226_clock_init_data.table;
		int clk_size = (int)msm8226_clock_init_data.size;

		pr_info("%s: androidboot.swap_sdcc = %d \n", __func__, swap_sdcc);

		adata = msm8226_auxdata_lookup_swap;
		pr_info("%s: use struct msm8226_auxdata_lookup_swap \n", __func__);

		for (i = 0; i < clk_size; i++) {
			if (!clk[i].dev_id)
				continue;
			if (strcmp(clk[i].dev_id, "msm_sdcc.1") == 0) {
				clk[i].dev_id = "msm_sdcc.2";
			} else
			if (strcmp(clk[i].dev_id, "msm_sdcc.2") == 0) {
				clk[i].dev_id = "msm_sdcc.1";
			}
		}
	}
	msm8226_init_gpiomux();
	board_dt_populate(adata);
	msm8226_add_drivers();
}

static const char *msm8226_dt_match[] __initconst = {
	"qcom,msm8226",
	"qcom,msm8926",
	"qcom,apq8026",
	NULL
};

DT_MACHINE_START(MSM8226_DT, "Qualcomm MSM 8x26 / MSM 8x28 (Flattened Device Tree)")
	.map_io = msm_map_msm8226_io,
	.init_irq = msm_dt_init_irq,
	.init_machine = msm8226_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8226_dt_match,
	.reserve = msm8226_reserve,
	.init_very_early = msm8226_early_memory,
	.restart = msm_restart,
	.smp = &arm_smp_ops,
MACHINE_END
