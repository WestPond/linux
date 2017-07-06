#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/watchdog.h>

#define DS1372_TIMEOUT_MAX		0x00FFFFFF
#define DS1372_TIMEOUT_MIN		0x00000001

static unsigned int heartbeat;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat, "Initial watchdog heartbeat in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct ds1372_priv {
	struct watchdog_device wdd;
};

static int ds1372_reset_wdt(struct i2c_client *client, int seed)
{
	char bytes[3];
	int ret;

	bytes[ 0 ] = seed;
	bytes[ 1 ] = seed >> 8;
	bytes[ 2 ] = seed >> 16;

	ret = i2c_smbus_write_i2c_block_data(client, 0x04, 3, bytes);
	if (ret) {
		dev_err(&client->dev, "ds1372_reset_wdt(): i2c write failed: %i\n", ret);
		return ret;
	}

	return 0;
}

static int ds1372_wdt_start(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = ds1372_reset_wdt(client, wdd->timeout);
	if (ret)
		return ret;

	ret = i2c_smbus_write_byte_data(client, 0x07, 0x49);
	if (ret) {
		dev_err(&client->dev, "ds1372_wdt_start(): i2c write failed: %i\n", ret);
		return ret;
	}

	return 0;
}

static int ds1372_wdt_stop(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_write_byte_data(client, 0x07, 0x09);
	if (ret) {
		dev_err(&client->dev, "ds1372_wdt_stop(): i2c write failed: %i\n", ret);
		return ret;
	}

	return 0;
}

static int ds1372_wdt_ping(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	return ds1372_reset_wdt(client, wdd->timeout);
}

static int ds1372_wdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int timeout)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	wdd->timeout = timeout;

	return ds1372_reset_wdt(client, timeout);
}

static unsigned int ds1372_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	char bytes[3];
	int ret;

	ret = i2c_smbus_read_i2c_block_data( client, 0x04, 3, bytes );
	if (ret < 0) {
		dev_err(&client->dev, "ds1372_wdt_get_timeleft(): i2c read failed: %i\n", ret);
		return -1;
	}

	return (bytes[2]<<16) | (bytes[1]<<8) | bytes[0];
}


static const struct watchdog_info ds1372_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "DS1372 based WDT",
};

static const struct watchdog_ops ds1372_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= ds1372_wdt_start,
	.stop		= ds1372_wdt_stop,
	.ping		= ds1372_wdt_ping,
	.set_timeout	= ds1372_wdt_set_timeout,
	.get_timeleft	= ds1372_wdt_get_timeleft,
};

static int ds1372_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct ds1372_priv *priv;
	int chip_id;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Get the chip ID, make sure it is what we expect */
	chip_id = i2c_smbus_read_byte_data(client, 0x09);
	if (chip_id != 0x75)
		return -EFAULT;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	i2c_set_clientdata(client, priv);

	priv->wdd.info = &ds1372_wdt_info;
	priv->wdd.ops = &ds1372_wdt_ops;
	priv->wdd.min_timeout = DS1372_TIMEOUT_MIN;
	priv->wdd.max_timeout = DS1372_TIMEOUT_MAX;
	priv->wdd.parent = &client->dev;

	watchdog_set_drvdata(&priv->wdd, priv);
	watchdog_init_timeout(&priv->wdd, heartbeat, &client->dev);
	watchdog_set_nowayout(&priv->wdd, nowayout);

	err = watchdog_register_device(&priv->wdd);
	if (err) {
		dev_err(&client->dev, "Failed to register watchdog device");
		return err;
	}

	return 0;
}

static int ds1372_remove(struct i2c_client *client)
{
	struct ds1372_priv *priv = i2c_get_clientdata( client );

	watchdog_unregister_device(&priv->wdd);

	return 0;
}


static const struct i2c_device_id ds1372_id[] = {
	{ "ds1372", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1372_id);

static const struct of_device_id ds1372_of_match[] = {
	{ .compatible = "ds,ds1372", },
	{ },
};
MODULE_DEVICE_TABLE(of, zrv_wdt_of_match);

static struct i2c_driver ds1372_driver = {
	.driver = {
		.name	= "ds1372",
		.of_match_table = ds1372_of_match,
	},
	.probe		= ds1372_probe,
	.remove		= ds1372_remove,
	.id_table	= ds1372_id,
};

module_i2c_driver(ds1372_driver);

MODULE_AUTHOR("Nathan Ford <nford@westpond.com>");
MODULE_DESCRIPTION("Watchdog driver for DS1372");
MODULE_LICENSE("GPL");
