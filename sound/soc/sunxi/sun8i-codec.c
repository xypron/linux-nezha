// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This driver supports the digital controls for the internal codec
 * found in Allwinner's A33 SoCs.
 *
 * (C) Copyright 2010-2016
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@Reuuimllatech.com>
 * Mylène Josserand <mylene.josserand@free-electrons.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/log2.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#define SUN8I_SYSCLK_CTL				0x00c
#define SUN8I_SYSCLK_CTL_AIF1CLK_ENA			11
#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL		(0x3 << 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_ENA			7
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL		(0x3 << 4)
#define SUN8I_SYSCLK_CTL_SYSCLK_ENA			3
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC			0
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK		(0x0 << 0)
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF2CLK		(0x1 << 0)
#define SUN8I_MOD_CLK_ENA				0x010
#define SUN8I_MOD_CLK_ENA_AIF1				15
#define SUN8I_MOD_CLK_ENA_AIF2				14
#define SUN8I_MOD_CLK_ENA_AIF3				13
#define SUN8I_MOD_CLK_ENA_ADC				3
#define SUN8I_MOD_CLK_ENA_DAC				2
#define SUN8I_MOD_RST_CTL				0x014
#define SUN8I_MOD_RST_CTL_AIF1				15
#define SUN8I_MOD_RST_CTL_AIF2				14
#define SUN8I_MOD_RST_CTL_AIF3				13
#define SUN8I_MOD_RST_CTL_ADC				3
#define SUN8I_MOD_RST_CTL_DAC				2
#define SUN8I_SYS_SR_CTRL				0x018
#define SUN8I_SYS_SR_CTRL_AIF_FS(n)			(16 - 4 * (n))
#define SUN8I_AIF_CLK_CTRL(n)				(0x040 * (n))
#define SUN8I_AIF_CLK_CTRL_MSTR_MOD			15
#define SUN8I_AIF_CLK_CTRL_CLK_INV			13
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV			9
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV			6
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ			4
#define SUN8I_AIF_CLK_CTRL_DATA_FMT			2
#define SUN8I_AIF_CLK_CTRL_MONO_PCM			1
#define SUN8I_AIF1_ADCDAT_CTRL				0x044
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA		15
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA		14
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC		10
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC		8
#define SUN8I_AIF1_DACDAT_CTRL				0x048
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA		15
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA		14
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC		10
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC		8
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_LOOP_ENA		0
#define SUN8I_AIF1_MXR_SRC				0x04c
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL	14
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL		13
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR	12
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR	10
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR		9
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL	8
#define SUN8I_AIF1_VOL_CTRL1				0x050
#define SUN8I_AIF1_VOL_CTRL1_AD0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL1_AD0R_VOL			0
#define SUN8I_AIF1_VOL_CTRL3				0x058
#define SUN8I_AIF1_VOL_CTRL3_DA0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL3_DA0R_VOL			0
#define SUN8I_AIF2_ADCDAT_CTRL				0x084
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA		15
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA		14
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC		10
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC		8
#define SUN8I_AIF2_DACDAT_CTRL				0x088
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA		15
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA		14
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC		10
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC		8
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_LOOP_ENA		0
#define SUN8I_AIF2_MXR_SRC				0x08c
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L	14
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR	13
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL		12
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R	10
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL	9
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR		8
#define SUN8I_AIF2_VOL_CTRL1				0x090
#define SUN8I_AIF2_VOL_CTRL1_ADCL_VOL			8
#define SUN8I_AIF2_VOL_CTRL1_ADCR_VOL			0
#define SUN8I_AIF2_VOL_CTRL2				0x098
#define SUN8I_AIF2_VOL_CTRL2_DACL_VOL			8
#define SUN8I_AIF2_VOL_CTRL2_DACR_VOL			0
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF1		(0x0 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF2		(0x1 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF1CLK	(0x2 << 0)
#define SUN8I_AIF3_DACDAT_CTRL				0x0c8
#define SUN8I_AIF3_DACDAT_CTRL_AIF3_LOOP_ENA		0
#define SUN8I_AIF3_PATH_CTRL				0x0cc
#define SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC		10
#define SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC		8
#define SUN8I_AIF3_PATH_CTRL_AIF3_PINS_TRI		7
#define SUN8I_ADC_DIG_CTRL				0x100
#define SUN8I_ADC_DIG_CTRL_ENAD				15
#define SUN8I_ADC_DIG_CTRL_ADOUT_DTS			2
#define SUN8I_ADC_DIG_CTRL_ADOUT_DLY			1
#define SUN8I_ADC_VOL_CTRL				0x104
#define SUN8I_ADC_VOL_CTRL_ADCL_VOL			8
#define SUN8I_ADC_VOL_CTRL_ADCR_VOL			0
#define SUN8I_DAC_DIG_CTRL				0x120
#define SUN8I_DAC_DIG_CTRL_ENDA				15
#define SUN8I_DAC_VOL_CTRL				0x124
#define SUN8I_DAC_VOL_CTRL_DACL_VOL			8
#define SUN8I_DAC_VOL_CTRL_DACR_VOL			0
#define SUN8I_DAC_MXR_SRC				0x130
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L		15
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L		14
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL		13
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL		12
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R		11
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R		10
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR		9
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR		8

#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK	GENMASK(9, 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK	GENMASK(7, 4)
#define SUN8I_SYS_SR_CTRL_AIF_FS_MASK(n)	(GENMASK(19, 16) >> (4 * (n)))
#define SUN8I_AIF_CLK_CTRL_CLK_INV_MASK		GENMASK(14, 13)
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK	GENMASK(12, 9)
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK	GENMASK(8, 6)
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK	GENMASK(5, 4)
#define SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK	GENMASK(3, 2)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_MASK	GENMASK(1, 0)

#define SUN8I_AIF_PCM_FMTS  (SNDRV_PCM_FMTBIT_S8|\
			     SNDRV_PCM_FMTBIT_S16_LE|\
			     SNDRV_PCM_FMTBIT_S20_LE|\
			     SNDRV_PCM_FMTBIT_S24_LE)
#define SUN8I_AIF_PCM_RATES (SNDRV_PCM_RATE_8000_48000|\
			     SNDRV_PCM_RATE_96000|\
			     SNDRV_PCM_RATE_192000|\
			     SNDRV_PCM_RATE_KNOT)

#define SUN8I_AIF_PCM_RATES (SNDRV_PCM_RATE_8000_48000|\
			     SNDRV_PCM_RATE_96000|\
			     SNDRV_PCM_RATE_192000|\
			     SNDRV_PCM_RATE_KNOT)

struct sun8i_codec {
	struct clk	*clk_module;
	bool		inverted_lrck;
};

static int sun8i_codec_get_hw_rate(struct snd_pcm_hw_params *params)
{
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 8000:
	case 7350:
		return 0x0;
	case 11025:
		return 0x1;
	case 12000:
		return 0x2;
	case 16000:
		return 0x3;
	case 22050:
		return 0x4;
	case 24000:
		return 0x5;
	case 32000:
		return 0x6;
	case 44100:
		return 0x7;
	case 48000:
		return 0x8;
	case 96000:
		return 0x9;
	case 192000:
		return 0xa;
	default:
		return -EINVAL;
	}
}

static int sun8i_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	unsigned int reg = SUN8I_AIF_CLK_CTRL(dai->id);
	u32 value;

	if (dai->id < 3) {
		/* clock masters */
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS: /* Codec slave, DAI master */
			value = 0x1;
			break;
		case SND_SOC_DAIFMT_CBM_CFM: /* Codec Master, DAI slave */
			value = 0x0;
			break;
		default:
			return -EINVAL;
		}
		regmap_update_bits(component->regmap, reg,
				   BIT(SUN8I_AIF_CLK_CTRL_MSTR_MOD),
				   value << SUN8I_AIF_CLK_CTRL_MSTR_MOD);
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* Normal */
		value = 0x0;
		break;
	case SND_SOC_DAIFMT_NB_IF: /* Inverted LRCK */
		value = 0x1;
		break;
	case SND_SOC_DAIFMT_IB_NF: /* Inverted BCLK */
		value = 0x2;
		break;
	case SND_SOC_DAIFMT_IB_IF: /* Both inverted */
		value = 0x3;
		break;
	default:
		return -EINVAL;
	}
	/*
	 * It appears that the DAI and the codec in the A33 SoC don't
	 * share the same polarity for the LRCK signal when they mean
	 * 'normal' and 'inverted' in the datasheet.
	 *
	 * Since the DAI here is our regular i2s driver that have been
	 * tested with way more codecs than just this one, it means
	 * that the codec probably gets it backward, and we have to
	 * invert the value here.
	 */
	value ^= scodec->inverted_lrck;
	regmap_update_bits(component->regmap, reg,
			   SUN8I_AIF_CLK_CTRL_CLK_INV_MASK,
			   value << SUN8I_AIF_CLK_CTRL_CLK_INV);

	if (dai->id < 3) {
		/* DAI format */
		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			value = 0x0;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			value = 0x1;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			value = 0x2;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			value = 0x3;
			break;
		default:
			return -EINVAL;
		}
		regmap_update_bits(component->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK,
				   value << SUN8I_AIF_CLK_CTRL_DATA_FMT);
	}

	return 0;
}

struct sun8i_codec_clk_div {
	u8	div;
	u8	val;
};

static const struct sun8i_codec_clk_div sun8i_codec_bclk_div[] = {
	{ .div = 1,	.val = 0 },
	{ .div = 2,	.val = 1 },
	{ .div = 4,	.val = 2 },
	{ .div = 6,	.val = 3 },
	{ .div = 8,	.val = 4 },
	{ .div = 12,	.val = 5 },
	{ .div = 16,	.val = 6 },
	{ .div = 24,	.val = 7 },
	{ .div = 32,	.val = 8 },
	{ .div = 48,	.val = 9 },
	{ .div = 64,	.val = 10 },
	{ .div = 96,	.val = 11 },
	{ .div = 128,	.val = 12 },
	{ .div = 192,	.val = 13 },
};

static u8 sun8i_codec_get_bclk_div(struct sun8i_codec *scodec,
				   unsigned int rate,
				   unsigned int channels,
				   unsigned int word_size)
{
	unsigned long clk_rate = clk_get_rate(scodec->clk_module);
	unsigned int div = clk_rate / rate / word_size / channels;
	unsigned int best_val = 0, best_diff = ~0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_codec_bclk_div); i++) {
		const struct sun8i_codec_clk_div *bdiv = &sun8i_codec_bclk_div[i];
		unsigned int diff = abs(bdiv->div - div);

		if (diff < best_diff) {
			best_diff = diff;
			best_val = bdiv->val;
		}
	}

	return best_val;
}

static int sun8i_codec_get_lrck_div(unsigned int channels,
				    unsigned int word_size)
{
	unsigned int div = word_size * channels;

	if (div < 16)
		div = 16;
	if (div > 256)
		return -EINVAL;

	return ilog2(div) - 4;
}

static int sun8i_codec_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	unsigned int slot_width = params_physical_width(params);
	unsigned int channels = params_channels(params);
	unsigned int reg = SUN8I_AIF_CLK_CTRL(dai->id);
	int sample_rate, lrck_div;
	u8 bclk_div;
	u32 value;

	/*
	 * There should be at least two slots in each frame, or else the codec
	 * cuts off the first bit of each sample, and often de-syncs.
	 */
	if (channels == 1)
		slot_width *= 2;

	if (dai->id < 3) {
		bclk_div = sun8i_codec_get_bclk_div(scodec, params_rate(params),
						    channels, slot_width);
		regmap_update_bits(component->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK,
				   bclk_div << SUN8I_AIF_CLK_CTRL_BCLK_DIV);

		lrck_div = sun8i_codec_get_lrck_div(channels, slot_width);
		if (lrck_div < 0)
			return lrck_div;

		regmap_update_bits(component->regmap, reg,
				   SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK,
				   lrck_div << SUN8I_AIF_CLK_CTRL_LRCK_DIV);
	} else {
		regmap_update_bits(component->regmap, reg,
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_MASK,
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLOCK_SRC_AIF2);
	}

	switch (params_width(params)) {
	case 8:
		value = 0x0;
		break;
	case 16:
		value = 0x1;
		break;
	case 20:
		value = 0x2;
		break;
	case 24:
		value = 0x3;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(component->regmap, reg,
			   SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK,
			   value << SUN8I_AIF_CLK_CTRL_WORD_SIZ);

	if (dai->id < 3) {
		value = channels == 1;
		regmap_update_bits(component->regmap, reg,
				   BIT(SUN8I_AIF_CLK_CTRL_MONO_PCM),
				   value << SUN8I_AIF_CLK_CTRL_MONO_PCM);

		sample_rate = sun8i_codec_get_hw_rate(params);
		if (sample_rate < 0)
			return sample_rate;

		regmap_update_bits(component->regmap, SUN8I_SYS_SR_CTRL,
				   SUN8I_SYS_SR_CTRL_AIF_FS_MASK(dai->id),
				   sample_rate << SUN8I_SYS_SR_CTRL_AIF_FS(dai->id));
	}

	return 0;
}

static const struct snd_soc_dai_ops sun8i_codec_dai_ops = {
	.hw_params = sun8i_codec_hw_params,
	.set_fmt = sun8i_set_fmt,
};

static struct snd_soc_dai_driver sun8i_codec_dais[] = {
	{
		.name = "sun8i-codec-aif1",
		.id = 1,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "sun8i-codec-aif2",
		.id = 2,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "sun8i-codec-aif3",
		.id = 3,
		/* playback capabilities */
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
		},
		/* capture capabilities */
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SUN8I_AIF_PCM_RATES,
			.formats = SUN8I_AIF_PCM_FMTS,
			.sig_bits = 24,
		},
		/* pcm operations */
		.ops = &sun8i_codec_dai_ops,
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},
};

static const DECLARE_TLV_DB_SCALE(sun8i_codec_vol_scale, -12000, 75, 1);

static const struct snd_kcontrol_new sun8i_codec_controls[] = {
	SOC_DOUBLE_TLV("AIF1 AD0 Capture Volume",
		       SUN8I_AIF1_VOL_CTRL1,
		       SUN8I_AIF1_VOL_CTRL1_AD0L_VOL,
		       SUN8I_AIF1_VOL_CTRL1_AD0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF1 DA0 Playback Volume",
		       SUN8I_AIF1_VOL_CTRL3,
		       SUN8I_AIF1_VOL_CTRL3_DA0L_VOL,
		       SUN8I_AIF1_VOL_CTRL3_DA0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 ADC Capture Volume",
		       SUN8I_AIF2_VOL_CTRL1,
		       SUN8I_AIF2_VOL_CTRL1_ADCL_VOL,
		       SUN8I_AIF2_VOL_CTRL1_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 DAC Playback Volume",
		       SUN8I_AIF2_VOL_CTRL2,
		       SUN8I_AIF2_VOL_CTRL2_DACL_VOL,
		       SUN8I_AIF2_VOL_CTRL2_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("ADC Capture Volume",
		       SUN8I_ADC_VOL_CTRL,
		       SUN8I_ADC_VOL_CTRL_ADCL_VOL,
		       SUN8I_ADC_VOL_CTRL_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("DAC Playback Volume",
		       SUN8I_DAC_VOL_CTRL,
		       SUN8I_DAC_VOL_CTRL_DACL_VOL,
		       SUN8I_DAC_VOL_CTRL_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
};

static const struct snd_kcontrol_new sun8i_aif1_loopback_switch =
	SOC_DAPM_SINGLE("AIF1 Loopback Switch",
			SUN8I_AIF1_DACDAT_CTRL,
			SUN8I_AIF1_DACDAT_CTRL_AIF1_LOOP_ENA, 1, 0);

static const struct snd_kcontrol_new sun8i_aif2_loopback_switch =
	SOC_DAPM_SINGLE("AIF2 Loopback Switch",
			SUN8I_AIF2_DACDAT_CTRL,
			SUN8I_AIF2_DACDAT_CTRL_AIF2_LOOP_ENA, 1, 0);

static const struct snd_kcontrol_new sun8i_aif3_loopback_switch =
	SOC_DAPM_SINGLE("Switch",
			SUN8I_AIF3_DACDAT_CTRL,
			SUN8I_AIF3_DACDAT_CTRL_AIF3_LOOP_ENA, 1, 0);

static const char *const sun8i_aif_stereo_mux_enum_names[] = {
	"Stereo", "Reverse Stereo", "Sum Mono", "Mix Mono"
};

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_ad0_stereo_mux_enum,
			    SUN8I_AIF1_ADCDAT_CTRL,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif1_ad0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 AD0 Stereo Capture Route",
		      sun8i_aif1_ad0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_da0_stereo_mux_enum,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif1_da0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 DA0 Stereo Playback Route",
		      sun8i_aif1_da0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_adc_stereo_mux_enum,
			    SUN8I_AIF2_ADCDAT_CTRL,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_adc_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 ADC Stereo Capture Route",
		      sun8i_aif2_adc_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_dac_stereo_mux_enum,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC,
			    sun8i_aif_stereo_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_dac_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 DAC Stereo Playback Route",
		      sun8i_aif2_dac_stereo_mux_enum);

static const char *const sun8i_aif3_mux_enum_names[] = {
	"None", "AIF2 Left", "AIF2 Right"
};

static SOC_ENUM_SINGLE_DECL(sun8i_aif3_adc_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC,
			    sun8i_aif3_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif3_adc_mux_control =
	SOC_DAPM_ENUM("AIF3 ADC Capture Route",
		      sun8i_aif3_adc_mux_enum);

static SOC_ENUM_SINGLE_DECL(sun8i_aif2_dac_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC,
			    sun8i_aif3_mux_enum_names);

static const struct snd_kcontrol_new sun8i_aif2_dac_mux_control =
	SOC_DAPM_ENUM("AIF3 DAC Playback Route",
		      sun8i_aif2_dac_mux_enum);

static const struct snd_kcontrol_new sun8i_aif1_ad0_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF1 DA0 Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF2 DAC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL, 1, 0),
};

static const struct snd_kcontrol_new sun8i_aif2_adc_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA0 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA1 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF2 DAC Rev Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer ADC Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR, 1, 0),
};

static const struct snd_kcontrol_new sun8i_dac_mixer_controls[] = {
	SOC_DAPM_DOUBLE("DAC Mixer AIF1 DA0 Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer AIF1 DA1 Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer AIF2 DAC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("DAC Mixer ADC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR, 1, 0),
};

static const struct snd_soc_dapm_widget sun8i_codec_dapm_widgets[] = {
	/* AIF Loopback Switches */
	SND_SOC_DAPM_SWITCH("AIF1 Slot 0 Left Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif1_loopback_switch),
	SND_SOC_DAPM_SWITCH("AIF1 Slot 0 Right Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif1_loopback_switch),

	SND_SOC_DAPM_SWITCH("AIF2 Left Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif2_loopback_switch),
	SND_SOC_DAPM_SWITCH("AIF2 Right Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif2_loopback_switch),

	SND_SOC_DAPM_SWITCH("AIF3 Loopback", SND_SOC_NOPM, 0, 0,
			    &sun8i_aif3_loopback_switch),

	/* AIF "ADC" Outputs */
	SND_SOC_DAPM_AIF_OUT("AIF1 AD0 Left", "AIF1 Capture", 0,
			     SUN8I_AIF1_ADCDAT_CTRL,
			     SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1 AD0 Right", "AIF1 Capture", 1,
			     SUN8I_AIF1_ADCDAT_CTRL,
			     SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA, 0),

	SND_SOC_DAPM_AIF_OUT("AIF2 ADC Left", "AIF2 Capture", 0,
			     SUN8I_AIF2_ADCDAT_CTRL,
			     SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2 ADC Right", "AIF2 Capture", 1,
			     SUN8I_AIF2_ADCDAT_CTRL,
			     SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA, 0),

	SND_SOC_DAPM_AIF_OUT("AIF3 ADC", "AIF3 Capture", 0,
			     SND_SOC_NOPM, 0, 0),

	/* AIF "ADC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 AD0 Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 AD0 Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 ADC Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 ADC Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),

	/* AIF "ADC" Muxes */
	SND_SOC_DAPM_MUX("AIF3 ADC Capture Route", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif3_adc_mux_control),

	/* AIF "ADC" Mixers */
	SOC_MIXER_ARRAY("AIF1 AD0 Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),
	SOC_MIXER_ARRAY("AIF1 AD0 Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),

	SOC_MIXER_ARRAY("AIF2 ADC Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),
	SOC_MIXER_ARRAY("AIF2 ADC Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),

	/* AIF "DAC" Muxes */
	SND_SOC_DAPM_MUX("AIF2 DAC Left Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DAC Right Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),

	/* AIF "DAC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 DA0 Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 DA0 Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 DAC Left Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DAC Right Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),

	/* AIF "DAC" Inputs */
	SND_SOC_DAPM_AIF_IN("AIF1 DA0 Left", "AIF1 Playback", 0,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF1 DA0 Right", "AIF1 Playback", 1,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA, 0),

	SND_SOC_DAPM_AIF_IN("AIF2 DAC Left", "AIF2 Playback", 0,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF2 DAC Right", "AIF2 Playback", 1,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA, 0),

	SND_SOC_DAPM_AIF_IN("AIF3 DAC", "AIF3 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	/* Main DAC Outputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_PGA("DAC Left", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC Right", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC", SUN8I_DAC_DIG_CTRL,
			    SUN8I_DAC_DIG_CTRL_ENDA, 0, NULL, 0),

	/* Main DAC Mixers */
	SOC_MIXER_ARRAY("DAC Left Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),
	SOC_MIXER_ARRAY("DAC Right Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),

	/* Main ADC Inputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_PGA("ADC Left", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC Right", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC", SUN8I_ADC_DIG_CTRL,
			    SUN8I_ADC_DIG_CTRL_ENAD, 0, NULL, 0),

	/* Module Resets */
	SND_SOC_DAPM_SUPPLY("RST AIF1", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST AIF2", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST AIF3", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST ADC", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST DAC", SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_DAC, 0, NULL, 0),

	/* Module Clocks */
	SND_SOC_DAPM_SUPPLY("MODCLK AIF1", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK AIF2", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK AIF3", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK ADC", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MODCLK DAC", SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_DAC, 0, NULL, 0),

	/* Clock Supplies */
	SND_SOC_DAPM_SUPPLY("AIF1CLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF1CLK_ENA, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2CLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF2CLK_ENA, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSCLK", SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_SYSCLK_ENA, 0, NULL, 0),

	SND_SOC_DAPM_CLOCK_SUPPLY("mod"),
};

static const struct snd_soc_dapm_route sun8i_codec_dapm_routes[] = {
	/* AIF Loopback Routes */
	{ "AIF1 Slot 0 Left Loopback", "AIF1 Loopback Switch", "AIF1 AD0 Left" },
	{ "AIF1 Slot 0 Right Loopback", "AIF1 Loopback Switch", "AIF1 AD0 Right" },

	{ "AIF2 Left Loopback", "AIF2 Loopback Switch", "AIF2 ADC Left" },
	{ "AIF2 Right Loopback", "AIF2 Loopback Switch", "AIF2 ADC Right" },

	{ "AIF3 Loopback", "Switch", "AIF3 ADC" },

	/* AIF "ADC" Output Routes */
	{ "AIF1 AD0 Left", NULL, "AIF1 AD0 Left Stereo Mux" },
	{ "AIF1 AD0 Right", NULL, "AIF1 AD0 Right Stereo Mux" },

	{ "AIF1 AD0 Left", NULL, "AIF1CLK" },
	{ "AIF1 AD0 Right", NULL, "AIF1CLK" },

	{ "AIF2 ADC Left", NULL, "AIF2 ADC Left Stereo Mux" },
	{ "AIF2 ADC Right", NULL, "AIF2 ADC Right Stereo Mux" },

	{ "AIF2 ADC Left", NULL, "AIF2CLK" },
	{ "AIF2 ADC Right", NULL, "AIF2CLK" },

	{ "AIF3 ADC", NULL, "AIF3 ADC Capture Route" },

	/* AIF "ADC" Mono/Stereo Mux Routes */
	{ "AIF1 AD0 Left Stereo Mux", "Stereo", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Reverse Stereo", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Sum Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Sum Mono", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Mix Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Left Stereo Mux", "Mix Mono", "AIF1 AD0 Right Mixer" },

	{ "AIF1 AD0 Right Stereo Mux", "Stereo", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Reverse Stereo", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Sum Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Sum Mono", "AIF1 AD0 Right Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Mix Mono", "AIF1 AD0 Left Mixer" },
	{ "AIF1 AD0 Right Stereo Mux", "Mix Mono", "AIF1 AD0 Right Mixer" },

	{ "AIF2 ADC Left Stereo Mux", "Stereo", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Reverse Stereo", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Sum Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Sum Mono", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Mix Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Left Stereo Mux", "Mix Mono", "AIF2 ADC Right Mixer" },

	{ "AIF2 ADC Right Stereo Mux", "Stereo", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Reverse Stereo", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Sum Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Sum Mono", "AIF2 ADC Right Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Mix Mono", "AIF2 ADC Left Mixer" },
	{ "AIF2 ADC Right Stereo Mux", "Mix Mono", "AIF2 ADC Right Mixer" },

	/* AIF "ADC" Mux Routes */
	{ "AIF3 ADC Capture Route", "AIF2 Left", "AIF2 ADC Left Mixer" },
	{ "AIF3 ADC Capture Route", "AIF2 Right", "AIF2 ADC Right Mixer" },

	/* AIF "ADC" Mixer Routes */
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF2 DAC Capture Switch", "AIF2 DAC Left Mux" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer ADC Capture Switch", "ADC Left" },
	{ "AIF1 AD0 Left Mixer", "AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Right Mux" },

	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF2 DAC Capture Switch", "AIF2 DAC Right Mux" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer ADC Capture Switch", "ADC Right" },
	{ "AIF1 AD0 Right Mixer", "AIF1 AD0 Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Left Mux" },

	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Right Mux" },
	{ "AIF2 ADC Left Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADC Left" },

	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DAC Left Mux" },
	{ "AIF2 ADC Right Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADC Right" },

	/* AIF "DAC" Mux Routes */
	{ "AIF2 DAC Left Mux", "None", "AIF2 DAC Left Stereo Mux" },
	{ "AIF2 DAC Left Mux", "AIF2 Left", "AIF3 DAC" },
	{ "AIF2 DAC Left Mux", "AIF2 Right", "AIF2 DAC Left Stereo Mux" },

	{ "AIF2 DAC Right Mux", "None", "AIF2 DAC Right Stereo Mux" },
	{ "AIF2 DAC Right Mux", "AIF2 Left", "AIF2 DAC Right Stereo Mux" },
	{ "AIF2 DAC Right Mux", "AIF2 Right", "AIF3 DAC" },

	/* AIF "DAC" Mono/Stereo Mux Routes */
	{ "AIF1 DA0 Left Stereo Mux", "Stereo", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Reverse Stereo", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Left Stereo Mux", "Sum Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Sum Mono", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Left Stereo Mux", "Mix Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Left Stereo Mux", "Mix Mono", "AIF1 DA0 Right" },

	{ "AIF1 DA0 Right Stereo Mux", "Stereo", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Right Stereo Mux", "Reverse Stereo", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Sum Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Sum Mono", "AIF1 DA0 Right" },
	{ "AIF1 DA0 Right Stereo Mux", "Mix Mono", "AIF1 DA0 Left" },
	{ "AIF1 DA0 Right Stereo Mux", "Mix Mono", "AIF1 DA0 Right" },

	{ "AIF2 DAC Left Stereo Mux", "Stereo", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Reverse Stereo", "AIF2 DAC Right" },
	{ "AIF2 DAC Left Stereo Mux", "Sum Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Sum Mono", "AIF2 DAC Right" },
	{ "AIF2 DAC Left Stereo Mux", "Mix Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Left Stereo Mux", "Mix Mono", "AIF2 DAC Right" },

	{ "AIF2 DAC Right Stereo Mux", "Stereo", "AIF2 DAC Right" },
	{ "AIF2 DAC Right Stereo Mux", "Reverse Stereo", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Sum Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Sum Mono", "AIF2 DAC Right" },
	{ "AIF2 DAC Right Stereo Mux", "Mix Mono", "AIF2 DAC Left" },
	{ "AIF2 DAC Right Stereo Mux", "Mix Mono", "AIF2 DAC Right" },

	/* AIF "DAC" Input Routes */
	{ "AIF1 DA0 Left", NULL, "AIF1 Slot 0 Left Loopback" },
	{ "AIF1 DA0 Right", NULL, "AIF1 Slot 0 Right Loopback" },

	{ "AIF1 DA0 Left", NULL, "AIF1CLK" },
	{ "AIF1 DA0 Right", NULL, "AIF1CLK" },

	{ "AIF2 DAC Left", NULL, "AIF2 Left Loopback" },
	{ "AIF2 DAC Right", NULL, "AIF2 Right Loopback" },

	{ "AIF2 DAC Left", NULL, "AIF2CLK" },
	{ "AIF2 DAC Right", NULL, "AIF2CLK" },

	{ "AIF3 DAC", NULL, "AIF3 Loopback" },

	/* Main DAC Output Routes */
	{ "DAC Left", NULL, "DAC Left Mixer" },
	{ "DAC Right", NULL, "DAC Right Mixer" },

	{ "DAC Left", NULL, "DAC" },
	{ "DAC Right", NULL, "DAC" },

	/* Main DAC Mixer Routes */
	{ "DAC Left Mixer", "DAC Mixer AIF1 DA0 Playback Switch", "AIF1 DA0 Left Stereo Mux" },
	{ "DAC Left Mixer", "DAC Mixer AIF2 DAC Playback Switch", "AIF2 DAC Left Mux" },
	{ "DAC Left Mixer", "DAC Mixer ADC Playback Switch", "ADC Left" },

	{ "DAC Right Mixer", "DAC Mixer AIF1 DA0 Playback Switch", "AIF1 DA0 Right Stereo Mux" },
	{ "DAC Right Mixer", "DAC Mixer AIF2 DAC Playback Switch", "AIF2 DAC Right Mux" },
	{ "DAC Right Mixer", "DAC Mixer ADC Playback Switch", "ADC Right" },

	/* Main ADC Input Routes */
	{ "ADC Left", NULL, "ADC" },
	{ "ADC Right", NULL, "ADC" },

	/* Module Supply Routes */
	{ "AIF1 AD0 Left", NULL, "RST AIF1" },
	{ "AIF1 AD0 Right", NULL, "RST AIF1" },
	{ "AIF1 DA0 Left", NULL, "RST AIF1" },
	{ "AIF1 DA0 Right", NULL, "RST AIF1" },

	{ "AIF2 ADC Left", NULL, "RST AIF2" },
	{ "AIF2 ADC Right", NULL, "RST AIF2" },
	{ "AIF2 DAC Left", NULL, "RST AIF2" },
	{ "AIF2 DAC Right", NULL, "RST AIF2" },

	/* AIF3 gets its bitclock from AIF2 */
	{ "AIF3 ADC", NULL, "RST AIF2" },
	{ "AIF3 ADC", NULL, "RST AIF3" },
	{ "AIF3 DAC", NULL, "RST AIF2" },
	{ "AIF3 DAC", NULL, "RST AIF3" },

	{ "ADC", NULL, "RST ADC" },
	{ "DAC", NULL, "RST DAC" },

	/* Module Reset Routes */
	{ "RST AIF1", NULL, "MODCLK AIF1" },
	{ "RST AIF2", NULL, "MODCLK AIF2" },
	{ "RST AIF3", NULL, "MODCLK AIF3" },
	{ "RST ADC", NULL, "MODCLK ADC" },
	{ "RST DAC", NULL, "MODCLK DAC" },

	/* Module Clock Routes */
	{ "MODCLK AIF1", NULL, "SYSCLK" },
	{ "MODCLK AIF2", NULL, "SYSCLK" },
	{ "MODCLK AIF3", NULL, "SYSCLK" },
	{ "MODCLK ADC", NULL, "SYSCLK" },
	{ "MODCLK DAC", NULL, "SYSCLK" },

	/* Clock Supply Routes */
	{ "SYSCLK", NULL, "AIF1CLK" },

	{ "AIF1CLK", NULL, "mod" },
	{ "AIF2CLK", NULL, "mod" },
};

static int sun8i_codec_component_probe(struct snd_soc_component *component)
{
	/* Set AIF1CLK clock source to PLL */
	regmap_update_bits(component->regmap, SUN8I_SYSCLK_CTL,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL);

	/* Set AIF2CLK clock source to PLL */
	regmap_update_bits(component->regmap, SUN8I_SYSCLK_CTL,
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK,
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL);

	/* Set SYSCLK clock source to AIF1CLK */
	regmap_update_bits(component->regmap, SUN8I_SYSCLK_CTL,
			   BIT(SUN8I_SYSCLK_CTL_SYSCLK_SRC),
			   SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK);

	return 0;
}

static const struct snd_soc_component_driver sun8i_soc_component = {
	.controls		= sun8i_codec_controls,
	.num_controls		= ARRAY_SIZE(sun8i_codec_controls),
	.dapm_widgets		= sun8i_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun8i_codec_dapm_widgets),
	.dapm_routes		= sun8i_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun8i_codec_dapm_routes),
	.probe			= sun8i_codec_component_probe,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config sun8i_codec_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN8I_DAC_MXR_SRC,
};

static int sun8i_codec_probe(struct platform_device *pdev)
{
	struct sun8i_codec *scodec;
	struct regmap *regmap;
	void __iomem *base;

	scodec = devm_kzalloc(&pdev->dev, sizeof(*scodec), GFP_KERNEL);
	if (!scodec)
		return -ENOMEM;

	scodec->clk_module = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(scodec->clk_module)) {
		dev_err(&pdev->dev, "Failed to get the module clock\n");
		return PTR_ERR(scodec->clk_module);
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	regmap = devm_regmap_init_mmio_clk(&pdev->dev, "bus", base,
					   &sun8i_codec_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "Failed to create our regmap\n");
		return PTR_ERR(regmap);
	}

	scodec->inverted_lrck = (uintptr_t)of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, scodec);

	return devm_snd_soc_register_component(&pdev->dev, &sun8i_soc_component,
					       sun8i_codec_dais,
					       ARRAY_SIZE(sun8i_codec_dais));
}

static const struct of_device_id sun8i_codec_of_match[] = {
	{
		.compatible = "allwinner,sun8i-a33-codec",
		.data = (void *)1,
	},
	{
		.compatible = "allwinner,sun50i-a64-codec",
		.data = (void *)0,
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_codec_of_match);

static struct platform_driver sun8i_codec_driver = {
	.driver = {
		.name = "sun8i-codec",
		.of_match_table = sun8i_codec_of_match,
	},
	.probe = sun8i_codec_probe,
};
module_platform_driver(sun8i_codec_driver);

MODULE_DESCRIPTION("Allwinner A33 (sun8i) codec driver");
MODULE_AUTHOR("Mylène Josserand <mylene.josserand@free-electrons.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun8i-codec");
