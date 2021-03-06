/*
 * ASoC Driver for HifiBerry Digi
 *
 * Author: Daniel Matuschek <info@crazy-audio.com>
 * based on the HifiBerry DAC driver by Florian Meier <florian.meier@koalo.de>
 *	Copyright 2013
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "../codecs/wm8804.h"

#define CLK_27MHZ_EN_GPIO	5
#define CLK_22MHZ5792_EN_GPIO	6
#define CLK_24MHZ576_EN_GPIO	13

static short int auto_shutdown_output = 0;
module_param(auto_shutdown_output, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(auto_shutdown_output, "Shutdown SP/DIF output if playback is stopped");

/*
 * Struct defining pins, direction and initial state 
 */
static struct gpio clkens[] = {
		{ CLK_27MHZ_EN_GPIO, GPIOF_OUT_INIT_HIGH, "Clk_27Mhz_EN" },
		{ CLK_22MHZ5792_EN_GPIO, GPIOF_OUT_INIT_LOW, "Clk_22Mhz5792_EN" },
		{ CLK_24MHZ576_EN_GPIO, GPIOF_OUT_INIT_LOW, "Clk_24Mhz576_EN" },
};

static int samplerate=44100;

static int snd_rpi_hifiberry_digi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	/* enable TX output */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);

	return 0;
}

static int snd_rpi_hifiberry_digi_startup(struct snd_pcm_substream *substream) {
	/* turn on digital output */
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x3c, 0x00);
	return 0;
}

static void snd_rpi_hifiberry_digi_shutdown(struct snd_pcm_substream *substream) {
	/* turn off output */
	if (auto_shutdown_output) {
		/* turn off output */
		struct snd_soc_pcm_runtime *rtd = substream->private_data;
		struct snd_soc_codec *codec = rtd->codec;
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x3c, 0x3c);
	}
}


static int snd_rpi_hifiberry_digi_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	int sysclk = 27000000; /* This is fixed on this board */

	long mclk_freq=0;
	int mclk_div=1;
	int mclk_src=WM8804_TX_CLKSRC_MCLK;
	int sampling_freq=1;

	int ret;

	samplerate = params_rate(params);


	if (samplerate<=96000) {
		mclk_freq=samplerate*256;
		mclk_div=WM8804_MCLKDIV_256FS;
	} else {
		mclk_freq=samplerate*128;
		mclk_div=WM8804_MCLKDIV_128FS;
	}
	
	switch (samplerate) {
		case 32000:
			sampling_freq=0x03;
			break;
		case 44100:
			sampling_freq=0x00;
			break;
		case 48000:
			sampling_freq=0x02;
			break;
		case 88200:
			sampling_freq=0x08;
			break;
		case 96000:
			sampling_freq=0x0a;
			break;
		case 176400:
			sampling_freq=0x0c;
			break;
		case 192000:
			sampling_freq=0x0e;
			break;
		default:
			dev_err(codec->dev,
			"Failed to set WM8804 SYSCLK, unsupported samplerate %d\n",
			samplerate);
	}
//TODO - might be able to combine switch statements somehow
	switch (samplerate) {
		case 32000:
		case 48000:
		case 96000:
		case 192000:
			/* disable 27MHz and 22.5792MHz clocks */
			gpio_set_value(CLK_27MHZ_EN_GPIO, 0);
			gpio_set_value(CLK_22MHZ5792_EN_GPIO, 0);		
			/* enable 24.576MHz clock */
			gpio_set_value(CLK_24MHZ576_EN_GPIO, 1);
			sysclk = 24576000;
			break;
		case 44100:
		case 88200:
		case 176400:
			/* disable 27MHz and 24.576MHz clocks */
			gpio_set_value(CLK_27MHZ_EN_GPIO, 0);
			gpio_set_value(CLK_24MHZ576_EN_GPIO, 0);
			/* enable 22.5792MHz clock */
			gpio_set_value(CLK_22MHZ5792_EN_GPIO, 1);
			sysclk = 22579200;
			break;
		default:
			dev_err(codec->dev,
			"Failed to set WM8804 SYSCLK, unsupported samplerate %d\n",
			samplerate);
	}

	snd_soc_dai_set_clkdiv(codec_dai, WM8804_MCLK_DIV, mclk_div);
	snd_soc_dai_set_pll(codec_dai, 0, 0, sysclk, mclk_freq);

	if (samplerate<=48000) {
		mclk_src=WM8804_TX_CLKSRC_PLL;
	} else {
		mclk_src=WM8804_TX_CLKSRC_MCLK;
	}
	ret = snd_soc_dai_set_sysclk(codec_dai, mclk_src,
					sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec->dev,
		"Failed to set WM8804 SYSCLK: %d\n", ret);
		return ret;
	}

	/* Enable TX output */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);

	/* Power on */ 
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x9, 0);

	/* set sampling frequency status bits */
	snd_soc_update_bits(codec, WM8804_SPDTX4, 0x0f, sampling_freq);

	return snd_soc_dai_set_bclk_ratio(cpu_dai,64);
}

/* machine stream operations */
static struct snd_soc_ops snd_rpi_hifiberry_digi_ops = {
	.hw_params = snd_rpi_hifiberry_digi_hw_params,
        .startup = snd_rpi_hifiberry_digi_startup,
        .shutdown = snd_rpi_hifiberry_digi_shutdown,
};

static struct snd_soc_dai_link snd_rpi_hifiberry_digi_dai[] = {
{
	.name		= "HifiBerry Digi",
	.stream_name	= "HifiBerry Digi HiFi",
	.cpu_dai_name	= "bcm2708-i2s.0",
	.codec_dai_name	= "wm8804-spdif",
	.platform_name	= "bcm2708-i2s.0",
	.codec_name	= "wm8804.1-003b",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &snd_rpi_hifiberry_digi_ops,
	.init		= snd_rpi_hifiberry_digi_init,
},
};

/* audio machine driver */
static struct snd_soc_card snd_rpi_hifiberry_digi = {
	.name         = "snd_rpi_hifiberry_digi",
	.owner        = THIS_MODULE,
	.dai_link     = snd_rpi_hifiberry_digi_dai,
	.num_links    = ARRAY_SIZE(snd_rpi_hifiberry_digi_dai),
};

static int snd_rpi_hifiberry_digi_probe(struct platform_device *pdev)
{
	int ret = 0;

	snd_rpi_hifiberry_digi.dev = &pdev->dev;

	if (pdev->dev.of_node) {
	    struct device_node *i2s_node;
	    struct snd_soc_dai_link *dai = &snd_rpi_hifiberry_digi_dai[0];
	    i2s_node = of_parse_phandle(pdev->dev.of_node,
					"i2s-controller", 0);

	    if (i2s_node) {
		dai->cpu_dai_name = NULL;
		dai->cpu_of_node = i2s_node;
		dai->platform_name = NULL;
		dai->platform_of_node = i2s_node;
	    }
	}

	ret = snd_soc_register_card(&snd_rpi_hifiberry_digi);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);
		return ret;
	}

	/* register CLKEN GPIOs, initialize */
	ret = gpio_request_array(clkens, ARRAY_SIZE(clkens));

	if (ret) {
		dev_err(&pdev->dev, "Unable to request GPIOs: %d\n", ret);
	}

	return ret;
}

static int snd_rpi_hifiberry_digi_remove(struct platform_device *pdev)
{
	int i;
	
	/* disable clocks */
	for(i = 0; i < ARRAY_SIZE(clkens); i++) {
		gpio_set_value(clkens[i].gpio, 0);
	}
	
	/* unregister gpios */
	gpio_free_array(clkens, ARRAY_SIZE(clkens));

	return snd_soc_unregister_card(&snd_rpi_hifiberry_digi);
}

static const struct of_device_id snd_rpi_hifiberry_digi_of_match[] = {
	{ .compatible = "hifiberry,hifiberry-digi", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_rpi_hifiberry_digi_of_match);

static struct platform_driver snd_rpi_hifiberry_digi_driver = {
	.driver = {
		.name   = "snd-hifiberry-digi",
		.owner  = THIS_MODULE,
		.of_match_table = snd_rpi_hifiberry_digi_of_match,
	},
	.probe          = snd_rpi_hifiberry_digi_probe,
	.remove         = snd_rpi_hifiberry_digi_remove,
};

module_platform_driver(snd_rpi_hifiberry_digi_driver);

MODULE_AUTHOR("Daniel Matuschek <info@crazy-audio.com>");
MODULE_DESCRIPTION("ASoC Driver for HifiBerry Digi");
MODULE_LICENSE("GPL v2");
