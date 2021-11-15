// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/libfdt.h>
#include <linux/pgtable.h>
#include <asm/image.h>
#include <asm/soc.h>

struct soc_cache riscv_soc_cache;

/*
 * This is called extremly early, before parse_dtb(), to allow initializing
 * SoC hardware before memory or any device driver initialization.
 */
void __init soc_early_init(void)
{
	void (*early_fn)(const void *fdt);
	const struct of_device_id *s;
	const void *fdt = dtb_early_va;

	for (s = (void *)&__soc_early_init_table_start;
	     (void *)s < (void *)&__soc_early_init_table_end; s++) {
		if (!fdt_node_check_compatible(fdt, 0, s->compatible)) {
			early_fn = s->data;
			early_fn(fdt);
			return;
		}
	}
}

void __init soc_setup_dma_coherency_dma_coherent(void)
{
	riscv_soc_cache.is_dma_coherent = true;
}

void __init soc_setup_dma_coherency_uncached_offset(uintptr_t dtb_pa,
						    int soc_node)
{
	const uint64_t *uncached_offset;

	uncached_offset = fdt_getprop((void *)dtb_pa, soc_node,
				      "cache-dma-uncached-offset", NULL);
	if (!uncached_offset)
		return;

	riscv_soc_cache.uncached_offset = be64_to_cpu(*uncached_offset);
}

void __init soc_setup_dma_coherency_custom_cmo(void)
{
	__riscv_custom_pte.cache = 0x7000000000000000;
	__riscv_custom_pte.mask  = 0xf800000000000000;
	__riscv_custom_pte.io    = BIT(63);
	__riscv_custom_pte.wc    = 0;

	riscv_soc_cache.has_custom_cmo = true;
}

void __init soc_setup_dma_coherency(uintptr_t dtb_pa)
{
	int soc_node;
	const char *soc_cache_dma;

	soc_node = fdt_path_offset((void *)dtb_pa, "/soc");
	if (soc_node < 0)
		return;

	soc_cache_dma = fdt_getprop((void *)dtb_pa, soc_node, "cache-dma", NULL);
	if (!soc_cache_dma)
		return;

	if (!strcmp(soc_cache_dma, "uncached-offset"))
		soc_setup_dma_coherency_uncached_offset(dtb_pa, soc_node);
	else if (!strcmp(soc_cache_dma, "custom-cmo"))
		soc_setup_dma_coherency_custom_cmo();
	else
		soc_setup_dma_coherency_dma_coherent();
}

void __init soc_setup_pbmt(uintptr_t dtb_pa)
{
	int mmu_node;
	const char *mmu_pbmt;

	mmu_node = fdt_path_offset((void *)dtb_pa, "/cpus/mmu");
	if (mmu_node < 0)
		return;

	mmu_pbmt = fdt_getprop((void *)dtb_pa, mmu_node, "pbmt", NULL);
	if (!mmu_pbmt)
		return;

	// TODO This is not the place, but for a RFC, that's ok.
	riscv_soc_cache.has_pbmt = true;
}

void __init soc_setup_vm(uintptr_t dtb_pa)
{
	/* DMA coherency */
	soc_setup_dma_coherency(dtb_pa);

	/* PMA */
	soc_setup_pbmt(dtb_pa);
}
