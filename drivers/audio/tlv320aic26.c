/****************************************************************************
 * drivers/audio/tlv320aic26.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* NuttX audio lower-half driver for the Texas Instruments TLV320AIC26 stereo
 * audio codec.  Handles register control over SPI, PLL
 * clock generation and the configure/start/stop/volume/mute sequences, and
 * streams audio over an I2S data bus.  By default the SoC is the I2S master
 * (BCLK/WS/MCLK) and the codec is the slave.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/param.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <debug.h>
#include <fcntl.h>
#include <pthread.h>
#include <mqueue.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/clock.h>
#include <nuttx/queue.h>
#include <nuttx/mqueue.h>
#include <nuttx/spi/spi.h>
#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/audio/tlv320aic26.h>

#include "tlv320aic26.h"

#if defined(CONFIG_AUDIO) && defined(CONFIG_AUDIO_TLV320AIC26)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TLV320AIC26_INFLIGHT
#  define CONFIG_TLV320AIC26_INFLIGHT      2
#endif

#ifndef CONFIG_TLV320AIC26_MSG_PRIO
#  define CONFIG_TLV320AIC26_MSG_PRIO      1
#endif

#ifndef CONFIG_TLV320AIC26_WORKER_STACKSIZE
#  define CONFIG_TLV320AIC26_WORKER_STACKSIZE  768
#endif

#ifndef CONFIG_TLV320AIC26_SPI_FREQUENCY
#  define CONFIG_TLV320AIC26_SPI_FREQUENCY 12000000
#endif

#ifndef CONFIG_TLV320AIC26_BUFFER_SIZE
#  define CONFIG_TLV320AIC26_BUFFER_SIZE   4096
#endif

#ifndef CONFIG_TLV320AIC26_NUM_BUFFERS
#  define CONFIG_TLV320AIC26_NUM_BUFFERS   4
#endif

/* Codec playback/capture routes. */

#define AIC26_ROUTE_PLAYBACK          0
#define AIC26_ROUTE_CAPTURE           1
#define AIC26_ROUTE_PLAYBACK_CAPTURE  2

/* FIELD_PREP(mask, val): pack val into the contiguous bit-field described by
 * mask (shift = number of trailing zeros in mask).
 */

#define FIELD_PREP(mask, val) \
  (((uint16_t)((uint16_t)(val) << __builtin_ctz(mask))) & (uint16_t)(mask))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic26_pll_cfg
{
  uint16_t d;
  uint8_t  p;
  uint8_t  j;
  uint8_t  q;
  uint8_t  fs_div;
  bool     pll_en;
  bool     fsref_44k1;
};

struct tlv320aic26_dev_s
{
  struct audio_lowerhalf_s dev;        /* AIC26 audio lower half (this dev)  */
  FAR struct spi_dev_s   *spi;         /* SPI control bus                    */
  uint32_t                devid;       /* SPI device id (chip select)        */
  FAR struct i2s_dev_s   *i2s;         /* I2S data bus                       */
  uint32_t                mclk_freq;   /* Codec MCLK frequency (Hz)          */
  struct dq_queue_s       pendq;       /* Pending buffers to be sent         */
  struct dq_queue_s       doneq;       /* Completed buffers to be returned   */
  struct file             mq;          /* Worker-thread message queue        */
  char                    mqname[16];  /* Message queue name                 */
  pthread_t               threadid;    /* Worker thread ID                   */
  mutex_t                 pendlock;    /* Protects pendq                     */
  int            	  result;      /* Result of the last transfer        */
  uint16_t                samprate;    /* Configured sample rate             */
  uint8_t                 nchannels;   /* Number of channels                 */
  uint8_t                 bpsamp;      /* Bits per sample                    */
  uint8_t                 route;       /* AIC26_ROUTE_*                      */
  uint8_t        	  inflight;    /* Audio buffers in-flight            */
  bool                    master;      /* Codec drives BCLK/WS (SLVMS=1)     */
  bool                    running;     /* Worker thread is running           */
  bool                    paused;      /* Playback is paused                 */
  bool                    terminating; /* Stop requested                     */
  bool                    reserved;    /* Device is reserved                 */
  bool                    configured;  /* Codec has been configured          */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  aic26_reg_write(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                            uint8_t addr, uint16_t val);
static int  aic26_reg_read(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                           uint8_t addr, FAR uint16_t *val);
static int  aic26_reg_update(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                             uint8_t addr, uint16_t mask, uint16_t val);
static int  aic26_calc_pll(uint32_t mclk, uint32_t fs,
                           FAR struct aic26_pll_cfg *out);
static int  aic26_set_pll(FAR struct tlv320aic26_dev_s *priv,
                          FAR const struct aic26_pll_cfg *pll);
static int  aic26_adc_setup(FAR struct tlv320aic26_dev_s *priv);
static int  aic26_do_configure(FAR struct tlv320aic26_dev_s *priv);
static void aic26_start_output(FAR struct tlv320aic26_dev_s *priv);
static void aic26_stop_output(FAR struct tlv320aic26_dev_s *priv);
static int  aic26_set_volume(FAR struct tlv320aic26_dev_s *priv, uint16_t vol);
static int  aic26_set_mute(FAR struct tlv320aic26_dev_s *priv, bool mute);

/* Audio lower-half methods */

static int  aic26_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                          FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  aic26_configure(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session,
                            FAR const struct audio_caps_s *caps);
#else
static int  aic26_configure(FAR struct audio_lowerhalf_s *dev,
                            FAR const struct audio_caps_s *caps);
#endif
static int  aic26_shutdown(FAR struct audio_lowerhalf_s *dev);
static void aic26_senddone(FAR struct i2s_dev_s *i2s,
                           FAR struct ap_buffer_s *apb, FAR void *arg,
                           int result);
static void aic26_returnbuffers(FAR struct tlv320aic26_dev_s *priv);
static int  aic26_sendbuffer(FAR struct tlv320aic26_dev_s *priv);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  aic26_start(FAR struct audio_lowerhalf_s *dev, FAR void *session);
#else
static int  aic26_start(FAR struct audio_lowerhalf_s *dev);
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  aic26_stop(FAR struct audio_lowerhalf_s *dev, FAR void *session);
#else
static int  aic26_stop(FAR struct audio_lowerhalf_s *dev);
#endif
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  aic26_pause(FAR struct audio_lowerhalf_s *dev, FAR void *session);
static int  aic26_resume(FAR struct audio_lowerhalf_s *dev, FAR void *session);
#else
static int  aic26_pause(FAR struct audio_lowerhalf_s *dev);
static int  aic26_resume(FAR struct audio_lowerhalf_s *dev);
#endif
#endif
static int  aic26_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb);
static int  aic26_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                               FAR struct ap_buffer_s *apb);
static int  aic26_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                        unsigned long arg);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  aic26_reserve(FAR struct audio_lowerhalf_s *dev,
                          FAR void **session);
static int  aic26_release(FAR struct audio_lowerhalf_s *dev,
                          FAR void *session);
#else
static int  aic26_reserve(FAR struct audio_lowerhalf_s *dev);
static int  aic26_release(FAR struct audio_lowerhalf_s *dev);
#endif
static FAR void *aic26_workerthread(pthread_addr_t pvarg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_aic26_audioops =
{
  aic26_getcaps,       /* getcaps        */
  aic26_configure,     /* configure      */
  aic26_shutdown,      /* shutdown       */
  aic26_start,         /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  aic26_stop,          /* stop           */
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  aic26_pause,         /* pause          */
  aic26_resume,        /* resume         */
#endif
  NULL,                /* allocbuffer    */
  NULL,                /* freebuffer     */
  aic26_enqueuebuffer, /* enqueue_buffer */
  aic26_cancelbuffer,  /* cancel_buffer  */
  aic26_ioctl,         /* ioctl          */
  NULL,                /* read           */
  NULL,                /* write          */
  aic26_reserve,       /* reserve        */
  aic26_release        /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: aic26_lock / aic26_unlock
 *
 * Description:
 *   Claim/release the SPI bus with the codec's control-port settings.
 *
 ****************************************************************************/

static void aic26_lock(FAR struct tlv320aic26_dev_s *priv)
{
  SPI_LOCK(priv->spi, true);
  SPI_SETFREQUENCY(priv->spi, CONFIG_TLV320AIC26_SPI_FREQUENCY);
  SPI_SETMODE(priv->spi, SPIDEV_MODE1);     /* CPOL=0 CPHA=1, MSB first */
  SPI_SETBITS(priv->spi, 8);
}

static void aic26_unlock(FAR struct tlv320aic26_dev_s *priv)
{
  SPI_LOCK(priv->spi, false);
}

/****************************************************************************
 * Name: aic26_reg_write
 *
 * Description:
 *   Write a 16-bit codec register: a 16-bit command word (R/W=0, page, addr)
 *   followed by the 16-bit value, MSB first.
 *
 ****************************************************************************/

static int aic26_reg_write(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                           uint8_t addr, uint16_t val)
{
  uint16_t cmd;
  uint8_t buf[4];

  cmd = FIELD_PREP(AIC26_CMD_PAGE, page) | FIELD_PREP(AIC26_CMD_ADDR, addr);

  buf[0] = (uint8_t)(cmd >> 8);
  buf[1] = (uint8_t)(cmd & 0xff);
  buf[2] = (uint8_t)(val >> 8);
  buf[3] = (uint8_t)(val & 0xff);

  aic26_lock(priv);
  SPI_SELECT(priv->spi, priv->devid, true);
  SPI_SNDBLOCK(priv->spi, buf, sizeof(buf));
  SPI_SELECT(priv->spi, priv->devid, false);
  aic26_unlock(priv);

  audinfo("write p%u:0x%02x <- 0x%04x\n", page, addr, val);
  return OK;
}

/****************************************************************************
 * Name: aic26_reg_read
 ****************************************************************************/

static int aic26_reg_read(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                          uint8_t addr, FAR uint16_t *val)
{
  uint16_t cmd;
  uint8_t txbuf[4] =
  {
    0
  };

  uint8_t rxbuf[4] =
  {
    0
  };

  cmd = AIC26_CMD_READ | FIELD_PREP(AIC26_CMD_PAGE, page) |
        FIELD_PREP(AIC26_CMD_ADDR, addr);

  txbuf[0] = (uint8_t)(cmd >> 8);
  txbuf[1] = (uint8_t)(cmd & 0xff);

  aic26_lock(priv);
  SPI_SELECT(priv->spi, priv->devid, true);
  SPI_EXCHANGE(priv->spi, txbuf, rxbuf, sizeof(txbuf));
  SPI_SELECT(priv->spi, priv->devid, false);
  aic26_unlock(priv);

  *val = ((uint16_t)rxbuf[2] << 8) | rxbuf[3];
  return OK;
}

/****************************************************************************
 * Name: aic26_reg_update
 ****************************************************************************/

static int aic26_reg_update(FAR struct tlv320aic26_dev_s *priv, uint8_t page,
                            uint8_t addr, uint16_t mask, uint16_t val)
{
  uint16_t reg;
  int ret;

  ret = aic26_reg_read(priv, page, addr, &reg);
  if (ret < 0)
    {
      return ret;
    }

  return aic26_reg_write(priv, page, addr, (reg & ~mask) | (val & mask));
}

/****************************************************************************
 * Name: aic26_calc_pll
 *
 * Description:
 *   Compute the codec PLL configuration for the given MCLK and sample rate
 *   Fs:
 *     - PLL bypass: Fsref = MCLK / (128 * Q), Q = 2..17;
 *     - PLL on:     Fsref = MCLK * K / (2048 * P), K = J.D, subject to the VCO
 *       and J/D range constraints.
 *   Fs = Fsref / N with N (fs_div) one of 1, 1.5, 2, 3, 4, 5, 5.5, 6.
 *
 ****************************************************************************/

static int aic26_calc_pll(uint32_t mclk, uint32_t fs,
                          FAR struct aic26_pll_cfg *out)
{
  static const uint32_t fsref_tbl[] =
  {
    48000, 44100
  };

  uint64_t num;
  uint64_t k10k;
  uint64_t vco_hz;
  uint32_t fsref;
  uint32_t mclk_p;
  uint32_t div_x2;
  uint32_t q;
  uint32_t j;
  uint32_t d;
  int8_t   fs_div;
  int      fi;
  uint8_t  p;

  memset(out, 0, sizeof(*out));

  if (fs == 0 || mclk == 0)
    {
      return -EINVAL;
    }

  for (fi = 0; fi < (int)(sizeof(fsref_tbl) / sizeof(fsref_tbl[0])); fi++)
    {
      fsref = fsref_tbl[fi];

      if ((fsref * 2) % fs != 0)
        {
          continue;
        }

      div_x2 = (fsref * 2) / fs;

      switch (div_x2)
        {
          case 2:
            fs_div = AIC26_FS_DIV_1;
            break;
          case 3:
            fs_div = AIC26_FS_DIV_1_5;
            break;
          case 4:
            fs_div = AIC26_FS_DIV_2;
            break;
          case 6:
            fs_div = AIC26_FS_DIV_3;
            break;
          case 8:
            fs_div = AIC26_FS_DIV_4;
            break;
          case 10:
            fs_div = AIC26_FS_DIV_5;
            break;
          case 11:
            fs_div = AIC26_FS_DIV_5_5;
            break;
          case 12:
            fs_div = AIC26_FS_DIV_6;
            break;
          default:
            continue;
        }

      out->fs_div     = fs_div;
      out->fsref_44k1 = (fi == 1);

      /* PLL bypass: Fsref = MCLK / (128 * Q), Q = 2..17. */

      if ((mclk % (128U * fsref)) == 0)
        {
          q = mclk / (128U * fsref);

          if (q >= 2 && q <= 17)
            {
              out->pll_en = false;
              out->q      = q;
              return 0;
            }
        }

      /* PLL enabled: search P for a valid J.D / VCO solution. */

      for (p = 1; p <= 8; p++)
        {
          mclk_p = mclk / p;

          if (mclk_p < 2000000 || mclk_p > 20000000)
            {
              continue;
            }

          num  = (uint64_t)fsref * 2048 * p * 10000ULL;
          k10k = num / mclk;
          j    = (uint32_t)(k10k / 10000);
          d    = (uint32_t)(k10k % 10000);

          if (d != 0)
            {
              if (j < 4 || j > 11 || mclk_p < 10000000)
                {
                  continue;
                }
            }
          else
            {
              if (j < 4 || j > 55)
                {
                  continue;
                }
            }

          if ((uint64_t)mclk * k10k != num)
            {
              continue;
            }

          vco_hz = ((uint64_t)mclk * k10k) / ((uint64_t)p * 10000);

          if (vco_hz < 80000000ULL || vco_hz > 110000000ULL)
            {
              continue;
            }

          out->pll_en = true;
          out->p      = p;
          out->j      = j;
          out->d      = d;
          return 0;
        }
    }

  auderr("ERROR: No clock config for MCLK=%" PRIu32 " Fs=%" PRIu32 "\n",
         mclk, fs);
  return -EINVAL;
}

/****************************************************************************
 * Name: aic26_set_pll
 ****************************************************************************/

static int aic26_set_pll(FAR struct tlv320aic26_dev_s *priv,
                         FAR const struct aic26_pll_cfg *pll)
{
  uint16_t pll1;
  uint16_t pll2;
  uint8_t  p_enc;
  uint8_t  q_enc;
  int ret;

  if (pll->pll_en)
    {
      p_enc = (pll->p == 8) ? 0 : pll->p;

      pll1 = AIC26_PLLSEL | FIELD_PREP(AIC26_PVAL, p_enc) |
             FIELD_PREP(AIC26_JVAL, pll->j);
      pll2 = FIELD_PREP(AIC26_DVAL, pll->d);
    }
  else
    {
      if (pll->q == 16)
        {
          q_enc = 0;
        }
      else if (pll->q == 17)
        {
          q_enc = 1;
        }
      else
        {
          q_enc = pll->q;
        }

      pll1 = FIELD_PREP(AIC26_QVAL, q_enc);
      pll2 = 0;
    }

  ret = aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_PLL2, pll2);
  if (ret < 0)
    {
      return ret;
    }

  return aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_PLL1, pll1);
}

/****************************************************************************
 * Name: aic26_adc_setup
 ****************************************************************************/

static int aic26_adc_setup(FAR struct tlv320aic26_dev_s *priv)
{
  uint16_t reg;
  int ret;

  reg = FIELD_PREP(AIC26_AGCTG, AIC26_AGCTG_M12) |
        FIELD_PREP(AIC26_AGCTC, AIC26_AGCTC_8_100) |
        AIC26_AGCEN;

  ret = aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_ADC_GAIN, reg);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_AUDIO_CTL3,
                         AIC26_AGCNL,
                         FIELD_PREP(AIC26_AGCNL, AIC26_AGCNL_M60));
  if (ret < 0)
    {
      return ret;
    }

  return aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_POWER_CTL,
                          AIC26_PWDNC | AIC26_ADPWDN, 0);
}

/****************************************************************************
 * Name: aic26_do_configure
 *
 * Description:
 *   Program the codec for the current sample rate / word size / channel
 *   count / route.  Data format is I2S.
 *
 ****************************************************************************/

static int aic26_do_configure(FAR struct tlv320aic26_dev_s *priv)
{
  struct aic26_pll_cfg pll;
  bool need_dac;
  bool need_adc;
  uint16_t reg;
  uint8_t wlen;
  uint32_t mclk;
  uint32_t q;
  int ret;

  need_dac = (priv->route != AIC26_ROUTE_CAPTURE);
  need_adc = (priv->route != AIC26_ROUTE_PLAYBACK);

  switch (priv->bpsamp)
    {
      case 16:
        wlen = AIC26_WLEN_16;
        break;
      case 20:
        wlen = AIC26_WLEN_20;
        break;
      case 24:
        wlen = AIC26_WLEN_24;
        break;
      case 32:
        wlen = AIC26_WLEN_32;
        break;
      default:
        return -EINVAL;
    }

  /* Use the live MCLK the I2S master drives; fall back to the board value. */

  mclk = priv->mclk_freq;
  if (priv->i2s != NULL)
    {
      q = I2S_GETMCLKFREQUENCY(priv->i2s);
      if (q >= 1000000 && q <= 60000000)
        {
          mclk = q;
        }
    }

  ret = aic26_calc_pll(mclk, priv->samprate, &pll);
  if (ret < 0)
    {
      return ret;
    }

  priv->configured = false;

  ret = aic26_reg_write(priv, AIC26_PAGE1, AIC26_P1_RESET, AIC26_RESET_VALUE);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic26_reg_write(priv, AIC26_PAGE1, AIC26_P1_REFERENCE,
                        AIC26_VREFM | AIC26_IREFV);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic26_set_pll(priv, &pll);
  if (ret < 0)
    {
      return ret;
    }

  /* Audio Control 1: word length + I2S format + DAC/ADC Fs dividers. */

  reg = FIELD_PREP(AIC26_WLEN, wlen) |
        FIELD_PREP(AIC26_DATFM, AIC26_DATFM_I2S);

  if (need_dac)
    {
      reg |= FIELD_PREP(AIC26_DACFS, pll.fs_div);
    }

  if (need_adc)
    {
      reg |= FIELD_PREP(AIC26_ADCHPF, AIC26_HPF_0045FS) |
             FIELD_PREP(AIC26_ADCIN, AIC26_ADCIN_MIC) |
             FIELD_PREP(AIC26_ADCFS, pll.fs_div);
    }

  ret = aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_AUDIO_CTL1, reg);
  if (ret < 0)
    {
      return ret;
    }

  /* Audio Control 3: Fsref selection + master/slave (SLVMS).
   * Exactly one side drives BCLK/WS: SLVMS = 0 makes the codec a slave (SoC
   * master, the default); priv->master sets SLVMS = 1 so the codec is master.
   */

  reg = 0;
  if (pll.fsref_44k1)
    {
      reg |= AIC26_REFFS;
    }

  if (priv->master)
    {
      reg |= AIC26_SLVMS;
    }

  ret = aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_AUDIO_CTL3, reg);
  if (ret < 0)
    {
      return ret;
    }

  if (need_adc)
    {
      ret = aic26_adc_setup(priv);
      if (ret < 0)
        {
          return ret;
        }
    }

  priv->configured = true;
  audinfo("Configured: Fs=%u ws=%u ch=%u\n",
          priv->samprate, priv->bpsamp, priv->nchannels);
  return OK;
}

/****************************************************************************
 * Name: aic26_start_output / aic26_stop_output
 ****************************************************************************/

static void aic26_start_output(FAR struct tlv320aic26_dev_s *priv)
{
  uint16_t pwr_val;

  if (!priv->configured || priv->route == AIC26_ROUTE_CAPTURE)
    {
      return;
    }

  pwr_val = AIC26_DAODRC;
  if (priv->route != AIC26_ROUTE_PLAYBACK_CAPTURE)
    {
      pwr_val |= AIC26_ADPWDN;
    }

  if (aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_POWER_CTL,
                       AIC26_PWDNC | AIC26_DAPWDN | AIC26_DAODRC |
                       AIC26_ADPWDN, pwr_val) < 0)
    {
      return;
    }

  /* DAC gain is attenuation-only (0 dB..-63 dB, 0.5 dB steps), so writing 0
   * clears both mutes (DALMU/DARMU) and attenuation (DALVL/DARVL) for maximum
   * output.
   */

  aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_DAC_GAIN, 0);
}

static void aic26_stop_output(FAR struct tlv320aic26_dev_s *priv)
{
  if (!priv->configured)
    {
      return;
    }

  aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_DAC_GAIN,
                   AIC26_DALMU | AIC26_DARMU, AIC26_DALMU | AIC26_DARMU);
  aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_POWER_CTL,
                   AIC26_DAPWDN, AIC26_DAPWDN);
}

/****************************************************************************
 * Name: aic26_set_volume
 *
 * Description:
 *   Map a NuttX volume {0..1000} to the codec DAC attenuation (0 dB..-63 dB,
 *   0.5 dB steps -> 0x00..0x7F) on both DAC channels.
 *
 ****************************************************************************/

static int aic26_set_volume(FAR struct tlv320aic26_dev_s *priv, uint16_t vol)
{
  uint16_t reg;
  uint8_t atten;
  int ret;

  /* vol {0..1000}: 1000 -> 0 dB (atten 0), 0 -> -63 dB (atten 0x7e). */

  atten = (uint8_t)(((1000u - MIN(vol, 1000u)) * 126u) / 1000u);
  if (atten > 0x7f)
    {
      atten = 0x7f;
    }

  ret = aic26_reg_read(priv, AIC26_PAGE2, AIC26_P2_DAC_GAIN, &reg);
  if (ret < 0)
    {
      return ret;
    }

  reg = (reg & ~(AIC26_DALVL | AIC26_DARVL)) |
        FIELD_PREP(AIC26_DALVL, atten) | FIELD_PREP(AIC26_DARVL, atten);

  return aic26_reg_write(priv, AIC26_PAGE2, AIC26_P2_DAC_GAIN, reg);
}

/****************************************************************************
 * Name: aic26_set_mute
 ****************************************************************************/

static int aic26_set_mute(FAR struct tlv320aic26_dev_s *priv, bool mute)
{
  return aic26_reg_update(priv, AIC26_PAGE2, AIC26_P2_DAC_GAIN,
                          AIC26_DALMU | AIC26_DARMU,
                          mute ? (AIC26_DALMU | AIC26_DARMU) : 0);
}

/****************************************************************************
 * Name: aic26_getcaps
 ****************************************************************************/

static int aic26_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                         FAR struct audio_caps_s *caps)
{
  DEBUGASSERT(caps && caps->ac_len >= sizeof(struct audio_caps_s));
  UNUSED(type);

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_channels = 2;
        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_INPUT |
                                       AUDIO_TYPE_FEATURE;
              break;
            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        /* Playback (DAC) and capture (mono ADC) share the same serial-audio
         * format and Fs range, so they report identical rate capabilities.
         */

        caps->ac_channels = 2;
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] =
              AUDIO_SAMP_RATE_8K  | AUDIO_SAMP_RATE_11K |
              AUDIO_SAMP_RATE_16K | AUDIO_SAMP_RATE_22K |
              AUDIO_SAMP_RATE_24K | AUDIO_SAMP_RATE_32K |
              AUDIO_SAMP_RATE_44K | AUDIO_SAMP_RATE_48K;
            caps->ac_controls.b[2] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_FEATURE:
        if (caps->ac_subtype == AUDIO_FU_UNDEF)
          {
            caps->ac_controls.b[0] = AUDIO_FU_VOLUME | AUDIO_FU_MUTE;
          }
        break;

      default:
        caps->ac_subtype  = 0;
        caps->ac_channels = 0;
        break;
    }

  return caps->ac_len;
}

/****************************************************************************
 * Name: aic26_configure
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_configure(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session,
                           FAR const struct audio_caps_s *caps)
#else
static int aic26_configure(FAR struct audio_lowerhalf_s *dev,
                           FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  int ret = OK;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif
  DEBUGASSERT(priv != NULL && caps != NULL);

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_FEATURE:
        switch (caps->ac_format.hw)
          {
#ifndef CONFIG_AUDIO_EXCLUDE_VOLUME
            case AUDIO_FU_VOLUME:
              ret = aic26_set_volume(priv, caps->ac_controls.hw[0]);
              break;

            case AUDIO_FU_MUTE:
              ret = aic26_set_mute(priv, caps->ac_controls.b[0] != 0);
              break;
#endif
            default:
              ret = -ENOTTY;
              break;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
        if (caps->ac_channels != 1 && caps->ac_channels != 2)
          {
            return -ERANGE;
          }

        priv->samprate  = caps->ac_controls.hw[0];
        priv->nchannels = caps->ac_channels;
        priv->bpsamp    = caps->ac_controls.b[2];
#ifdef CONFIG_AUDIO_TLV320AIC26_CAPTURE
        /* Bring up the ADC alongside the DAC for full-duplex, and match the
         * SoC RX side too.
         */

        priv->route     = AIC26_ROUTE_PLAYBACK_CAPTURE;
        I2S_RXCHANNELS(priv->i2s, priv->nchannels);
        I2S_RXDATAWIDTH(priv->i2s, priv->bpsamp);
        I2S_RXSAMPLERATE(priv->i2s, priv->samprate);
#else
        priv->route     = AIC26_ROUTE_PLAYBACK;
#endif

        /* Configure the SoC I2S master to match. */

        I2S_TXCHANNELS(priv->i2s, priv->nchannels);
        I2S_TXDATAWIDTH(priv->i2s, priv->bpsamp);
        I2S_TXSAMPLERATE(priv->i2s, priv->samprate);

        /* Then program the codec (PLL + framing). */

        ret = aic26_do_configure(priv);
        break;

      case AUDIO_TYPE_INPUT:
        /* Capture / record path.  The AIC26 has a mono audio ADC.  Bring up
         * the ADC only (route == CAPTURE, no DAC) and point the SoC RX side
         * at the matching rate/width/channels.
         */

        if (caps->ac_channels != 1 && caps->ac_channels != 2)
          {
            return -ERANGE;
          }

        priv->samprate  = caps->ac_controls.hw[0];
        priv->nchannels = caps->ac_channels;
        priv->bpsamp    = caps->ac_controls.b[2];
        priv->route     = AIC26_ROUTE_CAPTURE;

        /* Configure the SoC I2S RX to match. */

        I2S_RXCHANNELS(priv->i2s, priv->nchannels);
        I2S_RXDATAWIDTH(priv->i2s, priv->bpsamp);
        I2S_RXSAMPLERATE(priv->i2s, priv->samprate);

        /* Program the codec (PLL + framing + ADC). */

        ret = aic26_do_configure(priv);
        break;

      default:
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: aic26_shutdown
 ****************************************************************************/

static int aic26_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;

  DEBUGASSERT(priv);
  aic26_stop_output(priv);
  aic26_reg_write(priv, AIC26_PAGE1, AIC26_P1_RESET, AIC26_RESET_VALUE);
  priv->configured = false;
  return OK;
}

/****************************************************************************
 * Name: aic26_senddone
 *
 * Description:
 *   I2S transfer-complete callback: queue the finished buffer and wake the
 *   worker thread.
 *
 ****************************************************************************/

static void aic26_senddone(FAR struct i2s_dev_s *i2s,
                           FAR struct ap_buffer_s *apb, FAR void *arg,
                           int result)
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)arg;
  struct audio_msg_s msg;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(i2s && priv && priv->running && apb);
  UNUSED(i2s);

  flags = enter_critical_section();
  dq_addlast((FAR dq_entry_t *)apb, &priv->doneq);
  DEBUGASSERT(priv->inflight > 0);
  priv->inflight--;
  priv->result = result;
  leave_critical_section(flags);

  msg.msg_id = AUDIO_MSG_COMPLETE;
  ret = file_mq_send(&priv->mq, (FAR const char *)&msg, sizeof(msg),
                     CONFIG_TLV320AIC26_MSG_PRIO);
  if (ret < 0)
    {
      auderr("ERROR: file_mq_send failed: %d\n", ret);
    }
}

/****************************************************************************
 * Name: aic26_returnbuffers
 ****************************************************************************/

static void aic26_returnbuffers(FAR struct tlv320aic26_dev_s *priv)
{
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;

  flags = enter_critical_section();
  while (dq_peek(&priv->doneq) != NULL)
    {
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->doneq);
      leave_critical_section(flags);

      if ((apb->flags & AUDIO_APB_FINAL) != 0)
        {
          priv->terminating = true;
        }

      apb_free(apb);

#ifdef CONFIG_AUDIO_MULTI_SESSION
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK, NULL);
#else
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif
      flags = enter_critical_section();
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: aic26_sendbuffer
 ****************************************************************************/

static int aic26_sendbuffer(FAR struct tlv320aic26_dev_s *priv)
{
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  uint32_t timeout;
  int shift;
  int ret = OK;

  ret = nxmutex_lock(&priv->pendlock);
  if (ret < 0)
    {
      return ret;
    }

  while (priv->inflight < CONFIG_TLV320AIC26_INFLIGHT &&
         dq_peek(&priv->pendq) != NULL && !priv->paused)
    {
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);

      flags = enter_critical_section();
      priv->inflight++;
      leave_critical_section(flags);

      shift  = (priv->bpsamp == 8) ? 14 - 3 : 14 - 4;
      shift -= (priv->nchannels > 1) ? 1 : 0;

      if (priv->route == AIC26_ROUTE_CAPTURE)
        {
          /* Capture: the I2S RX fills an empty buffer up to nmaxbytes, so size
           * the timeout from the capacity.
           */

          timeout = MSEC2TICK(((uint32_t)apb->nmaxbytes << shift) /
                              (uint32_t)priv->samprate);

          ret = I2S_RECEIVE(priv->i2s, apb, aic26_senddone, priv, timeout);
          if (ret < 0)
            {
              auderr("ERROR: I2S_RECEIVE failed: %d\n", ret);
              break;
            }
        }
      else
        {
          timeout = MSEC2TICK(((uint32_t)(apb->nbytes - apb->curbyte) << shift) /
                              (uint32_t)priv->samprate);

          ret = I2S_SEND(priv->i2s, apb, aic26_senddone, priv, timeout);
          if (ret < 0)
            {
              auderr("ERROR: I2S_SEND failed: %d\n", ret);
              break;
            }
        }
    }

  nxmutex_unlock(&priv->pendlock);
  return ret;
}

/****************************************************************************
 * Name: aic26_start
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_start(FAR struct audio_lowerhalf_s *dev, FAR void *session)
#else
static int aic26_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  struct sched_param sparam;
  struct mq_attr attr;
  pthread_attr_t tattr;
  FAR void *value;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif

  /* Power up the DAC output before streaming. */

  aic26_start_output(priv);

  snprintf(priv->mqname, sizeof(priv->mqname), "/tmp/%" PRIXPTR,
           (uintptr_t)priv);

  attr.mq_maxmsg  = 16;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  attr.mq_curmsgs = 0;
  attr.mq_flags   = 0;

  ret = file_mq_open(&priv->mq, priv->mqname, O_RDWR | O_CREAT, 0644, &attr);
  if (ret < 0)
    {
      auderr("ERROR: Couldn't allocate message queue\n");
      return ret;
    }

  if (priv->threadid != 0)
    {
      pthread_join(priv->threadid, &value);
    }

  pthread_attr_init(&tattr);
  sparam.sched_priority = sched_get_priority_max(SCHED_FIFO) - 3;
  pthread_attr_setschedparam(&tattr, &sparam);
  pthread_attr_setstacksize(&tattr, CONFIG_TLV320AIC26_WORKER_STACKSIZE);

  ret = pthread_create(&priv->threadid, &tattr, aic26_workerthread,
                       (pthread_addr_t)priv);
  if (ret != OK)
    {
      auderr("ERROR: pthread_create failed: %d\n", ret);
    }
  else
    {
      pthread_setname_np(priv->threadid, "tlv320aic26");
    }

  return ret;
}

/****************************************************************************
 * Name: aic26_stop
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_stop(FAR struct audio_lowerhalf_s *dev, FAR void *session)
#else
static int aic26_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  struct audio_msg_s term_msg;
  FAR void *value;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif

  term_msg.msg_id = AUDIO_MSG_STOP;
  term_msg.u.data = 0;
  file_mq_send(&priv->mq, (FAR const char *)&term_msg, sizeof(term_msg),
               CONFIG_TLV320AIC26_MSG_PRIO);

  pthread_join(priv->threadid, &value);
  priv->threadid = 0;

  aic26_stop_output(priv);
  return OK;
}
#endif

/****************************************************************************
 * Name: aic26_pause / aic26_resume
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_pause(FAR struct audio_lowerhalf_s *dev, FAR void *session)
#else
static int aic26_pause(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif

  if (priv->running && !priv->paused)
    {
      priv->paused = true;
      aic26_set_mute(priv, true);
    }

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_resume(FAR struct audio_lowerhalf_s *dev, FAR void *session)
#else
static int aic26_resume(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif

  if (priv->running && priv->paused)
    {
      priv->paused = false;
      aic26_set_mute(priv, false);
      aic26_sendbuffer(priv);
    }

  return OK;
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: aic26_enqueuebuffer
 ****************************************************************************/

static int aic26_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                               FAR struct ap_buffer_s *apb)
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  struct audio_msg_s msg;
  int ret;

  ret = nxmutex_lock(&priv->pendlock);
  if (ret < 0)
    {
      return ret;
    }

  apb_reference(apb);
  apb->flags |= AUDIO_APB_OUTPUT_ENQUEUED;
  dq_addlast(&apb->dq_entry, &priv->pendq);
  nxmutex_unlock(&priv->pendlock);

  ret = OK;
  if (priv->mq.f_inode != NULL)
    {
      msg.msg_id = AUDIO_MSG_ENQUEUE;
      msg.u.data = 0;

      ret = file_mq_send(&priv->mq, (FAR const char *)&msg, sizeof(msg),
                         CONFIG_TLV320AIC26_MSG_PRIO);
      if (ret < 0)
        {
          auderr("ERROR: file_mq_send failed: %d\n", ret);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: aic26_cancelbuffer
 ****************************************************************************/

static int aic26_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                              FAR struct ap_buffer_s *apb)
{
  UNUSED(dev);
  UNUSED(apb);
  return OK;
}

/****************************************************************************
 * Name: aic26_ioctl
 ****************************************************************************/

static int aic26_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                       unsigned long arg)
{
  int ret = OK;
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
  FAR struct ap_buffer_info_s *bufinfo;
#endif

  UNUSED(dev);

  switch (cmd)
    {
      case AUDIOIOC_HWRESET:
        break;

#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
      case AUDIOIOC_GETBUFFERINFO:
        bufinfo              = (FAR struct ap_buffer_info_s *)arg;
        bufinfo->buffer_size = CONFIG_TLV320AIC26_BUFFER_SIZE;
        bufinfo->nbuffers    = CONFIG_TLV320AIC26_NUM_BUFFERS;
        break;
#else
        UNUSED(arg);
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: aic26_reserve
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_reserve(FAR struct audio_lowerhalf_s *dev,
                         FAR void **session)
#else
static int aic26_reserve(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  int ret;

  ret = nxmutex_lock(&priv->pendlock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->reserved)
    {
      ret = -EBUSY;
    }
  else
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      *session = NULL;
#endif
      priv->inflight    = 0;
      priv->running     = false;
      priv->paused      = false;
      priv->terminating = false;
      priv->reserved    = true;
    }

  nxmutex_unlock(&priv->pendlock);
  return ret;
}

/****************************************************************************
 * Name: aic26_release
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int aic26_release(FAR struct audio_lowerhalf_s *dev,
                         FAR void *session)
#else
static int aic26_release(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)dev;
  FAR void *value;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  UNUSED(session);
#endif

  if (priv->threadid != 0)
    {
      pthread_join(priv->threadid, &value);
      priv->threadid = 0;
    }

  ret = nxmutex_lock(&priv->pendlock);
  if (ret < 0)
    {
      return ret;
    }

  priv->reserved = false;
  nxmutex_unlock(&priv->pendlock);
  return OK;
}

/****************************************************************************
 * Name: aic26_workerthread
 ****************************************************************************/

static FAR void *aic26_workerthread(pthread_addr_t pvarg)
{
  FAR struct tlv320aic26_dev_s *priv = (FAR struct tlv320aic26_dev_s *)pvarg;
  struct audio_msg_s msg;
  unsigned int prio;
  ssize_t msglen;

  priv->terminating = false;
  priv->running     = true;

  while (priv->running || priv->inflight > 0)
    {
      if (priv->terminating && priv->inflight <= 0)
        {
          break;
        }

      aic26_sendbuffer(priv);

      msglen = file_mq_receive(&priv->mq, (FAR char *)&msg, sizeof(msg),
                               &prio);
      if (msglen < (ssize_t)sizeof(struct audio_msg_s))
        {
          continue;
        }

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DATA_REQUEST:
          case AUDIO_MSG_ENQUEUE:
            break;

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
          case AUDIO_MSG_STOP:
            priv->terminating = true;
            break;
#endif

          case AUDIO_MSG_COMPLETE:
            aic26_returnbuffers(priv);
            break;

          default:
            break;
        }
    }

  priv->running = false;

  /* Return any un-returned buffers and notify completion. */

  aic26_returnbuffers(priv);
  file_mq_close(&priv->mq);
  file_mq_unlink(priv->mqname);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tlv320aic26_initialize
 ****************************************************************************/

FAR struct audio_lowerhalf_s *
tlv320aic26_initialize(FAR struct spi_dev_s *spi, uint32_t devid,
                       FAR struct i2s_dev_s *i2s, uint32_t mclk_freq)
{
  FAR struct tlv320aic26_dev_s *priv;
  uint16_t reg;
  int ret;

  DEBUGASSERT(spi != NULL && i2s != NULL);

  priv = kmm_zalloc(sizeof(struct tlv320aic26_dev_s));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->dev.ops   = &g_aic26_audioops;
  priv->spi       = spi;
  priv->devid     = devid;
  priv->i2s       = i2s;
  priv->mclk_freq = mclk_freq;
  priv->samprate  = 48000;
  priv->nchannels = 2;
  priv->bpsamp    = 16;
  priv->route     = AIC26_ROUTE_PLAYBACK;
#ifdef CONFIG_TLV320AIC26_MASTER
  priv->master    = true;   /* codec drives BCLK/WS; SoC must be an I2S slave */
#else
  priv->master    = false;  /* codec is a slave; SoC is the I2S master (default) */
#endif

  nxmutex_init(&priv->pendlock);
  dq_init(&priv->pendq);
  dq_init(&priv->doneq);

  /* Reset the codec and verify it responds. */

  aic26_reg_write(priv, AIC26_PAGE1, AIC26_P1_RESET, AIC26_RESET_VALUE);

  ret = aic26_reg_read(priv, AIC26_PAGE2, AIC26_P2_POWER_CTL, &reg);
  if (ret < 0 || reg == 0x0000 || reg == 0xffff)
    {
      auderr("ERROR: No codec response (reg=0x%04x)\n", reg);
      nxmutex_destroy(&priv->pendlock);
      kmm_free(priv);
      return NULL;
    }

  aic26_reg_write(priv, AIC26_PAGE1, AIC26_P1_REFERENCE,
                  AIC26_VREFM | AIC26_IREFV);

  audinfo("TLV320AIC26 ready (MCLK=%" PRIu32 " Hz)\n", mclk_freq);
  return &priv->dev;
}

#endif /* CONFIG_AUDIO && CONFIG_AUDIO_TLV320AIC26 */
