/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/adau1761.h"
#include "imx-audmux.h"

#define IMX_ADAU1761_DAI_TDM_TX_MASK    (BIT(1)|BIT(0))
#define IMX_ADAU1761_DAI_TDM_RX_MASK    (BIT(1)|BIT(0))
#define IMX_ADAU1761_DAI_TDM_SLOTS      2
#define IMX_ADAU1761_DAI_TDM_SLOT_WIDTH 32

struct imx_adau1761_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	struct platform_device *pdev;
	struct clk *codec_clk;
	struct regmap *gpr;
	unsigned int clk_frequency;
};

static int imx_adau1761_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct imx_adau1761_data *data = container_of(rtd->card,
					struct imx_adau1761_data, card);
	struct device *dev = rtd->card->dev;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_rate = 48000 * 1024;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, ADAU17X1_PLL,
			ADAU17X1_PLL_SRC_MCLK, data->clk_frequency, pll_rate);
	if (ret) {
		dev_err(dev, "could not set pll\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAU17X1_CLK_SRC_PLL, pll_rate,
			SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(dev, "could not set system clock\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_dapm_widget imx_adau1761_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("In 1", NULL),
	SND_SOC_DAPM_LINE("In 2", NULL),
	SND_SOC_DAPM_LINE("In 3-4", NULL),

	SND_SOC_DAPM_LINE("Diff Out L", NULL),
	SND_SOC_DAPM_LINE("Diff Out R", NULL),
	SND_SOC_DAPM_LINE("Stereo Out", NULL),
	SND_SOC_DAPM_HP("Capless HP Out", NULL),
};

static int imx_adau1761_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int dai_fmt = rtd->dai_link->dai_fmt;
	struct snd_soc_card *card = rtd->card;
	struct imx_adau1761_data *data = snd_soc_card_get_drvdata(card);
	struct platform_device *pdev = data->pdev;
	unsigned int sample_rate = params_rate(params);
	int sample_format_width = params_width(params);
	unsigned int pll_rate;
	int ret;

	/* set the codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret) {
		dev_err(&pdev->dev, "failed to set the format for codec side\n");
		return ret;
	}

	/* set i.MX active slot mask */
	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
			IMX_ADAU1761_DAI_TDM_TX_MASK, IMX_ADAU1761_DAI_TDM_RX_MASK,
			IMX_ADAU1761_DAI_TDM_SLOTS, IMX_ADAU1761_DAI_TDM_SLOT_WIDTH);
	if (ret) {
		dev_err(&pdev->dev, "failed to set the TDM slot for cpu side\n");
		return ret;
	}

	/* set the AP DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret) {
		dev_err(&pdev->dev, "failed to set the format for cpu side\n");
		return ret;
	}

#ifdef ADAU1761_STATIC_DSP_ENABLE
	switch (sample_rate) {
	case 48000:
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 96000:
		pll_rate = 48000 * 1024;
		break;
	case 44100:
	case 7350:
	case 11025:
	case 14700:
	case 22050:
	case 29400:
	case 88200:
		pll_rate = 44100 * 1024;
		break;
	default:
		return -EINVAL;
	}
#else
	if (sample_format_width == 24)
		pll_rate = sample_rate * 300;
	else
		pll_rate = sample_rate * 1024;
#endif

	ret = snd_soc_dai_set_pll(codec_dai, ADAU17X1_PLL,
			ADAU17X1_PLL_SRC_MCLK, data->clk_frequency, pll_rate);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAU17X1_CLK_SRC_PLL, pll_rate,
			SND_SOC_CLOCK_IN);

	return ret;
}

static struct snd_soc_ops imx_adau1761_ops = {
	.hw_params = imx_adau1761_hw_params,
};

static int imx_adau1761_audmux_config(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	unsigned int int_port, ext_port;
	int ret;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) ,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		return ret;
	}
	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		return ret;
	}

	return 0;
}

static int imx_adau1761_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np;
	struct platform_device *cpu_pdev;
	struct i2c_client *codec_dev;
	struct device_node *gpr_np;
	struct imx_adau1761_data *data;
	int ret;

	cpu_np = of_parse_phandle(np, "cpu-dai", 0);
	if (!cpu_np) {
		ret = imx_adau1761_audmux_config(pdev);
		if (ret) {
			dev_err(&pdev->dev, "audmux missing or invalid\n");
			return ret;
		}

		cpu_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
		if (!cpu_np) {
			dev_err(&pdev->dev, "error config cpu dai or ssi-controller\n");
			goto fail;
		}
	}

	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "audio codec missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev) {
		dev_err(&pdev->dev, "failed to find codec platform device\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->codec_clk = devm_clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(data->codec_clk)) {
		/* assuming clock enabled by default */
		data->codec_clk = NULL;
		ret = of_property_read_u32(codec_np, "clock-frequency",
					&data->clk_frequency);
		if (ret) {
			dev_err(&codec_dev->dev,
				"clock-frequency missing or invalid\n");
			goto fail;
		}
	} else {
		gpr_np = of_parse_phandle(pdev->dev.of_node, "gpr", 0);
		if (gpr_np) {
			data->gpr = syscon_node_to_regmap(gpr_np);
			if (IS_ERR(data->gpr)) {
				ret = PTR_ERR(data->gpr);
				dev_err(&pdev->dev, "failed to get gpr regmap\n");
				goto fail;
			}

			/* set SAI2_MCLK_DIR to enable codec MCLK for imx6ul */
			regmap_update_bits(data->gpr, 4, 1 << 20, 1 << 20);
		}

		data->clk_frequency = clk_get_rate(data->codec_clk);
		ret = clk_prepare_enable(data->codec_clk);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable master-clock: %d\n", ret);
			return ret;
		}
	}
	dev_info(&pdev->dev, "clock-frequency = %u Hz\n", data->clk_frequency);

	data->pdev = pdev;

	data->dai.name = "ADAU1761";
	data->dai.stream_name = "ADAU1761";
	data->dai.codec_dai_name = "adau-hifi";
	data->dai.codec_of_node = codec_np;
	data->dai.cpu_dai_name = dev_name(&cpu_pdev->dev);
	data->dai.cpu_of_node = cpu_np;
	data->dai.platform_of_node = cpu_np;
	data->dai.ops = &imx_adau1761_ops;
	data->dai.init = &imx_adau1761_dai_init;
	data->dai.dai_fmt = SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS;

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto clk_fail;
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret)
		goto clk_fail;
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
	data->card.dapm_widgets = imx_adau1761_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_adau1761_dapm_widgets);

	ret = snd_soc_register_card(&data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto clk_fail;
	}

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

clk_fail:
	clk_put(data->codec_clk);
fail:
	if (cpu_np)
		of_node_put(cpu_np);
	if (codec_np)
		of_node_put(codec_np);

	return ret;
}

static int imx_adau1761_remove(struct platform_device *pdev)
{
	struct imx_adau1761_data *data = platform_get_drvdata(pdev);

	if (data->codec_clk) {
		clk_disable_unprepare(data->codec_clk);
		clk_put(data->codec_clk);
	}
	snd_soc_unregister_card(&data->card);

	return 0;
}

static const struct of_device_id imx_adau1761_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-adau1761", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_adau1761_dt_ids);

static struct platform_driver imx_adau1761_driver = {
	.driver = {
		.name = "imx-adau1761",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_adau1761_dt_ids,
	},
	.probe = imx_adau1761_probe,
	.remove = imx_adau1761_remove,
};
module_platform_driver(imx_adau1761_driver);

MODULE_AUTHOR("Maxim Paymushkin <maxim.paymushkin@gmail.com>");
MODULE_DESCRIPTION("Freescale i.MX ADAU1761 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-adau1761");
