/****************************************************************************
 * arch/tricore/src/tc4xx/tc4x_hsphy.c
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

#include <errno.h>
#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/arch.h>

#include "tricore_internal.h"
#include "hardware/tc4x_hsphy.h"


#define PMS_VMONP_VDDPHPHY0CON   0xF024909Cu
#define PMS_VMONP_VDDPHPHY0RST   0xF02490A0u
#define PMS_VMONP_VDDPHPHY0STAT  0xF02490A4u
#define PMS_VMONP_VDDPHPHY1CON   0xF02490A8u
#define PMS_VMONP_VDDPHPHY1RST   0xF02490ACu
#define PMS_VMONP_VDDPHPHY1STAT  0xF02490B0u
#define PMS_VMONP_VDDPHPHY2CON   0xF02490B4u
#define PMS_VMONP_VDDPHPHY2RST   0xF02490B8u
#define PMS_VMONP_VDDPHPHY2STAT  0xF02490BCu
#define PMS_VMONP_VDDHSIFCON     0xF02490C0u
#define PMS_VMONP_VDDHSIFRST     0xF02490C4u
#define PMS_VMONP_VDDHSIFSTAT    0xF02490C8u
#define PMS_VMONP_VDDPHY0CON     0xF0249114u
#define PMS_VMONP_VDDPHY0STAT    0xF024911Cu
#define PMS_VMONP_VDDPHY1CON     0xF0249120u
#define PMS_VMONP_VDDPHY2CON     0xF024912Cu

#define HSPHY_PMS_OVE         BIT(28)
#define HSPHY_PMS_RESETOFF    BIT(24)
#define HSPHY_PMS_RESETOFF_P  BIT(25)

static void hsphy_pms_enable(uintptr_t con, uintptr_t rst, uintptr_t stat)
{
  modreg32(HSPHY_PMS_OVE, HSPHY_PMS_OVE, con);

  if (stat)
    {
      int i;
      for (i = 0; i < 100; i++)
        {
          uint32_t s = getreg32(stat);
          uint8_t result   = (s >> 8) & 0xFF;
          uint8_t resetval = (s >> 0) & 0xFF;
          if (result > resetval)
            {
              break;
            }
          up_udelay(100);
        }
    }

  if (rst)
    {
      modreg32(HSPHY_PMS_RESETOFF_P,
               HSPHY_PMS_RESETOFF | HSPHY_PMS_RESETOFF_P,
               rst);
    }
}

static void hsphy_pms_enable_all(void)
{
  hsphy_pms_enable(PMS_VMONP_VDDHSIFCON,  PMS_VMONP_VDDHSIFRST,
                   PMS_VMONP_VDDHSIFSTAT);
  hsphy_pms_enable(PMS_VMONP_VDDPHY0CON,  0, PMS_VMONP_VDDPHY0STAT);
  hsphy_pms_enable(PMS_VMONP_VDDPHY1CON,  0, 0);
  hsphy_pms_enable(PMS_VMONP_VDDPHY2CON,  0, 0);
  hsphy_pms_enable(PMS_VMONP_VDDPHPHY0CON, PMS_VMONP_VDDPHPHY0RST,
                   PMS_VMONP_VDDPHPHY0STAT);
  hsphy_pms_enable(PMS_VMONP_VDDPHPHY1CON, PMS_VMONP_VDDPHPHY1RST,
                   PMS_VMONP_VDDPHPHY1STAT);
  hsphy_pms_enable(PMS_VMONP_VDDPHPHY2CON, PMS_VMONP_VDDPHPHY2RST,
                   PMS_VMONP_VDDPHPHY2STAT);
}


static inline void tc4x_hsphy_busy_us(uint32_t us)
{
  up_udelay(us);
}

static int tc4x_hsphy_clock_enable(uint32_t timeout_us)
{
  uint32_t clc;
  uint32_t waited = 0;

  clc = getreg32(TC4X_HSPHY_CLC);
  putreg32(clc & ~HSPHY_CLC_DISR, TC4X_HSPHY_CLC);

  while (getreg32(TC4X_HSPHY_CLC) & HSPHY_CLC_DISS)
    {
      if (waited >= timeout_us)
        {
          spierr("hsphy: CLC.DISS did not clear\n");
          return -ETIMEDOUT;
        }

      tc4x_hsphy_busy_us(10);
      waited += 10;
    }

  return OK;
}


int tc4x_hsphy_xspi_init(const struct tc4x_hsphy_xspi_cfg_s *cfg,
                         uint32_t timeout_us)
{
  uint32_t dll;
  uint32_t waited;
  int ret;

  if (cfg == NULL)
    {
      return -EINVAL;
    }


  hsphy_pms_enable_all();

  ret = tc4x_hsphy_clock_enable(timeout_us);
  if (ret < 0)
    {
      return ret;
    }


  modreg32(HSPHY_RST_STATCLR, HSPHY_RST_STATCLR, TC4X_HSPHY_RST_CTRLB);
  modreg32(HSPHY_RST_KRST,    HSPHY_RST_KRST,    TC4X_HSPHY_RST_CTRLA);
  modreg32(HSPHY_RST_KRST,    HSPHY_RST_KRST,    TC4X_HSPHY_RST_CTRLB);

  waited = 0;
  while (!(getreg32(TC4X_HSPHY_RST_STAT) & HSPHY_RST_KRST))
    {
      if (waited >= timeout_us)
        {
          spierr("hsphy: kernel reset timeout\n");
          return -ETIMEDOUT;
        }

      tc4x_hsphy_busy_us(10);
      waited += 10;
    }


  modreg32(0u, HSPHY_CMNCFG_FSR, TC4X_HSPHY_CMNCFG);


  dll = HSPHY_DLL_CFG_REFCLKSE;

  if (cfg->pen3vx)
    {
      dll |= HSPHY_DLL_CFG_PEN3VX;
    }


  if (!cfg->diff_clock)
    {
      dll |= HSPHY_DLL_CFG_PMODE;
    }

  if (cfg->fmode_high)
    {
      dll |= HSPHY_DLL_CFG_FMODE;
    }

  if (cfg->rxsamplesel)
    {
      dll |= HSPHY_DLL_CFG_RXSAMPLESEL;
    }

  dll |= ((uint32_t)cfg->txcfg << HSPHY_DLL_CFG_TXCFG_SHIFT) &
         HSPHY_DLL_CFG_TXCFG_MASK;
  dll |= ((uint32_t)cfg->rxcfg << HSPHY_DLL_CFG_RXCFG_SHIFT) &
         HSPHY_DLL_CFG_RXCFG_MASK;

  putreg32(dll, TC4X_HSPHY_DLL_CFG);


  dll |= HSPHY_DLL_CFG_POWER;
  putreg32(dll, TC4X_HSPHY_DLL_CFG);
  tc4x_hsphy_busy_us(10);

  dll |= HSPHY_DLL_CFG_TXEN;
  putreg32(dll, TC4X_HSPHY_DLL_CFG);
  tc4x_hsphy_busy_us(10);

  dll |= HSPHY_DLL_CFG_RXEN;
  putreg32(dll, TC4X_HSPHY_DLL_CFG);

  {
    extern int printf(const char *, ...);
    printf("HSPHY_DBG CLC=0x%08lx CMNCFG=0x%08lx DLL_CFG=0x%08lx "
           "RST_STAT=0x%08lx VDDHSIFSTAT=0x%08lx VDDPHY0STAT=0x%08lx "
           "VDDPHPHY0STAT=0x%08lx\n",
           (unsigned long)getreg32(TC4X_HSPHY_CLC),
           (unsigned long)getreg32(TC4X_HSPHY_CMNCFG),
           (unsigned long)getreg32(TC4X_HSPHY_DLL_CFG),
           (unsigned long)getreg32(TC4X_HSPHY_RST_STAT),
           (unsigned long)getreg32(PMS_VMONP_VDDHSIFSTAT),
           (unsigned long)getreg32(PMS_VMONP_VDDPHY0STAT),
           (unsigned long)getreg32(PMS_VMONP_VDDPHPHY0STAT));
  }

  /* Poll LOCKSTAT */

  waited = 0;
  while (!(getreg32(TC4X_HSPHY_DLL_CFG) & HSPHY_DLL_CFG_LOCKSTAT))
    {
      if (waited >= timeout_us)
        {
          spierr("hsphy: DLL lock timeout\n");
          return -ETIMEDOUT;
        }

      tc4x_hsphy_busy_us(10);
      waited += 10;
    }

  spiinfo("hsphy: DLL locked\n");
  return OK;
}
