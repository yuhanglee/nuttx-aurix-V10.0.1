/****************************************************************************
 * arch/tricore/src/tc4xx/tc4x_qspi.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include "tricore_internal.h"
#include "hardware/tc4x_qspi.h"
#include "tc4x_qspi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TC4XX_QSPI_FPER_HZ
#  define CONFIG_TC4XX_QSPI_FPER_HZ   200000000u
#endif

#define TC4X_QSPI_POLL_TIMEOUT        1000000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tc4x_qspi_config_s
{
  uintptr_t base;       /* QSPI module base address                        */
  uint32_t  fper;       /* fPER clock driving the QSPI module (Hz)        */
  uint8_t   bus;        /* NuttX SPI bus number                            */
  uint8_t   default_cs; /* Default hardware SLSO index (0..15)             */
  uint8_t   mris;       /* MRST input port selector (0..7)                 */
};

struct tc4x_qspi_priv_s
{
  struct spi_dev_s                  spidev;
  const struct tc4x_qspi_config_s  *config;
  mutex_t                           lock;
  uint32_t                          frequency;   /* Requested SPI clock Hz */
  uint32_t                          actual_freq; /* Achieved SPI clock Hz  */
  enum spi_mode_e                   mode;
  uint8_t                           nbits;       /* Bits per word (2..32)  */
  uint8_t                           cs;          /* Active SLSO index      */
  bool                              initialized;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int      tc4x_qspi_lock(FAR struct spi_dev_s *dev, bool lock);
static void     tc4x_qspi_select(FAR struct spi_dev_s *dev,
                                 uint32_t devid, bool selected);
static uint32_t tc4x_qspi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency);
static void     tc4x_qspi_setmode(FAR struct spi_dev_s *dev,
                                  enum spi_mode_e mode);
static void     tc4x_qspi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t  tc4x_qspi_status(FAR struct spi_dev_s *dev, uint32_t devid);
static uint32_t tc4x_qspi_send(FAR struct spi_dev_s *dev, uint32_t wd);
static void     tc4x_qspi_exchange(FAR struct spi_dev_s *dev,
                                   FAR const void *txbuffer,
                                   FAR void *rxbuffer, size_t nwords);
#ifndef CONFIG_SPI_EXCHANGE
static void     tc4x_qspi_sndblock(FAR struct spi_dev_s *dev,
                                   FAR const void *buffer, size_t nwords);
static void     tc4x_qspi_recvblock(FAR struct spi_dev_s *dev,
                                    FAR void *buffer, size_t nwords);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct spi_ops_s g_tc4x_qspi_ops =
{
  .lock             = tc4x_qspi_lock,
  .select           = tc4x_qspi_select,
  .setfrequency     = tc4x_qspi_setfrequency,
#ifdef CONFIG_SPI_DELAY_CONTROL
  .setdelay         = NULL,
#endif
  .setmode          = tc4x_qspi_setmode,
  .setbits          = tc4x_qspi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures       = NULL,
#endif
  .status           = tc4x_qspi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata          = NULL,
#endif
  .send             = tc4x_qspi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange         = tc4x_qspi_exchange,
#else
  .sndblock         = tc4x_qspi_sndblock,
  .recvblock        = tc4x_qspi_recvblock,
#endif
#ifdef CONFIG_SPI_TRIGGER
  .trigger          = NULL,
#endif
  .registercallback = NULL,
};

/* Per-bus config and state instances */

#ifdef CONFIG_TC4XX_QSPI0
static const struct tc4x_qspi_config_s g_qspi0_config =
{
  .base       = TC4X_QSPI0_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 0,
  .default_cs = CONFIG_TC4XX_QSPI0_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI0_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi0_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi0_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI0_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI0 */

#ifdef CONFIG_TC4XX_QSPI1
static const struct tc4x_qspi_config_s g_qspi1_config =
{
  .base       = TC4X_QSPI1_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 1,
  .default_cs = CONFIG_TC4XX_QSPI1_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI1_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi1_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi1_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI1_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI1 */

#ifdef CONFIG_TC4XX_QSPI2
static const struct tc4x_qspi_config_s g_qspi2_config =
{
  .base       = TC4X_QSPI2_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 2,
  .default_cs = CONFIG_TC4XX_QSPI2_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI2_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi2_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi2_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI2_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI2 */

#ifdef CONFIG_TC4XX_QSPI3
static const struct tc4x_qspi_config_s g_qspi3_config =
{
  .base       = TC4X_QSPI3_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 3,
  .default_cs = CONFIG_TC4XX_QSPI3_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI3_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi3_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi3_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI3_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI3 */

#ifdef CONFIG_TC4XX_QSPI4
static const struct tc4x_qspi_config_s g_qspi4_config =
{
  .base       = TC4X_QSPI4_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 4,
  .default_cs = CONFIG_TC4XX_QSPI4_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI4_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi4_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi4_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI4_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI4 */

#ifdef CONFIG_TC4XX_QSPI5
static const struct tc4x_qspi_config_s g_qspi5_config =
{
  .base       = TC4X_QSPI5_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 5,
  .default_cs = CONFIG_TC4XX_QSPI5_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI5_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi5_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi5_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI5_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI5 */

#ifdef CONFIG_TC4XX_QSPI6
static const struct tc4x_qspi_config_s g_qspi6_config =
{
  .base       = TC4X_QSPI6_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 6,
  .default_cs = CONFIG_TC4XX_QSPI6_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI6_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi6_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi6_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI6_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI6 */

#ifdef CONFIG_TC4XX_QSPI7
static const struct tc4x_qspi_config_s g_qspi7_config =
{
  .base       = TC4X_QSPI7_BASE,
  .fper       = CONFIG_TC4XX_QSPI_FPER_HZ,
  .bus        = 7,
  .default_cs = CONFIG_TC4XX_QSPI7_DEFAULT_CS,
  .mris       = CONFIG_TC4XX_QSPI7_MRIS,
};

static struct tc4x_qspi_priv_s g_qspi7_priv =
{
  .spidev      =
    {
      .ops = &g_tc4x_qspi_ops,
    },
  .config      = &g_qspi7_config,
  .lock        = NXMUTEX_INITIALIZER,
  .frequency   = 1000000u,
  .actual_freq = 0u,
  .mode        = SPIDEV_MODE0,
  .nbits       = 8,
  .cs          = CONFIG_TC4XX_QSPI7_DEFAULT_CS,
  .initialized = false,
};
#endif /* CONFIG_TC4XX_QSPI7 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qspi_getreg
 *
 * Description:
 *   Read a 32-bit QSPI module register.
 *
 * Input Parameters:
 *   cfg - Pointer to the QSPI bus configuration.
 *   off - Register byte offset from the module base address.
 *
 * Returned Value:
 *   The 32-bit register value.
 *
 ****************************************************************************/

static inline uint32_t qspi_getreg(const struct tc4x_qspi_config_s *cfg,
                                   uint32_t off)
{
  return getreg32(cfg->base + off);
}

/****************************************************************************
 * Name: qspi_putreg
 *
 * Description:
 *   Write a 32-bit value to a QSPI module register.
 *
 * Input Parameters:
 *   cfg - Pointer to the QSPI bus configuration.
 *   off - Register byte offset from the module base address.
 *   v   - Value to write.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static inline void qspi_putreg(const struct tc4x_qspi_config_s *cfg,
                               uint32_t off, uint32_t v)
{
  putreg32(v, cfg->base + off);
}

/****************************************************************************
 * Name: qspi_modreg
 *
 * Description:
 *   Read-modify-write a QSPI register: set the bits in setbits and clear
 *   the bits in clrbits without disturbing any other bit positions.
 *
 * Input Parameters:
 *   cfg     - Pointer to the QSPI bus configuration.
 *   off     - Register byte offset from the module base address.
 *   setbits - Mask of bits to set.
 *   clrbits - Mask of bits to clear.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static inline void qspi_modreg(const struct tc4x_qspi_config_s *cfg,
                               uint32_t off, uint32_t setbits,
                               uint32_t clrbits)
{
  modreg32(setbits, setbits | clrbits, cfg->base + off);
}

/****************************************************************************
 * Name: qspi_drain_rx
 *
 * Description:
 *   Drain up to 16 stale entries from the QSPI RX FIFO.
 *
 * Input Parameters:
 *   cfg - Pointer to the QSPI bus configuration.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void qspi_drain_rx(const struct tc4x_qspi_config_s *cfg)
{
  uint32_t i;
  uint32_t status;

  for (i = 0u; i < 16u; i++)
    {
      status = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
      if (((status & TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK)
           >> TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT) == 0u)
        {
          break;
        }

      (void)qspi_getreg(cfg, TC4X_QSPI_RXEXIT_OFFSET);
    }
}

/****************************************************************************
 * Name: qspi_bacon_base
 *
 * Description:
 *   Compute the fixed BACON register fields common to every transfer on
 *   this device.  The LAST bit is not included; callers OR it in as needed.
 *
 * Input Parameters:
 *   priv - Pointer to the QSPI driver private state.
 *
 * Returned Value:
 *   The 32-bit BACON base value.
 *
 ****************************************************************************/

static inline uint32_t qspi_bacon_base(const struct tc4x_qspi_priv_s *priv)
{
  return TC4X_QSPI_BACON_CS(priv->cs)
       | TC4X_QSPI_BACON_DL(priv->nbits)
       | TC4X_QSPI_BACON_MSB
       | (1u << TC4X_QSPI_BACON_LPRE_SHIFT)
       | TC4X_QSPI_BACON_LEAD(4u)
       | (1u << TC4X_QSPI_BACON_TPRE_SHIFT)
       | TC4X_QSPI_BACON_TRAIL(4u)
       | (2u << TC4X_QSPI_BACON_IPRE_SHIFT)
       | TC4X_QSPI_BACON_IDLE(4u);
}

/****************************************************************************
 * Name: qspi_poll_rx
 *
 * Description:
 *   Poll until one RX FIFO entry is available, then read it into *rx_out.
 *
 * Input Parameters:
 *   cfg    - Pointer to the QSPI bus configuration.
 *   rx_out - Location to store the received word on success.
 *
 * Returned Value:
 *   true on success; false on error flag or timeout.
 *
 ****************************************************************************/

static bool qspi_poll_rx(const struct tc4x_qspi_config_s *cfg,
                         FAR uint32_t *rx_out)
{
  uint32_t status;
  uint32_t fifo_level;
  uint32_t timeout;

  timeout = TC4X_QSPI_POLL_TIMEOUT;
  while (timeout > 0u)
    {
      status     = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
      fifo_level = (status & TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK)
                   >> TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT;
      if (fifo_level > 0u)
        {
          break;
        }

      if (status & TC4X_QSPI_STATUS_ERRORFLAGS_MASK)
        {
          qspi_putreg(cfg, TC4X_QSPI_FLAGSCLEAR_OFFSET,
                      TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK);
          return false;
        }

      timeout--;
    }

  if (timeout == 0u)
    {
      return false;
    }

  *rx_out = qspi_getreg(cfg, TC4X_QSPI_RXEXIT_OFFSET);
  return true;
}

/****************************************************************************
 * Name: qspi_store_rx
 *
 * Description:
 *   Store one received word into rxbuffer at the given index using the
 *   correct element width for priv->nbits.  No-op when rxbuffer is NULL.
 *
 * Input Parameters:
 *   priv     - Pointer to the QSPI driver private state.
 *   rxbuffer - Destination buffer, or NULL to discard.
 *   idx      - Element index into rxbuffer.
 *   rx       - Raw 32-bit word read from RXEXIT.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void qspi_store_rx(const struct tc4x_qspi_priv_s *priv,
                          FAR void *rxbuffer, size_t idx, uint32_t rx)
{
  if (rxbuffer == NULL)
    {
      return;
    }

  if (priv->nbits <= 8)
    {
      ((uint8_t *)rxbuffer)[idx] = (uint8_t)rx;
    }
  else if (priv->nbits <= 16)
    {
      ((uint16_t *)rxbuffer)[idx] = (uint16_t)rx;
    }
  else
    {
      ((uint32_t *)rxbuffer)[idx] = rx;
    }
}

/****************************************************************************
 * Name: qspi_compute_econ
 *
 * Description:
 *   Compute ECONz (baud rate, CPOL/CPH) for the requested frequency.
 *
 * Input Parameters:
 *   priv     - Pointer to the QSPI driver private state (for mode/config).
 *   freq     - Requested SPI clock frequency in Hz.
 *   out_econ - Location to store the computed ECONz register value.
 *
 * Returned Value:
 *   Achieved frequency in Hz; writes *out_econ with the register value.
 *
 ****************************************************************************/

static uint32_t qspi_compute_econ(const struct tc4x_qspi_priv_s *priv,
                                  uint32_t freq, uint32_t *out_econ)
{
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uint32_t a = 1u;          /* prescaler A (divide-by-2) */
  uint32_t div;             /* combined baud rate divisor */
  uint32_t q;
  uint32_t actual;
  uint32_t econ;

  if (freq == 0u)
    {
      freq = 1000000u;
    }

  div = a + 1u + 0u + 2u;  /* A+1+B+C = 4 */

  q = (cfg->fper + freq * div - 1u) / (freq * div);
  if (q == 0u)
    {
      q = 1u;
    }

  q -= 1u;
  if (q > 63u)
    {
      q = 63u;               /* Q is a 6-bit field */
    }

  actual = cfg->fper / ((q + 1u) * div);

  econ = TC4X_QSPI_ECON_Q(q)
       | TC4X_QSPI_ECON_A(a)
       | TC4X_QSPI_ECON_B(0u)
       | TC4X_QSPI_ECON_C(2u);

  if (priv->mode == SPIDEV_MODE1 || priv->mode == SPIDEV_MODE3)
    {
      econ |= TC4X_QSPI_ECON_CPH;
    }

  if (priv->mode == SPIDEV_MODE2 || priv->mode == SPIDEV_MODE3)
    {
      econ |= TC4X_QSPI_ECON_CPOL;
    }

  *out_econ = econ;
  return actual;
}

/****************************************************************************
 * Name: qspi_apply_cs
 *
 * Description:
 *   Enable the SLSO output driver for the current chip select (AOL=0,
 *   active-low).
 *
 * Input Parameters:
 *   priv - Pointer to the QSPI driver private state.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void qspi_apply_cs(const struct tc4x_qspi_priv_s *priv)
{
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uint32_t ssoc = qspi_getreg(cfg, TC4X_QSPI_SSOC_OFFSET);

  ssoc |=  TC4X_QSPI_SSOC_OEN_BIT(priv->cs);
  ssoc &= ~TC4X_QSPI_SSOC_AOL_BIT(priv->cs);
  qspi_putreg(cfg, TC4X_QSPI_SSOC_OFFSET, ssoc);
}

/****************************************************************************
 * Name: qspi_reconfigure
 *
 * Description:
 *   Recalculate and apply ECONz (baud rate + mode) and SSOC (CS enable)
 *   after any change to frequency, mode, nbits, or cs.
 *
 * Input Parameters:
 *   priv - Pointer to the QSPI driver private state.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void qspi_reconfigure(struct tc4x_qspi_priv_s *priv)
{
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uint32_t econ;

  priv->actual_freq = qspi_compute_econ(priv, priv->frequency, &econ);
  qspi_putreg(cfg, TC4X_QSPI_ECON_OFFSET(priv->cs), econ);
  qspi_apply_cs(priv);

  spiinfo("QSPI%u cs=%u mode=%d nbits=%u f=%lu actual=%lu econ=0x%08lx\n",
          cfg->bus, priv->cs, priv->mode, priv->nbits,
          (unsigned long)priv->frequency,
          (unsigned long)priv->actual_freq,
          (unsigned long)econ);
}

/****************************************************************************
 * Name: qspi_hw_init
 *
 * Description:
 *   One-time hardware init: enable CLC, open ACCEN, configure GLOBALCON/
 *   GLOBALCON1/PISEL/ECONz/SSOC, then set GLOBALCON.EN to start the SM.
 *
 * Input Parameters:
 *   priv - Pointer to the QSPI driver private state.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int qspi_hw_init(struct tc4x_qspi_priv_s *priv)
{
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uintptr_t prote;
  uint32_t  pval;
  uint32_t timeout;
  uint32_t globalcon_base;

  /* Enable module clock via PROTE  */

  prote = cfg->base + TC4X_QSPI_PROTE_OFFSET;
  pval  = getreg32(prote);

  if ((pval & 0x07u) == 0u)        /* RUN state -- must switch first   */
    {
      putreg32(0x09u, prote);      /* CONFIG state + SWEN              */
    }

  qspi_modreg(cfg, TC4X_QSPI_CLC_OFFSET, 0u, TC4X_QSPI_CLC_DISR);
  putreg32(0x08u, prote);          /* RUN state + SWEN                 */

  /* Poll CLC.DISS until the module clock is running */

  timeout = TC4X_QSPI_POLL_TIMEOUT;
  while (timeout > 0u &&
         (qspi_getreg(cfg, TC4X_QSPI_CLC_OFFSET) & TC4X_QSPI_CLC_DISS))
    {
      timeout--;
    }

  if (timeout == 0u)
    {
      spierr("QSPI%u: CLC enable timeout\n", cfg->bus);
      return -ETIMEDOUT;
    }

  /* Unlock ACCEN_WRA and ACCEN_RDA for full CPU access */

  prote = cfg->base + TC4X_QSPI_PROTE_OFFSET;
  pval  = getreg32(prote);

  if ((pval & 0x07u) == 0u)
    {
      putreg32(0x09u, prote);      /* CONFIG state + SWEN              */
    }

  qspi_putreg(cfg, TC4X_QSPI_ACCEN_WRA_OFFSET, 0xffffffffu);
  qspi_putreg(cfg, TC4X_QSPI_ACCEN_RDA_OFFSET, 0xffffffffu);
  putreg32(0x08u, prote);          /* RUN state + SWEN                 */

  /* GLOBALCON: set CLKSEL first (required before RESETS), EN=0 (PAUSE) */

  globalcon_base = TC4X_QSPI_GLOBALCON_TQ(0u)
                 | TC4X_QSPI_GLOBALCON_CLKSEL;

#ifdef CONFIG_TC4XX_QSPI4_LOOPBACK
  if (cfg->base == TC4X_QSPI4_BASE)
    {
      globalcon_base |= TC4X_QSPI_GLOBALCON_LB;
    }
#endif

  qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET, globalcon_base);

  /* RESETS=01b: flush SM + FIFOs before enabling */

  qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET,
              globalcon_base | TC4X_QSPI_GLOBALCON_RESETS_SM);

  /* Poll STATUS.PHASE == WAIT to confirm reset complete */

  timeout = TC4X_QSPI_POLL_TIMEOUT;
  while (timeout > 0u &&
         (qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET) &
          TC4X_QSPI_STATUS_PHASE_MASK) != TC4X_QSPI_STATUS_PHASE_WAIT)
    {
      timeout--;
    }

  if (timeout == 0u)
    {
      spierr("QSPI%u: RESETS did not clear (SM not in WAIT)\n", cfg->bus);
      return -ETIMEDOUT;
    }

  /* Clear RESETS, keep CLKSEL/LB, EN=0 */

  qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET, globalcon_base);

  /* GLOBALCON1 = 0: polled mode, no IRQs, combined FIFO */

  qspi_putreg(cfg, TC4X_QSPI_GLOBALCON1_OFFSET, 0u);

  /* PISEL: select MRST receive input */

  qspi_putreg(cfg, TC4X_QSPI_PISEL_OFFSET,
              TC4X_QSPI_PISEL_MRIS(cfg->mris));

  /* Apply default ECONz and SSOC */

  qspi_reconfigure(priv);

  /* Set EN=1: transition SM from PAUSE to RUN */

  qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET,
              globalcon_base | TC4X_QSPI_GLOBALCON_EN);

  /* Clear any stale event flags */

  qspi_putreg(cfg, TC4X_QSPI_FLAGSCLEAR_OFFSET,
              TC4X_QSPI_FLAGSCLEAR_TXC    |
              TC4X_QSPI_FLAGSCLEAR_RXC    |
              TC4X_QSPI_FLAGSCLEAR_USRC   |
              TC4X_QSPI_FLAGSCLEAR_PT1C   |
              TC4X_QSPI_FLAGSCLEAR_PT2C   |
              TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK);

  priv->initialized = true;

  spiinfo("QSPI%u init done: globalcon=0x%08lx clc=0x%08lx "
          "accen_wra=0x%08lx pisel=0x%08lx ssoc=0x%08lx\n",
          cfg->bus,
          (unsigned long)qspi_getreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET),
          (unsigned long)qspi_getreg(cfg, TC4X_QSPI_CLC_OFFSET),
          (unsigned long)qspi_getreg(cfg, TC4X_QSPI_ACCEN_WRA_OFFSET),
          (unsigned long)qspi_getreg(cfg, TC4X_QSPI_PISEL_OFFSET),
          (unsigned long)qspi_getreg(cfg, TC4X_QSPI_SSOC_OFFSET));

  return OK;
}

/****************************************************************************
 * Name: qspi_xfer_word
 *
 * Description:
 *   Transfer a single word via QSPI short data mode (BACON.LAST=1).
 *   Drains stale RX, waits for TX space, writes BACONENTRY + DATAENTRY,
 *   polls RXFIFOLEVEL, and returns the received word from RXEXIT.
 *
 * Input Parameters:
 *   priv   - Pointer to the QSPI driver private state.
 *   txword - Word to transmit.
 *
 * Returned Value:
 *   Received word, or 0u on timeout or error.
 *
 ****************************************************************************/

static uint32_t qspi_xfer_word(struct tc4x_qspi_priv_s *priv,
                               uint32_t txword)
{
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uint32_t bacon;
  uint32_t gc;
  uint32_t status;
  uint32_t timeout;
  uint32_t rxfifo_level;
  uint32_t txfifo_level;

  /* Drain stale RX entries */

  timeout = 16u;
  while (timeout--)
    {
      status = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
      rxfifo_level = (status & TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK)
                     >> TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT;
      if (rxfifo_level == 0u)
        {
          break;
        }

      (void)qspi_getreg(cfg, TC4X_QSPI_RXEXIT_OFFSET);
    }

  /* Wait for TX FIFO space */

  timeout = TC4X_QSPI_POLL_TIMEOUT;
  while (timeout > 0u)
    {
      status = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
      txfifo_level = (status & TC4X_QSPI_STATUS_TXFIFOLEVEL_MASK)
                     >> TC4X_QSPI_STATUS_TXFIFOLEVEL_SHIFT;
      if (txfifo_level < 4u)
        {
          break;
        }

      timeout--;
    }

  if (timeout == 0u)
    {
      spierr("QSPI%u: TX FIFO never drained "
             "(status=0x%08lx globalcon=0x%08lx) -- issuing RESETS\n",
             cfg->bus, (unsigned long)status,
             (unsigned long)qspi_getreg(cfg,
                             TC4X_QSPI_GLOBALCON_OFFSET));

      /* RESETS=01b: flush SM + FIFOs, then re-enable */

      gc = qspi_getreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET);
      qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET,
                  (gc & ~(TC4X_QSPI_GLOBALCON_EN |
                          TC4X_QSPI_GLOBALCON_RESETS_MASK)) |
                  TC4X_QSPI_GLOBALCON_RESETS_SM);

      timeout = TC4X_QSPI_POLL_TIMEOUT;
      while (timeout > 0u &&
             (qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET) &
              TC4X_QSPI_STATUS_PHASE_MASK) != TC4X_QSPI_STATUS_PHASE_WAIT)
        {
          timeout--;
        }

      qspi_putreg(cfg, TC4X_QSPI_GLOBALCON_OFFSET,
                  (gc & ~TC4X_QSPI_GLOBALCON_RESETS_MASK) |
                  TC4X_QSPI_GLOBALCON_EN);

      qspi_putreg(cfg, TC4X_QSPI_FLAGSCLEAR_OFFSET,
                  TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK |
                  TC4X_QSPI_FLAGSCLEAR_TXC |
                  TC4X_QSPI_FLAGSCLEAR_RXC);

      return 0u;
    }

  /* Write BACONENTRY: CS, DL=nbits-1, MSB first, LAST=1 */

  bacon = TC4X_QSPI_BACON_CS(priv->cs)
        | TC4X_QSPI_BACON_DL(priv->nbits)
        | TC4X_QSPI_BACON_MSB
        | TC4X_QSPI_BACON_LAST
        | (1u << TC4X_QSPI_BACON_LPRE_SHIFT)
        | TC4X_QSPI_BACON_LEAD(4u)
        | (1u << TC4X_QSPI_BACON_TPRE_SHIFT)
        | TC4X_QSPI_BACON_TRAIL(4u)
        | (2u << TC4X_QSPI_BACON_IPRE_SHIFT)
        | TC4X_QSPI_BACON_IDLE(4u);

  qspi_putreg(cfg, TC4X_QSPI_BACONENTRY_OFFSET, bacon);

  /* Write TX data */

  qspi_putreg(cfg, TC4X_QSPI_DATAENTRY_OFFSET(0), txword);

  /* Poll until RX FIFO has an entry */

  timeout = TC4X_QSPI_POLL_TIMEOUT;
  while (timeout > 0u)
    {
      status = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
      rxfifo_level = (status & TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK)
                     >> TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT;
      if (rxfifo_level > 0u)
        {
          break;
        }

      if (status & TC4X_QSPI_STATUS_ERRORFLAGS_MASK)
        {
          spierr("QSPI%u: error flags=0x%03lx\n", cfg->bus,
                 (unsigned long)(status &
                                 TC4X_QSPI_STATUS_ERRORFLAGS_MASK));
          qspi_putreg(cfg, TC4X_QSPI_FLAGSCLEAR_OFFSET,
                      TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK);
          return 0u;
        }

      timeout--;
    }

  if (timeout == 0u)
    {
      spierr("QSPI%u: RX FIFO never filled "
             "(status=0x%08lx globalcon=0x%08lx)\n",
             cfg->bus, (unsigned long)status,
             (unsigned long)qspi_getreg(cfg,
                             TC4X_QSPI_GLOBALCON_OFFSET));
      return 0u;
    }

  /* Read received word */

  return qspi_getreg(cfg, TC4X_QSPI_RXEXIT_OFFSET);
}

/****************************************************************************
 * Name: tc4x_qspi_lock
 *
 * Description:
 *   Acquire or release the SPI bus mutex.
 *
 * Input Parameters:
 *   dev  - SPI device handle.
 *   lock - true to acquire, false to release.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int tc4x_qspi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;

  if (lock)
    {
      return nxmutex_lock(&priv->lock);
    }

  return nxmutex_unlock(&priv->lock);
}

/****************************************************************************
 * Name: tc4x_qspi_select
 *
 * Description:
 *   Select or deselect a SPI device.  Maps the NuttX devid to a hardware
 *   SLSO index, switches SSOC and ECONz if the chip select changes.
 *   Hardware assertion timing is managed via the BACON register fields.
 *
 * Input Parameters:
 *   dev      - SPI device handle.
 *   devid    - NuttX SPI device identifier (encodes bus and index).
 *   selected - true to select, false to deselect (unused: hardware CS).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_select(FAR struct spi_dev_s *dev,
                             uint32_t devid, bool selected)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;
  uint32_t ssoc;
  uint8_t cs;

  /* SPIDEV_USER(0) maps to default_cs; other indices used as SLSO directly */

  if (SPIDEVID_INDEX(devid) == 0)
    {
      cs = priv->config->default_cs;
    }
  else
    {
      cs = (uint8_t)(SPIDEVID_INDEX(devid) & 0x0fu);
    }

  if (cs != priv->cs)
    {
      /* Disable old CS, switch to new, reprogram ECONz/SSOC. */

      ssoc = qspi_getreg(priv->config, TC4X_QSPI_SSOC_OFFSET);
      ssoc &= ~TC4X_QSPI_SSOC_OEN_BIT(priv->cs);
      qspi_putreg(priv->config, TC4X_QSPI_SSOC_OFFSET, ssoc);

      priv->cs = cs;
      qspi_reconfigure(priv);
    }

  UNUSED(selected); /* Hardware manages assertion timing via BACON */
}

/****************************************************************************
 * Name: tc4x_qspi_setfrequency
 *
 * Description:
 *   Set the SPI clock frequency.  Reprograms ECONz if the frequency
 *   differs from the current setting.
 *
 * Input Parameters:
 *   dev       - SPI device handle.
 *   frequency - Requested SPI clock frequency in Hz.
 *
 * Returned Value:
 *   The achieved SPI clock frequency in Hz.
 *
 ****************************************************************************/

static uint32_t tc4x_qspi_setfrequency(FAR struct spi_dev_s *dev,
                                       uint32_t frequency)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;

  if (priv->frequency == frequency && priv->actual_freq != 0u)
    {
      return priv->actual_freq;
    }

  priv->frequency = frequency;
  qspi_reconfigure(priv);

  return priv->actual_freq;
}

/****************************************************************************
 * Name: tc4x_qspi_setmode
 *
 * Description:
 *   Set the SPI clock polarity and phase (CPOL/CPHA).  Reprograms ECONz
 *   if the mode differs from the current setting.
 *
 * Input Parameters:
 *   dev  - SPI device handle.
 *   mode - SPIDEV_MODE0 .. SPIDEV_MODE3.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_setmode(FAR struct spi_dev_s *dev,
                              enum spi_mode_e mode)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;

  if (priv->mode == mode)
    {
      return;
    }

  priv->mode = mode;
  qspi_reconfigure(priv);
}

/****************************************************************************
 * Name: tc4x_qspi_setbits
 *
 * Description:
 *   Set the number of bits per SPI word (2..32).
 *
 * Input Parameters:
 *   dev   - SPI device handle.
 *   nbits - Word width in bits (2 through 32 inclusive).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;

  if (nbits < 2 || nbits > 32)
    {
      spierr("QSPI%u: invalid nbits=%d (allowed 2..32)\n",
             priv->config->bus, nbits);
      return;
    }

  priv->nbits = (uint8_t)nbits;
}

/****************************************************************************
 * Name: tc4x_qspi_status
 *
 * Description:
 *   Return the SPI device status bitmask.  Always reports the device as
 *   present; hardware-level presence detection is not supported.
 *
 * Input Parameters:
 *   dev   - SPI device handle (unused).
 *   devid - NuttX SPI device identifier (unused).
 *
 * Returned Value:
 *   SPI_STATUS_PRESENT
 *
 ****************************************************************************/

static uint8_t tc4x_qspi_status(FAR struct spi_dev_s *dev, uint32_t devid)
{
  UNUSED(dev);
  UNUSED(devid);
  return SPI_STATUS_PRESENT;
}

/****************************************************************************
 * Name: tc4x_qspi_send
 *
 * Description:
 *   Send one word on the SPI bus and return the received word.
 *
 * Input Parameters:
 *   dev - SPI device handle.
 *   wd  - Word to transmit.
 *
 * Returned Value:
 *   Received word.
 *
 ****************************************************************************/

static uint32_t tc4x_qspi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;
  return qspi_xfer_word(priv, wd);
}

/****************************************************************************
 * Name: tc4x_qspi_exchange
 *
 * Description:
 *   Exchange a block of data over the SPI bus.  Transmits from txbuffer
 *   (or sends 0xFF bytes when NULL) and stores received data into rxbuffer
 *   (or discards when NULL).  Uses BACON LAST=1 only on the final word to
 *   keep CS asserted across the whole transaction.
 *
 * Input Parameters:
 *   dev      - SPI device handle.
 *   txbuffer - Buffer of words to transmit, or NULL to send 0xFF.
 *   rxbuffer - Buffer to receive words into, or NULL to discard.
 *   nwords   - Number of words to exchange.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_exchange(FAR struct spi_dev_s *dev,
                               FAR const void *txbuffer,
                               FAR void *rxbuffer, size_t nwords)
{
  struct tc4x_qspi_priv_s *priv = (struct tc4x_qspi_priv_s *)dev;
  const struct tc4x_qspi_config_s *cfg = priv->config;
  uint32_t bacon_base;
  uint32_t status;
  uint32_t fifo_level;
  uint32_t timeout;
  uint32_t tx;
  uint32_t rx;
  bool last;
  size_t i;

  if (nwords == 0u)
    {
      return;
    }

  /* Drain any stale RX entries from a previous transfer */

  qspi_drain_rx(cfg);

  bacon_base = qspi_bacon_base(priv);

  /* Pipeline: TX[i]'s RX is committed when TX[i+1] is written;
   * the final word (LAST=1) commits its own RX and ends the frame.
   */

  for (i = 0u; i < nwords; i++)
    {
      last = (i == nwords - 1u);

      if (txbuffer == NULL)
        {
          tx = 0xffffffffu;
        }
      else if (priv->nbits <= 8)
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

      /* Wait for TX FIFO space */

      timeout = TC4X_QSPI_POLL_TIMEOUT;
      while (timeout > 0u)
        {
          status = qspi_getreg(cfg, TC4X_QSPI_STATUS_OFFSET);
          fifo_level = (status & TC4X_QSPI_STATUS_TXFIFOLEVEL_MASK)
                       >> TC4X_QSPI_STATUS_TXFIFOLEVEL_SHIFT;
          if (fifo_level < 4u)
            {
              break;
            }

          timeout--;
        }

      if (timeout == 0u)
        {
          return;
        }

      /* LAST=1 only on the final word — CS stays LOW across all words */

      qspi_putreg(cfg, TC4X_QSPI_BACONENTRY_OFFSET,
                  bacon_base | (last ? TC4X_QSPI_BACON_LAST : 0u));
      qspi_putreg(cfg, TC4X_QSPI_DATAENTRY_OFFSET(0), tx);

      /* Writing the next BACON commits RX[i-1] to the FIFO (i > 0) */

      if (i > 0u)
        {
          if (!qspi_poll_rx(cfg, &rx))
            {
              return;
            }

          qspi_store_rx(priv, rxbuffer, i - 1u, rx);
        }
    }

  /* Read the final RX word — committed after LAST=1 transfer completes */

  if (!qspi_poll_rx(cfg, &rx))
    {
      return;
    }

  qspi_store_rx(priv, rxbuffer, nwords - 1u, rx);
}

#ifndef CONFIG_SPI_EXCHANGE
/****************************************************************************
 * Name: tc4x_qspi_sndblock
 *
 * Description:
 *   Send a block of data over the SPI bus without receiving.
 *
 * Input Parameters:
 *   dev    - SPI device handle.
 *   buffer - Buffer of words to transmit.
 *   nwords - Number of words to send.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_sndblock(FAR struct spi_dev_s *dev,
                               FAR const void *buffer, size_t nwords)
{
  tc4x_qspi_exchange(dev, buffer, NULL, nwords);
}

/****************************************************************************
 * Name: tc4x_qspi_recvblock
 *
 * Description:
 *   Receive a block of data from the SPI bus, transmitting 0xFF bytes.
 *
 * Input Parameters:
 *   dev    - SPI device handle.
 *   buffer - Buffer to store received words.
 *   nwords - Number of words to receive.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tc4x_qspi_recvblock(FAR struct spi_dev_s *dev,
                                FAR void *buffer, size_t nwords)
{
  tc4x_qspi_exchange(dev, NULL, buffer, nwords);
}
#endif /* !CONFIG_SPI_EXCHANGE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4xx_qspibus_initialize
 *
 * Description:
 *   Initialize the selected QSPI bus and return a handle that can be passed
 *   directly to the SPI framework.  Subsequent calls with the same bus
 *   number return the cached, already-initialized handle.
 *
 * Input Parameters:
 *   bus - Zero-based QSPI bus number (0..7, subject to Kconfig).
 *
 * Returned Value:
 *   A non-NULL pointer to the SPI device handle on success; NULL if the
 *   bus number is not enabled or hardware initialization fails.
 *
 ****************************************************************************/

FAR struct spi_dev_s *tc4xx_qspibus_initialize(int bus)
{
  struct tc4x_qspi_priv_s *priv = NULL;
  int ret;

  switch (bus)
    {
#ifdef CONFIG_TC4XX_QSPI0
      case 0:
        priv = &g_qspi0_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI1
      case 1:
        priv = &g_qspi1_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI2
      case 2:
        priv = &g_qspi2_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI3
      case 3:
        priv = &g_qspi3_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI4
      case 4:
        priv = &g_qspi4_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI5
      case 5:
        priv = &g_qspi5_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI6
      case 6:
        priv = &g_qspi6_priv;
        break;
#endif
#ifdef CONFIG_TC4XX_QSPI7
      case 7:
        priv = &g_qspi7_priv;
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
