/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_xspi.c
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

#include <stddef.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/spi/xspi.h>
#include <nuttx/spi/xspi_dw_ssi.h>

#include "hardware/tc4x_clock.h"
#include "hardware/tc4x_hsphy.h"
#include "hardware/tc4x_xspi.h"

#include "tricore_internal.h"

#define TC4X_PMS_VMONP_VDDHSIFCON   0xF02490C0u
#define TC4X_PMS_VMONP_VDDHSIFRST   0xF02490C4u
#define TC4X_PMS_VMONP_VDDHSIFSTAT  0xF02490C8u
#define TC4X_PMS_VMONP_OVE          (1u << 28)
#define TC4X_PMS_VMONP_RESETOFF     (1u << 24)
#define TC4X_PMS_VMONP_RESETOFF_P   (1u << 25)

uint32_t xspi_read(uintptr_t reg)
{
  return getreg32(reg);
}

void xspi_write(uint32_t value, uintptr_t reg)
{
  putreg32(value, reg);
}

void xspi_modreg(uint32_t value, uint32_t mask, uintptr_t reg)
{
  modreg32(value, mask, reg);
}


static inline void tc4x_xspi_busy_us(uint32_t us)
{
  up_udelay(us);
}

static void tc4x_xspi_ccu_wait_unlocked(void)
{
  while (getreg32(TC4X_CLOCK_BASE + TC4X_CLOCK_CCUSTAT_OFFSET) &
         TC4X_CCUSTAT_LCK)
    {
      __asm__ volatile ("nop");
    }
}


static void tc4x_xspi_ccu_enable(void)
{
  uintptr_t reg = TC4X_CLOCK_BASE + TC4X_CLOCK_PERCCUCON1_OFFSET;
  uint32_t  val;

  tc4x_xspi_ccu_wait_unlocked();
  val  = getreg32(reg);
  val &= ~TC4X_PERCCUCON1_XSPISLDIV_MASK;
  val |= ((1u << TC4X_PERCCUCON1_XSPISLDIV_SHIFT) &
          TC4X_PERCCUCON1_XSPISLDIV_MASK);
  val |= TC4X_PERCCUCON1_XSPIPERON | TC4X_PERCCUCON1_UP;
  putreg32(val, reg);
  tc4x_xspi_ccu_wait_unlocked();
}


static void tc4x_xspi_pms_enable_vddhsif(void)
{
  uint32_t stat;
  int      i;

  modreg32(TC4X_PMS_VMONP_OVE, TC4X_PMS_VMONP_OVE,
           TC4X_PMS_VMONP_VDDHSIFCON);

  for (i = 0; i < 100; i++)
    {
      tc4x_xspi_busy_us(1000);
      stat = getreg32(TC4X_PMS_VMONP_VDDHSIFSTAT);
      if (((stat >> 8) & 0xFFu) > (stat & 0xFFu))
        {
          break;
        }
    }

  modreg32(TC4X_PMS_VMONP_RESETOFF_P,
           TC4X_PMS_VMONP_RESETOFF | TC4X_PMS_VMONP_RESETOFF_P,
           TC4X_PMS_VMONP_VDDHSIFRST);
}


static int tc4x_xspi_module_clc_enable(uint32_t timeout_us)
{
  uint32_t waited = 0;

  putreg32(0, TC4X_XSPI0_CLC);

  while (getreg32(TC4X_XSPI0_CLC) & XSPI_CLC_DISS)
    {
      if (waited >= timeout_us)
        {
          spierr("xspi: CLC.DISS did not clear\n");
          return -ETIMEDOUT;
        }

      tc4x_xspi_busy_us(10);
      waited += 10;
    }

  return OK;
}


static const struct xspi_dw_ssi_config_s g_tc4x_xspi0_cfg =
{
  .base          = TC4X_XSPI0_BASE,
  .ssi_clk_hz    = CONFIG_TC4X_XSPI_SSI_CLK_HZ,
#ifdef CONFIG_TC4X_XSPI_PORTSEL_GPIO
  .use_gpio_pads = true,
#else
  .use_gpio_pads = false,
#endif
};

static struct xspi_dw_ssi_s g_tc4x_xspi0;

#ifndef CONFIG_TC4X_XSPI_PORTSEL_GPIO
static const struct tc4x_hsphy_xspi_cfg_s g_tc4x_hsphy_cfg =
{
  .pen3vx      = false,    /* 1.8 V S26HS */
  .diff_clock  = true,     /* PMODE=0: differential CK_P/CK_N */
  .fmode_high  = false,    /* FMODE=0: below 166 MHz for bring-up */
  .rxsamplesel = false,    /* RXSAMPLESEL=0: HyperBus uses RWDS strobe */
  .txcfg       = 0,
  .rxcfg       = 2,
};
#endif


FAR struct xspi_dev_s *tc4x_xspi_initialize(int port)
{
  if (port != 0)
    {
      return NULL;
    }

  tc4x_xspi_ccu_enable();

  tc4x_xspi_pms_enable_vddhsif();

#ifndef CONFIG_TC4X_XSPI_PORTSEL_GPIO
  if (tc4x_hsphy_xspi_init(&g_tc4x_hsphy_cfg, 100000) < 0)
    {
      return NULL;
    }
#endif

  if (tc4x_xspi_module_clc_enable(1000) < 0)
    {
      return NULL;
    }

  return xspi_dw_ssi_initialize(&g_tc4x_xspi0, &g_tc4x_xspi0_cfg);
}
