// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018-2019 Samuel Holland <samuel@sholland.org>

#include <linux/completion.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/random.h>

enum {
	OP_MAGIC,
	OP_VERSION,
	OP_LOOPBACK,
	OP_LOOPBACK_INVERTED,
	OP_TIME_SECONDS,
	OP_TIME_TICKS,
	OP_DELAY_MICROS,
	OP_DELAY_MILLIS,
	OP_ADDR_SET_LO,
	OP_ADDR_SET_HI,
	OP_ADDR_READ,
	OP_ADDR_WRITE,
	OP_INVALID_1,
	OP_INVALID_2,
	OP_RESET = 16,
};

struct msgbox_demo {
	struct mbox_chan *rx_chan;
	struct mbox_chan *tx_chan;
	struct mbox_client cl;
	struct completion completion;
	uint32_t request;
	uint32_t response;
	uint32_t address;
	uint32_t value;
};

static void msgbox_demo_rx(struct mbox_client *cl, void *msg)
{
	struct msgbox_demo *demo = container_of(cl, struct msgbox_demo, cl);

	demo->response = *(uint32_t *)msg;
	complete(&demo->completion);
}

static int msgbox_demo_tx(struct msgbox_demo *demo, uint32_t request)
{
	unsigned long timeout = msecs_to_jiffies(10);
	int ret;

	demo->request  = request;
	demo->response = 0;
	reinit_completion(&demo->completion);

	ret = mbox_send_message(demo->tx_chan, &demo->request);
	if (ret < 0) {
		dev_err(demo->cl.dev, "Failed to send request: %d\n", ret);
		return ret;
	}

	if (wait_for_completion_timeout(&demo->completion, timeout))
		return 0;

	return -ETIMEDOUT;
}

static void msgbox_demo_do_operation(struct msgbox_demo *demo, uint16_t op)
{
	struct device *dev = demo->cl.dev;
	uint16_t data = 0;
	uint32_t resp = 0;
	int exp = 0;
	int ret;

	switch (op) {
	case OP_MAGIC:
		resp = 0x1a2a3a4a;
		break;
	case OP_LOOPBACK:
		data = get_random_u32();
		resp = data;
		break;
	case OP_LOOPBACK_INVERTED:
		data = get_random_u32();
		resp = ~data;
		break;
	case OP_DELAY_MICROS:
		data = 25000;
		exp  = -ETIMEDOUT;
		break;
	case OP_DELAY_MILLIS:
		data = 500;
		exp  = -ETIMEDOUT;
		break;
	case OP_ADDR_SET_LO:
		data = demo->address & 0xffff;
		resp = demo->address;
		break;
	case OP_ADDR_SET_HI:
		data = demo->address >> 16;
		break;
	case OP_ADDR_WRITE:
		data = demo->value;
		resp = demo->value;
		break;
	case OP_INVALID_1:
	case OP_INVALID_2:
		resp = -1U;
		break;
	case OP_RESET:
		exp  = -ETIMEDOUT;
		break;
	}

	dev_info(demo->cl.dev, "Sending opcode %d, data 0x%08x\n", op, data);
	ret = msgbox_demo_tx(demo, op << 16 | data);

	if (ret) {
		/* Nothing was received. */
		if (exp)
			dev_info(dev, "No response received, as expected\n");
		else
			dev_err(dev, "Timeout receiving response\n");
		return;
	}

	/* Something was received. */
	if (exp)
		dev_err(dev, "Unexpected response 0x%08x\n", demo->response);
	else if (!resp)
		dev_info(dev, "Received response 0x%08x\n", demo->response);
	else if (demo->response == resp)
		dev_info(dev, "Good response 0x%08x\n", resp);
	else
		dev_err(dev, "Expected 0x%08x, received 0x%08x\n",
			     resp, demo->response);
}

ssize_t demo_address_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct msgbox_demo *demo = dev_get_drvdata(dev);

	return sprintf(buf, "%08x\n", demo->address);
}

static ssize_t demo_address_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct msgbox_demo *demo = dev_get_drvdata(dev);
	uint32_t val;

	if (sscanf(buf, "%x", &val)) {
		demo->address = val;
		msgbox_demo_do_operation(demo, OP_ADDR_SET_HI);
		msgbox_demo_do_operation(demo, OP_ADDR_SET_LO);
		return count;
	}

	return 0;
}

ssize_t demo_value_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct msgbox_demo *demo = dev_get_drvdata(dev);

	msgbox_demo_do_operation(demo, OP_ADDR_READ);
	demo->value = demo->response;

	return sprintf(buf, "%08x\n", demo->value);
}

static ssize_t demo_value_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct msgbox_demo *demo = dev_get_drvdata(dev);
	int16_t val;

	if (sscanf(buf, "%hx", &val)) {
		demo->value = (int32_t)val;
		msgbox_demo_do_operation(demo, OP_ADDR_WRITE);
		return count;
	}

	return 0;
}

static ssize_t demo_operation_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct msgbox_demo *demo = dev_get_drvdata(dev);
	uint16_t val;

	if (sscanf(buf, "%hu", &val)) {
		msgbox_demo_do_operation(demo, val);
		return count;
	}

	return 0;
}

static DEVICE_ATTR(demo_address,   0644, demo_address_show, demo_address_store);
static DEVICE_ATTR(demo_value,     0644, demo_value_show,   demo_value_store);
static DEVICE_ATTR(demo_operation, 0200, NULL,              demo_operation_store);

static int msgbox_demo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_attribute *attr;
	struct msgbox_demo *demo;
	int ret;

	demo = devm_kzalloc(dev, sizeof(*demo), GFP_KERNEL);
	if (!demo)
		return -ENOMEM;

	demo->cl.dev         = dev;
	demo->cl.rx_callback = msgbox_demo_rx;

	if (of_get_property(dev->of_node, "mbox-names", NULL)) {
		demo->rx_chan = mbox_request_channel_byname(&demo->cl, "rx");
		if (IS_ERR(demo->rx_chan)) {
			ret = PTR_ERR(demo->rx_chan);
			dev_err(dev, "Failed to request rx mailbox channel\n");
			goto err;
		}
		demo->tx_chan = mbox_request_channel_byname(&demo->cl, "tx");
		if (IS_ERR(demo->tx_chan)) {
			ret = PTR_ERR(demo->tx_chan);
			dev_err(dev, "Failed to request tx mailbox channel\n");
			goto err_free_rx_chan;
		}
	} else {
		demo->rx_chan = mbox_request_channel(&demo->cl, 0);
		demo->tx_chan = demo->rx_chan;
		if (IS_ERR(demo->tx_chan)) {
			ret = PTR_ERR(demo->tx_chan);
			dev_err(dev, "Failed to request mailbox channel\n");
			goto err;
		}
	}

	attr = &dev_attr_demo_address;
	ret = device_create_file(dev, attr);
	if (ret)
		goto err_creating_files;
	attr = &dev_attr_demo_value;
	ret = device_create_file(dev, attr);
	if (ret)
		goto err_creating_files;
	attr = &dev_attr_demo_operation;
	ret = device_create_file(dev, attr);
	if (ret)
		goto err_creating_files;

	init_completion(&demo->completion);

	platform_set_drvdata(pdev, demo);

	msgbox_demo_do_operation(demo, OP_VERSION);

	return 0;

err_creating_files:
	dev_err(dev, "Failed to create sysfs attribute %s: %d\n",
		attr->attr.name, ret);
	if (demo->tx_chan != demo->rx_chan)
		mbox_free_channel(demo->tx_chan);
err_free_rx_chan:
	mbox_free_channel(demo->rx_chan);
err:
	return ret;
}

static int msgbox_demo_remove(struct platform_device *pdev)
{
	struct msgbox_demo *demo = platform_get_drvdata(pdev);

	if (demo->tx_chan != demo->rx_chan)
		mbox_free_channel(demo->tx_chan);
	mbox_free_channel(demo->rx_chan);

	return 0;
}

static const struct of_device_id msgbox_demo_of_match[] = {
	{ .compatible = "allwinner,sunxi-msgbox-demo" },
	{},
};
MODULE_DEVICE_TABLE(of, msgbox_demo_of_match);

static struct platform_driver msgbox_demo_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = msgbox_demo_of_match,
	},
	.probe  = msgbox_demo_probe,
	.remove = msgbox_demo_remove,
};
module_platform_driver(msgbox_demo_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("sunxi msgbox demo");
MODULE_LICENSE("GPL v2");
