// SPDX-License-Identifier: GPL-2.0-only
/*
 *  TI i2c 18 channels GPIO expander
 *
 *  Copyright (C) 2024 bytesatwork - https://www.bytesatwork.io
 */

#include <linux/bitmap.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define TCA6418_NGPIOS			18
#define TCA6418_NGPIO_BANKS		3

#define TCA6418_REG_INT_STAT		0x02
#define TCA6418_REG_GPIO_INT_STAT1	0x11
#define TCA6418_REG_GPIO_INT_STAT2	0x12
#define TCA6418_REG_GPIO_INT_STAT3	0x13
#define TCA6418_REG_GPIO_DAT_STAT1	0x14
#define TCA6418_REG_GPIO_DAT_STAT2	0x15
#define TCA6418_REG_GPIO_DAT_STAT3	0x16
#define TCA6418_REG_GPIO_DAT_OUT1	0x17
#define TCA6418_REG_GPIO_DAT_OUT2	0x18
#define TCA6418_REG_GPIO_DAT_OUT3	0x19
#define TCA6418_REG_GPIO_INT_EN1	0x1A
#define TCA6418_REG_GPIO_INT_EN2	0x1B
#define TCA6418_REG_GPIO_INT_EN3	0x1C
#define TCA6418_REG_GPIO_DIR1		0x23
#define TCA6418_REG_GPIO_DIR2		0x24
#define TCA6418_REG_GPIO_DIR3		0x25
#define TCA6418_REG_GPIO_INT_LVL1	0x26
#define TCA6418_REG_GPIO_INT_LVL2	0x27
#define TCA6418_REG_GPIO_INT_LVL3	0x28
#define TCA6418_REG_DEBOUNCE_DIS1	0x29
#define TCA6418_REG_DEBOUNCE_DIS2	0x2A
#define TCA6418_REG_DEBOUNCE_DIS3	0x2B
#define TCA6418_REG_GPIO_PULL1		0x2C
#define TCA6418_REG_GPIO_PULL2		0x2D
#define TCA6418_REG_GPIO_PULL3		0x2E
#define TCA6418_REG_MAX			0x2E

struct tca6418_chip {
	struct device *parent;
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	struct gpio_chip gpio;
	struct mutex bus_lock;
	DECLARE_BITMAP(enabled_irqs, TCA6418_NGPIOS);
	DECLARE_BITMAP(raising_edge_irqs, TCA6418_NGPIOS);
};

void tca6418_bus_lock(struct tca6418_chip *tca6418)
{
	mutex_lock(&tca6418->bus_lock);
}

void tca6418_bus_unlock(struct tca6418_chip *tca6418)
{
	mutex_unlock(&tca6418->bus_lock);
}

static unsigned int tca6418_line_to_bitnr(unsigned int line)
{
	if (line < 8)
		return 7 - line;
	else
		return line;
}

static u8 tca6418_line_to_mask(unsigned int line)
{
	unsigned int bit_nr = tca6418_line_to_bitnr(line);

	return BIT(bit_nr % 8);
}

static int tca6418_line_to_reg(unsigned int line, unsigned int start_reg,
			       u8 *reg)
{
	if (line < TCA6418_NGPIOS)
		*reg = start_reg + (line / 8);
	else
		return -EINVAL;

	return 0;
}

static int tca6418_line_to_direction_reg(unsigned int line, u8 *reg)
{
	return tca6418_line_to_reg(line, TCA6418_REG_GPIO_DIR1, reg);
}

static int tca6418_line_to_data_reg(unsigned int line, u8 *reg)
{
	return tca6418_line_to_reg(line, TCA6418_REG_GPIO_DAT_OUT1, reg);
}

static int tca6418_line_to_state_reg(unsigned int line, u8 *reg)
{
	return tca6418_line_to_reg(line, TCA6418_REG_GPIO_DAT_STAT1, reg);
}

static int tca6418_clear_int_stat(struct tca6418_chip *tca6418)
{
	int ret;

	ret = regmap_write(tca6418->regmap, TCA6418_REG_INT_STAT, 0x02);
	if (ret)
		dev_warn(tca6418->parent, "Could not clear interrupt: %i\n",
			 ret);

	return ret;
}

static bool tca6418_readable_register(struct device *dev, unsigned int reg)
{
	return true;
}

static bool tca6418_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TCA6418_REG_INT_STAT:
	case TCA6418_REG_GPIO_DAT_OUT1:
	case TCA6418_REG_GPIO_DAT_OUT2:
	case TCA6418_REG_GPIO_DAT_OUT3:
	case TCA6418_REG_GPIO_INT_EN1:
	case TCA6418_REG_GPIO_INT_EN2:
	case TCA6418_REG_GPIO_INT_EN3:
	case TCA6418_REG_GPIO_DIR1:
	case TCA6418_REG_GPIO_DIR2:
	case TCA6418_REG_GPIO_DIR3:
	case TCA6418_REG_GPIO_INT_LVL1:
	case TCA6418_REG_GPIO_INT_LVL2:
	case TCA6418_REG_GPIO_INT_LVL3:
	case TCA6418_REG_DEBOUNCE_DIS1:
	case TCA6418_REG_DEBOUNCE_DIS2:
	case TCA6418_REG_DEBOUNCE_DIS3:
	case TCA6418_REG_GPIO_PULL1:
	case TCA6418_REG_GPIO_PULL2:
	case TCA6418_REG_GPIO_PULL3:
		return true;
	default:
		return false;
	}
}

static bool tca6418_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TCA6418_REG_INT_STAT:
	case TCA6418_REG_GPIO_INT_STAT1:
	case TCA6418_REG_GPIO_INT_STAT2:
	case TCA6418_REG_GPIO_INT_STAT3:
	case TCA6418_REG_GPIO_DAT_STAT1:
	case TCA6418_REG_GPIO_DAT_STAT2:
	case TCA6418_REG_GPIO_DAT_STAT3:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tca6418_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.readable_reg		= tca6418_readable_register,
	.writeable_reg		= tca6418_writeable_register,
	.volatile_reg		= tca6418_volatile_register,
	.cache_type		= REGCACHE_RBTREE,
	.max_register		= TCA6418_REG_MAX,
	.disable_locking	= true,
	.can_sleep		= true,
};

static int tca6418_get_direction(struct gpio_chip *chip, unsigned int line)
{
	struct tca6418_chip *tca6418 = gpiochip_get_data(chip);
	u8 reg;
	int ret;

	ret = tca6418_line_to_direction_reg(line, &reg);
	if (ret)
		return ret;


	tca6418_bus_lock(tca6418);
	ret = regmap_test_bits(tca6418->regmap, reg,
			       tca6418_line_to_mask(line));
	tca6418_bus_unlock(tca6418);

	if (ret < 0)
		return ret;
	else if (ret)
		return GPIO_LINE_DIRECTION_OUT;
	else
		return GPIO_LINE_DIRECTION_IN;
}

static int tca6418_direction_input(struct gpio_chip *chip, unsigned int line)
{
	struct tca6418_chip *tca6418 = gpiochip_get_data(chip);
	u8 reg;
	u8 mask;
	int ret;

	ret = tca6418_line_to_direction_reg(line, &reg);
	if (unlikely(ret))
		return ret;
	mask = tca6418_line_to_mask(line);

	tca6418_bus_lock(tca6418);
	ret = regmap_write_bits(tca6418->regmap, reg, mask, 0);
	tca6418_bus_unlock(tca6418);

	return ret;
}

static int tca6418_direction_output(struct gpio_chip *chip, unsigned int line,
				    int value)
{
	struct tca6418_chip *tca6418 = gpiochip_get_data(chip);
	u8 reg_dir;
	u8 reg_data;
	u8 mask;
	int ret;

	ret = tca6418_line_to_direction_reg(line, &reg_dir);
	if (unlikely(ret))
		return ret;
	ret = tca6418_line_to_data_reg(line, &reg_data);
	if (unlikely(ret))
		return ret;
	mask = tca6418_line_to_mask(line);

	tca6418_bus_lock(tca6418);
	ret = regmap_update_bits(tca6418->regmap, reg_dir, mask, mask);
	if (!ret)
		ret = regmap_update_bits(tca6418->regmap, reg_data, mask,
					 value ? mask : 0);
	tca6418_bus_unlock(tca6418);

	return ret;
}

static int tca6418_gpio_get(struct gpio_chip *chip, unsigned int line)
{
	struct tca6418_chip *tca6418 = gpiochip_get_data(chip);
	u8 reg;
	u8 mask;
	int ret;

	ret = tca6418_line_to_state_reg(line, &reg);
	if (ret)
		return ret;
	mask = tca6418_line_to_mask(line);

	tca6418_bus_lock(tca6418);
	ret = regmap_test_bits(tca6418->regmap, reg, mask);
	tca6418_bus_unlock(tca6418);

	return ret;
}

static int tca6418_gpio_set(struct gpio_chip *chip, unsigned int line,
			     int value)
{
	struct tca6418_chip *tca6418 = gpiochip_get_data(chip);
	int ret;
	u8 reg;
	u8 mask;

	if (tca6418_line_to_data_reg(line, &reg))
		return -EINVAL;
	mask = tca6418_line_to_mask(line);

	tca6418_bus_lock(tca6418);
	ret = regmap_update_bits(tca6418->regmap, reg, mask, value ? mask : 0);
	tca6418_bus_unlock(tca6418);

	if (ret)
		dev_warn(tca6418->parent, "Could not set gpio: %i\n", ret);

	return ret;
}

static const struct gpio_chip tca6418_gpio = {
	.label			= "tca6418",
	.owner			= THIS_MODULE,
	.get_direction		= tca6418_get_direction,
	.direction_input	= tca6418_direction_input,
	.direction_output	= tca6418_direction_output,
	.get			= tca6418_gpio_get,
	.set			= tca6418_gpio_set,
	.base			= -1,
	.ngpio			= TCA6418_NGPIOS,
	.can_sleep		= true,
};

static void tca6418_irq_mask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct tca6418_chip *tca6418 = gpiochip_get_data(gc);

	if (unlikely(hwirq >= tca6418->gpio.ngpio)) {
		dev_warn(tca6418->parent, "Mask IRQ %li out of bound\n", hwirq);
	} else {
		clear_bit(tca6418_line_to_bitnr(hwirq), tca6418->enabled_irqs);
		gpiochip_disable_irq(gc, hwirq);
	}
}

static void tca6418_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct tca6418_chip *tca6418 = gpiochip_get_data(gc);

	if (unlikely(hwirq >= tca6418->gpio.ngpio)) {
		dev_warn(tca6418->parent, "Unmask IRQ %li out of bound\n",
			 hwirq);
	} else {
		gpiochip_enable_irq(gc, hwirq);
		set_bit(tca6418_line_to_bitnr(hwirq), tca6418->enabled_irqs);
	}
}

static int tca6418_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct tca6418_chip *tca6418 = gpiochip_get_data(gc);

	if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		dev_err(tca6418->parent, "Both edge IRQ not supported\n");
		return -EINVAL;
	} else if (unlikely(hwirq >= tca6418->gpio.ngpio)) {
		dev_err(tca6418->parent, "IRQ %li out of bound\n", hwirq);
		return -EINVAL;
	} else if (flow_type & IRQ_TYPE_EDGE_RISING) {
		set_bit(tca6418_line_to_bitnr(hwirq),
			tca6418->raising_edge_irqs);
	} else {
		clear_bit(tca6418_line_to_bitnr(hwirq),
			  tca6418->raising_edge_irqs);
	}

	return 0;
}

static void tca6418_irq_bus_lock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct tca6418_chip *tca6418 = gpiochip_get_data(gc);

	tca6418_bus_lock(tca6418);
}

static void tca6418_irq_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct tca6418_chip *tca6418 = gpiochip_get_data(gc);

	for (int i = 0 ; i < TCA6418_NGPIO_BANKS ; i++) {
		u8 int_en = bitmap_get_value8(tca6418->enabled_irqs, i * 8);
		u8 int_lvl = bitmap_get_value8(tca6418->raising_edge_irqs, i * 8);

		regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_EN1 + i,
			     int_en);
		regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_LVL1 + i,
			     int_lvl);
	}

	tca6418_bus_unlock(tca6418);
}

static const struct irq_chip tca6418_irq_chip = {
	.name			= "tca6418",
	.irq_mask		= tca6418_irq_mask,
	.irq_unmask		= tca6418_irq_unmask,
	.irq_set_type		= tca6418_irq_set_type,
	.irq_bus_lock		= tca6418_irq_bus_lock,
	.irq_bus_sync_unlock	= tca6418_irq_bus_sync_unlock,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int tca6418_dispatch_irq(struct tca6418_chip *tca6418)
{
	int ret;
	int nested_irq;
	unsigned int int_stat_gpio[TCA6418_NGPIO_BANKS];

	for (int i = 0 ; i < ARRAY_SIZE(int_stat_gpio) ; i++) {
		ret = regmap_read(tca6418->regmap,
				  TCA6418_REG_GPIO_INT_STAT1 + i,
				  &int_stat_gpio[i]);
		if (ret)
			dev_warn_ratelimited(tca6418->parent,
					     "Could not read INT_STAT%i: %i\n",
					     i, ret);
	}

	ret = 0;
	for (int i = 0 ; i < TCA6418_NGPIOS ; i++) {
		if (!(int_stat_gpio[i / 8] & tca6418_line_to_mask(i)))
			continue;

		nested_irq = irq_find_mapping(tca6418->gpio.irq.domain, i);
		if (unlikely(nested_irq <= 0)) {
			dev_warn_ratelimited(tca6418->parent,
					     "unmapped interrupt %i\n", i);
			continue;
		}
		handle_nested_irq(nested_irq);
		ret = 1;
	}
	tca6418_clear_int_stat(tca6418);

	return ret;
}

static irqreturn_t tca6418_irq_handler(int irq, void *devid)
{
	struct tca6418_chip *tca6418 = devid;
	int irq_ret;
	int ret;
	unsigned int int_stat;

	tca6418_bus_lock(tca6418);
	do {
		irq_ret = tca6418_dispatch_irq(tca6418);
		ret = regmap_read(tca6418->regmap, TCA6418_REG_INT_STAT,
				  &int_stat);
		if (ret) {
			dev_warn_ratelimited(tca6418->parent,
					     "Could not read INT_STAT: %i\n",
					     ret);
			break;
		}
	} while (int_stat);
	tca6418_bus_unlock(tca6418);

	return IRQ_RETVAL(irq_ret);
}

static int tca6418_init_gpio(struct tca6418_chip *tca6418)
{
	struct gpio_irq_chip *girq;
	unsigned int int_stat;
	int ret;

	tca6418->gpio = tca6418_gpio;
	tca6418->gpio.parent = tca6418->parent;
	if (tca6418->i2c_client->irq) {
		/* Acknowledge IRQs if there are */
		for (int i = 0 ; i < TCA6418_NGPIO_BANKS ; i++) {
			ret = regmap_read(tca6418->regmap,
					  TCA6418_REG_GPIO_INT_STAT1 + i,
					  &int_stat);
			if (ret) {
				dev_warn(tca6418->parent,
					 "Could not read INT_STAT%i:\n", ret);
				return ret;
			}
		}
		ret = tca6418_clear_int_stat(tca6418);
		if (ret)
			return ret;

		girq = &tca6418->gpio.irq;
		gpio_irq_chip_set_chip(girq, &tca6418_irq_chip);
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
		girq->threaded = true;
		ret = devm_request_threaded_irq(tca6418->parent,
						tca6418->i2c_client->irq, NULL,
						tca6418_irq_handler,
						IRQF_TRIGGER_FALLING |
							IRQF_ONESHOT |
							IRQF_SHARED,
						"tca6418_irq", tca6418);
		if (ret < 0) {
			dev_err(tca6418->parent, "Failed to request IRQ: %i\n",
				ret);
			return ret;
		}
	} else {
		dev_info(tca6418->parent, "No IRQ configured\n");
	}

	return devm_gpiochip_add_data(tca6418->parent, &tca6418->gpio, tca6418);
}

static int tca6418_init_regmap(struct tca6418_chip *tca6418)
{
	int ret;

	tca6418->regmap = devm_regmap_init_i2c(tca6418->i2c_client,
					       &tca6418_regmap_config);
	if (IS_ERR(tca6418->regmap))
		return PTR_ERR(tca6418->regmap);
	regcache_mark_dirty(tca6418->regmap);
	ret = regcache_sync(tca6418->regmap);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DAT_OUT1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DAT_OUT2, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DAT_OUT3, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_EN1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_EN2, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_EN3, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DIR1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DIR2, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_DIR3, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_LVL1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_LVL2, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_INT_LVL3, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_DEBOUNCE_DIS1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_DEBOUNCE_DIS2, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_DEBOUNCE_DIS3, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_PULL1, 0);
	if (ret)
		return ret;
	ret = regmap_write(tca6418->regmap, TCA6418_REG_GPIO_PULL2, 0);
	if (ret)
		return ret;
	return regmap_write(tca6418->regmap, TCA6418_REG_GPIO_PULL3, 0);
}

static const struct i2c_device_id tca6418_id[] = {
	{ "tca6418" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tca6418_id);

static const struct of_device_id tca6418_dt_ids[] = {
	{ .compatible = "ti,tca6418"},
	{ }
};
MODULE_DEVICE_TABLE(of, tca6418_dt_ids);

static int tca6418_probe(struct i2c_client *client)
{
	struct tca6418_chip *tca6418;
	int ret;

	tca6418 = devm_kzalloc(&client->dev, sizeof(*tca6418), GFP_KERNEL);
	if (tca6418 == NULL)
		return -ENOMEM;
	i2c_set_clientdata(client, tca6418);

	tca6418->parent = &client->dev;
	tca6418->i2c_client = client;
	mutex_init(&tca6418->bus_lock);

	ret = tca6418_init_regmap(tca6418);
	if (ret)
		return ret;

	ret = tca6418_init_gpio(tca6418);
	if (ret)
		return ret;

	dev_info(tca6418->parent, "Device initialized");

	return 0;
}

static void tca6418_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "Device removed");
}

static struct i2c_driver tca6418_driver = {
	.driver = {
		.name	= "tca6418",
		.of_match_table = tca6418_dt_ids,
	},
	.probe		= tca6418_probe,
	.remove		= tca6418_remove,
	.id_table	= tca6418_id,
};

static int __init tca6418_init(void)
{
	return i2c_add_driver(&tca6418_driver);
}
subsys_initcall(tca6418_init);

static void __exit tca6418_exit(void)
{
	i2c_del_driver(&tca6418_driver);
}
module_exit(tca6418_exit);

MODULE_AUTHOR("Guy Morand <guy.morand@bytesatwork.ch>");
MODULE_DESCRIPTION("GPIO expander driver for TCA6418E");
MODULE_LICENSE("GPL");
