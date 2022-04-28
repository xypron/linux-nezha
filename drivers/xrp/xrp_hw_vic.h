
#ifndef _XRP_HW_VIC_H
#define _XRP_HW_VIC_H



#define CLK_U0_HIFI4_CLK_CORE_CTRL_REG_ADDR(addr)                          ((void *)addr + 0x0U) //0x10230000 U0_STG_CRG__SAIF_BD_APBS__BASE_ADDR
#define STG_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR(addr)               ((void *)addr + 0x74U)
#define STG_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR(addr)               ((void *)addr + 0x78U)

#define CLK_U0_HIFI4_CLK_CORE_ENABLE_DATA                   1
#define CLK_U0_HIFI4_CLK_CORE_DISABLE_DATA                  0
#define CLK_U0_HIFI4_CLK_CORE_EN_SHIFT                      31
#define CLK_U0_HIFI4_CLK_CORE_EN_MASK                       0x80000000U
#define RST_U0_HIFI4_RST_AXI_MASK                           (0x1 << 2)
#define RST_U0_HIFI4_RST_CORE_MASK                          (0x1 << 1)

#define STG_SYSCONSAIF__SYSCFG_44_ADDR(addr)                ((void *)addr + 0x2cU) //0x10240000 U0_STG_SYSCON__SAIF_BD_APBS__BASE_ADDR
#define STG_SYSCONSAIF__SYSCFG_56_ADDR(addr)                ((void *)addr + 0x38U)
#define STG_SYSCONSAIF__SYSCFG_68_ADDR(addr)                ((void *)addr + 0x44U)

#define U0_HIFI4_STATVECTORSEL_SHIFT                        0xCU
#define U0_HIFI4_ALTRESETVEC_SHIFT                          0x0U
#define U0_HIFI4_RUNSTALL_SHIFT                             0x12U

#define U0_HIFI4_STATVECTORSEL_MASK                         0x1000U
#define U0_HIFI4_ALTRESETVEC_MASK                           0xFFFFFFFFU
#define U0_HIFI4_RUNSTALL_MASK                              0x40000U

#define _ENABLE_CLOCK_CLK_U0_HIFI4_CLK_CORE_(addr)          saif_set_reg(CLK_U0_HIFI4_CLK_CORE_CTRL_REG_ADDR(addr), CLK_U0_HIFI4_CLK_CORE_ENABLE_DATA, CLK_U0_HIFI4_CLK_CORE_EN_SHIFT, CLK_U0_HIFI4_CLK_CORE_EN_MASK)
#define _DISABLE_CLOCK_CLK_U0_HIFI4_CLK_CORE_(addr) 	    saif_set_reg(CLK_U0_HIFI4_CLK_CORE_CTRL_REG_ADDR(addr), CLK_U0_HIFI4_CLK_CORE_DISABLE_DATA, CLK_U0_HIFI4_CLK_CORE_EN_SHIFT, CLK_U0_HIFI4_CLK_CORE_EN_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_HIFI4_RST_AXI_(addr) 	saif_assert_rst(STG_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR(addr), STG_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR(addr), RST_U0_HIFI4_RST_AXI_MASK)
#define _ASSERT_RESET_RSTGEN_RST_U0_HIFI4_RST_CORE_(addr)   saif_assert_rst(STG_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR(addr), STG_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR(addr), RST_U0_HIFI4_RST_CORE_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_HIFI4_RST_AXI_(addr) 	saif_clear_rst(STG_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR(addr), STG_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR(addr), RST_U0_HIFI4_RST_AXI_MASK)
#define _CLEAR_RESET_RSTGEN_RST_U0_HIFI4_RST_CORE_(addr) 	saif_clear_rst(STG_CRG_RSTGEN_SOFTWARE_RESET_ASSERT0_REG_ADDR(addr), STG_CRG_RSTGEN_SOFTWARE_RESET_STATUS0_REG_ADDR(addr), RST_U0_HIFI4_RST_CORE_MASK)
#define SET_U0_HIFI4_RUNSTALL(addr,data)                    saif_set_reg(STG_SYSCONSAIF__SYSCFG_56_ADDR(addr),data,U0_HIFI4_RUNSTALL_SHIFT,U0_HIFI4_RUNSTALL_MASK)
#define SET_U0_HIFI4_STATVECTORSEL(addr,data)               saif_set_reg(STG_SYSCONSAIF__SYSCFG_68_ADDR(addr),data,U0_HIFI4_STATVECTORSEL_SHIFT,U0_HIFI4_STATVECTORSEL_MASK)
#define SET_U0_HIFI4_ALTRESETVEC(addr,data)                 saif_set_reg(STG_SYSCONSAIF__SYSCFG_44_ADDR(addr),data,U0_HIFI4_ALTRESETVEC_SHIFT,U0_HIFI4_ALTRESETVEC_MASK)

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

