/*
 * soc-pcm.c  --  ALSA SoC PCM
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 * Copyright (C) 2010 Slimlogic Ltd.
 * Copyright (C) 2010 Texas Instruments Inc.
 *
 * Authors: Liam Girdwood <lrg@ti.com>
 *          Mark Brown <broonie@opensource.wolfsonmicro.com>       
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

static DEFINE_MUTEX(pcm_mutex);

static int soc_pcm_apply_symmetry(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	if (!codec_dai->driver->symmetric_rates &&
	    !cpu_dai->driver->symmetric_rates &&
	    !rtd->dai_link->symmetric_rates)
		return 0;

	/* This can happen if multiple streams are starting simultaneously -
	 * the second can need to get its constraints before the first has
	 * picked a rate.  Complain and allow the application to carry on.
	 */
	if (!rtd->rate) {
		dev_warn(&rtd->dev,
			 "Not enforcing symmetric_rates due to race\n");
		return 0;
	}

	dev_dbg(&rtd->dev, "Symmetry forces %dHz rate\n", rtd->rate);

	ret = snd_pcm_hw_constraint_minmax(substream->runtime,
					   SNDRV_PCM_HW_PARAM_RATE,
					   rtd->rate, rtd->rate);
	if (ret < 0) {
		dev_err(&rtd->dev,
			"Unable to apply rate symmetry constraint: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Called by ALSA when a PCM substream is opened, the runtime->hw record is
 * then initialized and any private data can be allocated. This also calls
 * startup for the cpu DAI, platform, machine and codec DAI.
 */
static int soc_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai_driver *cpu_dai_drv = cpu_dai->driver;
	struct snd_soc_dai_driver *codec_dai_drv = codec_dai->driver;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	/* startup the audio subsystem */
	if (cpu_dai->driver->ops->startup) {
		ret = cpu_dai->driver->ops->startup(substream, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open interface %s\n",
				cpu_dai->name);
			goto out;
		}
	}

	if (platform->driver->ops && platform->driver->ops->open) {
		ret = platform->driver->ops->open(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open platform %s\n", platform->name);
			goto platform_err;
		}
	}

	if (codec_dai->driver->ops->startup) {
		ret = codec_dai->driver->ops->startup(substream, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open codec %s\n",
				codec_dai->name);
			goto codec_dai_err;
		}
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->startup) {
		ret = rtd->dai_link->ops->startup(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: %s startup failed\n", rtd->dai_link->name);
			goto machine_err;
		}
	}

	/* Check that the codec and cpu DAIs are compatible */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw.rate_min =
			max(codec_dai_drv->playback.rate_min,
			    cpu_dai_drv->playback.rate_min);
		runtime->hw.rate_max =
			min(codec_dai_drv->playback.rate_max,
			    cpu_dai_drv->playback.rate_max);
		runtime->hw.channels_min =
			max(codec_dai_drv->playback.channels_min,
				cpu_dai_drv->playback.channels_min);
		runtime->hw.channels_max =
			min(codec_dai_drv->playback.channels_max,
				cpu_dai_drv->playback.channels_max);
		runtime->hw.formats =
			codec_dai_drv->playback.formats & cpu_dai_drv->playback.formats;
		runtime->hw.rates =
			codec_dai_drv->playback.rates & cpu_dai_drv->playback.rates;
		if (codec_dai_drv->playback.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= cpu_dai_drv->playback.rates;
		if (cpu_dai_drv->playback.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= codec_dai_drv->playback.rates;
	} else {
		runtime->hw.rate_min =
			max(codec_dai_drv->capture.rate_min,
			    cpu_dai_drv->capture.rate_min);
		runtime->hw.rate_max =
			min(codec_dai_drv->capture.rate_max,
			    cpu_dai_drv->capture.rate_max);
		runtime->hw.channels_min =
			max(codec_dai_drv->capture.channels_min,
				cpu_dai_drv->capture.channels_min);
		runtime->hw.channels_max =
			min(codec_dai_drv->capture.channels_max,
				cpu_dai_drv->capture.channels_max);
		runtime->hw.formats =
			codec_dai_drv->capture.formats & cpu_dai_drv->capture.formats;
		runtime->hw.rates =
			codec_dai_drv->capture.rates & cpu_dai_drv->capture.rates;
		if (codec_dai_drv->capture.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= cpu_dai_drv->capture.rates;
		if (cpu_dai_drv->capture.rates
			   & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))
			runtime->hw.rates |= codec_dai_drv->capture.rates;
	}

	ret = -EINVAL;
	snd_pcm_limit_hw_rates(runtime);
	if (!runtime->hw.rates) {
		printk(KERN_ERR "asoc: %s <-> %s No matching rates\n",
			codec_dai->name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.formats) {
		printk(KERN_ERR "asoc: %s <-> %s No matching formats\n",
			codec_dai->name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.channels_min || !runtime->hw.channels_max ||
	    runtime->hw.channels_min > runtime->hw.channels_max) {
		printk(KERN_ERR "asoc: %s <-> %s No matching channels\n",
				codec_dai->name, cpu_dai->name);
		goto config_err;
	}

	/* Symmetry only applies if we've already got an active stream. */
	if (cpu_dai->active || codec_dai->active) {
		ret = soc_pcm_apply_symmetry(substream);
		if (ret != 0)
			goto config_err;
	}

	pr_debug("asoc: %s <-> %s info:\n",
			codec_dai->name, cpu_dai->name);
	pr_debug("asoc: rate mask 0x%x\n", runtime->hw.rates);
	pr_debug("asoc: min ch %d max ch %d\n", runtime->hw.channels_min,
		 runtime->hw.channels_max);
	pr_debug("asoc: min rate %d max rate %d\n", runtime->hw.rate_min,
		 runtime->hw.rate_max);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active++;
		codec_dai->playback_active++;
	} else {
		cpu_dai->capture_active++;
		codec_dai->capture_active++;
	}
	cpu_dai->active++;
	codec_dai->active++;
	rtd->codec->active++;
	mutex_unlock(&rtd->pcm_mutex);
	return 0;

config_err:
	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

machine_err:
	if (codec_dai->driver->ops->shutdown)
		codec_dai->driver->ops->shutdown(substream, codec_dai);

codec_dai_err:
	if (platform->driver->ops && platform->driver->ops->close)
		platform->driver->ops->close(substream);

platform_err:
	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);
out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

/*
 * Power down the audio subsystem pmdown_time msecs after close is called.
 * This is to ensure there are no pops or clicks in between any music tracks
 * due to DAPM power cycling.
 */
static void close_delayed_work(struct work_struct *work)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(work, struct snd_soc_pcm_runtime, delayed_work.work);
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	pr_debug("pop wq checking: %s status: %s waiting: %s\n",
		 codec_dai->driver->playback.stream_name,
		 codec_dai->playback_active ? "active" : "inactive",
		 codec_dai->pop_wait ? "yes" : "no");

	/* are we waiting on this codec DAI stream */
	if (codec_dai->pop_wait == 1) {
		codec_dai->pop_wait = 0;
		snd_soc_dapm_stream_event(rtd,
			codec_dai->driver->playback.stream_name,
			SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);
}

/*
 * Called by ALSA when a PCM substream is closed. Private data can be
 * freed here. The cpu DAI, codec DAI, machine and platform are also
 * shutdown.
 */
static int soc_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active--;
		codec_dai->playback_active--;
	} else {
		cpu_dai->capture_active--;
		codec_dai->capture_active--;
	}

	cpu_dai->active--;
	codec_dai->active--;
	codec->active--;

	if (!cpu_dai->active && !codec_dai->active)
		rtd->rate = 0;

	/* Muting the DAC suppresses artifacts caused during digital
	 * shutdown, for example from stopping clocks.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_digital_mute(codec_dai, 1);

	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);

	if (codec_dai->driver->ops->shutdown)
		codec_dai->driver->ops->shutdown(substream, codec_dai);

	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

	if (platform->driver->ops && platform->driver->ops->close)
		platform->driver->ops->close(substream);
	cpu_dai->runtime = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* start delayed pop wq here for playback streams */
		codec_dai->pop_wait = 1;
		schedule_delayed_work(&rtd->delayed_work,
			msecs_to_jiffies(rtd->pmdown_time));
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(rtd,
			codec_dai->driver->capture.stream_name,
			SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);
	return 0;
}

/*
 * Called by ALSA when the PCM substream is prepared, can set format, sample
 * rate, etc.  This function is non atomic and can be called multiple times,
 * it can refer to the runtime info.
 */
static int soc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (rtd->dai_link->ops && rtd->dai_link->ops->prepare) {
		ret = rtd->dai_link->ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine prepare error\n");
			goto out;
		}
	}

	if (platform->driver->ops && platform->driver->ops->prepare) {
		ret = platform->driver->ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: platform prepare error\n");
			goto out;
		}
	}

	if (codec_dai->driver->ops->prepare) {
		ret = codec_dai->driver->ops->prepare(substream, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: codec DAI prepare error\n");
			goto out;
		}
	}

	if (cpu_dai->driver->ops->prepare) {
		ret = cpu_dai->driver->ops->prepare(substream, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: cpu DAI prepare error\n");
			goto out;
		}
	}

	/* cancel any delayed stream shutdown that is pending */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    codec_dai->pop_wait) {
		codec_dai->pop_wait = 0;
		cancel_delayed_work(&rtd->delayed_work);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dapm_stream_event(rtd,
					  codec_dai->driver->playback.stream_name,
					  SND_SOC_DAPM_STREAM_START);
	else
		snd_soc_dapm_stream_event(rtd,
					  codec_dai->driver->capture.stream_name,
					  SND_SOC_DAPM_STREAM_START);

	snd_soc_dai_digital_mute(codec_dai, 0);

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

/*
 * Called by ALSA when the hardware params are set by application. This
 * function can also be called multiple times and can allocate buffers
 * (using snd_pcm_lib_* ). It's non-atomic.
 */
static int soc_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_params) {
		ret = rtd->dai_link->ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine hw_params failed\n");
			goto out;
		}
	}

	if (codec_dai->driver->ops->hw_params) {
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't set codec %s hw params\n",
				codec_dai->name);
			goto codec_err;
		}
	}

	if (cpu_dai->driver->ops->hw_params) {
		ret = cpu_dai->driver->ops->hw_params(substream, params, cpu_dai);
		if (ret < 0) {
			printk(KERN_ERR "asoc: interface %s hw params failed\n",
				cpu_dai->name);
			goto interface_err;
		}
	}

	if (platform->driver->ops && platform->driver->ops->hw_params) {
		ret = platform->driver->ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: platform %s hw params failed\n",
				platform->name);
			goto platform_err;
		}
	}

	rtd->rate = params_rate(params);

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;

platform_err:
	if (cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

interface_err:
	if (codec_dai->driver->ops->hw_free)
		codec_dai->driver->ops->hw_free(substream, codec_dai);

codec_err:
	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

/*
 * Frees resources allocated by hw_params, can be called multiple times
 */
static int soc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	/* apply codec digital mute */
	if (!codec->active)
		snd_soc_dai_digital_mute(codec_dai, 1);

	/* free any machine hw params */
	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	/* free any DMA resources */
	if (platform->driver->ops && platform->driver->ops->hw_free)
		platform->driver->ops->hw_free(substream);

	/* now free hw params for the DAIs  */
	if (codec_dai->driver->ops->hw_free)
		codec_dai->driver->ops->hw_free(substream, codec_dai);

	if (cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

	mutex_unlock(&rtd->pcm_mutex);
	return 0;
}

static int soc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	if (codec_dai->driver->ops->trigger) {
		ret = codec_dai->driver->ops->trigger(substream, cmd, codec_dai);
		if (ret < 0)
			return ret;
	}

	if (platform->driver->ops && platform->driver->ops->trigger) {
		ret = platform->driver->ops->trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (cpu_dai->driver->ops->trigger) {
		ret = cpu_dai->driver->ops->trigger(substream, cmd, cpu_dai);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/*
 * soc level wrapper for pointer callback
 * If cpu_dai, codec_dai, platform driver has the delay callback, than
 * the runtime->delay will be updated accordingly.
 */
static snd_pcm_uframes_t soc_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t offset = 0;
	snd_pcm_sframes_t delay = 0;

	if (platform->driver->ops && platform->driver->ops->pointer)
		offset = platform->driver->ops->pointer(substream);

	if (cpu_dai->driver->ops->delay)
		delay += cpu_dai->driver->ops->delay(substream, cpu_dai);

	if (codec_dai->driver->ops->delay)
		delay += codec_dai->driver->ops->delay(substream, codec_dai);

	if (platform->driver->delay)
		delay += platform->driver->delay(substream, codec_dai);

	runtime->delay = delay;

	return offset;
}

//Planet 20120615 Audio Driver Improvement Start
#if 0
/* ASoC PCM operations */
static struct snd_pcm_ops soc_pcm_ops = {
	.open		= soc_pcm_open,
	.close		= soc_pcm_close,
	.hw_params	= soc_pcm_hw_params,
	.hw_free	= soc_pcm_hw_free,
	.prepare	= soc_pcm_prepare,
	.trigger	= soc_pcm_trigger,
	.pointer	= soc_pcm_pointer,
};
#endif
//Planet 20120615 Audio Driver Improvement End

/* create a new pcm */
int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_pcm *pcm;
	char new_name[64];
	int ret = 0, playback = 0, capture = 0;
	struct snd_pcm_ops *soc_pcm_ops;	//Planet 20120615 Audio Driver Improvement

	/* check client and interface hw capabilities */
	snprintf(new_name, sizeof(new_name), "%s %s-%d",
			rtd->dai_link->stream_name, codec_dai->name, num);

	if (codec_dai->driver->playback.channels_min)
		playback = 1;
	if (codec_dai->driver->capture.channels_min)
		capture = 1;

	dev_dbg(rtd->card->dev, "registered pcm #%d %s\n",num,new_name);
	ret = snd_pcm_new(rtd->card->snd_card, new_name,
			num, playback, capture, &pcm);
	if (ret < 0) {
		printk(KERN_ERR "asoc: can't create pcm for codec %s\n", codec->name);
		return ret;
	}

	/* DAPM dai link stream work */
	INIT_DELAYED_WORK(&rtd->delayed_work, close_delayed_work);

	rtd->pcm = pcm;
	pcm->private_data = rtd;

//Planet 20120615 Audio Driver Improvement Start
#if(1) 
	soc_pcm_ops = kmalloc(sizeof(struct snd_pcm_ops), GFP_KERNEL);
	soc_pcm_ops->open		= soc_pcm_open;
	soc_pcm_ops->close		= soc_pcm_close;
	soc_pcm_ops->hw_params	= soc_pcm_hw_params;
	soc_pcm_ops->hw_free	= soc_pcm_hw_free;
	soc_pcm_ops->prepare	= soc_pcm_prepare;
	soc_pcm_ops->trigger	= soc_pcm_trigger;
	if (platform->driver->ops) {
    	soc_pcm_ops->mmap = platform->driver->ops->mmap;
    	soc_pcm_ops->pointer = platform->driver->ops->pointer;
    	soc_pcm_ops->ioctl = platform->driver->ops->ioctl;
    	soc_pcm_ops->copy = platform->driver->ops->copy;
    	soc_pcm_ops->silence = platform->driver->ops->silence;
    	soc_pcm_ops->ack = platform->driver->ops->ack;
    	soc_pcm_ops->page = platform->driver->ops->page;
    }
#else
	if (platform->driver->ops) {
		soc_pcm_ops.mmap = platform->driver->ops->mmap;
		soc_pcm_ops.pointer = platform->driver->ops->pointer;
		soc_pcm_ops.ioctl = platform->driver->ops->ioctl;
		soc_pcm_ops.copy = platform->driver->ops->copy;
		soc_pcm_ops.silence = platform->driver->ops->silence;
		soc_pcm_ops.ack = platform->driver->ops->ack;
		soc_pcm_ops.page = platform->driver->ops->page;
	}
#endif
//Planet 20120615 Audio Driver Improvement End

//Planet 20120615 Audio Driver Improvement Start
#if(1) 
	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, soc_pcm_ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, soc_pcm_ops);
#else
	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &soc_pcm_ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &soc_pcm_ops);
#endif
//Planet 20120615 Audio Driver Improvement End

	if (platform->driver->pcm_new) {
		ret = platform->driver->pcm_new(rtd);
		if (ret < 0) {
			pr_err("asoc: platform pcm constructor failed\n");
			return ret;
		}
	}

	pcm->private_free = platform->driver->pcm_free;
	printk(KERN_INFO "asoc: %s <-> %s mapping ok\n", codec_dai->name,
		cpu_dai->name);
	return ret;
}
