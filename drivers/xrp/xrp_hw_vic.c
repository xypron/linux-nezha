#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include "xrp_internal.h"
#include "xrp_address_map.h"
#include "xrp_hw.h"
#include "xrp_hw_vic.h"

struct hw_vic_vaddress vp6_con_vaddr;

#ifndef BIT
#define BIT(x)  (1UL << (x))
#endif

uint32_t saif_get_reg(void __iomem* addr,uint32_t shift,uint32_t mask)
{
	uint32_t tmp;
	tmp = __raw_readl(addr); //MA_INW
	tmp = (tmp & mask) >> shift;
	return tmp;
}

void saif_set_reg(void __iomem* addr,uint32_t data,uint32_t shift,uint32_t mask)
{
	uint32_t tmp;
	tmp = __raw_readl(addr);
	tmp &= ~mask;
	tmp |= (data<<shift) & mask;
	__raw_writel(tmp, addr); //MA_OUTW
}

void saif_assert_rst(void __iomem* addr,void __iomem* addr_status,uint32_t mask)
{
	uint32_t tmp;
	tmp = __raw_readl(addr);
	tmp |= mask;
	__raw_writel(tmp, addr);
	do{
		tmp = __raw_readl(addr_status);
	}while((tmp&mask)!=0);
}

void saif_clear_rst (void __iomem* addr,void __iomem* addr_status,uint32_t mask)
{
	uint32_t tmp;
	tmp = __raw_readl(addr);
	tmp &= ~mask;
	__raw_writel(tmp, addr);
	do{
		tmp = __raw_readl(addr_status);
	}while((tmp&mask)!=mask);
}

static inline void intc_write_reg(void __iomem* base, u32 addr, u32 v)
{
	if (base)
		__raw_writel(v, base + addr);
}

static inline u32 intc_read_reg(void __iomem* base, u32 addr)
{
	if (base)
		return __raw_readl(base + addr);
	else
		return 0;
}

static void *get_hw_sync_data(void *hw_arg, size_t *sz)
{
	static const u32 irq_mode[] =
	{
		[XRP_IRQ_NONE] = XRP_DSP_SYNC_IRQ_MODE_NONE,
		[XRP_IRQ_LEVEL] = XRP_DSP_SYNC_IRQ_MODE_LEVEL,
		[XRP_IRQ_EDGE] = XRP_DSP_SYNC_IRQ_MODE_EDGE,
		[XRP_IRQ_EDGE_SW] = XRP_DSP_SYNC_IRQ_MODE_EDGE,
	};
	struct xrp_hw_vic *hw = hw_arg;
	struct xrp_hw_vic_sync_data *hw_sync_data =
		kmalloc(sizeof(*hw_sync_data), GFP_KERNEL);

	if (!hw_sync_data)
		return NULL;

	*hw_sync_data = (struct xrp_hw_vic_sync_data)
	{
		.device_mmio_base = 0,
		.intc_irq_mode = hw->intc_irq_mode,
		.intc_irq[0] = hw->intc_irq[0],
		.intc_irq[1] = hw->intc_irq[1],
		.intc_irq_src[0] = hw->intc_irq_src[0],
		.intc_irq_src[1] = hw->intc_irq_src[1],
		.dsp_irq_mode = irq_mode[hw->intc_irq_mode],
		.dsp_irq[0] = hw->dsp_irq[0],
		.dsp_irq[1] = hw->dsp_irq[1],
	};
	*sz = sizeof(*hw_sync_data);
	return hw_sync_data;
}

static irqreturn_t vic_irq_handler(int irq, void *dev_id)
{
	struct xrp_hw_vic* hw = dev_id;
	irqreturn_t ret;

	pr_debug("vic_irq_handler recv irq ...\n");
	ret = xrp_irq_handler(irq, hw->xrp);

	if (ret == IRQ_HANDLED)
	{
		//intc_set_mask(hw->regs,hw->intc_irq_src[1]);
	}

	return ret;
}

void intc_set_mask_xrp(void *hw_arg)
{
	struct xrp_hw_vic *hw __attribute__((unused)) = hw_arg;

	//intc_set_mask(hw->regs,hw->intc_irq_src[1]);
	return;
}

long init_hw(struct platform_device *pdev, void *hw_arg, int mem_idx, enum xrp_init_flags *init_flags)
{
	struct resource *mem;
	struct xrp_hw_vic *hw = hw_arg;
	int irq;
	long ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, mem_idx);
	if (!mem)
		return -ENODEV;

	hw->crg_regs_phys = mem->start;
	hw->crg_regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(hw->crg_regs))
	{
		hw->crg_regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, mem_idx+1);
	if (!mem)
		return -ENODEV;

	hw->syscon_regs_phys = mem->start;
	hw->syscon_regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(hw->syscon_regs))
	{
		hw->syscon_regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	}

	hw->syscon_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "starfive,stg-syscon");
	if (IS_ERR(hw->syscon_regmap)) {
		dev_err(&pdev->dev, "[hifi4] can't get starfive,stg-syscon.\n");
		return PTR_ERR(hw->syscon_regmap);
	}

	hw->core_clk = devm_clk_get(&pdev->dev,"core_clk");
	if (IS_ERR(hw->core_clk))
		return -ENODEV;

	hw->core_rst = devm_reset_control_get(&pdev->dev, "rst_core");
	if (IS_ERR(hw->core_rst))
		return -ENODEV;

	hw->axi_rst = devm_reset_control_get(&pdev->dev, "rst_axi");
	if (IS_ERR(hw->axi_rst))
		return -ENODEV;
	dev_info(&pdev->dev, "[hifi4] get rst handle ok.\n");

	hw->xrp = platform_get_drvdata(pdev);

	ret = of_property_read_u32_array(pdev->dev.of_node,
                     "dsp-irq",
                     hw->dsp_irq,
                     ARRAY_SIZE(hw->dsp_irq));
	if (ret == 0)
	{
		ret = of_property_read_u32_array(pdev->dev.of_node,
                       "intc-irq-src",
                       hw->intc_irq_src,
                       ARRAY_SIZE(hw->intc_irq_src));
		if (ret != 0)
		{
			hw->intc_irq_src[0] = hw->dsp_irq[0];
			hw->intc_irq_src[1] = hw->dsp_irq[1];
			ret = 0;
		}
	}

	if (ret == 0)
	{
		u32 intc_irq_mode;

		ret = of_property_read_u32(pdev->dev.of_node,
                       "intc-irq-mode",
                       &intc_irq_mode);
		if (intc_irq_mode < XRP_IRQ_MAX)
			hw->intc_irq_mode = intc_irq_mode;
		else
			ret = -ENOENT;
	}

	if (ret == 0)
	{
		dev_dbg(&pdev->dev,
			"%s: device IRQ MMIO host src0 = 0x%08x, src1 = 0x%08x, device IRQ = %d, IRQ = %d, IRQ mode = %d",
			__func__, hw->intc_irq_src[0],hw->intc_irq_src[0],
			hw->dsp_irq[0], hw->dsp_irq[1], hw->intc_irq_mode);
	}
	else
	{
		dev_info(&pdev->dev,"using polling mode on the device side\n");
	}

	ret = of_property_read_u32_array(pdev->dev.of_node, "intc-irq",
                     hw->intc_irq,
                     ARRAY_SIZE(hw->intc_irq));

	if (ret == 0 && hw->intc_irq_mode != XRP_IRQ_NONE)
		irq = platform_get_irq(pdev, 1);
	else
		irq = -1;

	if (irq >= 0)
	{
		dev_dbg(&pdev->dev, "%s: host IRQ = %d, ",__func__, irq);
		ret = devm_request_irq(&pdev->dev, irq, vic_irq_handler,
                       IRQF_SHARED, pdev->name, hw);
		if (ret < 0)
		{
			dev_err(&pdev->dev, "request_irq %d failed\n", irq);
			goto err;
		}
		*init_flags |= XRP_INIT_USE_HOST_IRQ;
	}
	else
	{
		dev_info(&pdev->dev, "using polling mode on the host side\n");
	}
	ret = 0;
	dev_info(&pdev->dev, "hw init end: %d %d\n",hw->intc_irq[0],hw->intc_irq[1]);
err:
	return ret;
}

void send_irq(void *hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;

	switch (hw->intc_irq_mode)
	{
		case XRP_IRQ_EDGE_SW:

		case XRP_IRQ_EDGE:

		case XRP_IRQ_LEVEL:
			wmb();
			pr_debug("%s: trigger\n",__func__);
			//intc_set_select(hw->regs,hw->intc_irq_src[0],hw->intc_irq[0]);
			//intc_set_type(hw->regs,hw->intc_irq_src[0],0);
			//intc_clear_mask(hw->regs,hw->intc_irq_src[0]);
			//intc_set_software(hw->regs,hw->intc_irq_src[0]);
			break;
		default:
			break;
    }
}

void halt(void *hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;
	if (NULL == hw)
		return;

	regmap_update_bits(hw->syscon_regmap, STG_RUNSTALLADDR_OFFSET, U0_HIFI4_RUNSTALL_MASK, (1 << U0_HIFI4_RUNSTALL_SHIFT));
	pr_debug("vp6 halt.\n");
}

void release(void *hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;
	if (NULL == hw)
		return;

	regmap_update_bits(hw->syscon_regmap, STG_RUNSTALLADDR_OFFSET, U0_HIFI4_RUNSTALL_MASK, (0 << U0_HIFI4_RUNSTALL_SHIFT));
	pr_debug("vp6 begin run.\n");
}

void reset(void *hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;
	if (NULL == hw)
		return;

	regmap_update_bits(hw->syscon_regmap, STG_STATVECTORSELADDR_OFFSET, U0_HIFI4_STATVECTORSEL_MASK, (1 << U0_HIFI4_STATVECTORSEL_SHIFT));
	regmap_update_bits(hw->syscon_regmap, STG_ALTRESETVECADDR_OFFSET, U0_HIFI4_ALTRESETVEC_MASK, 0xf0000000);

	clk_prepare_enable(hw->core_clk);

	reset_control_assert(hw->axi_rst);

	reset_control_assert(hw->core_rst);

	reset_control_deassert(hw->axi_rst);
    	
	reset_control_deassert(hw->core_rst);

	pr_debug("vp6 initialise end.\n");
}

static int enable(void *hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;
	if (NULL == hw)
		return -1;

	clk_prepare_enable(hw->core_clk);
	
	reset_control_deassert(hw->axi_rst);
	
	reset_control_deassert(hw->core_rst);

	return 0;
}

static void disable(void * hw_arg)
{
	struct xrp_hw_vic *hw = hw_arg;
	
	reset_control_assert(hw->core_rst);
	
	reset_control_assert(hw->axi_rst);
	
	clk_disable_unprepare(hw->core_clk);

	pr_debug("vp6 disable ...\n");
	return ;
}

const struct xrp_hw_ops hw_ops = {
	.get_hw_sync_data = get_hw_sync_data,
	.enable = enable,
	.reset = reset,
	.halt = halt,
	.disable = disable,
	.send_irq = send_irq,
	.release = release,
};

