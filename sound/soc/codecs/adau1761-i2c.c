/*
 * Driver for ADAU1361/ADAU1461/ADAU1761/ADAU1961 codec
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "adau1761.h"

static const char reg_avdd[]  = "AVDD";
static const char reg_iovdd[] = "IOVDD";

static int adau1761_i2c_reg_ctrl(struct device *dev, bool state)
{
	struct adau *adau = dev_get_drvdata(dev);
	int ret;

	if (state) {
		ret = regulator_bulk_enable(ARRAY_SIZE(adau->regulators),
					adau->regulators);
		if (ret)
			dev_err(dev, "failed to enable regulators: %d\n", ret);
		/* Chip needs time to wakeup. Not mentioned in datasheet */
		usleep_range(10000, 20000);
	} else {
		ret = regulator_bulk_disable(ARRAY_SIZE(adau->regulators),
					adau->regulators);
		if (ret)
			dev_err(dev, "failed to disable regulators: %d\n", ret);
	}
	return ret;
}

static int adau1761_i2c_reg_init(struct device *dev)
{
	struct adau *adau = dev_get_drvdata(dev);
	int ret;

	adau->regulators[0].supply = reg_avdd;
	adau->regulators[1].supply = reg_iovdd;
	ret = regulator_bulk_get(dev,
				 ARRAY_SIZE(adau->regulators),
				 adau->regulators);
	if (ret) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ret = adau1761_i2c_reg_ctrl(dev, true);
	if (ret)
		goto free_reg;

	return 0;

free_reg:
	regulator_bulk_free(ARRAY_SIZE(adau->regulators),
				adau->regulators);
	return ret;
}

static int adau1761_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regmap_config config;
	int ret;

	config = adau1761_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 16;

	ret = adau1761_probe(dev,
				devm_regmap_init_i2c(client, &config),
				id->driver_data, NULL);
	if (ret)
		return ret;

	return adau1761_i2c_reg_init(dev);
}

static int adau1761_i2c_remove(struct i2c_client *client)
{
	struct adau *adau = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	adau17x1_remove(dev);

	adau1761_i2c_reg_ctrl(dev, false);
	regulator_bulk_free(ARRAY_SIZE(adau->regulators),
				adau->regulators);

	return 0;
}

static const struct i2c_device_id adau1761_i2c_ids[] = {
	{ "adau1361", ADAU1361 },
	{ "adau1461", ADAU1761 },
	{ "adau1761", ADAU1761 },
	{ "adau1961", ADAU1361 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1761_i2c_ids);

#if defined(CONFIG_OF)
static const struct of_device_id adau1761_i2c_dt_ids[] = {
	{ .compatible = "adi,adau1361", },
	{ .compatible = "adi,adau1461", },
	{ .compatible = "adi,adau1761", },
	{ .compatible = "adi,adau1961", },
	{ },
};
MODULE_DEVICE_TABLE(of, adau1761_i2c_dt_ids);
#endif

static struct i2c_driver adau1761_i2c_driver = {
	.driver = {
		.name = "adau1761",
		.of_match_table = of_match_ptr(adau1761_i2c_dt_ids),
	},
	.probe = adau1761_i2c_probe,
	.remove = adau1761_i2c_remove,
	.id_table = adau1761_i2c_ids,
};
module_i2c_driver(adau1761_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1361/ADAU1461/ADAU1761/ADAU1961 CODEC I2C driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
