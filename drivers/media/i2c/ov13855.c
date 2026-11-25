// SPDX-License-Identifier: GPL-2.0
/*
 * OmniVision OV13855 sensor driver.
 * Register sequences from the Rockchip OV13855 driver.
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define OV13855_CHIP_ID			0x00d855
#define OV13855_REG_CHIP_ID		CCI_REG24(0x300a)
#define OV13855_REG_MODE		CCI_REG8(0x0100)
#define OV13855_REG_RESET		CCI_REG8(0x0103)
#define OV13855_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV13855_REG_GAIN		CCI_REG16(0x3508)
#define OV13855_REG_VTS			CCI_REG16(0x380e)
#define OV13855_REG_TEST_PATTERN	CCI_REG8(0x5e00)

#define OV13855_XCLK			24000000
#define OV13855_LINK_FREQ		270000000
#define OV13855_LANES			4
#define OV13855_PIXEL_RATE		(2LL * OV13855_LINK_FREQ * OV13855_LANES / 10)
#define OV13855_WIDTH			2112
#define OV13855_HEIGHT			1568
#define OV13855_LINE_LENGTH		(2 * 0x0462)
#define OV13855_VTS_DEFAULT		0x0c89
#define OV13855_VTS_MIN			0x0648
#define OV13855_VTS_MAX			0x7fff
#define OV13855_EXPOSURE_MARGIN		8
#define OV13855_EXPOSURE_DEFAULT	0x0400
#define OV13855_CODE			MEDIA_BUS_FMT_SBGGR10_1X10

struct ov13855 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *exposure;
	struct regmap *regmap;
	struct clk *xclk;
	struct regulator *vdd;
	struct gpio_desc *reset;
	struct gpio_desc *powerdown;
};

static const s64 ov13855_link_freq[] = { OV13855_LINK_FREQ };
static const char * const ov13855_patterns[] = { "Disabled", "Color Bars" };

static const struct cci_reg_sequence ov13855_global_regs[] = {
	{ CCI_REG8(0x0300), 0x02 },
	{ CCI_REG8(0x0301), 0x00 },
	{ CCI_REG8(0x0302), 0x5a },
	{ CCI_REG8(0x0303), 0x00 },
	{ CCI_REG8(0x0304), 0x00 },
	{ CCI_REG8(0x0305), 0x01 },
	{ CCI_REG8(0x030b), 0x06 },
	{ CCI_REG8(0x030c), 0x02 },
	{ CCI_REG8(0x030d), 0x88 },
	{ CCI_REG8(0x0312), 0x11 },
	{ CCI_REG8(0x3022), 0x01 },
	{ CCI_REG8(0x3013), 0x32 },
	{ CCI_REG8(0x3016), 0x72 },
	{ CCI_REG8(0x301b), 0xf0 },
	{ CCI_REG8(0x301f), 0xd0 },
	{ CCI_REG8(0x3106), 0x15 },
	{ CCI_REG8(0x3107), 0x23 },
	{ CCI_REG8(0x3500), 0x00 },
	{ CCI_REG8(0x3501), 0x80 },
	{ CCI_REG8(0x3502), 0x00 },
	{ CCI_REG8(0x3508), 0x02 },
	{ CCI_REG8(0x3509), 0x00 },
	{ CCI_REG8(0x350a), 0x00 },
	{ CCI_REG8(0x350e), 0x00 },
	{ CCI_REG8(0x3510), 0x00 },
	{ CCI_REG8(0x3511), 0x02 },
	{ CCI_REG8(0x3512), 0x00 },
	{ CCI_REG8(0x3600), 0x2b },
	{ CCI_REG8(0x3601), 0x52 },
	{ CCI_REG8(0x3602), 0x60 },
	{ CCI_REG8(0x3612), 0x05 },
	{ CCI_REG8(0x3613), 0xa4 },
	{ CCI_REG8(0x3620), 0x80 },
	{ CCI_REG8(0x3621), 0x10 },
	{ CCI_REG8(0x3622), 0x30 },
	{ CCI_REG8(0x3624), 0x1c },
	{ CCI_REG8(0x3640), 0x10 },
	{ CCI_REG8(0x3641), 0x70 },
	{ CCI_REG8(0x3661), 0x80 },
	{ CCI_REG8(0x3662), 0x12 },
	{ CCI_REG8(0x3664), 0x73 },
	{ CCI_REG8(0x3665), 0xa7 },
	{ CCI_REG8(0x366e), 0xff },
	{ CCI_REG8(0x366f), 0xf4 },
	{ CCI_REG8(0x3674), 0x00 },
	{ CCI_REG8(0x3679), 0x0c },
	{ CCI_REG8(0x367f), 0x01 },
	{ CCI_REG8(0x3680), 0x0c },
	{ CCI_REG8(0x3681), 0x50 },
	{ CCI_REG8(0x3682), 0x50 },
	{ CCI_REG8(0x3683), 0xa9 },
	{ CCI_REG8(0x3684), 0xa9 },
	{ CCI_REG8(0x3709), 0x5f },
	{ CCI_REG8(0x3714), 0x24 },
	{ CCI_REG8(0x371a), 0x3e },
	{ CCI_REG8(0x3737), 0x04 },
	{ CCI_REG8(0x3738), 0xcc },
	{ CCI_REG8(0x3739), 0x12 },
	{ CCI_REG8(0x373d), 0x26 },
	{ CCI_REG8(0x3764), 0x20 },
	{ CCI_REG8(0x3765), 0x20 },
	{ CCI_REG8(0x37a1), 0x36 },
	{ CCI_REG8(0x37a8), 0x3b },
	{ CCI_REG8(0x37ab), 0x31 },
	{ CCI_REG8(0x37c2), 0x04 },
	{ CCI_REG8(0x37c3), 0xf1 },
	{ CCI_REG8(0x37c5), 0x00 },
	{ CCI_REG8(0x37d8), 0x03 },
	{ CCI_REG8(0x37d9), 0x0c },
	{ CCI_REG8(0x37da), 0xc2 },
	{ CCI_REG8(0x37dc), 0x02 },
	{ CCI_REG8(0x37e0), 0x00 },
	{ CCI_REG8(0x37e1), 0x0a },
	{ CCI_REG8(0x37e2), 0x14 },
	{ CCI_REG8(0x37e3), 0x04 },
	{ CCI_REG8(0x37e4), 0x2a },
	{ CCI_REG8(0x37e5), 0x03 },
	{ CCI_REG8(0x37e6), 0x04 },
	{ CCI_REG8(0x3800), 0x00 },
	{ CCI_REG8(0x3801), 0x00 },
	{ CCI_REG8(0x3802), 0x00 },
	{ CCI_REG8(0x3803), 0x08 },
	{ CCI_REG8(0x3804), 0x10 },
	{ CCI_REG8(0x3805), 0x9f },
	{ CCI_REG8(0x3806), 0x0c },
	{ CCI_REG8(0x3807), 0x57 },
	{ CCI_REG8(0x3808), 0x10 },
	{ CCI_REG8(0x3809), 0x80 },
	{ CCI_REG8(0x380a), 0x0c },
	{ CCI_REG8(0x380b), 0x40 },
	{ CCI_REG8(0x380c), 0x04 },
	{ CCI_REG8(0x380d), 0x62 },
	{ CCI_REG8(0x380e), 0x0c },
	{ CCI_REG8(0x380f), 0x8e },
	{ CCI_REG8(0x3811), 0x10 },
	{ CCI_REG8(0x3813), 0x08 },
	{ CCI_REG8(0x3814), 0x01 },
	{ CCI_REG8(0x3815), 0x01 },
	{ CCI_REG8(0x3816), 0x01 },
	{ CCI_REG8(0x3817), 0x01 },
	{ CCI_REG8(0x3820), 0xa8 },
	{ CCI_REG8(0x3821), 0x00 },
	{ CCI_REG8(0x3822), 0xc2 },
	{ CCI_REG8(0x3823), 0x18 },
	{ CCI_REG8(0x3826), 0x11 },
	{ CCI_REG8(0x3827), 0x1c },
	{ CCI_REG8(0x3829), 0x03 },
	{ CCI_REG8(0x3832), 0x00 },
	{ CCI_REG8(0x3c80), 0x00 },
	{ CCI_REG8(0x3c87), 0x01 },
	{ CCI_REG8(0x3c8c), 0x19 },
	{ CCI_REG8(0x3c8d), 0x1c },
	{ CCI_REG8(0x3c90), 0x00 },
	{ CCI_REG8(0x3c91), 0x00 },
	{ CCI_REG8(0x3c92), 0x00 },
	{ CCI_REG8(0x3c93), 0x00 },
	{ CCI_REG8(0x3c94), 0x40 },
	{ CCI_REG8(0x3c95), 0x54 },
	{ CCI_REG8(0x3c96), 0x34 },
	{ CCI_REG8(0x3c97), 0x04 },
	{ CCI_REG8(0x3c98), 0x00 },
	{ CCI_REG8(0x3d8c), 0x73 },
	{ CCI_REG8(0x3d8d), 0xc0 },
	{ CCI_REG8(0x3f00), 0x0b },
	{ CCI_REG8(0x3f03), 0x00 },
	{ CCI_REG8(0x4001), 0xe0 },
	{ CCI_REG8(0x4008), 0x00 },
	{ CCI_REG8(0x4009), 0x0f },
	{ CCI_REG8(0x4011), 0xf0 },
	{ CCI_REG8(0x4050), 0x04 },
	{ CCI_REG8(0x4051), 0x0b },
	{ CCI_REG8(0x4052), 0x00 },
	{ CCI_REG8(0x4053), 0x80 },
	{ CCI_REG8(0x4054), 0x00 },
	{ CCI_REG8(0x4055), 0x80 },
	{ CCI_REG8(0x4056), 0x00 },
	{ CCI_REG8(0x4057), 0x80 },
	{ CCI_REG8(0x4058), 0x00 },
	{ CCI_REG8(0x4059), 0x80 },
	{ CCI_REG8(0x405e), 0x00 },
	{ CCI_REG8(0x4500), 0x07 },
	{ CCI_REG8(0x4503), 0x00 },
	{ CCI_REG8(0x450a), 0x04 },
	{ CCI_REG8(0x4809), 0x04 },
	{ CCI_REG8(0x480c), 0x12 },
	{ CCI_REG8(0x481f), 0x30 },
	{ CCI_REG8(0x4833), 0x10 },
	{ CCI_REG8(0x4837), 0x0e },
	{ CCI_REG8(0x4902), 0x01 },
	{ CCI_REG8(0x4d00), 0x03 },
	{ CCI_REG8(0x4d01), 0xc9 },
	{ CCI_REG8(0x4d02), 0xbc },
	{ CCI_REG8(0x4d03), 0xd7 },
	{ CCI_REG8(0x4d04), 0xf0 },
	{ CCI_REG8(0x4d05), 0xa2 },
	{ CCI_REG8(0x5000), 0xff },
	{ CCI_REG8(0x5001), 0x07 },
	{ CCI_REG8(0x5040), 0x39 },
	{ CCI_REG8(0x5041), 0x10 },
	{ CCI_REG8(0x5042), 0x10 },
	{ CCI_REG8(0x5043), 0x84 },
	{ CCI_REG8(0x5044), 0x62 },
	{ CCI_REG8(0x5180), 0x00 },
	{ CCI_REG8(0x5181), 0x10 },
	{ CCI_REG8(0x5182), 0x02 },
	{ CCI_REG8(0x5183), 0x0f },
	{ CCI_REG8(0x5200), 0x1b },
	{ CCI_REG8(0x520b), 0x07 },
	{ CCI_REG8(0x520c), 0x0f },
	{ CCI_REG8(0x5300), 0x04 },
	{ CCI_REG8(0x5301), 0x0c },
	{ CCI_REG8(0x5302), 0x0c },
	{ CCI_REG8(0x5303), 0x0f },
	{ CCI_REG8(0x5304), 0x00 },
	{ CCI_REG8(0x5305), 0x70 },
	{ CCI_REG8(0x5306), 0x00 },
	{ CCI_REG8(0x5307), 0x80 },
	{ CCI_REG8(0x5308), 0x00 },
	{ CCI_REG8(0x5309), 0xa5 },
	{ CCI_REG8(0x530a), 0x00 },
	{ CCI_REG8(0x530b), 0xd3 },
	{ CCI_REG8(0x530c), 0x00 },
	{ CCI_REG8(0x530d), 0xf0 },
	{ CCI_REG8(0x530e), 0x01 },
	{ CCI_REG8(0x530f), 0x10 },
	{ CCI_REG8(0x5310), 0x01 },
	{ CCI_REG8(0x5311), 0x20 },
	{ CCI_REG8(0x5312), 0x01 },
	{ CCI_REG8(0x5313), 0x20 },
	{ CCI_REG8(0x5314), 0x01 },
	{ CCI_REG8(0x5315), 0x20 },
	{ CCI_REG8(0x5316), 0x08 },
	{ CCI_REG8(0x5317), 0x08 },
	{ CCI_REG8(0x5318), 0x10 },
	{ CCI_REG8(0x5319), 0x88 },
	{ CCI_REG8(0x531a), 0x88 },
	{ CCI_REG8(0x531b), 0xa9 },
	{ CCI_REG8(0x531c), 0xaa },
	{ CCI_REG8(0x531d), 0x0a },
	{ CCI_REG8(0x5405), 0x02 },
	{ CCI_REG8(0x5406), 0x67 },
	{ CCI_REG8(0x5407), 0x01 },
	{ CCI_REG8(0x5408), 0x4a },
};
static const struct cci_reg_sequence ov13855_2112x1568_regs[] = {
	{ CCI_REG8(0x0300), 0x02 },
	{ CCI_REG8(0x0301), 0x00 },
	{ CCI_REG8(0x0302), 0x5a },
	{ CCI_REG8(0x0303), 0x01 },
	{ CCI_REG8(0x0304), 0x00 },
	{ CCI_REG8(0x0305), 0x01 },
	{ CCI_REG8(0x3022), 0x01 },
	{ CCI_REG8(0x3013), 0x32 },
	{ CCI_REG8(0x3016), 0x72 },
	{ CCI_REG8(0x301b), 0xf0 },
	{ CCI_REG8(0x301f), 0xd0 },
	{ CCI_REG8(0x3106), 0x15 },
	{ CCI_REG8(0x3107), 0x23 },
	{ CCI_REG8(0x3500), 0x00 },
	{ CCI_REG8(0x3501), 0x64 },
	{ CCI_REG8(0x3502), 0x00 },
	{ CCI_REG8(0x3622), 0x30 },
	{ CCI_REG8(0x3624), 0x1c },
	{ CCI_REG8(0x3662), 0x10 },
	{ CCI_REG8(0x3709), 0x5f },
	{ CCI_REG8(0x3714), 0x28 },
	{ CCI_REG8(0x3737), 0x08 },
	{ CCI_REG8(0x3739), 0x20 },
	{ CCI_REG8(0x37a1), 0x36 },
	{ CCI_REG8(0x37a8), 0x3b },
	{ CCI_REG8(0x37ab), 0x31 },
	{ CCI_REG8(0x37c2), 0x14 },
	{ CCI_REG8(0x37d9), 0x0c },
	{ CCI_REG8(0x37e1), 0x0a },
	{ CCI_REG8(0x37e2), 0x14 },
	{ CCI_REG8(0x37e3), 0x08 },
	{ CCI_REG8(0x37e4), 0x38 },
	{ CCI_REG8(0x37e5), 0x03 },
	{ CCI_REG8(0x37e6), 0x08 },
	{ CCI_REG8(0x3800), 0x00 },
	{ CCI_REG8(0x3801), 0x00 },
	{ CCI_REG8(0x3802), 0x00 },
	{ CCI_REG8(0x3803), 0x08 },
	{ CCI_REG8(0x3804), 0x10 },
	{ CCI_REG8(0x3805), 0x9f },
	{ CCI_REG8(0x3806), 0x0c },
	{ CCI_REG8(0x3807), 0x4f },
	{ CCI_REG8(0x3808), 0x08 },
	{ CCI_REG8(0x3809), 0x40 },
	{ CCI_REG8(0x380a), 0x06 },
	{ CCI_REG8(0x380b), 0x20 },
	{ CCI_REG8(0x380c), 0x04 },
	{ CCI_REG8(0x380d), 0x62 },
	{ CCI_REG8(0x380e), 0x0c },
	{ CCI_REG8(0x380f), 0x89 },
	{ CCI_REG8(0x3811), 0x08 },
	{ CCI_REG8(0x3812), 0x00 },
	{ CCI_REG8(0x3813), 0x02 },
	{ CCI_REG8(0x3814), 0x03 },
	{ CCI_REG8(0x3815), 0x01 },
	{ CCI_REG8(0x3816), 0x03 },
	{ CCI_REG8(0x3817), 0x01 },
	{ CCI_REG8(0x3820), 0xab },
	{ CCI_REG8(0x3821), 0x00 },
	{ CCI_REG8(0x3826), 0x04 },
	{ CCI_REG8(0x3827), 0x90 },
	{ CCI_REG8(0x3829), 0x07 },
	{ CCI_REG8(0x3f03), 0x00 },
	{ CCI_REG8(0x4009), 0x0d },
	{ CCI_REG8(0x4011), 0xf0 },
	{ CCI_REG8(0x4050), 0x04 },
	{ CCI_REG8(0x4051), 0x0b },
	{ CCI_REG8(0x4500), 0x07 },
	{ CCI_REG8(0x4837), 0x1c },
	{ CCI_REG8(0x4902), 0x01 },
	{ CCI_REG8(0x4d00), 0x03 },
	{ CCI_REG8(0x4d01), 0xc9 },
	{ CCI_REG8(0x4d02), 0xbc },
	{ CCI_REG8(0x4d03), 0xd7 },
	{ CCI_REG8(0x4d04), 0xf0 },
	{ CCI_REG8(0x4d05), 0xa2 },
	{ CCI_REG8(0x5000), 0xff },
	{ CCI_REG8(0x5041), 0x10 },
	{ CCI_REG8(0x5042), 0x10 },
	{ CCI_REG8(0x5043), 0x84 },
	{ CCI_REG8(0x5044), 0x62 },
	{ CCI_REG8(0x5300), 0x04 },
	{ CCI_REG8(0x5301), 0x0c },
	{ CCI_REG8(0x5302), 0x0c },
	{ CCI_REG8(0x5303), 0x0f },
	{ CCI_REG8(0x5305), 0x70 },
	{ CCI_REG8(0x5307), 0x80 },
	{ CCI_REG8(0x5309), 0xa5 },
	{ CCI_REG8(0x530b), 0xd3 },
	{ CCI_REG8(0x5319), 0x88 },
	{ CCI_REG8(0x531a), 0x88 },
	{ CCI_REG8(0x531b), 0xa9 },
	{ CCI_REG8(0x531c), 0xaa },
	{ CCI_REG8(0x531d), 0x0a },
	{ CCI_REG8(0x5405), 0x02 },
	{ CCI_REG8(0x5406), 0x67 },
	{ CCI_REG8(0x5407), 0x01 },
	{ CCI_REG8(0x5408), 0x4a },
};

static int ov13855_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *sensor = container_of(sd, struct ov13855, sd);
	int ret;

	ret = regulator_enable(sensor->vdd);
	if (ret)
		return ret;

	ret = clk_prepare_enable(sensor->xclk);
	if (ret) {
		regulator_disable(sensor->vdd);
		return ret;
	}

	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(sensor->reset, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(sensor->powerdown, 0);
	/* Allow at least 8192 XCLK cycles before the first I2C transaction. */
	usleep_range(700, 1000);

	return 0;
}

static int ov13855_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *sensor = container_of(sd, struct ov13855, sd);

	gpiod_set_value_cansleep(sensor->powerdown, 1);
	gpiod_set_value_cansleep(sensor->reset, 1);
	clk_disable_unprepare(sensor->xclk);
	regulator_disable(sensor->vdd);

	return 0;
}

static int ov13855_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13855 *sensor = container_of(ctrl->handler, struct ov13855, ctrls);
	struct device *dev = sensor->sd.dev;
	unsigned int exposure_max;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = OV13855_HEIGHT + ctrl->val - OV13855_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(sensor->exposure, 4, exposure_max, 1,
					 OV13855_EXPOSURE_DEFAULT);
	}

	if (!pm_runtime_get_if_in_use(dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_write(sensor->regmap, OV13855_REG_EXPOSURE,
				ctrl->val << 4, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(sensor->regmap, OV13855_REG_GAIN, ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(sensor->regmap, OV13855_REG_VTS,
				OV13855_HEIGHT + ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(sensor->regmap, OV13855_REG_TEST_PATTERN,
				ctrl->val ? BIT(7) : 0, NULL);
		break;
	}

	pm_runtime_put(dev);
	return ret;
}

static const struct v4l2_ctrl_ops ov13855_ctrl_ops = {
	.s_ctrl = ov13855_set_ctrl,
};

static int ov13855_init_controls(struct ov13855 *sensor)
{
	struct v4l2_ctrl_handler *ctrls = &sensor->ctrls;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	const unsigned int hblank = OV13855_LINE_LENGTH - OV13855_WIDTH;
	int ret;

	ret = v4l2_fwnode_device_parse(sensor->sd.dev, &props);
	if (ret)
		return ret;

	v4l2_ctrl_handler_init(ctrls, 8);
	ctrl = v4l2_ctrl_new_int_menu(ctrls, NULL, V4L2_CID_LINK_FREQ, 0, 0,
				     ov13855_link_freq);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrls, NULL, V4L2_CID_PIXEL_RATE, OV13855_PIXEL_RATE,
			  OV13855_PIXEL_RATE, 1, OV13855_PIXEL_RATE);
	ctrl = v4l2_ctrl_new_std(ctrls, NULL, V4L2_CID_HBLANK, hblank, hblank, 1,
				 hblank);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrls, &ov13855_ctrl_ops, V4L2_CID_VBLANK,
			  OV13855_VTS_MIN - OV13855_HEIGHT,
			  OV13855_VTS_MAX - OV13855_HEIGHT, 1,
			  OV13855_VTS_DEFAULT - OV13855_HEIGHT);
	sensor->exposure = v4l2_ctrl_new_std(ctrls, &ov13855_ctrl_ops,
					     V4L2_CID_EXPOSURE, 4,
					     OV13855_VTS_DEFAULT - OV13855_EXPOSURE_MARGIN,
					     1, OV13855_EXPOSURE_DEFAULT);
	v4l2_ctrl_new_std(ctrls, &ov13855_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  0x80, 0x7c0, 1, 0x80);
	v4l2_ctrl_new_std_menu_items(ctrls, &ov13855_ctrl_ops, V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov13855_patterns) - 1, 0, 0,
				     ov13855_patterns);
	ret = v4l2_ctrl_new_fwnode_properties(ctrls, &ov13855_ctrl_ops, &props);
	if (ret) {
		v4l2_ctrl_handler_free(ctrls);
		return ret;
	}

	sensor->sd.ctrl_handler = ctrls;
	return 0;
}

static int ov13855_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct ov13855 *sensor = container_of(sd, struct ov13855, sd);
	int ret;

	ret = pm_runtime_resume_and_get(sd->dev);
	if (ret < 0)
		return ret;

	ret = cci_write(sensor->regmap, OV13855_REG_RESET, 1, NULL);
	if (ret)
		goto error;

	usleep_range(2000, 3000);
	cci_multi_reg_write(sensor->regmap, ov13855_global_regs,
			    ARRAY_SIZE(ov13855_global_regs), &ret);
	cci_multi_reg_write(sensor->regmap, ov13855_2112x1568_regs,
			    ARRAY_SIZE(ov13855_2112x1568_regs), &ret);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls);
	cci_write(sensor->regmap, OV13855_REG_MODE, 1, &ret);
	if (!ret)
		return 0;

error:
	pm_runtime_put_sync(sd->dev);
	return ret;
}

static int ov13855_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state, u32 pad,
				   u64 streams_mask)
{
	struct ov13855 *sensor = container_of(sd, struct ov13855, sd);
	int ret;

	ret = cci_write(sensor->regmap, OV13855_REG_MODE, 0, NULL);
	if (ret)
		dev_warn(sd->dev, "failed to put sensor into standby: %d\n", ret);

	pm_runtime_put(sd->dev);
	return 0;
}

static const struct v4l2_mbus_framefmt ov13855_format = {
	.width = OV13855_WIDTH,
	.height = OV13855_HEIGHT,
	.code = OV13855_CODE,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static int ov13855_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = ov13855_format;
	return 0;
}

static int ov13855_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	fmt->format = ov13855_format;
	return 0;
}

static int ov13855_enum_code(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = OV13855_CODE;
	return 0;
}

static int ov13855_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *size)
{
	if (size->index || size->code != OV13855_CODE)
		return -EINVAL;

	size->min_width = size->max_width = OV13855_WIDTH;
	size->min_height = size->max_height = OV13855_HEIGHT;
	return 0;
}

static int ov13855_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
		/* The full active area is read out using 2x2 binning. */
		sel->r = (struct v4l2_rect) {
			.width = 2 * OV13855_WIDTH,
			.height = 2 * OV13855_HEIGHT,
		};
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_internal_ops ov13855_internal_ops = {
	.init_state = ov13855_init_state,
};

static const struct v4l2_subdev_video_ops ov13855_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops ov13855_pad_ops = {
	.enum_mbus_code = ov13855_enum_code,
	.enum_frame_size = ov13855_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov13855_set_fmt,
	.get_selection = ov13855_get_selection,
	.enable_streams = ov13855_enable_streams,
	.disable_streams = ov13855_disable_streams,
};

static const struct v4l2_subdev_ops ov13855_subdev_ops = {
	.video = &ov13855_video_ops,
	.pad = &ov13855_pad_ops,
};

static int ov13855_check_endpoint(struct device *dev)
{
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "missing CSI-2 endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret)
		return ret;

	if (ep.bus.mipi_csi2.num_data_lanes != OV13855_LANES ||
	    ep.nr_of_link_frequencies != 1 ||
	    ep.link_frequencies[0] != OV13855_LINK_FREQ)
		ret = dev_err_probe(dev, -EINVAL,
				    "expected four CSI-2 lanes at 270 MHz link frequency\n");

	v4l2_fwnode_endpoint_free(&ep);
	return ret;
}

static int ov13855_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov13855 *sensor;
	u64 id;
	int ret;

	ret = ov13855_check_endpoint(dev);
	if (ret)
		return ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	sensor->xclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk))
		return dev_err_probe(dev, PTR_ERR(sensor->xclk), "failed to get xclk\n");
	if (clk_get_rate(sensor->xclk) != OV13855_XCLK)
		return dev_err_probe(dev, -EINVAL, "expected 24 MHz xclk\n");

	sensor->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(sensor->vdd))
		return dev_err_probe(dev, PTR_ERR(sensor->vdd), "failed to get module power\n");

	sensor->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset))
		return dev_err_probe(dev, PTR_ERR(sensor->reset), "failed to get reset\n");

	sensor->powerdown = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->powerdown))
		return dev_err_probe(dev, PTR_ERR(sensor->powerdown), "failed to get powerdown\n");

	v4l2_i2c_subdev_init(&sensor->sd, client, &ov13855_subdev_ops);
	sensor->sd.internal_ops = &ov13855_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = ov13855_power_on(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power sensor\n");

	ret = cci_read(sensor->regmap, OV13855_REG_CHIP_ID, &id, NULL);
	if (ret) {
		dev_err_probe(dev, ret, "failed to read chip ID\n");
		goto error_power_off;
	}
	if (id != OV13855_CHIP_ID) {
		ret = dev_err_probe(dev, -ENODEV, "unexpected chip ID 0x%06llx\n", id);
		goto error_power_off;
	}

	ret = ov13855_init_controls(sensor);
	if (ret)
		goto error_power_off;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto error_controls;

	sensor->sd.state_lock = sensor->ctrls.lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto error_entity;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto error_pm;

	pm_runtime_idle(dev);
	dev_info(dev, "OV13855 chip ID 0x%06llx\n", id);
	return 0;

error_pm:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	v4l2_subdev_cleanup(&sensor->sd);
error_entity:
	media_entity_cleanup(&sensor->sd.entity);
error_controls:
	v4l2_ctrl_handler_free(&sensor->ctrls);
error_power_off:
	ov13855_power_off(dev);
	return ret;
}

static void ov13855_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev)) {
		ov13855_power_off(dev);
		pm_runtime_set_suspended(dev);
	}
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov13855_pm_ops, ov13855_power_off, ov13855_power_on,
				 NULL);

static const struct of_device_id ov13855_of_match[] = {
	{ .compatible = "ovti,ov13855" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov13855_of_match);

static struct i2c_driver ov13855_driver = {
	.driver = {
		.name = "ov13855",
		.of_match_table = ov13855_of_match,
		.pm = pm_ptr(&ov13855_pm_ops),
	},
	.probe = ov13855_probe,
	.remove = ov13855_remove,
};
module_i2c_driver(ov13855_driver);

MODULE_DESCRIPTION("OmniVision OV13855 RAW Bayer sensor driver");
MODULE_LICENSE("GPL");
