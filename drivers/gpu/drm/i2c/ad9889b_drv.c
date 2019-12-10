#include <linux/component.h>
//#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/kthread.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

struct ad9889b_priv {
	struct i2c_client	*client;

	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_connector connector;

	u8 hpd;
	u8 rxs;
	struct task_struct *hpd_poll;

	int irq;

	struct i2c_client *edid_client;

	u8 enabled;
};

#define conn_to_ad9889b_priv(x) container_of(x, struct ad9889b_priv, connector)
#define enc_to_ad9889b_priv(x)  container_of(x, struct ad9889b_priv, encoder)
#define bridge_to_ad9889b_priv(x) container_of(x, struct ad9889b_priv, bridge)

static int ad9889b_reg_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int ad9889b_reg_write(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

static int ad9889b_reg_mod(struct i2c_client *client, u8 reg, u8 mask, u8 val)
{
	u8 v = i2c_smbus_read_byte_data(client, reg);
	v = (v & ~mask) | (val & mask);
	return i2c_smbus_write_byte_data(client, reg, v);
}

static void ad9889b_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs ad9889b = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = ad9889b_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ad9889b_connector_get_modes(struct drm_connector *connector)
{
	int count = drm_add_modes_noedid(connector, 1920, 1080);
	drm_set_preferred_mode(connector, 640, 480);
	return count;
}

static struct drm_encoder *
ad9889b_connector_best_encoder(struct drm_connector *connector)
{
	struct ad9889b_priv *priv = conn_to_ad9889b_priv(connector);

	return priv->bridge.encoder;
}

static int ad9889b_connector_detect(struct drm_connector *connector,
									struct drm_modeset_acquire_ctx *ctx,
									bool force)
{
	struct ad9889b_priv *priv = conn_to_ad9889b_priv(connector);
	u8 val;

	val = ad9889b_reg_read(priv->client, 0x42);
	return val & 0x40 ? connector_status_connected : connector_status_disconnected;
}

static const struct drm_connector_helper_funcs ad9889b_connector_helper_funcs = {
	.get_modes = ad9889b_connector_get_modes,
	.detect_ctx = ad9889b_connector_detect,
	.best_encoder = ad9889b_connector_best_encoder,
};

static int ad9889b_connector_init(struct ad9889b_priv *priv,
				  struct drm_device *drm)
{
	struct drm_connector *connector = &priv->connector;
	int ret;

	connector->interlace_allowed = 0;

	if (priv->irq)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else
		connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector, &ad9889b_connector_helper_funcs);
	ret = drm_connector_init(drm, connector, &ad9889b, DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	drm_connector_attach_encoder(&priv->connector, priv->bridge.encoder);

	return 0;
}

static int ad9889b_bridge_attach(struct drm_bridge *bridge)
{
	struct ad9889b_priv *priv = bridge_to_ad9889b_priv(bridge);

	return ad9889b_connector_init(priv, bridge->dev);
}

static void ad9889b_bridge_detach(struct drm_bridge *bridge)
{
	struct ad9889b_priv *priv = bridge_to_ad9889b_priv(bridge);

	drm_connector_cleanup(&priv->connector);
}

static enum drm_mode_status ad9889b_bridge_mode_valid(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode)
{
	if (mode->hdisplay > 1920)
		return MODE_H_ILLEGAL;
	if (mode->vdisplay > 1080)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

static void ad9889b_bridge_enable(struct drm_bridge *bridge)
{
	struct ad9889b_priv *priv = bridge_to_ad9889b_priv(bridge);

// TODO: Power up transmitter

	priv->enabled = 1;
}

static void ad9889b_bridge_disable(struct drm_bridge *bridge)
{
	struct ad9889b_priv *priv = bridge_to_ad9889b_priv(bridge);
// TODO: Power down transmitter
	priv->enabled = 0;
}

static void ad9889b_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted_mode)
{ }

static const struct drm_bridge_funcs ad9889b_bridge_funcs = {
	.attach = ad9889b_bridge_attach,
	.detach = ad9889b_bridge_detach,
	.mode_valid = ad9889b_bridge_mode_valid,
	.disable = ad9889b_bridge_disable,
	.mode_set = ad9889b_bridge_mode_set,
	.enable = ad9889b_bridge_enable,
};

static void ad9889b_mute(struct ad9889b_priv *priv, u8 mute)
{
	ad9889b_reg_mod(priv->client, 0x45, 0xc0, mute ? 0x40 : 0x80 );
}

static void ad9889b_power_up(struct ad9889b_priv *priv)
{
	u8 val;
	int i;

	/* Power up chip */
	ad9889b_reg_mod(priv->client, 0x41, 0x40, 0);
	for (i = 0; i < 20; ++i) {
		val = ad9889b_reg_read(priv->client, 0x41);
		if ((val & 0x40) == 0)
			break;
		ad9889b_reg_mod(priv->client, 0x41, 0x40, 0);
		msleep(10);
	}

	/* Static setup */
	ad9889b_reg_mod(priv->client, 0x0a, 0x60, 0x00);
	ad9889b_reg_write(priv->client, 0x98, 0x07);		/* Driver does 0x03 */
	ad9889b_reg_write(priv->client, 0x9c, 0x38);
	ad9889b_reg_write(priv->client, 0x9d, 0x61);
	ad9889b_reg_write(priv->client, 0xa2, 0x87);
	ad9889b_reg_write(priv->client, 0xa3, 0x87);
	ad9889b_reg_write(priv->client, 0xbb, 0xff);
}

static void ad9889b_power_down(struct ad9889b_priv *priv)
{
	/* Power down */
	ad9889b_reg_mod(priv->client, 0x41, 0x40, 0x40);

	/* Turn off TMDS */
	ad9889b_reg_write(priv->client, 0xa1, 0x3c);
}

static void ad9889b_setup(struct ad9889b_priv *priv)
{
	/* Input format: 48Khz, 24-bit RGB, >30Hz */
	ad9889b_reg_write(priv->client, 0x15, 0x20);
	/* Output format: RGB */
	ad9889b_reg_write(priv->client, 0x16, 0x30);
	/* 16:9 aspect, color upconvert */
	ad9889b_reg_mod(priv->client, 0x17, 0x06, 0x06);
	/* RGB output, Valid AVI info */
	ad9889b_reg_mod(priv->client, 0x45, 0x3e, 0x08);
	/* Disable CSC */
	ad9889b_reg_mod(priv->client, 0x3b, 0x01, 0x00);
	/* AVI Info Frame full range */
	ad9889b_reg_mod(priv->client, 0xcd, 0x06, 0x04);


	/* Scan information pc */
	ad9889b_reg_write(priv->client, 0x46, 0x80);
	/* Same aspect ratio */
	ad9889b_reg_write(priv->client, 0x47, 0x80);
	/* No clk delay */
	ad9889b_reg_write(priv->client, 0xba, 0x60);

	/* Disable HDPC, not supporting */
	ad9889b_reg_mod(priv->client, 0xaf, 0x82, 0x00);	/* HDCP desired = 0, Frame Encryption = 0, DVI */

	/* SPDIF Audio */
	ad9889b_reg_mod(priv->client, 0x44, 0x80, 0x80);
	/* No I2S */
	ad9889b_reg_write(priv->client, 0x0c, 0x00);
	/* Select SPDIF */
	ad9889b_reg_write(priv->client, 0x0a, 0x08);
}

static void ad9889b_irq_enable(struct ad9889b_priv *priv, u8 enable)
{
	u8 irqs = enable ? 0xc0 : 0;
	u8 val;
	int i;

	/* enable HPD/Rx/EDID irq */
	ad9889b_reg_write(priv->client, 0x94, irqs);
	for (i = 0; i < 100; ++i) {
		val = ad9889b_reg_read(priv->client, 0x94);
		if (val == irqs)
			return;
		ad9889b_reg_write(priv->client, 0x94, irqs);
	}
}

static void ad9889b_hpd_poll(struct ad9889b_priv *priv, u8 flags)
{
	struct drm_device *dev = priv->connector.dev;
	u8 val;

	/* TODO: Only power up the transmitter on hpd if the output has been enabled */

	if (flags & 0x40) {
		/* Read HPD status */
		val = ad9889b_reg_read(priv->client, 0x42);
		if ((val & 0x40) && !priv->hpd) {
			priv->hpd = 1;

			ad9889b_power_up(priv);
			ad9889b_mute(priv, 1);
			ad9889b_setup(priv);

			if (dev)
				drm_kms_helper_hotplug_event(dev);
		} else if (!(val & 0x40) && priv->hpd) {
			priv->hpd = 0;

			ad9889b_power_down(priv);
		}
	}

	if (flags & 0x20) {
		val = ad9889b_reg_read(priv->client, 0x42);
		if ((val & 0x20) && !priv->rxs) {
			priv->rxs = 1;

			/* Power on TMSD */
			ad9889b_reg_write(priv->client, 0xa1, 0x0);
			ad9889b_mute(priv, 0);
		} else if (!(val & 0x20) && priv->rxs) {
			priv->rxs = 0;

			/* Power off tsmds */
			ad9889b_mute(priv, 1);
			ad9889b_reg_write(priv->client, 0xa1, 0x3c);
		}
	}
}

static int ad9889b_hpd_task(void *data)
{
	struct ad9889b_priv *priv = data;

	while (1) {
		ad9889b_hpd_poll(priv, 0xff);
		msleep(1000);
	}

	return 0;
}

static irqreturn_t ad9889b_irq_thread(int irq, void *data)
{
	struct ad9889b_priv *priv = data;
	u8 val = 0;

	/* Disable interrupts while we clear */
	ad9889b_irq_enable(priv, 0);
	/* Query interrupt status */
	val = ad9889b_reg_read(priv->client, 0x96);
	/* Clear interrupt */
	ad9889b_reg_write(priv->client, 0x96, val);
	/* Re-enable interrupts */
	ad9889b_irq_enable(priv, 1);

	if (!val)
		return IRQ_NONE;

	ad9889b_hpd_poll(priv, val);

	return IRQ_HANDLED;
}

static void ad9889b_destroy(struct device *dev)
{
	struct ad9889b_priv *priv = dev_get_drvdata(dev);

	drm_bridge_remove(&priv->bridge);
}

static int ad9889b_create(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ad9889b_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	INIT_LIST_HEAD(&priv->bridge.list);

	priv->client = client;
	priv->edid_client = i2c_new_dummy(client->adapter, 0x38);

	ad9889b_power_down(priv);

	/* Early setup */
	ad9889b_reg_mod(client, 0xa5, 0xc0, 0xc0);		// This conflicts with spec, but is in ref driver...
	ad9889b_reg_write(client, 0x9d, 0x68);			// This also conflicts with spec, but is in ref driver

	/* initialize the optional IRQ */
	if (client->irq) {
		unsigned long irq_flags;

		/* Clear interrupts */
		ad9889b_reg_write(client, 0x96, 0xff);

		irq_flags = irqd_get_trigger_type(irq_get_irq_data(client->irq));
		irq_flags |= IRQF_SHARED | IRQF_ONESHOT;
		ret = request_threaded_irq(client->irq, NULL,
					   ad9889b_irq_thread, irq_flags,
					   "ad9889b", priv);
		if (ret) {
			dev_err(dev, "failed to request IRQ#%u: %d\n",
				client->irq, ret);
			goto err_irq;
		}

		ad9889b_irq_enable(priv, 1);
		priv->irq = 1;
	} else {
		ad9889b_irq_enable(priv, 0);

		priv->hpd_poll = kthread_run (ad9889b_hpd_task, priv, "ad9889_hpd_poll");
	}

	/* TODO: When the enable / disable code has been writen, this is no longer needed */
	ad9889b_hpd_poll(priv, 0xff);

	priv->bridge.funcs = &ad9889b_bridge_funcs;
#ifdef CONFIG_OF
	priv->bridge.of_node = dev->of_node;
#endif

	drm_bridge_add(&priv->bridge);

	return 0;

err_irq:
	return ret;
}

/* DRM encoder functions */

static void ad9889b_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs ad9889b_encoder_funcs = {
	.destroy = ad9889b_encoder_destroy,
};

static int ad9889b_encoder_init(struct device *dev, struct drm_device *drm)
{
	struct ad9889b_priv *priv = dev_get_drvdata(dev);
	u32 crtcs = 0;
	int ret;

	if (dev->of_node)
		crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/* If no CRTCs were found, fall back to our old behaviour */
	if (crtcs == 0) {
		dev_warn(dev, "Falling back to first CRTC\n");
		crtcs = 1 << 0;
	}

	priv->encoder.possible_crtcs = crtcs;

	ret = drm_encoder_init(drm, &priv->encoder, &ad9889b_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		goto err_encoder;

	ret = drm_bridge_attach(&priv->encoder, &priv->bridge, NULL);
	if (ret)
		goto err_bridge;

	return 0;

err_bridge:
	drm_encoder_cleanup(&priv->encoder);
err_encoder:
	return ret;
}

static int ad9889b_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;

	return ad9889b_encoder_init(dev, drm);
}

static void ad9889b_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct ad9889b_priv *priv = dev_get_drvdata(dev);

	drm_encoder_cleanup(&priv->encoder);
}

static const struct component_ops ad9889b_ops = {
	.bind = ad9889b_bind,
	.unbind = ad9889b_unbind,
};

static int ad9889b_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "adapter does not support I2C\n");
		return -EIO;
	}

	ret = ad9889b_create(&client->dev);
	if (ret)
		return ret;

	ret = component_add(&client->dev, &ad9889b_ops);
	if (ret)
		ad9889b_destroy(&client->dev);
	return ret;
}

static int ad9889b_remove(struct i2c_client *client)
{
	component_del(&client->dev, &ad9889b_ops);
	ad9889b_destroy(&client->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ad9889b_dt_ids[] = {
	{ .compatible = "ad9889b", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad9889b_dt_ids);
#endif

static const struct i2c_device_id ad9889b_ids[] = {
	{ "ad9889b", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad9889b_ids);

static struct i2c_driver ad9889b_driver = {
	.probe = ad9889b_probe,
	.remove = ad9889b_remove,
	.driver = {
		.name = "ad9889b",
		.of_match_table = of_match_ptr(ad9889b_dt_ids),
	},
	.id_table = ad9889b_ids,
};

module_i2c_driver(ad9889b_driver);

MODULE_AUTHOR("Nathan Ford <nford@westpond.com");
MODULE_DESCRIPTION("Analog Devices AD9889B HDMI Encoder");
MODULE_LICENSE("GPL");
