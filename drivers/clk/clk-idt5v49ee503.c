#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include "clk-idt5v49ee503.h"

/* The idt5vee503 has 5 outputs. Each output has a fixed divider. Each output can source from
 * a selection of inputs, some of which are the other outputs. We support src selection of the pll's.
 * No attempt is made to prevent two outputs from sharing a pll and overstepping each other
 * out0 -> mclk, out1, out3
 * out1 -> mclk, pll0, pll1, pll2, pll3, out2
 * out2 -> mclk, pll0, pll1, pll2, pll3, out1, out3
 * out3 -> mclk, pll0, pll1, pll2, pll3, out1, out6
 * out6 -> mclk, pll1
 */

#define IDT5V_CLKSRC_PLL0		0
#define IDT5V_CLKSRC_PLL1		1
#define IDT5V_CLKSRC_PLL2		2
#define IDT5V_CLKSRC_PLL3		3
#define IDT5V_CLKSRC_OUT1		4
#define IDT5V_CLKSRC_OUT2		5
#define IDT5V_CLKSRC_OUT3		6
#define IDT5V_CLKSRC_OUT6		7
#define IDT5V_CLKSRC_REF		8

#define IDT5V_CLKSRC_PLL0_MASK	(1 << IDT5V_CLKSRC_PLL0)
#define IDT5V_CLKSRC_PLL1_MASK	(1 << IDT5V_CLKSRC_PLL1)
#define IDT5V_CLKSRC_PLL2_MASK	(1 << IDT5V_CLKSRC_PLL2)
#define IDT5V_CLKSRC_PLL3_MASK	(1 << IDT5V_CLKSRC_PLL3)
#define IDT5V_CLKSRC_OUT1_MASK	(1 << IDT5V_CLKSRC_OUT1)
#define IDT5V_CLKSRC_OUT2_MASK	(1 << IDT5V_CLKSRC_OUT2)
#define IDT5V_CLKSRC_OUT3_MASK	(1 << IDT5V_CLKSRC_OUT3)
#define IDT5V_CLKSRC_OUT6_MASK	(1 << IDT5V_CLKSRC_OUT6)
#define IDT5V_CLKSRC_REF_MASK	(1 << IDT5V_CLKSRC_REF)

struct idt5v_driver_data;

struct idt5v_clk_port {
	struct clk_hw hw;
	struct clk_init_data init;

	u8 clk_id;
	u32 clk_src;

	u8 enabled;
	struct idt_freq_params params;

	struct idt5v_driver_data *drvdata;
};

struct idt5v_driver_data {
	struct i2c_client	*client;

	struct clk			*refclk;
	const char 			*refclk_name;

	struct idt5v_clk_port hw[ 5 ];
};

/* Clock registers for variour PLL parameters */
struct idt_pll_reg_set
{
	u8 N_lreg;		/* feedback divider LSB N[7:0]  */
	u8 N_mreg;		/* feedback divider MSB N[11:8] */
	u8 N_mlshft;	/* N[11:8] nibble left-shift value */
	u8 D_reg;		/* reference divider D[6:0]  */
	u8 IP_reg;		/* Loop Filter  */
};

static char *clk_names[ 5 ] = {
	"clkout0",
	"clkout1",
	"clkout2",
	"clkout3",
	"clkout6" };

static u32 clk_src_mask[ 5 ] = {
	IDT5V_CLKSRC_REF_MASK | IDT5V_CLKSRC_OUT1_MASK | IDT5V_CLKSRC_OUT3_MASK,
	IDT5V_CLKSRC_REF_MASK | IDT5V_CLKSRC_PLL0_MASK | IDT5V_CLKSRC_PLL1_MASK | IDT5V_CLKSRC_PLL2_MASK | IDT5V_CLKSRC_PLL3_MASK | IDT5V_CLKSRC_OUT2_MASK,
	IDT5V_CLKSRC_REF_MASK | IDT5V_CLKSRC_PLL0_MASK | IDT5V_CLKSRC_PLL1_MASK | IDT5V_CLKSRC_PLL2_MASK | IDT5V_CLKSRC_PLL3_MASK | IDT5V_CLKSRC_OUT1_MASK | IDT5V_CLKSRC_OUT3_MASK,
	IDT5V_CLKSRC_REF_MASK | IDT5V_CLKSRC_PLL0_MASK | IDT5V_CLKSRC_PLL1_MASK | IDT5V_CLKSRC_PLL2_MASK | IDT5V_CLKSRC_PLL3_MASK | IDT5V_CLKSRC_OUT1_MASK | IDT5V_CLKSRC_OUT6_MASK,
	IDT5V_CLKSRC_REF_MASK | IDT5V_CLKSRC_PLL1_MASK,
};

static const u8 clk_src_sel[9] = {
	0x04,	/* 100 PLL0 */
	0x05,	/* 101 PLL1 */
	0x06,	/* 110 PLL2 */
	0x07,	/* 111 PLL3 */
	0x00,	/* 000 OUT1 */
	0x10,	/* ( OUT2 is a switch, indicate with high nibble ) */
	0x01,	/* 001 OUT3 */
	0x10,	/* ( OUT6 is a switch, indicate with high nibble ) */
	0x02,	/* REF clk input */
};

static const struct idt_pll_reg_set idt5v_pll_regs[4] = {
	/*N[7:0] N[11:8] Nsh D[6:0] IP */
	{ 0x18,  0x1c,   0,  0x10,  0x0c }, /* PLL0 */
	{ 0x30,  0x34,   0,  0x28,  0x24 }, /* PLL1 */
	{ 0x48,  0x4c,   0,  0x40,  0x3c }, /* PLL2 */
	{ 0x64,  0x34,   4,  0x5c,  0x58 }, /* PLL3 */
};

static const u8 idt5v_div_regs[4] = {  0x88, 0x90, 0x94, 0xA8 };

static inline struct idt5v_clk_port *to_idt5v_clk(struct clk_hw *hw)
{
	return container_of(hw, struct idt5v_clk_port, hw);
}

/* The IDT5V I2C protocol is two bytes, a command byte followied by the
 * address of interest
 */
static int idt5v_reg_write(struct i2c_client *client, u8 reg, u8 mask, u8 val)
{
	u8 buf[3];
	u8 v;
	int ret;

	buf[0] = 0;		/* Command PROGREAD/PROGWRITE */
	buf[1] = reg;
	ret = i2c_master_send(client, buf, 2);
	if (ret != 2)
		return ret;

	/* Read returns ID byte? then reg data */
	ret = i2c_master_recv(client, buf, 2);
	if (ret != 2)
		return ret;
	v = buf[1];

	buf[0] = 0;		/* Command PROGREAD/PROGWRITE */
	buf[1] = reg;
	buf[2] = (v & ~mask) | (val & mask);
	ret = i2c_master_send(client, buf, 3);
	if (ret != 3)
		return ret;

	return 0;
}

/* Lookup closest requested frequency in pre-defined table. Values are
 * chosen if they are within 0.5% of desired.
 */
static int idt5v_lookup_freq(u64 freq, struct idt_freq_params *entry)
{
	int	 i;
	u64 max_diff = freq, diff_before, diff_after;

	/* max_diff is 0.5% of dersired clock */
	do_div(max_diff, 200);

	for (i = 0; idt5v49ee503_freq_tbl[i].f_out != ULONG_MAX; i++)
		if (idt5v49ee503_freq_tbl[i].f_out >= freq)
			break;

	diff_before = freq - idt5v49ee503_freq_tbl[i-1].f_out;
	diff_after = idt5v49ee503_freq_tbl[i].f_out - freq;

	if ((diff_before < diff_after) && (diff_before < max_diff)) {
		memcpy(entry, &idt5v49ee503_freq_tbl[i-1], sizeof(struct idt_freq_params));
		return 0;
	}

	if ((diff_after < diff_before) && (diff_after < max_diff)) {
		memcpy(entry, &idt5v49ee503_freq_tbl[i], sizeof(struct idt_freq_params));
		return 0;
	}

	return -EINVAL;
}

/* Calculate dividers */
static int idt5v_calc_freq(u64 freq, u64 ref, struct idt_freq_params *entry)
{
	u32 n, d, odiv;
	u64 max_diff, diff;
	u64 tmp;
	u32 factor;

	memset(entry, 0, sizeof(*entry));

	max_diff = freq;
	do_div(max_diff, 10000);

	for (odiv = 4; odiv < 256; odiv+=2) {
		for (d = odiv/*1*/; d < 127; d++) {
			tmp = freq * odiv * d;
			do_div(tmp, ref);
			if (tmp < 1 || tmp > 4095)
				continue;
			n = tmp;

			/* check if this n gives accurate Fout */
			tmp = ref * n;
			factor = d * odiv;
			do_div(tmp, factor);
			if (tmp > freq)
				diff = tmp - freq;
			else
				diff = freq - tmp;
			
			if (diff < max_diff) {
				printk(KERN_DEBUG "dividers found for Fout = %lld. n=%d d=%d odiv %d\n",
				       freq, n, d, odiv);
				printk(KERN_DEBUG "diff = %lld. max diff %lld freq %lld tmp %lld\n",
				       diff, max_diff, freq, tmp);

				entry->f_out = tmp;
				entry->odiv = odiv;
				entry->d = d;
				entry->n = n;
				return 0;
			}
		}
	}

	return -EINVAL;
}

static int idt5v_pll_ctrl(struct idt5v_clk_port *clk_port, u8 enabled)
{
	u8 mask = (1 << clk_port->clk_src) & 0xf;

	if (mask)
		return idt5v_reg_write(clk_port->drvdata->client,
			0x04, mask, enabled ? mask : 0x0 );
	else
		return 0;
}

static int idt5v_pll_cfg(struct idt5v_clk_port *clk_port)
{
	const struct idt_pll_reg_set *pll_regs = &idt5v_pll_regs[clk_port->clk_src];
	u8 n_msb, n_lsb;
	u8 div_val;
	u8 loop_filter;
	int ret;

	/* Set PLL denominator */
	ret = idt5v_reg_write(clk_port->drvdata->client,
		pll_regs->D_reg, 0xff, clk_port->params.d );
	if (ret)
		return ret;

	/* PLL0 has lower precision */
	if (clk_port->clk_src == IDT5V_CLKSRC_PLL0) {
		n_lsb = (clk_port->params.n >> 1) & 0xff;
		n_msb = (clk_port->params.n >> 9) & 0x0f;
	} else {
		n_lsb = clk_port->params.n & 0xff;
		n_msb = (clk_port->params.n >> 8) & 0x0f;
	}

	/* Set PLL numerator */
	ret = idt5v_reg_write(clk_port->drvdata->client,
		pll_regs->N_lreg, 0xff, n_lsb );
	if (ret)
		return ret;
	ret = idt5v_reg_write(clk_port->drvdata->client,
		pll_regs->N_mreg, 0xf << pll_regs->N_mlshft, n_msb << pll_regs->N_mlshft );
	if (ret)
		return ret;

	/* Configure output divider */
	if (clk_port->params.odiv == 1)
		div_val = 0xff;
	else if (clk_port->params.odiv == 2)
		div_val = 0;
	else /* ODIV = (Q[6:0] + 2) * 2 */
		div_val = ((clk_port->params.odiv >> 1) - 2) | 0x80; 
	ret = idt5v_reg_write(clk_port->drvdata->client,
		idt5v_div_regs[ clk_port->clk_src], 0xff, div_val );
	if (ret)
		return ret;

	/* if loop filter params are not provided (no match in lookup table),
		use hw default value (0x10) */
	if ((clk_port->params.Ip == 0) && (clk_port->params.Rz == 0) && (clk_port->params.Cz == 0))
		loop_filter = 0x10;
	else {
		loop_filter = (clk_port->params.Rz / 4) & 0xf;
		loop_filter |= ((clk_port->params.Ip / 6) & 0x7 ) << 4;
		loop_filter |= (((clk_port->params.Cz - 196) / 217) & 0x1) << 7;
	}
	ret = idt5v_reg_write(clk_port->drvdata->client,
		pll_regs->IP_reg, 0xff, loop_filter);
	if (ret)
		return ret;

	return 0;
}

static int idt5v_out_ctrl(struct idt5v_clk_port *clk_port, u8 enabled)
{
	u8 mask;

	switch (clk_port->clk_id) {
	case 0:
		mask = 0x01;
		break;
	case 1:
		mask = 0x02;
		break;
	case 2:
		mask = 0x04;
		break;
	case 3:
		mask = 0x08;
		break;
	case 4:
		mask = 0x40;
		break;
	default:
		return 0;
	}

	return idt5v_reg_write(clk_port->drvdata->client,
		0x03, mask, enabled ? mask : 0x0 );
}

static int idt5v_prepare(struct clk_hw *hw)
{
	struct idt5v_clk_port *clk_port = to_idt5v_clk(hw);
	int ret;

	clk_port->enabled = 1;

	ret = idt5v_pll_ctrl(clk_port, 1);
	if (ret)
		return ret;
	ret = idt5v_out_ctrl(clk_port, 1);
	if (ret)
		return ret;

	return 0;
}

static void idt5v_unprepare(struct clk_hw *hw)
{
	struct idt5v_clk_port *clk_port = to_idt5v_clk(hw);

	clk_port->enabled = 0;

	/* Suspend PLL and output */
	idt5v_pll_ctrl(clk_port, 0);
	idt5v_out_ctrl(clk_port, 0);
}

static int idt5v_is_prepared(struct clk_hw *hw)
{
	struct idt5v_clk_port *clk_port = to_idt5v_clk(hw);

	return clk_port->enabled;
}

static unsigned long idt5v_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct idt5v_clk_port *clk_port = to_idt5v_clk(hw);

	return clk_port->params.f_out;
}

static long idt5v_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct idt_freq_params params;
	int ret;

	/* With a 25Mhz ref clk we have a pre-calculated table we can look through */
	if (*parent_rate == 25000000 ) {
		ret = idt5v_lookup_freq(rate, &params);
		if (!ret)
			return params.f_out;
	}

	/* Otherwise calcualate */
	ret = idt5v_calc_freq(rate, *parent_rate, &params);
	if (!ret)
		return params.f_out;

	return -1;
}

static int idt5v_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct idt5v_clk_port *clk_port = to_idt5v_clk(hw);
	u8 src_sel = clk_src_sel[clk_port->clk_src];
	int ret;

	if (parent_rate != 25000000 || idt5v_lookup_freq(rate, &clk_port->params)) {
		if (idt5v_calc_freq(rate, parent_rate, &clk_port->params))
			return -1;
	}

	/* Stop pll/out */
	if (clk_port->enabled) {
		ret = idt5v_pll_ctrl(clk_port, 0);
		if (ret)
			return ret;
		ret = idt5v_out_ctrl(clk_port, 0);
		if (ret)
			return ret;
	}

	/* is clk ( out1 / out3 ) duplicated from a neighbor ( out2 / out6 ) */
	if (src_sel == 0x10) {
		if (clk_port->clk_id == 1) {
			ret = idt5v_reg_write(clk_port->drvdata->client,
				0x75, 0x02, 0 );
		} else if(clk_port->clk_id == 3) {
			ret = idt5v_reg_write(clk_port->drvdata->client,
				0x75, 0x01, 0 );
		} else {
			ret =  -EINVAL;
		}
		return ret;
	}

	/* If this output is sourced from a pll then setup the pll */
	if (clk_port->clk_src < 4) {
		ret = idt5v_pll_cfg(clk_port);
		if (ret)
			return ret;
	}

	/* Set the src mux selection */
	switch (clk_port->clk_id) {
	case 0:
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xc0,
			0x30, src_sel << 4 );
		break;
	case 1:
		/* Clear output switch */
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0x75, 0x02, 1 );
		if (ret)
			return ret;

		/* LSB in 0xc0 */
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xc0, 0xc0, src_sel << 4 );
		if (ret)
			return ret;

		/* MSB in 0xc4 */
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xc4, 0x01, src_sel >> 2 );
		if (ret)
			return ret;

		break;
	case 2:
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xc4, 0x0e, src_sel << 1 );
		break;
	case 3:
		/* Clear output switch */
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0x75, 0x01, 1 );
		if (ret)
			return ret;

		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xc4, 0x70, src_sel << 4 );
		break;
	case 4:
		ret = idt5v_reg_write(clk_port->drvdata->client,
			0xcc, 0xe0, src_sel << 5 );
		break;
	}

	/* Start pll/out */
	if (clk_port->enabled) {
		ret = idt5v_pll_ctrl(clk_port, 1);
		if (ret)
			return ret;
		ret = idt5v_out_ctrl(clk_port, 1);
		if (ret)
			return ret;
	}

	return ret;
}

static const struct clk_ops idt5v_clk_ops = {
	.prepare = idt5v_prepare,
	.unprepare = idt5v_unprepare,
	.is_prepared = idt5v_is_prepared,
	.recalc_rate = idt5v_recalc_rate,
	.round_rate = idt5v_round_rate,
	.set_rate = idt5v_set_rate,
};

static struct clk_hw *
idt5v_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct idt5v_driver_data *drvdata = data;
	unsigned int idx = clkspec->args[0];

	return &drvdata->hw[idx].hw;
}

static int idt5v_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct idt5v_driver_data *drvdata;
	struct device *dev = &client->dev;
	struct device_node *child, *np = dev->of_node;
	u32 num, src;
	const char *val;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	i2c_set_clientdata(client, drvdata);
	drvdata->client = client;

	drvdata->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(drvdata->refclk)) {
		dev_err(&client->dev, "Could not get refclk\n");
		return PTR_ERR(drvdata->refclk);
	}

	drvdata->refclk_name = __clk_get_name(drvdata->refclk);

	/* Set SW control of chip */
	ret = idt5v_reg_write(client, 0x0, 0x01, 1 );
	if (ret)
		return ret;

	/* Sel;ect CFG 0 */
	ret = idt5v_reg_write(client, 0x1, 0x07, 0 );
	if (ret)
		return ret;

	/* Enable all outputs regardless of OE pin */
	ret = idt5v_reg_write(client, 0x2, 0x4f, 0 );
	if (ret)
		return ret;

	/* Put all outputs in suspend */
	ret = idt5v_reg_write(client, 0x3, 0x4f, 0 );
	if (ret)
		return ret;

	/* Put all PLLs in suspend */
	ret = idt5v_reg_write(client, 0x4, 0x0f, 0 );
	if (ret)
		return ret;

	/* Setup clock outputs */
	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, "reg", &num)) {
			dev_err(&client->dev, "missing reg property of %pOFn\n",
				child);
			of_node_put(child);
			return -EINVAL;
		}

		if (num > 3 && num != 6) {
			dev_err(&client->dev, "invalid clkout %d\n", num);
			of_node_put(child);
			return -EINVAL;
		}
		if (num == 6)
			num = 4;

		if (of_property_read_string(child, "idt,src", &val)) {
			dev_err(&client->dev, "missing src property of %pOFn\n",
				child);
			of_node_put(child);
			return -EINVAL;
		}

		src = 0;
		if (!strcmp(val, "xtal"))
			src = IDT5V_CLKSRC_REF;
		else if (!strcmp(val, "pll0"))
			src = IDT5V_CLKSRC_PLL0;
		else if (!strcmp(val, "pll1"))
			src = IDT5V_CLKSRC_PLL1;
		else if (!strcmp(val, "pll2"))
			src = IDT5V_CLKSRC_PLL2;
		else if (!strcmp(val, "pll3"))
			src = IDT5V_CLKSRC_PLL3;
		else if (!strcmp(val, "out1"))
			src = IDT5V_CLKSRC_OUT1;
		else if (!strcmp(val, "out2"))
			src = IDT5V_CLKSRC_OUT2;
		else if (!strcmp(val, "out3"))
			src = IDT5V_CLKSRC_OUT3;
		else if (!strcmp(val, "out6"))
			src = IDT5V_CLKSRC_OUT6;

		if (!(clk_src_mask[num] & src)) {
			dev_err(&client->dev, "invalid src for output %pOFn\n",
				child);
			of_node_put(child);
			return -EINVAL;
		}

		drvdata->hw[num].drvdata = drvdata;
		drvdata->hw[num].clk_id = num;
		drvdata->hw[num].clk_src = src;
		drvdata->hw[num].init.flags = 0;
		drvdata->hw[num].init.name = clk_names[ num ];
		drvdata->hw[num].init.ops = &idt5v_clk_ops;
		drvdata->hw[num].init.num_parents = 1;
		drvdata->hw[num].init.parent_names = &drvdata->refclk_name;
		drvdata->hw[num].hw.init = &drvdata->hw[num].init;

		ret = devm_clk_hw_register(dev, &drvdata->hw[num].hw);
		if (ret < 0) {
			dev_err(&client->dev, "Could not register clock port\n");
			return ret;
		}
	}

	ret = devm_of_clk_add_hw_provider(dev, idt5v_of_clk_get, drvdata);
	if (ret)
		dev_err(&client->dev, "Could not add clk provider\n");

	return ret;
}

static int idt5v_i2c_remove(struct i2c_client *client)
{
	/* TODO: free drv data?? */
	of_clk_del_provider(client->dev.of_node);

	return 0;
}

static const struct of_device_id clk_idt5v_of_match[] = {
	{ .compatible = "idt,idt5v49ee503" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_idt5v_of_match);

static const struct i2c_device_id idt5v_i2c_ids[] = {
	{ "idt5v49ee503", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, idt5v_i2c_ids);

static struct i2c_driver idt5v_driver = {
	.driver = {
		.name = "idt5v49ee503",
		.of_match_table = of_match_ptr(clk_idt5v_of_match),
	},
	.probe		= idt5v_i2c_probe,
	.remove		= idt5v_i2c_remove,
	.id_table	= idt5v_i2c_ids,
};
module_i2c_driver(idt5v_driver);

MODULE_AUTHOR("kostap <kostap@gandalf602.il.marvell.com>");
MODULE_AUTHOR("Nathan Ford <nford@westpond.com>");
MODULE_DESCRIPTION("IDT5V49EE503 Clock driver");
MODULE_LICENSE("GPL");
