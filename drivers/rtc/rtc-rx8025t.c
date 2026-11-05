// SPDX-License-Identifier: GPL-2.0-only
/* Epson RX-8025T RTC driver */

#include <linux/bcd.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/rtc.h>

#define RX8025T_REG_SEC		0x00
#define RX8025T_REG_MIN		0x01
#define RX8025T_REG_HOUR	0x02
#define RX8025T_REG_WDAY	0x03
#define RX8025T_REG_MDAY	0x04
#define RX8025T_REG_MONTH	0x05
#define RX8025T_REG_YEAR	0x06
#define RX8025T_REG_RAM		0x07
#define RX8025T_REG_FLAG	0x0e

#define RX8025T_FLAG_VDET	BIT(0)
#define RX8025T_FLAG_VLF	BIT(1)

#define RX8025T_REG_EXT_MODE	0x30
#define RX8025T_REG_EXT_CTRL	0x32
#define RX8025T_REG_EXT_CLEAR	0x40
#define RX8025T_REG_EXT_5A	0x5a
#define RX8025T_REG_EXT_5C	0x5c

static int rx8025t_read_reg(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "failed to read register 0x%02x: %pe\n",
			reg, ERR_PTR(ret));

	return ret;
}

static int rx8025t_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret = i2c_smbus_write_byte_data(client, reg, value);

	if (ret)
		dev_err(&client->dev, "failed to write register 0x%02x: %pe\n",
			reg, ERR_PTR(ret));

	return ret;
}

static int rx8025t_read_regs(struct i2c_client *client, u8 reg,
			     u8 length, u8 *values)
{
	int ret = i2c_smbus_read_i2c_block_data(client, reg, length, values);

	if (ret == length)
		return 0;

	if (ret >= 0)
		ret = -EIO;

	dev_err(&client->dev, "failed to read registers 0x%02x-0x%02x: %pe\n",
		reg, reg + length - 1, ERR_PTR(ret));
	return ret;
}

static int rx8025t_write_regs(struct i2c_client *client, u8 reg,
			      u8 length, const u8 *values)
{
	int ret = i2c_smbus_write_i2c_block_data(client, reg, length, values);

	if (ret)
		dev_err(&client->dev,
			"failed to write registers 0x%02x-0x%02x: %pe\n",
			reg, reg + length - 1, ERR_PTR(ret));

	return ret;
}

static int rx8025t_update_bits(struct i2c_client *client, u8 reg,
			       u8 mask, u8 value)
{
	int ret = rx8025t_read_reg(client, reg);

	if (ret < 0)
		return ret;

	return rx8025t_write_reg(client, reg, (ret & ~mask) | (value & mask));
}

static int rx8025t_extended_init(struct i2c_client *client)
{
	int ret;
	int exit_ret;

	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_MODE, 0xd1);
	if (ret)
		return ret;

	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_CLEAR, 0x00);
	if (ret)
		goto exit_extended_mode;

	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_CTRL, 0x81);
	if (ret)
		goto exit_extended_mode;
	msleep(50);

	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_CTRL, 0x80);
	if (ret)
		goto exit_extended_mode;
	msleep(50);

	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_CTRL, 0x04);
	if (ret)
		goto exit_extended_mode;
	ret = rx8025t_write_reg(client, RX8025T_REG_EXT_CTRL, 0x04);
	if (ret)
		goto exit_extended_mode;

	ret = rx8025t_update_bits(client, RX8025T_REG_EXT_5C, GENMASK(3, 0),
				    0x0c);
	if (ret)
		goto exit_extended_mode;

	ret = rx8025t_update_bits(client, RX8025T_REG_EXT_5A, GENMASK(7, 5),
				    GENMASK(7, 5));

exit_extended_mode:
	exit_ret = rx8025t_write_reg(client, RX8025T_REG_EXT_MODE, 0x00);
	return ret ?: exit_ret;
}

static int rx8025t_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 date[7];
	int ret;

	ret = rx8025t_read_regs(client, RX8025T_REG_SEC, sizeof(date), date);
	if (ret)
		return ret;

	tm->tm_sec = bcd2bin(date[RX8025T_REG_SEC] & 0x7f);
	tm->tm_min = bcd2bin(date[RX8025T_REG_MIN] & 0x7f);
	tm->tm_hour = bcd2bin(date[RX8025T_REG_HOUR] & 0x3f);
	tm->tm_wday = ffs(date[RX8025T_REG_WDAY] & 0x7f) - 1;
	tm->tm_mday = bcd2bin(date[RX8025T_REG_MDAY] & 0x3f);
	tm->tm_mon = bcd2bin(date[RX8025T_REG_MONTH] & 0x1f) - 1;
	tm->tm_year = bcd2bin(date[RX8025T_REG_YEAR]) + 100;

	return 0;
}

static int rx8025t_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 date[7];

	date[RX8025T_REG_SEC] = bin2bcd(tm->tm_sec);
	date[RX8025T_REG_MIN] = bin2bcd(tm->tm_min);
	date[RX8025T_REG_HOUR] = bin2bcd(tm->tm_hour);
	date[RX8025T_REG_WDAY] = BIT(tm->tm_wday);
	date[RX8025T_REG_MDAY] = bin2bcd(tm->tm_mday);
	date[RX8025T_REG_MONTH] = bin2bcd(tm->tm_mon + 1);
	date[RX8025T_REG_YEAR] = bin2bcd(tm->tm_year - 100);

	return rx8025t_write_regs(client, RX8025T_REG_SEC, sizeof(date), date);
}

static const struct rtc_class_ops rx8025t_rtc_ops = {
	.read_time = rx8025t_read_time,
	.set_time = rx8025t_set_time,
};

static ssize_t ram_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret = rx8025t_read_reg(to_i2c_client(dev), RX8025T_REG_RAM);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "0x%02x\n", ret);
}

static ssize_t ram_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	u8 value;
	int ret;

	ret = kstrtou8(buf, 0, &value);
	if (ret)
		return ret;

	ret = rx8025t_write_reg(to_i2c_client(dev), RX8025T_REG_RAM, value);
	return ret ?: count;
}
static DEVICE_ATTR_RW(ram);

static struct attribute *rx8025t_attrs[] = {
	&dev_attr_ram.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx8025t);

static int rx8025t_init(struct i2c_client *client)
{
	int flags;
	int ret;

	flags = rx8025t_read_reg(client, RX8025T_REG_FLAG);
	if (flags < 0)
		return flags;

	if (flags & (RX8025T_FLAG_VLF | RX8025T_FLAG_VDET)) {
		dev_warn(&client->dev,
			 "power loss detected, clock needs to be set\n");
		ret = rx8025t_write_reg(client, RX8025T_REG_FLAG,
					 flags & ~(RX8025T_FLAG_VLF |
						   RX8025T_FLAG_VDET));
		if (ret)
			return ret;
	}

	return rx8025t_extended_init(client);
}

static int rx8025t_probe(struct i2c_client *client)
{
	struct rtc_device *rtc;
	int ret;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK))
		return dev_err_probe(&client->dev, -EOPNOTSUPP,
				     "adapter lacks required SMBus operations\n");

	ret = rx8025t_init(client);
	if (ret)
		return ret;

	rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &rx8025t_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;
	clear_bit(RTC_FEATURE_ALARM, rtc->features);

	return devm_rtc_register_device(rtc);
}

static const struct i2c_device_id rx8025t_id[] = {
	{ "rx8025t" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rx8025t_id);

static const struct of_device_id rx8025t_of_match[] = {
	{ .compatible = "epson,rx8025t" },
	{ }
};
MODULE_DEVICE_TABLE(of, rx8025t_of_match);

static struct i2c_driver rx8025t_driver = {
	.driver = {
		.name = "rtc-rx8025t",
		.of_match_table = rx8025t_of_match,
		.dev_groups = rx8025t_groups,
	},
	.probe = rx8025t_probe,
	.id_table = rx8025t_id,
};
module_i2c_driver(rx8025t_driver);

MODULE_AUTHOR("MYIR Electronics");
MODULE_DESCRIPTION("Epson RX-8025T RTC driver");
MODULE_LICENSE("GPL");
