// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2021 Samuel Holland <samuel@sholland.org>

#include <linux/crc8.h>
#include <linux/i2c.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm_wakeirq.h>

#define KB151_CRC8_POLYNOMIAL		0x31

/* Limited to one byte per column */
#define KB151_MAX_ROWS			8
/* Limited by adjacent registers */
#define KB151_MAX_COLS			15

#define KB151_DEVICE_ID_HI		0x00
#define KB151_DEVICE_ID_HI_VALUE		0x4b
#define KB151_DEVICE_ID_LO		0x01
#define KB151_DEVICE_ID_LO_VALUE		0x42
#define KB151_FW_REVISION		0x02
#define KB151_FW_FEATURES		0x03
#define KB151_SYS_CONFIG		0x04
#define KB151_SYS_CONFIG_DISABLE_SCAN		BIT(0)
#define KB151_SYS_CONFIG_POLL_MODE		BIT(1)
#define KB151_SCAN_DATA			0x10

struct kb151 {
	struct input_dev *input;
	u8 crc_table[CRC8_TABLE_SIZE];
	u8 row_shift;
	u8 rows;
	u8 cols;
	u8 buf_swap;
	u8 buf[];
};

static void kb151_update(struct i2c_client *client)
{
	struct kb151 *kb151 = i2c_get_clientdata(client);
	unsigned short *keymap = kb151->input->keycode;
	struct device *dev = &client->dev;
	size_t buf_len = kb151->cols + 1;
	u8 *old_buf = kb151->buf;
	u8 *new_buf = kb151->buf;
	int col, crc, ret, row;

	if (kb151->buf_swap)
		old_buf += buf_len;
	else
		new_buf += buf_len;

	ret = i2c_smbus_read_i2c_block_data(client, KB151_SCAN_DATA,
					    buf_len, new_buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read scan data: %d\n", ret);
		return;
	}

	crc = crc8(kb151->crc_table, new_buf, buf_len, CRC8_INIT_VALUE);
	if (crc != CRC8_GOOD_VALUE(kb151->crc_table)) {
		dev_err(dev, "Bad scan data\n");
		return;
	}

	for (col = 0; col < kb151->cols; ++col) {
		u8 old = *old_buf++;
		u8 new = *new_buf++;
		u8 changed = old ^ new;

		for (row = 0; row < kb151->rows; ++row) {
			int code = MATRIX_SCAN_CODE(row, col, kb151->row_shift);
			u8 pressed = new & BIT(row);

			if (!(changed & BIT(row)))
				continue;

			dev_dbg(&client->dev, "row %u col %u %sed\n",
				row, col, pressed ? "press" : "releas");
			input_report_key(kb151->input, keymap[code], pressed);
		}
	}
	input_sync(kb151->input);

	kb151->buf_swap = !kb151->buf_swap;
}

static int kb151_open(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);
	int ret, val;

	ret = i2c_smbus_read_byte_data(client, KB151_SYS_CONFIG);
	if (ret < 0)
		return ret;

	val = ret & ~KB151_SYS_CONFIG_DISABLE_SCAN;
	ret = i2c_smbus_write_byte_data(client, KB151_SYS_CONFIG, val);
	if (ret < 0)
		return ret;

	kb151_update(client);

	enable_irq(client->irq);

	return 0;
}

static void kb151_close(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);
	int ret, val;

	disable_irq(client->irq);

	ret = i2c_smbus_read_byte_data(client, KB151_SYS_CONFIG);
	if (ret < 0)
		return;

	val = ret | KB151_SYS_CONFIG_DISABLE_SCAN;
	i2c_smbus_write_byte_data(client, KB151_SYS_CONFIG, val);
}

static irqreturn_t kb151_irq_thread(int irq, void *data)
{
	struct i2c_client *client = data;

	kb151_update(client);

	return IRQ_HANDLED;
}

static int kb151_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 info[KB151_SYS_CONFIG + 1];
	unsigned int rows, cols;
	struct kb151 *kb151;
	bool poll_mode;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, 0, sizeof(info), info);
	if (ret < 0)
		return ret;

	if (info[KB151_DEVICE_ID_HI] != KB151_DEVICE_ID_HI_VALUE ||
	    info[KB151_DEVICE_ID_LO] != KB151_DEVICE_ID_LO_VALUE)
		return -ENODEV;

	poll_mode = info[KB151_SYS_CONFIG] & KB151_SYS_CONFIG_POLL_MODE;
	dev_info(dev, "Found KB151 with firmware %d (features=0x%x mode=%s)\n",
		 info[KB151_FW_REVISION], info[KB151_FW_FEATURES],
		 poll_mode ? "poll" : "irq");

	ret = matrix_keypad_parse_properties(dev, &rows, &cols);
	if (ret)
		return ret;

	if (rows > KB151_MAX_ROWS || cols > KB151_MAX_COLS) {
		dev_err(dev, "Unsupported matrix size (%ux%u)\n", rows, cols);
		return -EINVAL;
	}

	kb151 = devm_kzalloc(dev, struct_size(kb151, buf, 2 * (cols + 1)), GFP_KERNEL);
	if (!kb151)
		return -ENOMEM;

	i2c_set_clientdata(client, kb151);

	crc8_populate_msb(kb151->crc_table, KB151_CRC8_POLYNOMIAL);

	kb151->row_shift = get_count_order(cols);
	kb151->rows = rows;
	kb151->cols = cols;

	kb151->input = devm_input_allocate_device(dev);
	if (!kb151->input)
		return -ENOMEM;

	input_set_drvdata(kb151->input, client);

	kb151->input->name = client->name;
	kb151->input->phys = "kb151/input0";
	kb151->input->id.bustype = BUS_I2C;
	kb151->input->open = kb151_open;
	kb151->input->close = kb151_close;

	ret = matrix_keypad_build_keymap(NULL, NULL, rows, cols,
					 NULL, kb151->input);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to build keymap\n");

	ret = devm_request_threaded_irq(dev, client->irq,
					NULL, kb151_irq_thread,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					client->name, client);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request IRQ\n");

	ret = input_register_device(kb151->input);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register input\n");

	if (device_property_read_bool(dev, "wakeup-source")) {
		device_set_wakeup_capable(dev, true);

		ret = dev_pm_set_wake_irq(dev, client->irq);
		if (ret)
			dev_warn(dev, "Failed to set wake IRQ\n");
	}

	return 0;
}

static const struct of_device_id kb151_of_match[] = {
	{ .compatible = "pine64,kb151" },
	{ }
};
MODULE_DEVICE_TABLE(of, kb151_of_match);

static struct i2c_driver kb151_driver = {
	.probe_new	= kb151_probe,
	.driver		= {
		.name		= "kb151",
		.of_match_table = kb151_of_match,
	},
};
module_i2c_driver(kb151_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Pine64 KB151 keyboard driver");
MODULE_LICENSE("GPL");
