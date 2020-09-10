#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <linux/component.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#define DCON_CTL0				0x0000
#define DCON_IRQ_CTL			0x0008
#define DCON_VGA_GLOBAL			0x0080
#define DCON_VGA_CHA			0x0084
#define DCON_VGA_CHB			0x0088
#define DCON_VGA_CHC			0x008C
#define DCON_VGA_CHA_STA		0x0090
#define DCON_VGA_CHB_STA		0x0094
#define DCON_VGA_CHC_STA		0x0098

#define VGA_CHANNEL_DEFAULT		0x90c78

/* The DCON also controls enable / disable of the parallel interface,
 * but ae leaving that on at all times
 */

struct dove_vga_priv {
	void __iomem		*base;

	struct drm_encoder encoder;
	struct drm_bridge	bridge;
	struct drm_connector	connector;
};

#define conn_to_dove_vga_priv(x) container_of(x, struct dove_vga_priv, connector)
#define enc_to_dove_vga_priv(x)  container_of(x, struct dove_vga_priv, encoder)
#define bridge_to_dove_vga_priv(x) container_of(x, struct dove_vga_priv, bridge)

static void dove_vga_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs dove_vga_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dove_vga_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dove_vga_connector_get_modes(struct drm_connector *connector)
{
	int count = drm_add_modes_noedid(connector, 1920, 1080);
	drm_set_preferred_mode(connector, 640, 480);
	return count;
}

static struct drm_encoder *
dove_vga_connector_best_encoder(struct drm_connector *connector)
{
	struct dove_vga_priv *priv = conn_to_dove_vga_priv(connector);

	return priv->bridge.encoder;
}

static int dove_vga_connector_detect(struct drm_connector *connector,
				struct drm_modeset_acquire_ctx *ctx,
				bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_helper_funcs dove_vga_connector_helper_funcs = {
	.get_modes = dove_vga_connector_get_modes,
	.detect_ctx = dove_vga_connector_detect,
	.best_encoder = dove_vga_connector_best_encoder,
};

static int dove_vga_bridge_attach(struct drm_bridge *bridge)
{
	struct dove_vga_priv *priv = bridge_to_dove_vga_priv(bridge);
	struct drm_connector *connector = &priv->connector;
	int ret;

	connector->interlace_allowed = 0;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector, &dove_vga_connector_helper_funcs);
	ret = drm_connector_init(bridge->dev, connector,
				&dove_vga_connector_funcs, DRM_MODE_CONNECTOR_VGA);
	if (ret)
		return ret;

	drm_connector_attach_encoder(&priv->connector, priv->bridge.encoder);

	return 0;
}

static void dove_vga_bridge_detach(struct drm_bridge *bridge)
{
	struct dove_vga_priv *priv = bridge_to_dove_vga_priv(bridge);

	drm_connector_cleanup(&priv->connector);
}

static enum drm_mode_status dove_vga_bridge_mode_valid(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode)
{
	if (mode->hdisplay > 1920)
		return MODE_H_ILLEGAL;
	if (mode->vdisplay > 1080)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

static void dove_vga_bridge_enable(struct drm_bridge *bridge)
{
	struct dove_vga_priv *priv = bridge_to_dove_vga_priv(bridge);
	uint32_t ctl0 = readl(priv->base+DCON_CTL0);

	/* Enable VGA clock */
	ctl0 &= ~(0x1 << 25);
	writel(ctl0, priv->base+DCON_CTL0);

	/* Enable DAC channels */
	writel(VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHA);
	writel(VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHB);
	writel(VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHC);
}

static void dove_vga_bridge_disable(struct drm_bridge *bridge)
{
	struct dove_vga_priv *priv = bridge_to_dove_vga_priv(bridge);
	uint32_t ctl0 = readl(priv->base+DCON_CTL0);

	/* Disable DAC channels */
	writel((0x1 << 25), priv->base+DCON_VGA_CHA);
	writel((0x1 << 25), priv->base+DCON_VGA_CHB);
	writel((0x1 << 25), priv->base+DCON_VGA_CHC);

	/* Disable VGA clock */
	ctl0 |= (0x1 << 25);
	writel(ctl0, priv->base+DCON_CTL0);
}

static void dove_vga_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted_mode)
{ }

static const struct drm_bridge_funcs dove_vga_bridge_funcs = {
	.attach = dove_vga_bridge_attach,
	.detach = dove_vga_bridge_detach,
	.mode_valid = dove_vga_bridge_mode_valid,
	.disable = dove_vga_bridge_disable,
	.mode_set = dove_vga_bridge_mode_set,
	.enable = dove_vga_bridge_enable,
};

static void dove_vga_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs dove_vga_encoder_funcs = {
	.destroy = dove_vga_encoder_destroy,
};

static int dove_vga_bind(struct device *dev, struct device *master, void *data)
{
	struct dove_vga_priv *priv = dev_get_drvdata(dev);
	struct drm_device *drm = data;
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

	ret = drm_encoder_init(drm, &priv->encoder, &dove_vga_encoder_funcs,
			       DRM_MODE_ENCODER_DAC, NULL);
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

static void dove_vga_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct dove_vga_priv *priv = dev_get_drvdata(dev);

	drm_encoder_cleanup(&priv->encoder);
}

static const struct component_ops dove_vga_ops = {
	.bind = dove_vga_bind,
	.unbind = dove_vga_unbind,
};

static int dove_vga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct dove_vga_priv *priv;
	void __iomem *base;
	uint32_t ctl0;
	int ret;

	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(dev, priv);

	INIT_LIST_HEAD(&priv->bridge.list);

	priv->bridge.funcs = &dove_vga_bridge_funcs;
#ifdef CONFIG_OF
	priv->bridge.of_node = dev->of_node;
#endif

	drm_bridge_add(&priv->bridge);

	ret = component_add(dev, &dove_vga_ops);
	if (ret)
		drm_bridge_remove(&priv->bridge);

	priv->base = base;

	writel((0x1 << 25) | VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHA);
	writel((0x1 << 25) | VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHB);
	writel((0x1 << 25) | VGA_CHANNEL_DEFAULT, priv->base+DCON_VGA_CHC);

	ctl0 = readl(priv->base+DCON_CTL0);
	ctl0 &= ( 0xf << 6 );		/* Make sure 1:1 mapping of port -> lcd */
	ctl0 |= (0x1 << 25);		/* Power down VGA */
	writel(ctl0, priv->base+DCON_CTL0);

	return ret;
}

static int dove_vga_remove(struct platform_device *pdev)
{
	struct dove_vga_priv *priv = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &dove_vga_ops);
	drm_bridge_remove(&priv->bridge);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dove_vga_dt_ids[] = {
	{ .compatible = "marvell,dove-dcon", },
	{ }
};
MODULE_DEVICE_TABLE(of, dove_vga_dt_ids);
#endif

static struct platform_driver dove_vga_driver = {
	.probe	= dove_vga_probe,
	.remove	= dove_vga_remove,
	.driver		= {
		.name		= "dove-dcon",
		.of_match_table	= of_match_ptr(dove_vga_dt_ids),
	},
};
module_platform_driver(dove_vga_driver);

MODULE_AUTHOR("Nathan Ford <nford@westpond.com");
MODULE_DESCRIPTION("DRM component for Dove DCON VGA output");
MODULE_LICENSE("GPL");
