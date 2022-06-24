
#ifndef _XRP_HW_VIC_H
#define _XRP_HW_VIC_H

#define STG_RUNSTALLADDR_OFFSET				    0x38U
#define STG_STATVECTORSELADDR_OFFSET			    0x44U
#define STG_ALTRESETVECADDR_OFFSET			    0x2CU

#define U0_HIFI4_STATVECTORSEL_SHIFT                        0xCU
#define U0_HIFI4_ALTRESETVEC_SHIFT                          0x0U
#define U0_HIFI4_RUNSTALL_SHIFT                             0x12U

#define U0_HIFI4_STATVECTORSEL_MASK                         0x1000U
#define U0_HIFI4_ALTRESETVEC_MASK                           0xFFFFFFFFU
#define U0_HIFI4_RUNSTALL_MASK                              0x40000U

struct hw_vic_vaddress
{
	void __iomem *noc_base_vp;            // 0x11854000
	u32 noc_base_size;                    // 0x1A0+1
	void __iomem *rstgen_base_vp;         // 0x11840000
	u32 rstgen_base_size;                 // 0x1C+1
	void __iomem *clkgen_base_vp;         // 0x11800000
	u32 clkgen_base_size;                 // 0x2E4+1
};

enum xrp_irq_mode
{
	XRP_IRQ_NONE,
	XRP_IRQ_LEVEL,
	XRP_IRQ_EDGE,
	XRP_IRQ_EDGE_SW,
	XRP_IRQ_MAX,
};

enum {
	XRP_DSP_SYNC_IRQ_MODE_NONE = 0x0,
	XRP_DSP_SYNC_IRQ_MODE_LEVEL = 0x1,
	XRP_DSP_SYNC_IRQ_MODE_EDGE = 0x2,
};

struct xrp_hw_vic
{
	struct xvp* xrp;
	phys_addr_t crg_regs_phys;
	void __iomem *crg_regs;
	phys_addr_t syscon_regs_phys;
	void __iomem *syscon_regs;
	struct clk *core_clk;
	struct reset_control *core_rst;
	struct reset_control *axi_rst;
	struct regmap *syscon_regmap;
	enum xrp_irq_mode intc_irq_mode;
	u32 dsp_irq[2];
	u32 intc_irq_src[2];
	u32 intc_irq[2];
};

struct xrp_hw_vic_sync_data {
	__u32 device_mmio_base;
	__u32 intc_irq_mode;
        __u32 dsp_irq_mode;
	__u32 intc_irq[2];
	__u32 intc_irq_src[2];
	__u32 dsp_irq[2];
};

#define INTC_SC0_STATUS            0x00
#define INTC_SC0_TYPL              0x04
#define INTC_SC0_TYPH              0x08
#define INTC_SC0_SEL0              0x0c
#define INTC_SC0_CLR               0x10
#define INTC_SC0_MSK               0x14
#define INTC_SC0_RAW               0x18
#define INTC_SC0_INT               0x1c

#define INTC_SC1_STATUS            0x20
#define INTC_SC1_TYPL              0x24
#define INTC_SC1_TYPH              0x28
#define INTC_SC1_SEL0              0x2c
#define INTC_SC1_CLR               0x30
#define INTC_SC1_MSK               0x34
#define INTC_SC1_RAW               0x38
#define INTC_SC1_INT               0x3c

#define INTC_SC0_SEL1              0x40
#define INTC_SC1_SEL1              0x44

#define INTC_SC0_SOFT              0x48
#define INTC_SC1_SOFT              0x4c

#define INTC_GRP_SRC_IRQ_MASK      0x1F
#define INTC_GRP_SRC_IRQS_NUM      (INTC_GRP_SRC_IRQ_MASK + 1)
#define INTC_GRP_IRQ_MASK          0x3F

void intc_set_mask_xrp(void *);
#endif

