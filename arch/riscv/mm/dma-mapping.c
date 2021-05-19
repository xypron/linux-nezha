// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-map-ops.h>
#include <asm/sbi.h>

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	void *ptr = page_address(page);

	memset(ptr, 0, size);
	sbi_dma_sync(page_to_phys(page), size, SBI_DMA_BIDIRECTIONAL);
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		sbi_dma_sync(paddr, size, dir);
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		return;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		sbi_dma_sync(paddr, size, dir);
		break;
	default:
		BUG();
	}
}
