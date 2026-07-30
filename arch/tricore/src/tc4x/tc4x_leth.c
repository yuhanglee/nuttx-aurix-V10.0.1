/****************************************************************************
 * arch/tricore/src/tc4xx/tc4x_leth.c
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

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/bits.h>
#include <nuttx/arch.h>

#include "tricore_internal.h"
#include "tc4x_leth.h"
#include "hardware/tc4x_clock.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

int xgmac_initialize(int intf);

/* Enable the 50 MHz RMII reference clock (PERCCUCON1.LETH100PERON).  Done
 * here rather than in up_clockconfig() so the extra clock domain only
 * comes up during ethernet bring-up, well after the console is running.
 */

static void leth_enable_refclk(void)
{
  uint32_t val = getreg32(TC4X_CLOCK_BASE + TC4X_CLOCK_PERCCUCON1_OFFSET);

  val |= TC4X_PERCCUCON1_LETH100PERON;

  while (getreg32(TC4X_CLOCK_BASE + TC4X_CLOCK_CCUSTAT_OFFSET) &
         TC4X_CCUSTAT_LCK)
    {
    }

  putreg32(val, TC4X_CLOCK_BASE + TC4X_CLOCK_PERCCUCON1_OFFSET);
}

uint32_t xgmac_read(uintptr_t reg)
{
  return getreg32(reg);
}

void xgmac_write(uint32_t value, uintptr_t reg)
{
  putreg32(value, reg);
}

void xgmac_modreg(uint32_t value, uint32_t mask, uintptr_t reg)
{
  modreg32(value, mask, reg);
}

/* Enable the LETH module clock, release the kernel reset and open the
 * access-protection so the DWC MAC core becomes reachable.  Port 0 is
 * ungated by clearing its PEN disable bit (PEN uses inverted polarity).
 */

static int init_leth(void)
{
  int i;

  xgmac_modreg(0, LETH_CLC_DISR, TC4XX_LETH_CLC);
  for (i = 0; i < 100; i++)
    {
      if ((xgmac_read(TC4XX_LETH_CLC) & LETH_CLC_DISS) == 0)
        break;
      up_mdelay(1);
    }

  if (i == 100)
    return -EIO;

  xgmac_modreg(LETH_RST_CTRLB_ENDINIT, LETH_RST_CTRLB_ENDINIT,
               TC4XX_LETH_RST_CTRLB);
  xgmac_modreg(LETH_RST_CTRL_RST, LETH_RST_CTRL_RST, TC4XX_LETH_RST_CTRLA);
  xgmac_modreg(LETH_RST_CTRL_RST, LETH_RST_CTRL_RST, TC4XX_LETH_RST_CTRLB);

  for (i = 0; i < 100; i++)
    {
      if (xgmac_read(TC4XX_LETH_RST_STAT) & LETH_RST_STAT_RSTSTAT)
        break;
      up_mdelay(1);
    }

  if (i == 100)
    {
      nerr("LETH kernel reset timeout\n");
      return -EIO;
    }

  xgmac_modreg(LETH_RST_CTRLB_ENDINIT, LETH_RST_CTRLB_ENDINIT,
               TC4XX_LETH_RST_CTRLB);

  /* Enable Port 0 clock (bit low-active) and wait for the status bit */

  xgmac_modreg(0, LETH_PEN_P0DIS, TC4XX_LETH_PEN);
  for (i = 0; i < 100; i++)
    {
      if ((xgmac_read(TC4XX_LETH_PEN) & LETH_PEN_P0STAT) == 0)
        break;
      up_mdelay(1);
    }

  /* Open read and write access for every master to the global, per-MAC and
   * per-DMA-channel register regions.  The DMA channel block in particular
   * is read-protected out of reset; without opening ACCENCH the CPU takes a
   * bus error on the first DMA register access in xgmac_initialize.
   */

  xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_GBL_WRA);
  xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_GBL_WRB);
  xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_GBL_RDA);
  xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_GBL_RDB);

  for (i = 0; i < LETH_ACCEN_NMAC; i++)
    {
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_MAC_WRA(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_MAC_WRB(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_MAC_RDA(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_MAC_RDB(i));
    }

  for (i = 0; i < LETH_ACCEN_NCH; i++)
    {
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_CH_WRA(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_CH_WRB(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_CH_RDA(i));
      xgmac_write(0xFFFFFFFF, TC4XX_LETH_ACCEN_CH_RDB(i));
    }

  return OK;
}

/* Select the RMII interface for Port 0.  EPR is latched by the MAC on the
 * DMA software reset, so this must run before xgmac_initialize().  All
 * RMII data inputs use source A (reset default 0); only MDIO uses source
 * E and needs its mux plus MDIOEN programmed.
 */

static void init_leth_rmii(void)
{
#if CONFIG_DWXGMAC_LINK_SPEED == 10
  uint32_t epr = LETH_PORTCTRL0_EPR_RMII10;
#else
  uint32_t epr = LETH_PORTCTRL0_EPR_RMII100;
#endif
  uint32_t src = CONFIG_TRICORE_LETH_RMII_RX_SRC;

  /* Select the interface mode, route each RMII RX input to its source and
   * enable the RX data path.  RXDEN gates reception; without it (and the
   * per-signal input selects) the MAC receives no frames.  TC14EN must stay
   * cleared for RMII -- it is only for the TC1.4 interface.
   */

  xgmac_modreg((epr << LETH_PORTCTRL0_EPR_SHIFT) |
               LETH_PORTCTRL0_RXDEN |
               (src << LETH_PORTCTRL0_RXDV_SHIFT) |
               (src << LETH_PORTCTRL0_RXD0_SHIFT) |
               (src << LETH_PORTCTRL0_RXD1_SHIFT) |
               (src << LETH_PORTCTRL0_RXCLK_SHIFT),
               LETH_PORTCTRL0_EPR_MASK | LETH_PORTCTRL0_TC14EN |
               LETH_PORTCTRL0_RXDEN | LETH_PORTCTRL0_RXDV_MASK |
               LETH_PORTCTRL0_RXD0_MASK | LETH_PORTCTRL0_RXD1_MASK |
               LETH_PORTCTRL0_RXCLK_MASK,
               TC4XX_LETH_PORTCTRL0(0));

  xgmac_modreg(LETH_PORTCTRL1_MDIO_SRC_E | LETH_PORTCTRL1_MDIOEN,
               LETH_PORTCTRL1_MDIO_MASK | LETH_PORTCTRL1_MDIOEN,
               TC4XX_LETH_PORTCTRL1(0));
}

/* Program the bridge for one-port operation on Port 0: Q_CH_MAPPING_EN=0
 * and PORT_SEL=0 (both cleared), enable TxQ0/RxC0 on the port and bind all
 * DMA channels to Port 0.
 */

static void init_leth_bridge(void)
{
  xgmac_modreg(LETH_BR_MAX_PKT_LEN,
               LETH_BR_FWDCTRL_QCHMAP_EN | LETH_BR_FWDCTRL_PORT_SEL |
               LETH_BR_FWDCTRL_MAXPKT, TC4XX_LETH_BR_FWDCTRL);

  xgmac_write(LETH_BR_PORT_CTRL_TXQ0 | LETH_BR_PORT_CTRL_RXC0,
              TC4XX_LETH_BR_PORT_CTRL(0));

  xgmac_write(0, TC4XX_LETH_BR_DMA_PORT_SEL);
  xgmac_write(0, TC4XX_LETH_BR_FLUSH_LPBK);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int tc4x_leth_init(int intf)
{
  int ret;

  leth_enable_refclk();

  ret = init_leth();
  if (ret < 0)
    return ret;

  init_leth_rmii();

  ret = xgmac_initialize(intf);
  if (ret < 0)
    return ret;

  init_leth_bridge();

  return OK;
}
