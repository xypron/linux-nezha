// SPDX-License-Identifier: GPL-2.0-only
//
// R_INTC driver for Allwinner A31 and newer SoCs
//

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

/*
 * The R_INTC manages between 32 and 64 IRQs, divided into four groups. Example
 * bit numbers are for the A31-A64 variant:
 *
 *   Bit      0: The "External NMI" input, connected in series to a GIC SPI.
 *   Bits  1-15: "Direct" IRQs for CPUS peripherals, connected in parallel to
 *               the GIC and mapped 1:1 to the SPIs following the NMI SPI.
 *   Bits 16-18: "Banked" IRQs for peripherals that have separate interfaces
 *               for the ARM CPUs and ARISC. They do not map to any GIC SPI.
 *   Bits 19-31: "Muxed" IRQs, each corresponding to a group of up to 8 SPIs.
 *               Later variants added a second PENDING and ENABLE register to
 *               make use of all 128 mux inputs (16 IRQ lines).
 *
 * Since the direct IRQs are in the middle of the muxed IRQ range, they do not
 * increase the number of HWIRQs needed.
 */
#define SUN6I_NR_IRQS			64
#define SUN6I_NR_DIRECT_IRQS		16
#define SUN6I_NR_MUX_INPUTS		128
#define SUN6I_NR_HWIRQS			SUN6I_NR_MUX_INPUTS

#define SUN6I_NMI_CTRL			(0x0c)
#define SUN6I_IRQ_PENDING(n)		(0x10 + 4 * (n))
#define SUN6I_IRQ_ENABLE(n)		(0x40 + 4 * (n))
#define SUN6I_MUX_ENABLE(n)		(0xc0 + 4 * (n))

#define SUN6I_NMI_IRQ_BIT		BIT(0)

static void __iomem *base;
static irq_hw_number_t nmi_hwirq;
static u32 nmi_type;

static struct irq_chip sun6i_r_intc_edge_chip;
static struct irq_chip sun6i_r_intc_level_chip;

static void sun6i_r_intc_nmi_ack(void)
{
	/*
	 * The NMI channel has a latch separate from its trigger type.
	 * This latch must be cleared to clear the signal to the GIC.
	 */
	writel_relaxed(SUN6I_NMI_IRQ_BIT, base + SUN6I_IRQ_PENDING(0));
}

static void sun6i_r_intc_irq_mask(struct irq_data *data)
{
	if (data->hwirq == nmi_hwirq)
		sun6i_r_intc_nmi_ack();

	irq_chip_mask_parent(data);
}

static void sun6i_r_intc_irq_unmask(struct irq_data *data)
{
	if (data->hwirq == nmi_hwirq)
		sun6i_r_intc_nmi_ack();

	irq_chip_unmask_parent(data);
}

static int sun6i_r_intc_irq_set_type(struct irq_data *data, unsigned int type)
{
	/*
	 * The GIC input labeled "External NMI" connects to bit 0 of the R_INTC
	 * PENDING register, not to the pin directly. So the trigger type of the
	 * GIC input does not depend on the trigger type of the NMI pin itself.
	 *
	 * Only the NMI channel is routed through this interrupt controller on
	 * its way to the GIC. Other IRQs are routed to the GIC and R_INTC in
	 * parallel; they must have a trigger type appropriate for the GIC.
	 */
	if (data->hwirq == nmi_hwirq) {
		struct irq_chip *chip;
		u32 nmi_src_type;

		switch (type) {
		case IRQ_TYPE_LEVEL_LOW:
			chip = &sun6i_r_intc_level_chip;
			nmi_src_type = 0;
			break;
		case IRQ_TYPE_EDGE_FALLING:
			chip = &sun6i_r_intc_edge_chip;
			nmi_src_type = 1;
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			chip = &sun6i_r_intc_level_chip;
			nmi_src_type = 2;
			break;
		case IRQ_TYPE_EDGE_RISING:
			chip = &sun6i_r_intc_edge_chip;
			nmi_src_type = 3;
			break;
		default:
			pr_err("%pOF: invalid trigger type %d for IRQ %d\n",
			       irq_domain_get_of_node(data->domain), type,
			       data->irq);
			return -EBADR;
		}

		irq_set_chip_handler_name_locked(data, chip,
						 handle_fasteoi_irq, NULL);

		writel_relaxed(nmi_src_type, base + SUN6I_NMI_CTRL);

		/*
		 * Use the trigger type from the OF node for the NMI's
		 * R_INTC to GIC connection.
		 */
		type = nmi_type;
	}

	return irq_chip_set_type_parent(data, type);
}

static struct irq_chip sun6i_r_intc_edge_chip = {
	.name			= "sun6i-r-intc",
	.irq_mask		= sun6i_r_intc_irq_mask,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= sun6i_r_intc_irq_set_type,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_set_vcpu_affinity	= irq_chip_set_vcpu_affinity_parent,
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static struct irq_chip sun6i_r_intc_level_chip = {
	.name			= "sun6i-r-intc",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= sun6i_r_intc_irq_unmask,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= sun6i_r_intc_irq_set_type,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_set_vcpu_affinity	= irq_chip_set_vcpu_affinity_parent,
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static int sun6i_r_intc_domain_translate(struct irq_domain *domain,
					 struct irq_fwspec *fwspec,
					 unsigned long *hwirq,
					 unsigned int *type)
{
	/* Accept the old two-cell binding for the NMI only. */
	if (fwspec->param_count == 2 && fwspec->param[0] == 0) {
		*hwirq = nmi_hwirq;
		*type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	/* Otherwise this binding should match the GIC SPI binding. */
	if (fwspec->param_count < 3)
		return -EINVAL;
	if (fwspec->param[0] != GIC_SPI)
		return -EINVAL;

	*hwirq = fwspec->param[1];
	*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static int sun6i_r_intc_domain_alloc(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs, void *arg)
{
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec gic_fwspec;
	unsigned long hwirq;
	unsigned int type;
	int i, ret;

	ret = sun6i_r_intc_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;
	if (hwirq + nr_irqs > SUN6I_NR_HWIRQS)
		return -EINVAL;

	/* Construct a GIC-compatible fwspec from this fwspec. */
	gic_fwspec = (struct irq_fwspec) {
		.fwnode      = domain->parent->fwnode,
		.param_count = 3,
		.param       = { GIC_SPI, hwirq, type },
	};

	for (i = 0; i < nr_irqs; ++i)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &sun6i_r_intc_level_chip, NULL);

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &gic_fwspec);
}

static const struct irq_domain_ops sun6i_r_intc_domain_ops = {
	.translate	= sun6i_r_intc_domain_translate,
	.alloc		= sun6i_r_intc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static void sun6i_r_intc_resume(void)
{
	int i;

	/* Only the NMI is relevant during normal operation. */
	writel_relaxed(SUN6I_NMI_IRQ_BIT, base + SUN6I_IRQ_ENABLE(0));
	for (i = 1; i < BITS_TO_U32(SUN6I_NR_IRQS); ++i)
		writel_relaxed(0, base + SUN6I_IRQ_ENABLE(i));
}

static int __init sun6i_r_intc_init(struct device_node *node,
				    struct device_node *parent)
{
	struct irq_domain *domain, *parent_domain;
	struct of_phandle_args parent_irq;
	int ret;

	/* Extract the NMI's R_INTC to GIC mapping from the OF node. */
	ret = of_irq_parse_one(node, 0, &parent_irq);
	if (ret)
		return ret;
	if (parent_irq.args_count < 3 || parent_irq.args[0] != GIC_SPI)
		return -EINVAL;
	nmi_hwirq = parent_irq.args[1];
	nmi_type = parent_irq.args[2];

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: Failed to obtain parent domain\n", node);
		return -ENXIO;
	}

	base = of_io_request_and_map(node, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("%pOF: Failed to map MMIO region\n", node);
		return PTR_ERR(base);
	}

	sun6i_r_intc_nmi_ack();
	sun6i_r_intc_resume();

	domain = irq_domain_add_hierarchy(parent_domain, 0,
					  SUN6I_NR_HWIRQS, node,
					  &sun6i_r_intc_domain_ops, NULL);
	if (!domain) {
		pr_err("%pOF: Failed to allocate domain\n", node);
		iounmap(base);
		return -ENOMEM;
	}

	return 0;
}
IRQCHIP_DECLARE(sun6i_r_intc, "allwinner,sun6i-a31-r-intc", sun6i_r_intc_init);
