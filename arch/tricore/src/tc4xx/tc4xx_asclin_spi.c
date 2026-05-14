/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_asclin_spi.c
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


#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include "tricore_internal.h"
#include "tricore_asclin.h"
#include "tc4xx_gpio.h"
#include "hardware/tc4xx_asclin_spi.h"



#ifndef ASCLIN_FLAGS_OMT
#  define ASCLIN_FLAGS_OMT            BIT(11)
#endif

#ifndef ASCLIN_FLAGSCLEAR_OMTC
#  define ASCLIN_FLAGSCLEAR_OMTC      BIT(11)
#endif

#ifndef ASCLIN_FLAGSSET_SWTRGS
#  define ASCLIN_FLAGSSET_SWTRGS      BIT(12)
#endif

#ifndef ASCLIN_FLAGSCLEAR_SWTRGC
#  define ASCLIN_FLAGSCLEAR_SWTRGC    BIT(12)
#endif

#ifndef ASCLIN_FLAGSCLEAR_TWRQC
#  define ASCLIN_FLAGSCLEAR_TWRQC     BIT(13)
#endif

#ifndef ASCLIN_FLAGSCLEAR_TRRQC
#  define ASCLIN_FLAGSCLEAR_TRRQC     BIT(15)
#endif


#ifndef ASCLIN_TXFIFOCON_INW_2
#  define ASCLIN_TXFIFOCON_INW_2    (2u << ASCLIN_TXFIFOCON_INW_SHIFT)
#endif

#ifndef ASCLIN_RXFIFOCON_OUTW_2
#  define ASCLIN_RXFIFOCON_OUTW_2   (2u << ASCLIN_RXFIFOCON_OUTW_SHIFT)
#endif

/* DATCON.TRGSRC encodings */

#define ASCLIN_DATCON_TRGSRC_NONE   (0u << ASCLIN_DATCON_TRGSRC_SHIFT)
#define ASCLIN_DATCON_TRGSRC_SW     (3u << ASCLIN_DATCON_TRGSRC_SHIFT)

/* Default ASCLIN clock frequency */

#ifndef TC4X_ASCLINF_FREQUENCY
#  define TC4X_ASCLINF_FREQUENCY    200000000u
#endif

#ifndef TC4X_ASCLIN_CLK_SRC
#  define TC4X_ASCLIN_CLK_SRC       ASCLIN_CSR_CLKSEL_FASCLINF
#endif

/* Polling timeout */

#define SPI_POLL_TIMEOUT            1000000u


/* Per-bus hardware configuration */

struct tc4xx_spi_config_s
{
  uintptr_t     base;            /* ASCLIN register base address */
  uint32_t      fclk;            /* Input clock frequency (Hz) */
  uint8_t       clk_src;         /* CSR.CLKSEL value */
  uint8_t       bus;             /* SPI bus number (for spi_register) */
  gpio_pinset_t mosi_pin;        /* MOSI (ATX) pin configuration */
  gpio_pinset_t miso_pin;        /* MISO (ARX) pin configuration */
  gpio_pinset_t sclk_pin;        /* SCLK (ASCLKO) pin configuration */
  gpio_pinset_t cs_pin;          /* CS pin (GPIO-controlled) */
  uint8_t       rx_alti;         /* IOCR.ALTI for RX input select */
  bool          loopback;        /* Enable internal loopback for testing */
};

/* Per-bus runtime state */

struct tc4xx_spi_priv_s
{
  struct spi_dev_s              spidev;
  const struct tc4xx_spi_config_s *config;
  mutex_t                       lock;
  uint32_t                      frequency;
  uint32_t                      actual_freq;
  enum spi_mode_e               mode;
  int                           nbits;
  bool                          initialized;
};


static int      tc4xx_spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void     tc4xx_spi_select(FAR struct spi_dev_s *dev,
                                 uint32_t devid, bool selected);
static uint32_t tc4xx_spi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency);
static void     tc4xx_spi_setmode(FAR struct spi_dev_s *dev,
                                  enum spi_mode_e mode);
static void     tc4xx_spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t  tc4xx_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid);
static uint32_t tc4xx_spi_send(FAR struct spi_dev_s *dev, uint32_t wd);
#ifdef CONFIG_SPI_EXCHANGE
static void     tc4xx_spi_exchange(FAR struct spi_dev_s *dev,
                                   FAR const void *txbuffer,
                                   FAR void *rxbuffer, size_t nwords);
#else
static void     tc4xx_spi_sndblock(FAR struct spi_dev_s *dev,
                                   FAR const void *buffer, size_t nwords);
static void     tc4xx_spi_recvblock(FAR struct spi_dev_s *dev,
                                    FAR void *buffer, size_t nwords);
#endif


static const struct spi_ops_s g_asclin_spi_ops =
{
  .lock         = tc4xx_spi_lock,
  .select       = tc4xx_spi_select,
  .setfrequency = tc4xx_spi_setfrequency,
#ifdef CONFIG_SPI_DELAY_CONTROL
  .setdelay     = NULL,
#endif
  .setmode      = tc4xx_spi_setmode,
  .setbits      = tc4xx_spi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures   = NULL,
#endif
  .status       = tc4xx_spi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata      = NULL,
#endif
  .send         = tc4xx_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange     = tc4xx_spi_exchange,
#else
  .sndblock     = tc4xx_spi_sndblock,
  .recvblock    = tc4xx_spi_recvblock,
#endif
#ifdef CONFIG_SPI_TRIGGER
  .trigger      = NULL,
#endif
  .registercallback = NULL,
};


#ifdef CONFIG_TC4XX_ASCLIN3_SPI

static const struct tc4xx_spi_config_s g_spi0_config =
{
  .base      = TRICORE_ASCLIN_BASE(3),
  .fclk      = TC4X_ASCLINF_FREQUENCY,
  .clk_src   = TC4X_ASCLIN_CLK_SRC,
  .bus        = 0,


  .mosi_pin  = AURIX_GPIO(15, 6, GPIO_PERIPH, GPIO_ALT2) | GPIO_PERIPH_OWN_PAD,
  .miso_pin  = AURIX_GPIO(15, 7, GPIO_INPUT,  GPIO_PULL_NONE),
  .sclk_pin  = AURIX_GPIO(15, 8, GPIO_PERIPH, GPIO_ALT6) | GPIO_PERIPH_OWN_PAD,
  .cs_pin    = AURIX_GPIO(14, 7, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH,
  .rx_alti   = ASCLIN_IOCR_ALTI_A,

#ifdef CONFIG_TC4XX_ASCLIN3_SPI_LOOPBACK
  .loopback  = true,
#else
  .loopback  = false,
#endif
};

static struct tc4xx_spi_priv_s g_spi0_priv =
{
  .spidev      = { .ops = &g_asclin_spi_ops },
  .config      = &g_spi0_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .initialized = false,
};

#endif /* CONFIG_TC4XX_ASCLIN3_SPI */


static inline uint32_t spi_getreg(const struct tc4xx_spi_config_s *cfg,
                                  uint32_t offset)
{
  return getreg32(cfg->base + offset);
}

static inline void spi_putreg(const struct tc4xx_spi_config_s *cfg,
                              uint32_t offset, uint32_t value)
{
  putreg32(value, cfg->base + offset);
}

static inline void spi_modreg(const struct tc4xx_spi_config_s *cfg,
                              uint32_t offset,
                              uint32_t setbits, uint32_t clrbits)
{
  modreg32(setbits, setbits | clrbits, cfg->base + offset);
}


static int spi_wait_clk(const struct tc4xx_spi_config_s *cfg, bool on)
{
  uint32_t timeout = SPI_POLL_TIMEOUT;

  while (timeout--)
    {
      uint32_t csr = spi_getreg(cfg, ASCLIN_CSR_OFFSET);
      if (!!(csr & ASCLIN_CSR_CON) == on)
        {
          return OK;
        }
    }

  spierr("ERROR: clock wait timeout (expected %s)\n", on ? "ON" : "OFF");
  return -ETIMEDOUT;
}


static int spi_set_clk(const struct tc4xx_spi_config_s *cfg,
                       uint8_t clksel)
{
  spi_putreg(cfg, ASCLIN_CSR_OFFSET, clksel);
  return spi_wait_clk(cfg, clksel != ASCLIN_CSR_CLKSEL_NONE);
}


static uint32_t spi_compute_baudrate(const struct tc4xx_spi_config_s *cfg,
                                     uint32_t freq)
{
  uint32_t prescaler;
  uint32_t oversampling;
  uint32_t samplepoint;
  uint32_t fpd;
  uint32_t target_fovs;
  uint32_t best_num = 1;
  uint32_t best_den = 1;
  uint32_t best_err = UINT32_MAX;
  uint32_t actual;
  uint32_t bitcon;
  uint32_t brg;


  oversampling = 15u;  /* register value: actual oversampling = value + 1 */
  samplepoint  = 7u;   /* register value: first half = value + 1 = 8 ticks */


  prescaler = 0;

  /* target_fovs = freq * (oversampling + 1) */

  target_fovs = freq * (oversampling + 1);

  /* Try to find a prescaler that works well */

  for (prescaler = 0; prescaler < 4096; prescaler++)
    {
      fpd = cfg->fclk / (prescaler + 1);


      if (fpd == 0)
        {
          continue;
        }

      /* Simple search: try DEN from 1 to 4095, compute NUM */

      uint32_t den;
      uint32_t num;

      for (den = 1; den <= 4095; den++)
        {
          uint64_t num64 = ((uint64_t)target_fovs * den + (fpd / 2)) / fpd;
          if (num64 == 0 || num64 > 4095)
            {
              continue;
            }

          num = (uint32_t)num64;

          /* Compute actual frequency */

          uint64_t fovs = ((uint64_t)fpd * num) / den;
          actual = (uint32_t)(fovs / (oversampling + 1));

          uint32_t err;
          if (actual > freq)
            {
              err = actual - freq;
            }
          else
            {
              err = freq - actual;
            }

          if (err < best_err)
            {
              best_err = err;
              best_num = num;
              best_den = den;

              if (err == 0)
                {
                  goto found;
                }
            }
        }

      /* If we found a good enough match with this prescaler, stop */

      if (best_err < (freq / 1000))
        {
          break;
        }
    }

found:

  /* Program BITCON: prescaler, oversampling, samplepoint, single sample */

  bitcon = (prescaler & 0xFFFu)
         | ((oversampling & 0xFu) << ASCLIN_BITCON_OVERSAMPLING_SHIFT)
         | ((samplepoint & 0xFu) << ASCLIN_BITCON_SAMPLEPOINT_SHIFT);

  spi_putreg(cfg, ASCLIN_BITCON_OFFSET, bitcon);

  /* Program BRG: numerator and denominator */

  brg = (best_den & 0xFFFu)
      | ((best_num & 0xFFFu) << 16u);

  spi_putreg(cfg, ASCLIN_BRG_OFFSET, brg);

  /* Compute actual achieved frequency */

  fpd = cfg->fclk / (prescaler + 1);
  actual = (uint32_t)(((uint64_t)fpd * best_num) /
                      ((uint64_t)best_den * (oversampling + 1)));

  spiinfo("SPI baud: req=%lu actual=%lu pre=%lu num=%lu den=%lu ovs=%lu\n",
          (unsigned long)freq, (unsigned long)actual,
          (unsigned long)prescaler, (unsigned long)best_num,
          (unsigned long)best_den, (unsigned long)(oversampling + 1));

  return actual;
}


static int spi_hw_init(struct tc4xx_spi_priv_s *priv)
{
  const struct tc4xx_spi_config_s *cfg = priv->config;
  uint32_t reg;
  int ret;

  /* Step 1: Enable module clock */

  spi_putreg(cfg, ASCLIN_CLC_OFFSET, 0u);

  uint32_t timeout = SPI_POLL_TIMEOUT;
  while (timeout--)
    {
      if (!(spi_getreg(cfg, ASCLIN_CLC_OFFSET) & ASCLIN_CLC_DISS))
        {
          break;
        }
    }

  if (timeout == 0)
    {
      spierr("ERROR: ASCLIN module clock enable timeout\n");
      return -ETIMEDOUT;
    }

  /* Step 2: Configure GPIO pins */

  if (!cfg->loopback)
    {
      aurix_config_gpio(cfg->mosi_pin);
      aurix_config_gpio(cfg->miso_pin);
      aurix_config_gpio(cfg->sclk_pin);
    }

  /* CS pin: configure as GPIO output, drive inactive (high) */

  aurix_config_gpio(cfg->cs_pin);
  aurix_gpio_write(cfg->cs_pin, true);

  /* Step 3: Disable operating clock for register configuration */

  ret = spi_set_clk(cfg, ASCLIN_CSR_CLKSEL_NONE);
  if (ret < 0)
    {
      spierr("ERROR: clock disable failed\n");
      return ret;
    }


  reg = ASCLIN_FRAMECON_MODE_SPI
      | ASCLIN_FRAMECON_MSB
      | (1u << ASCLIN_FRAMECON_LEAD_SHIFT)
      | (1u << ASCLIN_FRAMECON_STOP_SHIFT)
      | (1u << ASCLIN_FRAMECON_IDLE_SHIFT);

  spi_putreg(cfg, ASCLIN_FRAMECON_OFFSET, reg);

  /* Step 5: Configure bit timing and baud rate */

  priv->actual_freq = spi_compute_baudrate(cfg, priv->frequency);

  /* Step 6: DATCON - data length, no trigger initially */

  reg = ASCLIN_DATCON_DATLEN(priv->nbits);
  spi_putreg(cfg, ASCLIN_DATCON_OFFSET, reg);

  /* Step 7: IOCR - clock polarity, slave select polarity, input select */

  reg = (cfg->rx_alti & ASCLIN_IOCR_ALTI_MASK);

  /* SPOL = 1 means SLSO idle is high (active low CS) */

  reg |= ASCLIN_IOCR_SPOL;

  /* Set CPOL based on current SPI mode */

  if (priv->mode == SPIDEV_MODE2 || priv->mode == SPIDEV_MODE3)
    {
      reg |= ASCLIN_IOCR_CPOL;
    }

  /* Enable loopback if configured */

  if (cfg->loopback)
    {
      reg |= ASCLIN_IOCR_LB;
    }

  spi_putreg(cfg, ASCLIN_IOCR_OFFSET, reg);


  reg = ASCLIN_TXFIFOCON_FLUSH
      | ASCLIN_TXFIFOCON_ENO
      | ASCLIN_TXFIFOCON_FM_COMBINED
      | ((priv->nbits > 8) ? ASCLIN_TXFIFOCON_INW_2
                           : ASCLIN_TXFIFOCON_INW_1);

  spi_putreg(cfg, ASCLIN_TXFIFOCON_OFFSET, reg);

  reg = ASCLIN_RXFIFOCON_FLUSH
      | ASCLIN_RXFIFOCON_ENI
      | ((priv->nbits > 8) ? ASCLIN_RXFIFOCON_OUTW_2
                           : ASCLIN_RXFIFOCON_OUTW_1);

  spi_putreg(cfg, ASCLIN_RXFIFOCON_OFFSET, reg);

  /* Step 9: Clear all flags, disable all flag interrupts */

  spi_putreg(cfg, ASCLIN_FLAGSCLEAR_OFFSET, ASCLIN_FLAGSCLEAR_ALL);
  spi_putreg(cfg, ASCLIN_FLAGSENABLE_OFFSET, 0u);

  /* Step 10: Enable operating clock */

  ret = spi_set_clk(cfg, cfg->clk_src);
  if (ret < 0)
    {
      spierr("ERROR: clock enable failed\n");
      return ret;
    }

  priv->initialized = true;
  spiinfo("ASCLIN SPI bus %d initialized (base=0x%08lx loopback=%d)\n",
          cfg->bus, (unsigned long)cfg->base, cfg->loopback);

  return OK;
}


static uint32_t spi_exchange_word(struct tc4xx_spi_priv_s *priv,
                                  uint32_t txword)
{
  const struct tc4xx_spi_config_s *cfg = priv->config;
  uint32_t rxword;
  uint32_t timeout;

  /* Clear only TC flag. DO NOT flush FIFOs per byte. */

  spi_putreg(cfg, ASCLIN_FLAGSCLEAR_OFFSET, ASCLIN_FLAGSCLEAR_TCC);

  /* Write TX data */

  spi_putreg(cfg, ASCLIN_TXDATA_OFFSET, txword);

  /* Poll for transfer complete (FLAGS.TC = 1) */

  timeout = SPI_POLL_TIMEOUT;
  while (timeout--)
    {
      uint32_t flags = spi_getreg(cfg, ASCLIN_FLAGS_OFFSET);
      if (flags & ASCLIN_FLAGS_TC)
        {
          break;
        }
    }

  if (timeout == 0)
    {
      spierr("ERROR: SPI transfer timeout\n");
      return 0;
    }

  /* Read received data */

  rxword = spi_getreg(cfg, ASCLIN_RXDATA_OFFSET);

  return rxword;
}


static void spi_reconfigure(struct tc4xx_spi_priv_s *priv)
{
  const struct tc4xx_spi_config_s *cfg = priv->config;
  uint32_t iocr;
  uint32_t datcon;
  uint32_t framecon;

  /* Must disable clock to change config registers */

  spi_set_clk(cfg, ASCLIN_CSR_CLKSEL_NONE);

  /* Update IOCR for clock polarity */

  iocr = spi_getreg(cfg, ASCLIN_IOCR_OFFSET);
  iocr &= ~ASCLIN_IOCR_CPOL;

  if (priv->mode == SPIDEV_MODE2 || priv->mode == SPIDEV_MODE3)
    {
      iocr |= ASCLIN_IOCR_CPOL;
    }

  spi_putreg(cfg, ASCLIN_IOCR_OFFSET, iocr);


  framecon = spi_getreg(cfg, ASCLIN_FRAMECON_OFFSET);
  framecon &= ~ASCLIN_FRAMECON_LEAD_MASK;

  if (priv->mode == SPIDEV_MODE1 || priv->mode == SPIDEV_MODE3)
    {
      /* CPHA=1: reduce lead delay to 0 */

      framecon |= (0u << ASCLIN_FRAMECON_LEAD_SHIFT);
    }
  else
    {
      /* CPHA=0: standard lead delay of 1 */

      framecon |= (1u << ASCLIN_FRAMECON_LEAD_SHIFT);
    }

  spi_putreg(cfg, ASCLIN_FRAMECON_OFFSET, framecon);

  /* Update DATCON for data length */

  datcon = ASCLIN_DATCON_DATLEN(priv->nbits);
  spi_putreg(cfg, ASCLIN_DATCON_OFFSET, datcon);

  /* Update baud rate */

  priv->actual_freq = spi_compute_baudrate(cfg, priv->frequency);


  {
    uint32_t txfifo;
    uint32_t rxfifo;

    txfifo = ASCLIN_TXFIFOCON_FLUSH
           | ASCLIN_TXFIFOCON_ENO
           | ASCLIN_TXFIFOCON_FM_COMBINED
           | ((priv->nbits > 8) ? ASCLIN_TXFIFOCON_INW_2
                                : ASCLIN_TXFIFOCON_INW_1);

    spi_putreg(cfg, ASCLIN_TXFIFOCON_OFFSET, txfifo);

    rxfifo = ASCLIN_RXFIFOCON_FLUSH
           | ASCLIN_RXFIFOCON_ENI
           | ((priv->nbits > 8) ? ASCLIN_RXFIFOCON_OUTW_2
                                : ASCLIN_RXFIFOCON_OUTW_1);

    spi_putreg(cfg, ASCLIN_RXFIFOCON_OFFSET, rxfifo);
  }

  /* Clear flags */

  spi_putreg(cfg, ASCLIN_FLAGSCLEAR_OFFSET, ASCLIN_FLAGSCLEAR_ALL);

  /* Re-enable clock */

  spi_set_clk(cfg, cfg->clk_src);
}


static int tc4xx_spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;

  if (lock)
    {
      return nxmutex_lock(&priv->lock);
    }
  else
    {
      return nxmutex_unlock(&priv->lock);
    }
}


static void tc4xx_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                             bool selected)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;

  spiinfo("devid=%lu selected=%d\n", (unsigned long)devid, selected);

  /* Active low: selected=true means drive CS low */

  aurix_gpio_write(priv->config->cs_pin, !selected);
}


static uint32_t tc4xx_spi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;

  if (priv->frequency == frequency && priv->actual_freq != 0)
    {
      return priv->actual_freq;
    }

  priv->frequency = frequency;
  spi_reconfigure(priv);

  spiinfo("frequency=%lu actual=%lu\n",
          (unsigned long)frequency, (unsigned long)priv->actual_freq);

  return priv->actual_freq;
}


static void tc4xx_spi_setmode(FAR struct spi_dev_s *dev,
                              enum spi_mode_e mode)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;

  if (priv->mode == mode)
    {
      return;
    }

  priv->mode = mode;
  spi_reconfigure(priv);

  spiinfo("mode=%d\n", mode);
}


static void tc4xx_spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;

  if (nbits < 2 || nbits > 16)
    {
      spierr("ERROR: invalid nbits=%d (must be 2..16)\n", nbits);
      return;
    }

  if (priv->nbits == nbits)
    {
      return;
    }

  priv->nbits = nbits;
  spi_reconfigure(priv);

  spiinfo("nbits=%d\n", nbits);
}


static uint8_t tc4xx_spi_status(FAR struct spi_dev_s *dev, uint32_t devid)
{
  return SPI_STATUS_PRESENT;
}


static uint32_t tc4xx_spi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;
  return spi_exchange_word(priv, wd);
}


#ifdef CONFIG_SPI_EXCHANGE
static void tc4xx_spi_exchange(FAR struct spi_dev_s *dev,
                               FAR const void *txbuffer,
                               FAR void *rxbuffer, size_t nwords)
{
  struct tc4xx_spi_priv_s *priv = (struct tc4xx_spi_priv_s *)dev;
  const struct tc4xx_spi_config_s *cfg = priv->config;
  FAR const uint8_t *txp8 = (FAR const uint8_t *)txbuffer;
  FAR const uint16_t *txp16 = (FAR const uint16_t *)txbuffer;
  FAR uint8_t *rxp8 = (FAR uint8_t *)rxbuffer;
  FAR uint16_t *rxp16 = (FAR uint16_t *)rxbuffer;
  size_t i;

  spiinfo("exchange nwords=%zu nbits=%d\n", nwords, priv->nbits);

  /* Flush both FIFOs and clear all flags ONCE before the burst.
   * Per-byte flushing corrupts the shifter mid-frame.
   */

  spi_modreg(cfg, ASCLIN_TXFIFOCON_OFFSET,
             ASCLIN_TXFIFOCON_FLUSH, ASCLIN_TXFIFOCON_FLUSH);
  spi_modreg(cfg, ASCLIN_RXFIFOCON_OFFSET,
             ASCLIN_RXFIFOCON_FLUSH, ASCLIN_RXFIFOCON_FLUSH);
  spi_putreg(cfg, ASCLIN_FLAGSCLEAR_OFFSET, ASCLIN_FLAGSCLEAR_ALL);

  for (i = 0; i < nwords; i++)
    {
      uint32_t txword;
      uint32_t rxword;

      /* Get TX word */

      if (txbuffer != NULL)
        {
          if (priv->nbits <= 8)
            {
              txword = (uint32_t)txp8[i];
            }
          else
            {
              txword = (uint32_t)txp16[i];
            }
        }
      else
        {
          txword = 0xffu; /* Send 0xFF when no TX data (standard SPI idle) */
        }

      /* Exchange */

      rxword = spi_exchange_word(priv, txword);

      /* Store RX word */

      if (rxbuffer != NULL)
        {
          if (priv->nbits <= 8)
            {
              rxp8[i] = (uint8_t)(rxword & 0xffu);
            }
          else
            {
              rxp16[i] = (uint16_t)(rxword & 0xffffu);
            }
        }
    }
}
#else

static void tc4xx_spi_sndblock(FAR struct spi_dev_s *dev,
                               FAR const void *buffer, size_t nwords)
{
  tc4xx_spi_exchange(dev, buffer, NULL, nwords);
}

static void tc4xx_spi_recvblock(FAR struct spi_dev_s *dev,
                                FAR void *buffer, size_t nwords)
{
  tc4xx_spi_exchange(dev, NULL, buffer, nwords);
}
#endif



FAR struct spi_dev_s *tc4xx_asclin_spibus_initialize(int bus)
{
  struct tc4xx_spi_priv_s *priv = NULL;
  int ret;

  switch (bus)
    {
#ifdef CONFIG_TC4XX_ASCLIN3_SPI
      case 0:
        priv = &g_spi0_priv;
        break;
#endif
      default:
        spierr("ERROR: unsupported SPI bus %d\n", bus);
        return NULL;
    }

  if (priv->initialized)
    {
      return &priv->spidev;
    }

  ret = spi_hw_init(priv);
  if (ret < 0)
    {
      spierr("ERROR: SPI HW init failed: %d\n", ret);
      return NULL;
    }

  return &priv->spidev;
}
