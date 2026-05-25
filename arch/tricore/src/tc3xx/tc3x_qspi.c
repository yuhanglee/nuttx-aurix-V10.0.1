/****************************************************************************
 * arch/tricore/src/tc3xx/tc3x_qspi.c
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
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include "tricore_internal.h"
#include "hardware/tc3x_qspi.h"
#include "tc3x_qspi.h"

#ifndef CONFIG_TC3XX_QSPI_FSPB_HZ
#  define CONFIG_TC3XX_QSPI_FSPB_HZ   100000000u
#endif

#define TC3X_QSPI_POLL_TIMEOUT        1000000u


struct tc3x_qspi_config_s
{
  uintptr_t base;
  uint32_t  fspb;        /* fSPB driving the QSPI module (Hz)              */
  uint8_t   bus;         /* NuttX SPI bus number                           */
  uint8_t   default_cs;  /* Default hardware SLSO index (0..15)            */
  uint8_t   mris;        /* MRST input port selector (0..7)                */
};

struct tc3x_qspi_priv_s
{
  struct spi_dev_s                  spidev;
  const struct tc3x_qspi_config_s  *config;
  mutex_t                           lock;
  uint32_t                          frequency;
  uint32_t                          actual_freq;
  enum spi_mode_e                   mode;
  uint8_t                           nbits;
  uint8_t                           cs;
  bool                              initialized;
};


static int      tc3x_qspi_lock(FAR struct spi_dev_s *dev, bool lock);
static void     tc3x_qspi_select(FAR struct spi_dev_s *dev,
                                 uint32_t devid, bool selected);
static uint32_t tc3x_qspi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency);
static void     tc3x_qspi_setmode(FAR struct spi_dev_s *dev,
                                  enum spi_mode_e mode);
static void     tc3x_qspi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t  tc3x_qspi_status(FAR struct spi_dev_s *dev, uint32_t devid);
static uint32_t tc3x_qspi_send(FAR struct spi_dev_s *dev, uint32_t wd);
#ifdef CONFIG_SPI_EXCHANGE
static void     tc3x_qspi_exchange(FAR struct spi_dev_s *dev,
                                   FAR const void *txbuffer,
                                   FAR void *rxbuffer, size_t nwords);
#else
static void     tc3x_qspi_sndblock(FAR struct spi_dev_s *dev,
                                   FAR const void *buffer, size_t nwords);
static void     tc3x_qspi_recvblock(FAR struct spi_dev_s *dev,
                                    FAR void *buffer, size_t nwords);
#endif

static const struct spi_ops_s g_tc3x_qspi_ops =
{
  .lock         = tc3x_qspi_lock,
  .select       = tc3x_qspi_select,
  .setfrequency = tc3x_qspi_setfrequency,
#ifdef CONFIG_SPI_DELAY_CONTROL
  .setdelay     = NULL,
#endif
  .setmode      = tc3x_qspi_setmode,
  .setbits      = tc3x_qspi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures   = NULL,
#endif
  .status       = tc3x_qspi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata      = NULL,
#endif
  .send         = tc3x_qspi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange     = tc3x_qspi_exchange,
#else
  .sndblock     = tc3x_qspi_sndblock,
  .recvblock    = tc3x_qspi_recvblock,
#endif
#ifdef CONFIG_SPI_TRIGGER
  .trigger      = NULL,
#endif
  .registercallback = NULL,
};

/* QSPI bus instances (one config struct per enabled module) ---------------*/

#ifdef CONFIG_TC3XX_QSPI2

static const struct tc3x_qspi_config_s g_qspi2_config =
{
  .base       = TC3X_QSPI2_BASE,
  .fspb       = CONFIG_TC3XX_QSPI_FSPB_HZ,
  .bus        = 2,
  .default_cs = CONFIG_TC3XX_QSPI2_DEFAULT_CS,
  .mris       = 1,
};

static struct tc3x_qspi_priv_s g_qspi2_priv =
{
  .spidev      = { .ops = &g_tc3x_qspi_ops },
  .config      = &g_qspi2_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE1,
  .nbits       = 16,
  .cs          = CONFIG_TC3XX_QSPI2_DEFAULT_CS,
  .initialized = false,
};

#endif

#ifdef CONFIG_TC3XX_QSPI3

static const struct tc3x_qspi_config_s g_qspi3_config =
{
  .base       = TC3X_QSPI3_BASE,
  .fspb       = CONFIG_TC3XX_QSPI_FSPB_HZ,
  .bus        = 3,
  .default_cs = CONFIG_TC3XX_QSPI3_DEFAULT_CS,
  .mris       = 0,
};

static struct tc3x_qspi_priv_s g_qspi3_priv =
{
  .spidev      = { .ops = &g_tc3x_qspi_ops },
  .config      = &g_qspi3_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC3XX_QSPI3_DEFAULT_CS,
  .initialized = false,
};

#endif

static inline uint32_t qspi_getreg(const struct tc3x_qspi_config_s *cfg,
                                   uint32_t off)
{
  return getreg32(cfg->base + off);
}

static inline void qspi_putreg(const struct tc3x_qspi_config_s *cfg,
                               uint32_t off, uint32_t v)
{
  putreg32(v, cfg->base + off);
}

static inline void qspi_modreg(const struct tc3x_qspi_config_s *cfg,
                               uint32_t off, uint32_t setbits,
                               uint32_t clrbits)
{
  modreg32(setbits, setbits | clrbits, cfg->base + off);
}

/* fSCLK = fSPB / ((Q+1) * (A+1)); A=1 (divide-by-2), 6-bit Q. */

static uint32_t qspi_compute_econ(const struct tc3x_qspi_priv_s *priv,
                                  uint32_t freq, uint32_t *out_econ)
{
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint32_t a = 1;
  uint32_t q;
  uint32_t actual;
  uint32_t econ;

  if (freq == 0)
    {
      freq = 1000000u;
    }

  q = (cfg->fspb + freq * (a + 1) - 1) / (freq * (a + 1));
  if (q == 0)
    {
      q = 1;
    }

  q -= 1;
  if (q > 63)
    {
      q = 63;            /* 6-bit field cap                              */
    }

  actual = cfg->fspb / ((q + 1) * (a + 1));

  econ = TC3X_QSPI_ECON_Q(q)
       | TC3X_QSPI_ECON_A(a)
       | TC3X_QSPI_ECON_B(0)
       | TC3X_QSPI_ECON_C(2);

  if (priv->mode == SPIDEV_MODE1 || priv->mode == SPIDEV_MODE3)
    {
      econ |= TC3X_QSPI_ECON_CPH;
    }

  if (priv->mode == SPIDEV_MODE2 || priv->mode == SPIDEV_MODE3)
    {
      econ |= TC3X_QSPI_ECON_CPOL;
    }

  *out_econ = econ;
  return actual;
}

/* Engine-driven /CS: OEN[cs]=1 (engine drives the pad), AOL[cs]=0
 * (active LOW). Engine holds idle = HIGH between BACON frames and
 * deactivates after BACON.LAST=1.
 */

static void qspi_apply_cs(const struct tc3x_qspi_priv_s *priv)
{
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint32_t ssoc = qspi_getreg(cfg, TC3X_QSPI_SSOC_OFFSET);

  ssoc |=  TC3X_QSPI_SSOC_OEN_BIT(priv->cs);
  ssoc &= ~TC3X_QSPI_SSOC_AOL_BIT(priv->cs);
  qspi_putreg(cfg, TC3X_QSPI_SSOC_OFFSET, ssoc);
}

static void qspi_reconfigure(struct tc3x_qspi_priv_s *priv)
{
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint32_t new_econ;
  uint32_t cur_econ;
  uint8_t slot = priv->cs & 0x07;   /* ECON0..7 only; SLSO N and N+8 share */

  priv->actual_freq = qspi_compute_econ(priv, priv->frequency, &new_econ);
  cur_econ = qspi_getreg(cfg, TC3X_QSPI_ECON_OFFSET(slot));

  if (cur_econ != new_econ)
    {
      qspi_putreg(cfg, TC3X_QSPI_ECON_OFFSET(slot), new_econ);
      spiinfo("QSPI%u cs=%u slot=%u mode=%d nbits=%u f=%lu actual=%lu "
              "econ=0x%08lx (write)\n",
              cfg->bus, priv->cs, slot, priv->mode, priv->nbits,
              (unsigned long)priv->frequency,
              (unsigned long)priv->actual_freq,
              (unsigned long)new_econ);
    }

  qspi_apply_cs(priv);
}

static int qspi_hw_init(struct tc3x_qspi_priv_s *priv)
{
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint32_t timeout;
  uint32_t globalcon;

  /* CLC.DISR = 0 in an EndInit-protected window, then poll DISS clear. */

  aurix_cpu_endinit_enable(false);
  qspi_modreg(cfg, TC3X_QSPI_CLC_OFFSET, 0u, TC3X_QSPI_CLC_DISR);
  aurix_cpu_endinit_enable(true);

  timeout = TC3X_QSPI_POLL_TIMEOUT;
  while (timeout--)
    {
      if (!(qspi_getreg(cfg, TC3X_QSPI_CLC_OFFSET) & TC3X_QSPI_CLC_DISS))
        {
          break;
        }
    }
  if (timeout == 0)
    {
      spierr("QSPI%u: CLC enable timeout\n", cfg->bus);
      return -ETIMEDOUT;
    }

  /* GLOBALCON: controller, EN, CLKSEL, RESETS=RUN, TQ=0, EXPECT=15 (max
   * receive-expect timeout so the engine doesn't latch a sticky error
   * while software queues the next BACONENTRY in a chained transfer —
   * see Zephyr aurix driver commit 72cb3a18).
   */

  globalcon = TC3X_QSPI_GLOBALCON_TQ(0)
            | TC3X_QSPI_GLOBALCON_EXPECT(15)
            | TC3X_QSPI_GLOBALCON_MS_MASTER
            | TC3X_QSPI_GLOBALCON_EN
            | TC3X_QSPI_GLOBALCON_CLKSEL
            | TC3X_QSPI_GLOBALCON_RESETS_RUN;
  qspi_putreg(cfg, TC3X_QSPI_GLOBALCON_OFFSET, globalcon);

  qspi_putreg(cfg, TC3X_QSPI_GLOBALCON1_OFFSET, 0u);  /* single FIFO entry */

  qspi_putreg(cfg, TC3X_QSPI_PISEL_OFFSET,
              TC3X_QSPI_PISEL_MRIS(cfg->mris));

  qspi_reconfigure(priv);

  qspi_putreg(cfg, TC3X_QSPI_FLAGSCLEAR_OFFSET,
              TC3X_QSPI_FLAGSCLEAR_TXC |
              TC3X_QSPI_FLAGSCLEAR_RXC |
              TC3X_QSPI_FLAGSCLEAR_USRC |
              TC3X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK);

  priv->initialized = true;
  spiinfo("QSPI%u initialized base=0x%08lx fSPB=%lu Hz\n",
          cfg->bus, (unsigned long)cfg->base, (unsigned long)cfg->fspb);

  return OK;
}

/* Shift 1..4 bytes through DATAENTRY[0] with one BACONENTRY entry.
 *   is_last=true  -> BACON.LAST=1; engine deactivates SLSO after the chunk.
 *   is_last=false -> engine holds SLSO asserted, waits for the next entry
 *                    (multi-byte CS-held transfers, UM §37.3.2.5).
 * BACON.BYTE=0, BACON.MSB=1, DL = (chunk_bytes*8)-1 (iLLD IfxQspi.c:88-89).
 */

static uint32_t qspi_xfer_chunk(struct tc3x_qspi_priv_s *priv,
                                uint32_t txword, size_t chunk_bytes,
                                bool is_last)
{
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint32_t bacon;
  uint32_t status;
  uint32_t timeout;
  uint32_t dl_bits_minus_1;

  if (chunk_bytes < 1 || chunk_bytes > 4)
    {
      spierr("QSPI%u: chunk_bytes=%zu out of range\n",
             cfg->bus, chunk_bytes);
      return 0;
    }

  timeout = 16;
  while (timeout--)
    {
      status = qspi_getreg(cfg, TC3X_QSPI_STATUS_OFFSET);
      if (((status & TC3X_QSPI_STATUS_RXFIFOLEVEL_MASK) >>
            TC3X_QSPI_STATUS_RXFIFOLEVEL_SHIFT) == 0)
        {
          break;
        }
      (void)qspi_getreg(cfg, TC3X_QSPI_RXEXIT_OFFSET);
    }

  timeout = TC3X_QSPI_POLL_TIMEOUT;
  while (timeout--)
    {
      status = qspi_getreg(cfg, TC3X_QSPI_STATUS_OFFSET);
      if (((status & TC3X_QSPI_STATUS_TXFIFOLEVEL_MASK) >>
            TC3X_QSPI_STATUS_TXFIFOLEVEL_SHIFT) < 4)
        {
          break;
        }
    }
  if (timeout == 0)
    {
      spierr("QSPI%u: TX FIFO never drained\n", cfg->bus);
      return 0;
    }

  dl_bits_minus_1 = (uint32_t)(chunk_bytes * 8u) - 1u;
  bacon = TC3X_QSPI_BACON_CS(priv->cs)
        | ((dl_bits_minus_1 << TC3X_QSPI_BACON_DL_SHIFT) &
           TC3X_QSPI_BACON_DL_MASK)
        | TC3X_QSPI_BACON_MSB
        | (is_last ? TC3X_QSPI_BACON_LAST : 0u)
        | (1u << TC3X_QSPI_BACON_LPRE_SHIFT)
        | TC3X_QSPI_BACON_LEAD(4)
        | (1u << TC3X_QSPI_BACON_TPRE_SHIFT)
        | TC3X_QSPI_BACON_TRAIL(4)
        | (2u << TC3X_QSPI_BACON_IPRE_SHIFT)
        | TC3X_QSPI_BACON_IDLE(4);

  qspi_putreg(cfg, TC3X_QSPI_BACONENTRY_OFFSET, bacon);
  qspi_putreg(cfg, TC3X_QSPI_DATAENTRY_OFFSET(0), txword);

  timeout = TC3X_QSPI_POLL_TIMEOUT;
  while (timeout--)
    {
      status = qspi_getreg(cfg, TC3X_QSPI_STATUS_OFFSET);
      if (((status & TC3X_QSPI_STATUS_RXFIFOLEVEL_MASK) >>
            TC3X_QSPI_STATUS_RXFIFOLEVEL_SHIFT) > 0)
        {
          break;
        }
      if (status & TC3X_QSPI_STATUS_ERRORFLAGS_MASK)
        {
          spierr("QSPI%u: error flags=0x%03lx\n", cfg->bus,
                 (unsigned long)(status & TC3X_QSPI_STATUS_ERRORFLAGS_MASK));
          qspi_putreg(cfg, TC3X_QSPI_FLAGSCLEAR_OFFSET,
                      TC3X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK);
          return 0;
        }
    }
  if (timeout == 0)
    {
      spierr("QSPI%u: RX FIFO never filled (status=0x%08lx)\n",
             cfg->bus, (unsigned long)status);
      return 0;
    }

  return qspi_getreg(cfg, TC3X_QSPI_RXEXIT_OFFSET);
}

/* SPI_SEND: single-word path, always LAST=1. The 200-nop spin is
 * load-bearing rate-limiting for the TLF35682 PMIC on QSPI2 — do not
 * remove without measuring on hardware (see CLAUDE.md).
 */

static uint32_t qspi_xfer_word(struct tc3x_qspi_priv_s *priv,
                               uint32_t txword)
{
  size_t chunk_bytes;
  uint32_t rxword;

  if (priv->nbits <= 8)        chunk_bytes = 1;
  else if (priv->nbits <= 16)  chunk_bytes = 2;
  else if (priv->nbits <= 24)  chunk_bytes = 3;
  else                         chunk_bytes = 4;

  rxword = qspi_xfer_chunk(priv, txword, chunk_bytes, true);

  for (volatile int i = 0; i < 200; i++)
    {
      __asm__ __volatile__("nop");
    }

  return rxword;
}

static int tc3x_qspi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void tc3x_qspi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                             bool selected)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  const struct tc3x_qspi_config_s *cfg = priv->config;
  uint8_t cs = (uint8_t)(SPIDEVID_INDEX(devid) & 0x0f);
  uint32_t ssoc;

  UNUSED(selected);

  if (cs != priv->cs)
    {
      ssoc = qspi_getreg(cfg, TC3X_QSPI_SSOC_OFFSET);
      ssoc &= ~TC3X_QSPI_SSOC_OEN_BIT(priv->cs);
      qspi_putreg(cfg, TC3X_QSPI_SSOC_OFFSET, ssoc);
      priv->cs = cs;
    }

  qspi_reconfigure(priv);
}

static uint32_t tc3x_qspi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  uint32_t unused_econ;

  if (priv->frequency == frequency && priv->actual_freq != 0)
    {
      return priv->actual_freq;
    }

  priv->frequency = frequency;
  priv->actual_freq = qspi_compute_econ(priv, frequency, &unused_econ);
  return priv->actual_freq;
}

static void tc3x_qspi_setmode(FAR struct spi_dev_s *dev,
                              enum spi_mode_e mode)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;

  if (priv->mode == mode)
    {
      return;
    }
  priv->mode = mode;
}

static void tc3x_qspi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  if (nbits < 2 || nbits > 32)
    {
      spierr("QSPI: invalid nbits=%d (allowed 2..32)\n", nbits);
      return;
    }
  priv->nbits = (uint8_t)nbits;
}

static uint8_t tc3x_qspi_status(FAR struct spi_dev_s *dev, uint32_t devid)
{
  UNUSED(dev);
  UNUSED(devid);
  return SPI_STATUS_PRESENT;
}

static uint32_t tc3x_qspi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  return qspi_xfer_word(priv, wd);
}

#ifdef CONFIG_SPI_EXCHANGE
/* One SPI_EXCHANGE = one CS-held block. LAST=0 on the first nwords-1
 * entries (engine keeps SLSO asserted in EXPECT state); LAST=1 only on
 * the final entry (engine deactivates SLSO at the end of the block).
 */

static void tc3x_qspi_exchange(FAR struct spi_dev_s *dev,
                               FAR const void *txbuffer,
                               FAR void *rxbuffer, size_t nwords)
{
  struct tc3x_qspi_priv_s *priv = (struct tc3x_qspi_priv_s *)dev;
  size_t chunk_bytes;
  size_t i;

  if (nwords == 0)
    {
      return;
    }

  if (priv->nbits <= 8)        chunk_bytes = 1;
  else if (priv->nbits <= 16)  chunk_bytes = 2;
  else if (priv->nbits <= 24)  chunk_bytes = 3;
  else                         chunk_bytes = 4;

  for (i = 0; i < nwords; i++)
    {
      uint32_t tx = 0xffffffffu;
      uint32_t rx;
      bool is_last = (i == nwords - 1);

      if (txbuffer != NULL)
        {
          if (priv->nbits <= 8)
            {
              tx = ((const uint8_t *)txbuffer)[i];
            }
          else if (priv->nbits <= 16)
            {
              tx = ((const uint16_t *)txbuffer)[i];
            }
          else
            {
              tx = ((const uint32_t *)txbuffer)[i];
            }
        }

      rx = qspi_xfer_chunk(priv, tx, chunk_bytes, is_last);

      if (rxbuffer != NULL)
        {
          if (priv->nbits <= 8)
            {
              ((uint8_t *)rxbuffer)[i]  = (uint8_t)rx;
            }
          else if (priv->nbits <= 16)
            {
              ((uint16_t *)rxbuffer)[i] = (uint16_t)rx;
            }
          else
            {
              ((uint32_t *)rxbuffer)[i] = rx;
            }
        }
    }
}
#else
static void tc3x_qspi_sndblock(FAR struct spi_dev_s *dev,
                               FAR const void *buffer, size_t nwords)
{
  tc3x_qspi_exchange(dev, buffer, NULL, nwords);
}

static void tc3x_qspi_recvblock(FAR struct spi_dev_s *dev,
                                FAR void *buffer, size_t nwords)
{
  tc3x_qspi_exchange(dev, NULL, buffer, nwords);
}
#endif

FAR struct spi_dev_s *tc3xx_qspibus_initialize(int bus)
{
  struct tc3x_qspi_priv_s *priv = NULL;
  int ret;

  switch (bus)
    {
#ifdef CONFIG_TC3XX_QSPI2
      case 2:
        priv = &g_qspi2_priv;
        break;
#endif
#ifdef CONFIG_TC3XX_QSPI3
      case 3:
        priv = &g_qspi3_priv;
        break;
#endif
      default:
        spierr("QSPI: bus %d not enabled\n", bus);
        return NULL;
    }

  if (priv->initialized)
    {
      return &priv->spidev;
    }

  ret = qspi_hw_init(priv);
  if (ret < 0)
    {
      spierr("QSPI%d: hw_init failed: %d\n", bus, ret);
      return NULL;
    }

  return &priv->spidev;
}
