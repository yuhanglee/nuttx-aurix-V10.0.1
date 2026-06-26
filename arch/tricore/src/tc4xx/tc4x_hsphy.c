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
