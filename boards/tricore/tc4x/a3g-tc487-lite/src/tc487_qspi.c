/****************************************************************************
 * boards/tricore/tc4x/a3g-tc487-lite/src/tc487_qspi.c
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

#ifdef CONFIG_TC4X_QSPI

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include "tricore_gpio.h"
#include "tc4x_qspi.h"
#include "hardware/tc4x_qspi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Direct register access macro for QSPI4 loopback diagnostics only       */

#define QSPI4_REG(off) \
        (*(volatile uint32_t *)(TC4X_QSPI4_BASE + (off)))

/****************************************************************************
 * Private Functions -- per-instance pinmux
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifdef CONFIG_TC4X_QSPI0
/****************************************************************************
 * Name: tc487_qspi0_pinmux
 *
 * Description:
 *   Configure port pins for QSPI0 (P20.14/P20.12/P20.11/P20.8).
 *   Port 20 is shared with Ethernet; do not enable both.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi0_pinmux(void)
{
  /* MOSI: P20.14 -> QSPI0_MTSR_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(20, 14, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P20.12 -> QSPI0_MRSTA_F (input, no pull, MRIS=0 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(20, 12, GPIO_INPUT, GPIO_PULL_NONE));

  /* SCLK: P20.11 -> QSPI0_SCLK_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(20, 11, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* CS0:  P20.8  -> QSPI0_SLSO0_F (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(20, 8, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI0 */

#ifdef CONFIG_TC4X_QSPI1
/****************************************************************************
 * Name: tc487_qspi1_pinmux
 *
 * Description:
 *   Configure port pins for QSPI1 (P10.3/P10.6/P10.2, CS9=P10.5).
 *   No SLSO0_F on Port 10 in BGA292; set CONFIG_TC4X_QSPI1_DEFAULT_CS=9.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi1_pinmux(void)
{
  /* MOSI: P10.3 -> QSPI1_MTSR_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(10, 3, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P10.6 -> QSPI1_MRSTC_F (input, no pull, MRIS=2 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(10, 6, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P10.2 -> QSPI1_SCLK_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(10, 2, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* CS9:  P10.5 -> QSPI1_SLSO9   (ALT4, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(10, 5, GPIO_PERIPH, GPIO_ALT4)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI1 */

#ifdef CONFIG_TC4X_QSPI2
/****************************************************************************
 * Name: tc487_qspi2_pinmux
 *
 * Description:
 *   Configure port pins for QSPI2 (P15.6/P15.7/P15.8/P15.2).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi2_pinmux(void)
{
  /* MOSI: P15.6 -> QSPI2_MTSR  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(15, 6, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P15.7 -> QSPI2_MRSTB (input, no pull, MRIS=1 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(15, 7, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P15.8 -> QSPI2_SCLK  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(15, 8, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* CS0:  P15.2 -> QSPI2_SLSO0 (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(15, 2, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI2 */

#ifdef CONFIG_TC4X_QSPI3
/****************************************************************************
 * Name: tc487_qspi3_pinmux
 *
 * Description:
 *   Configure port pins for QSPI3 (P02.6/P02.5/P02.7/P02.4).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi3_pinmux(void)
{
  /* MOSI: P02.6 -> QSPI3_MTSR_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(2, 6, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P02.5 -> QSPI3_MRSTA_F (input, no pull, MRIS=0 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(2, 5, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P02.7 -> QSPI3_SCLK_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(2, 7, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* CS0:  P02.4 -> QSPI3_SLSO0_F (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(2, 4, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI3 */

#ifdef CONFIG_TC4X_QSPI4
/****************************************************************************
 * Name: tc487_qspi4_pinmux
 *
 * Description:
 *   Configure port pins for QSPI4 (P22.0/P22.1/P22.3, CS6=P23.1).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi4_pinmux(void)
{
  /* MOSI: P22.0 -> QSPI4_MTSR_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(22, 0, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P22.1 -> QSPI4_MRSTB_F (input, no pull, MRIS=1 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(22, 1, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P22.3 -> QSPI4_SCLK_F  (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(22, 3, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);

  /* CS6:  P23.1 -> QSPI4_SLSO6   (ALT3, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(23, 1, GPIO_PERIPH, GPIO_ALT3)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI4 */

#ifdef CONFIG_TC4X_QSPI5
/****************************************************************************
 * Name: tc487_qspi5_pinmux
 *
 * Description:
 *   Configure port pins for QSPI5 (P22.2/P22.0/P22.3).
 *   Conflicts with QSPI4 on Port 22; do not enable both.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi5_pinmux(void)
{
  /* MOSI: P22.2 -> QSPI5_MTSR  (ALT6, peripheral drives the pad) */

  aurix_config_gpio(AURIX_GPIO(22, 2, GPIO_PERIPH, GPIO_ALT6)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P22.0 -> QSPI5_MRSTC (input, no pull, MRIS=2)
   * NOTE: P22.0 is also QSPI4_MTSR_F (ALT3) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(22, 0, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P22.3 -> QSPI5_SCLK  (ALT2, peripheral drives the pad)
   * NOTE: P22.3 is also QSPI4_SCLK_F (ALT3) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(22, 3, GPIO_PERIPH, GPIO_ALT2)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI5 */

#ifdef CONFIG_TC4X_QSPI6
/****************************************************************************
 * Name: tc487_qspi6_pinmux
 *
 * Description:
 *   Configure port pins for QSPI6 (P23.1/P23.4).
 *   MOSI P23.1 ALT8 conflicts with QSPI4 CS6; do not enable both.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi6_pinmux(void)
{
  /* MOSI: P23.1 -> QSPI6_MTSR_F  (ALT8, peripheral drives the pad)
   * NOTE: P23.1 is also QSPI4_SLSO6 (ALT3) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(23, 1, GPIO_PERIPH, GPIO_ALT8)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P23.4 -> QSPI6_MRSTD_F (input, no pull, MRIS=3 in PISEL) */

  aurix_config_gpio(AURIX_GPIO(23, 4, GPIO_INPUT, GPIO_PULL_UP));
}
#endif /* CONFIG_TC4X_QSPI6 */

#ifdef CONFIG_TC4X_QSPI7
/****************************************************************************
 * Name: tc487_qspi7_pinmux
 *
 * Description:
 *   Configure port pins for QSPI7 (P15.6/P15.7/P15.2).
 *   Conflicts with QSPI2 on Port 15; do not enable both.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi7_pinmux(void)
{
  /* MOSI: P15.6 -> QSPI7_MTSR_F  (ALT7, peripheral drives the pad)
   * NOTE: P15.6 is also QSPI2_MTSR (ALT3) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(15, 6, GPIO_PERIPH, GPIO_ALT7)
                    | GPIO_PERIPH_OWN_PAD);

  /* MISO: P15.7 -> QSPI7_MRSTE_F (input, no pull, MRIS=4 in PISEL)
   * NOTE: P15.7 is also QSPI2_MRSTB (input) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(15, 7, GPIO_INPUT, GPIO_PULL_UP));

  /* SCLK: P15.2 -> QSPI7_SCLK_F  (ALT7, peripheral drives the pad)
   * NOTE: P15.2 is also QSPI2_SLSO0 (ALT3) -- mutually exclusive.
   */

  aurix_config_gpio(AURIX_GPIO(15, 2, GPIO_PERIPH, GPIO_ALT7)
                    | GPIO_PERIPH_OWN_PAD);
}
#endif /* CONFIG_TC4X_QSPI7 */

#ifdef CONFIG_TC4X_QSPI4_LOOPBACK
/****************************************************************************
 * Name: tc487_qspi4_lb_one
 *
 * Description:
 *   Transfer one 16-bit word in QSPI4 loopback mode (GLOBALCON.LB=1).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static uint16_t tc487_qspi4_lb_one(uint16_t tx)
{
  uint32_t bacon;
  uint32_t status;
  uint32_t timeout = 200000u;

  /* Drain any stale RX FIFO entries */

  while ((QSPI4_REG(TC4X_QSPI_STATUS_OFFSET) &
          TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK) != 0u)
    {
      (void)QSPI4_REG(TC4X_QSPI_RXEXIT_OFFSET);
      if (--timeout == 0u)
        {
          break;
        }
    }

  /* Clear stale event flags */

  QSPI4_REG(TC4X_QSPI_FLAGSCLEAR_OFFSET) =
      TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK |
      TC4X_QSPI_FLAGSCLEAR_TXC |
      TC4X_QSPI_FLAGSCLEAR_RXC;

  /* Build BACONENTRY for 16-bit MSB-first short-data frame on CS6 */

  bacon = TC4X_QSPI_BACON_CS(CONFIG_TC4X_QSPI4_DEFAULT_CS)
        | TC4X_QSPI_BACON_DL(16u)
        | TC4X_QSPI_BACON_MSB
        | TC4X_QSPI_BACON_LAST
        | (1u << TC4X_QSPI_BACON_LPRE_SHIFT)
        | TC4X_QSPI_BACON_LEAD(4u)
        | (1u << TC4X_QSPI_BACON_TPRE_SHIFT)
        | TC4X_QSPI_BACON_TRAIL(4u)
        | (2u << TC4X_QSPI_BACON_IPRE_SHIFT)
        | TC4X_QSPI_BACON_IDLE(4u);

  QSPI4_REG(TC4X_QSPI_BACONENTRY_OFFSET)     = bacon;
  QSPI4_REG(TC4X_QSPI_DATAENTRY_OFFSET(0))   = (uint32_t)tx;

  /* Poll for RX FIFO level >= 1 */

  for (timeout = 200000u; timeout > 0u; timeout--)
    {
      status = QSPI4_REG(TC4X_QSPI_STATUS_OFFSET);
      if (((status & TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK) >>
            TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT) > 0u)
        {
          break;
        }
    }

  if (timeout == 0u)
    {
      return 0xdeadu;
    }

  return (uint16_t)QSPI4_REG(TC4X_QSPI_RXEXIT_OFFSET);
}

/****************************************************************************
 * Name: tc487_qspi4_loopback_test
 *
 * Description:
 *   Run a pattern sweep with GLOBALCON.LB=1 and print pass/fail to stdout.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

static void tc487_qspi4_loopback_test(void)
{
  static const uint16_t patterns[] =
    {
      0x0000u, 0xffffu, 0xaaaau, 0x5555u,
      0x1234u, 0x8001u, 0x00ffu, 0xff00u,
    };

  uint32_t saved_gc = QSPI4_REG(TC4X_QSPI_GLOBALCON_OFFSET);
  uint32_t pass = 0;
  uint32_t fail = 0;
  size_t   i;

  printf("=== QSPI4 INTERNAL LOOPBACK SELF-TEST (LB=1) ===\n");

  /* Reset SM and FIFOs (RESETS=01b), keep SSOC.OEN set for the CS */

  QSPI4_REG(TC4X_QSPI_GLOBALCON_OFFSET) =
      (saved_gc & ~TC4X_QSPI_GLOBALCON_EN) |
      TC4X_QSPI_GLOBALCON_RESETS_SM;

  {
    uint32_t timeout = 100000u;
    while (timeout-- &&
           (QSPI4_REG(TC4X_QSPI_STATUS_OFFSET) &
            TC4X_QSPI_STATUS_PHASE_MASK) != TC4X_QSPI_STATUS_PHASE_WAIT)
      {
      }
  }

  /* Clear RESETS, re-assert EN, keep LB=1 */

  QSPI4_REG(TC4X_QSPI_GLOBALCON_OFFSET) =
      (saved_gc & ~TC4X_QSPI_GLOBALCON_RESETS_MASK) |
      TC4X_QSPI_GLOBALCON_EN |
      TC4X_QSPI_GLOBALCON_LB;

  for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
      uint16_t tx  = patterns[i];
      uint16_t rx  = tc487_qspi4_lb_one(tx);
      bool     ok  = (rx == tx);
      uint32_t err = QSPI4_REG(TC4X_QSPI_STATUS_OFFSET) &
                     TC4X_QSPI_STATUS_ERRORFLAGS_MASK;

      printf("  tx=0x%04x rx=0x%04x err=0x%03" PRIx32 " %s\n",
             tx, rx, err, ok ? "[PASS]" : "[FAIL]");
      if (ok)
        {
          pass++;
        }
      else
        {
          fail++;
        }
    }

  /* Restore GLOBALCON (LB flag state) */

  QSPI4_REG(TC4X_QSPI_GLOBALCON_OFFSET) = saved_gc;
  QSPI4_REG(TC4X_QSPI_FLAGSCLEAR_OFFSET) =
      TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK |
      TC4X_QSPI_FLAGSCLEAR_TXC            |
      TC4X_QSPI_FLAGSCLEAR_RXC;

  printf("=== QSPI4 LB result: %" PRIu32 " pass / %" PRIu32 " fail ===\n",
         pass, fail);
  fflush(stdout);
}
#endif /* CONFIG_TC4X_QSPI4_LOOPBACK */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Name: tc487_qspi_initialize
 *
 * Description:
 *   Configure pins, init lower-half driver, and register /dev/spiN.
 *
 * Input Parameters:
 *   bus - QSPI instance number (0-7), used as the /dev/spiN index.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

int tc487_qspi_initialize(int bus)
{
  FAR struct spi_dev_s *qspi;
  int                   ret;

  switch (bus)
    {
#ifdef CONFIG_TC4X_QSPI0
      case 0:
        tc487_qspi0_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI1
      case 1:
        tc487_qspi1_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI2
      case 2:
        tc487_qspi2_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI3
      case 3:
        tc487_qspi3_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI4
      case 4:
        tc487_qspi4_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI5
      case 5:
        tc487_qspi5_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI6
      case 6:
        tc487_qspi6_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_QSPI7
      case 7:
        tc487_qspi7_pinmux();
        break;
#endif
      default:
        _err("ERROR: QSPI%d not enabled in config\n", bus);
        return -ENODEV;
    }

  qspi = tc4x_qspibus_initialize(bus);
  if (qspi == NULL)
    {
      _err("ERROR: tc4x_qspibus_initialize(%d) failed\n", bus);
      return -ENODEV;
    }

#ifdef CONFIG_SPI_DRIVER
  ret = spi_register(qspi, bus);
  if (ret < 0)
    {
      _err("ERROR: spi_register(qspi%d, %d) failed: %d\n", bus, bus, ret);
      return ret;
    }

  spiinfo("QSPI%d registered as /dev/spi%d\n", bus, bus);
#else
  ret = OK;
#endif

  return ret;
}

/****************************************************************************
 * Name: tc487_qspi_bringup
 *
 * Description:
 *   Initialize all enabled QSPI instances.  Called from
 *   board_app_initialize().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

int tc487_qspi_bringup(void)
{
  static bool g_done;
  int ret = OK;

  /* board_app_initialize() runs twice (board_late_initialize() at boot plus
   * the NSH BOARDIOC_INIT); register the QSPI /dev/spiN nodes exactly once so
   * the second pass does not fail with -EEXIST.
   */

  if (g_done)
    {
      return OK;
    }

  g_done = true;

#ifdef CONFIG_TC4X_QSPI0
  ret = tc487_qspi_initialize(0);
  if (ret < 0)
    {
      _err("ERROR: QSPI0 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI1
  ret = tc487_qspi_initialize(1);
  if (ret < 0)
    {
      _err("ERROR: QSPI1 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI2
  ret = tc487_qspi_initialize(2);
  if (ret < 0)
    {
      _err("ERROR: QSPI2 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI3
  ret = tc487_qspi_initialize(3);
  if (ret < 0)
    {
      _err("ERROR: QSPI3 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI4
  ret = tc487_qspi_initialize(4);
  if (ret < 0)
    {
      _err("ERROR: QSPI4 init failed: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_TC4X_QSPI4_LOOPBACK
  tc487_qspi4_loopback_test();
#endif
#endif

#ifdef CONFIG_TC4X_QSPI5
  ret = tc487_qspi_initialize(5);
  if (ret < 0)
    {
      _err("ERROR: QSPI5 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI6
  ret = tc487_qspi_initialize(6);
  if (ret < 0)
    {
      _err("ERROR: QSPI6 init failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI7
  ret = tc487_qspi_initialize(7);
  if (ret < 0)
    {
      _err("ERROR: QSPI7 init failed: %d\n", ret);
      return ret;
    }
#endif

  return ret;
}

#endif /* CONFIG_TC4X_QSPI */
