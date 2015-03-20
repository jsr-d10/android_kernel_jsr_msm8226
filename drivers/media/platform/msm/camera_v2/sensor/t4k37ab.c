/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "msm_sensor.h"
#define t4k37ab_SENSOR_NAME "t4k37ab"
DEFINE_MSM_MUTEX(t4k37ab_mut);

static struct msm_sensor_ctrl_t t4k37ab_s_ctrl;

static struct msm_sensor_power_setting t4k37ab_power_setting[] = {

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VIO,
		.config_val = 0,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VIO,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VDIG,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VDIG,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VANA,
		.config_val = GPIO_OUT_LOW,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_VANA,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 15,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_HIGH,
		.delay = 40,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 24000000,
		.delay = 50,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},

};

static struct v4l2_subdev_info t4k37ab_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id t4k37ab_i2c_id[] = {
	{t4k37ab_SENSOR_NAME, (kernel_ulong_t)&t4k37ab_s_ctrl},
	{ }
};

static int32_t msm_t4k37ab_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return msm_sensor_i2c_probe(client, id, &t4k37ab_s_ctrl);
}

static struct i2c_driver t4k37ab_i2c_driver = {
	.id_table = t4k37ab_i2c_id,
	.probe  = msm_t4k37ab_i2c_probe,
	.driver = {
		.name = t4k37ab_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client t4k37ab_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id t4k37ab_dt_match[] = {
	{.compatible = "qcom,t4k37ab", .data = &t4k37ab_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, t4k37ab_dt_match);

static struct platform_driver t4k37ab_platform_driver = {
	.driver = {
		.name = "qcom,t4k37ab",
		.owner = THIS_MODULE,
		.of_match_table = t4k37ab_dt_match,
	},
};

static const char *t4k37abVendor = "Toshiba";
static const char *t4k37abNAME = "t4k37ab";
static const char *t4k37abSize = "13.0M";

static ssize_t sensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	sprintf(buf, "%s %s %s\n", t4k37abVendor, t4k37abNAME, t4k37abSize);
	return strlen(buf) + 1;
}

static DEVICE_ATTR(sensor, 0444, sensor_vendor_show, NULL);

static struct kobject *android_t4k37ab;

static int t4k37ab_sysfs_init(void)
{
	int ret;
	pr_info("%s: kobject create and add\n", __func__);
	android_t4k37ab = kobject_create_and_add("rear_camera", NULL);
	if (android_t4k37ab == NULL) {
		pr_err("%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret ;
	}
	pr_info("%s: sysfs_create_file\n", __func__);
	ret = sysfs_create_file(android_t4k37ab, &dev_attr_sensor.attr);
	if (ret) {
		pr_err("%s: sysfs_create_file failed\n", __func__);
		kobject_del(android_t4k37ab);
	}
	return 0;
}

static int32_t t4k37ab_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	match = of_match_device(t4k37ab_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	return rc;
}

static int __init t4k37ab_init_module(void)
{
	int32_t rc = 0;
	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&t4k37ab_platform_driver, t4k37ab_platform_probe);
	if (!rc) {
		t4k37ab_sysfs_init();
		return rc;
	}
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&t4k37ab_i2c_driver);
}

static void __exit t4k37ab_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (t4k37ab_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&t4k37ab_s_ctrl);
		platform_driver_unregister(&t4k37ab_platform_driver);
	} else
		i2c_del_driver(&t4k37ab_i2c_driver);
	return;
}

static struct msm_sensor_ctrl_t t4k37ab_s_ctrl = {
	.sensor_i2c_client = &t4k37ab_sensor_i2c_client,
	.power_setting_array.power_setting = t4k37ab_power_setting,
	.power_setting_array.size = ARRAY_SIZE(t4k37ab_power_setting),
	.msm_sensor_mutex = &t4k37ab_mut,
	.sensor_v4l2_subdev_info = t4k37ab_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(t4k37ab_subdev_info),
};

module_init(t4k37ab_init_module);
module_exit(t4k37ab_exit_module);
MODULE_DESCRIPTION("t4k37ab");
MODULE_LICENSE("GPL v2");