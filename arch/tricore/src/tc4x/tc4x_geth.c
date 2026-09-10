/****************************************************************************
 * drivers/net/tc4x_geth.c
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
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/bits.h>
#include <nuttx/arch.h>
#include <nuttx/net/mdio.h>

#include "tricore_internal.h"
#include "tc4x_geth.h"
#include "hardware/tc4x_port.h"


#define PORT_DRVCFG_REG(port, pin)                              \
  (AURIX_PORT_ADDR(port) + PORT_PADCFG_DRVCFG(pin))

#define PORT_PROTE_REG(port)                                    \
  (AURIX_PORT_ADDR(port) + PORT_ACCGRP_PROTE(0))

#define AURIX_PROT_STATE_RUN    0
#define AURIX_PROT_STATE_CONFIG 1
#define AURIX_PROT_SWEN         BIT(3)

static void tc4x_port_prot_config(uint8_t port)
{
  uintptr_t prot = PORT_PROTE_REG(port);
  uint32_t  val  = getreg32(prot);

  if ((val & 0x07) == AURIX_PROT_STATE_RUN)
    {
      putreg32(AURIX_PROT_STATE_CONFIG | AURIX_PROT_SWEN, prot);
    }
}

static void tc4x_port_prot_run(uint8_t port)
{
  putreg32(AURIX_PROT_STATE_RUN | AURIX_PROT_SWEN, PORT_PROTE_REG(port));
}

static void tc4x_port_set_drvcfg(uint8_t port, uint8_t pin, uint32_t val)
{
  uint32_t mask = PORT_DRVCFG_DIR | PORT_DRVCFG_OD | PORT_DRVCFG_MODE_MASK;

  tc4x_port_prot_config(port);
  modreg32(val & mask, mask, PORT_DRVCFG_REG(port, pin));
  tc4x_port_prot_run(port);
}

static void tc4x_port_set_output_alt(uint8_t port, uint8_t pin, uint8_t alt)
{
  tc4x_port_set_drvcfg(port, pin,
                       ((uint32_t)alt << PORT_DRVCFG_MODE_SHIFT) |
                       PORT_DRVCFG_DIR);
}

static void tc4x_port_set_input(uint8_t port, uint8_t pin)
{
  tc4x_port_set_drvcfg(port, pin, 0);
}

int xgmac_initialize(int intf);
int dwxgmac_mac_isr(int irq, FAR void *context, FAR void *arg);

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

static void pms_enable_supply(uintptr_t con, uintptr_t rst, uintptr_t stat)
{
  int i;

  xgmac_modreg(PMS_VMONP_OVE, PMS_VMONP_OVE, con);

  /* Wait for supply to settle -- poll STAT.RESULT > STAT.RESETVAL */
  if (stat) {
    for (i = 0; i < 100; i++)
      {
        up_mdelay(1);
        uint32_t s = xgmac_read(stat);
        uint8_t result = (s >> 8) & 0xFF;
        uint8_t resetval = (s >> 0) & 0xFF;
        if (result > resetval)
          break;
      }
  }

  if (rst) {
    xgmac_modreg(PMS_VMONP_RESETOFF_P,
                 PMS_VMONP_RESETOFF | PMS_VMONP_RESETOFF_P,
                 rst);
  }
}

static void init_pms_supplies(void)
{
  pms_enable_supply(PMS_VMONP_VDDHSIFCON, PMS_VMONP_VDDHSIFRST,
                    PMS_VMONP_VDDHSIFSTAT);

  pms_enable_supply(PMS_VMONP_VDDPHY0CON, 0, PMS_VMONP_VDDPHY0STAT);
  pms_enable_supply(PMS_VMONP_VDDPHY1CON, 0, 0);
  pms_enable_supply(PMS_VMONP_VDDPHY2CON, 0, 0);

#ifdef CONFIG_TRICORE_GETH_IF_SGMII
  /* Triboard SGMII SerDes needs the VDDPH_PHY analog rails too. */
  pms_enable_supply(PMS_VMONP_VDDPHPHY0CON, PMS_VMONP_VDDPHPHY0RST,
                    PMS_VMONP_VDDPHPHY0STAT);
  pms_enable_supply(PMS_VMONP_VDDPHPHY1CON, PMS_VMONP_VDDPHPHY1RST,
                    PMS_VMONP_VDDPHPHY1STAT);
  pms_enable_supply(PMS_VMONP_VDDPHPHY2CON, PMS_VMONP_VDDPHPHY2RST,
                    PMS_VMONP_VDDPHPHY2STAT);
#endif
}

static int init_hsphy_mode(int port,
                           const struct tc4x_geth_sigmux_s *mux)
{
  int i;

  /* Enable HSPHY clock */
  xgmac_modreg(0, BIT(0), TC4XX_HSPHY_CLC);
  for (i = 0; i < 100; i++)
    {
      if ((xgmac_read(TC4XX_HSPHY_CLC) & BIT(1)) == 0)
        break;
      up_mdelay(1);
    }

  if (i == 100)
    {
      nerr("HSPHY CLC enable timeout\n");
      return -EIO;
    }

  /* Kernel reset HSPHY */
  xgmac_modreg(BIT(31), BIT(31), TC4XX_HSPHY_RSTB);
  xgmac_modreg(BIT(0), BIT(0), TC4XX_HSPHY_RSTA);
  xgmac_modreg(BIT(0), BIT(0), TC4XX_HSPHY_RSTB);

  for (i = 0; i < 100; i++)
    {
      if (xgmac_read(TC4XX_HSPHY_RSTSTAT) & BIT(0))
        break;
      up_mdelay(1);
    }

  if (i == 100)
    {
      nerr("HSPHY kernel reset timeout\n");
      return -EIO;
    }

#ifdef CONFIG_TRICORE_GETH_IF_SGMII
  /* PHYCTRL1: FSP=0 (XGMAC function), PRS=0 (internal refclk),
   * PWRDWN=0 -> wait 25us -> RST=0.
   */
  xgmac_modreg(0, BIT(0) | BIT(1) | BIT(8), TC4XX_HSPHY_PHYCTRL1(port));
  up_udelay(25);
  xgmac_modreg(0, BIT(7), TC4XX_HSPHY_PHYCTRL1(port));

  /* IntfMode[30:28]=SGMII(=4) plus this board's MDIO mux. */

  xgmac_modreg((4u << 28) |
               ((mux->mdio << GETH_ETH_MDIO_SHIFT) & GETH_ETH_MDIO_MASK) |
               GETH_ETH_MDIOEN,
               GENMASK(30, 28) | GETH_ETH_MDIO_MASK | GETH_ETH_MDIOEN,
               TC4XX_HSPHY_ETH(port));

  for (i = 0; i < 100; i++)
    {
      if (xgmac_read(TC4XX_HSPHY_PHYCTRL1(port)) & BIT(3))
        break;
      up_mdelay(1);
    }

  if (i == 100)
    {
      nerr("HSPHY PHY%d INITDONE timeout\n", port);
      return -EIO;
    }

  xgmac_modreg(BIT(4), BIT(4), TC4XX_HSPHY_PHYCTRL1(port));
#else
  xgmac_modreg(0, BIT(8), TC4XX_HSPHY_PHYCTRL1(port));
  up_udelay(25);
  xgmac_modreg(0, BIT(7), TC4XX_HSPHY_PHYCTRL1(port));

  xgmac_modreg(BIT(29), GENMASK(30,28), TC4XX_HSPHY_ETH(port)); // rmii

  xgmac_modreg(((mux->rxd0 << GETH_ETH_RXD0_SHIFT) & GETH_ETH_RXD0_MASK) |
               ((mux->rxd1 << GETH_ETH_RXD1_SHIFT) & GETH_ETH_RXD1_MASK),
               GETH_ETH_RXD0_MASK | GETH_ETH_RXD1_MASK,
               TC4XX_HSPHY_ETH(port));
  xgmac_modreg((mux->refclk << GETH_ETH_REFCLK_SHIFT) & GETH_ETH_REFCLK_MASK,
               GETH_ETH_REFCLK_MASK, TC4XX_HSPHY_ETH(port));
  xgmac_modreg((mux->crsdv << GETH_ETH_CRSDV_SHIFT) & GETH_ETH_CRSDV_MASK,
               GETH_ETH_CRSDV_MASK, TC4XX_HSPHY_ETH(port));


  xgmac_modreg(((mux->mdio << GETH_ETH_MDIO_SHIFT) & GETH_ETH_MDIO_MASK) |
               GETH_ETH_MDIOEN,
               GETH_ETH_MDIO_MASK | GETH_ETH_MDIOEN,
               TC4XX_HSPHY_ETH(port));
#endif

  xgmac_modreg(BIT(4), BIT(4), TC4XX_HSPHY_CMNCFG);

#ifndef CONFIG_TRICORE_GETH_IF_SGMII
  if (port == 0)
    {
      /* TX outputs: alt10 per BGA292 RMIIC pin map. */
      tc4x_port_set_output_alt(16,  6, 10);  /* TXD0 */
      tc4x_port_set_output_alt(16,  8, 10);  /* TXD1 */
      tc4x_port_set_output_alt(16, 13, 10);  /* TXEN */
      tc4x_port_set_output_alt(21,  2,  8);  /* MDC alt8 */

      /* RX inputs: PHY drives, no pull. */
      tc4x_port_set_input(16, 0);  /* RXD1 D */
      tc4x_port_set_input(16, 1);  /* CRS_DV C */
      tc4x_port_set_input(16, 2);  /* REFCLK D */
      tc4x_port_set_input(16, 4);  /* RXD0 D */

    }
#endif

  return OK;
}

static int xgmac_mac_bridge_isr(int irq, FAR void *context, FAR void *arg)
{
  int32_t bridge_sts;

  bridge_sts = xgmac_read(TC4XX_BRIDGE_INTR_STATUS);

  if (!(bridge_sts & BIT(16)))
    return OK;  /* Not a DMA interrupt */

  dwxgmac_mac_isr(irq, context, arg);

  return OK;
}

static int init_geth(int port)
{
  int i;

  /* Enable GETH clock */
  xgmac_modreg(0, BIT(0), TC4XX_ETH_CLC);
  for (i = 0; i < 100; i++)
    {
      if ((xgmac_read(TC4XX_ETH_CLC) & BIT(1)) == 0)
        break;
      up_mdelay(1);
    }

  if (i == 100)
    return -EIO;

  xgmac_modreg(BIT(31), BIT(31), TC4XX_ETH_RST_CTRLB);
  xgmac_modreg(BIT(0), BIT(0), TC4XX_ETH_RST_CTRLA);
  xgmac_modreg(BIT(0), BIT(0), TC4XX_ETH_RST_CTRLB);

  for (i = 0; i < 100; i++)
    {
      if (xgmac_read(TC4XX_ETH_RST_STAT) & BIT(0))
        break;
      up_mdelay(1);
    }

  if (i == 100)
    {
      nerr("GETH kernel reset timeout\n");
      return -EIO;
    }

  xgmac_modreg(BIT(31), BIT(31), TC4XX_ETH_RST_CTRLB);

  xgmac_modreg(BIT(port), BIT(port), TC4XX_ETH_MACEN);

  /* Open all access protection registers */
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_GBL_WRA);
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_GBL_WRB);

  /* Enable per-MAC access. Both MAC0 and MAC1 unconditionally
   * so MAC1 or MAC0 works from board.
   */
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_MAC0_WRA);
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_MAC0_WRB);
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_MAC1_WRA);
  xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_MAC1_WRB);

  for (i = 0; i < 8; i++)
    {
      xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_CH_WRA(i));
      xgmac_write(0xFFFFFFFF, TC4XX_ETH_ACCEN_CH_WRB(i));
    }

  return OK;
}

static void init_geth_bridge(int hw_port)
{
  if (hw_port == 1)
    {
      /* Triboard: single-port bridge mode routes to Port 1 (MAC1).
       * FWDCTRL bit 30 = PORT_SEL, bit 31 = Q_CH_MAPPING_EN.
       * Reset value is 0x40 (MAX_PKT_LENGTH); preserve it, add PORT_SEL=1.
       */
      xgmac_write(0x01u, TC4XX_BRIDGE_DMA_PORT_SEL);
      xgmac_modreg(BIT(30), BIT(30) | BIT(31), TC4XX_BRIDGE_FWDCTRL);
      xgmac_write(0x01000100u, TC4XX_BRIDGE_PORT1_CTRL);
    }
  else
    {
      /* DMA_PORT_SELECTION: assign all 8 DMA channels to port 0. */
      xgmac_write(0, TC4XX_BRIDGE_DMA_PORT_SEL);
      xgmac_write(0xFF00FF00u, TC4XX_BRIDGE_PORT0_CTRL);
    }

  /* Clear any stale port loopback / RxQ flush state. */
  xgmac_write(0, TC4XX_BRIDGE_BASE + 0x004);
}

int tc4x_geth_init(int intf, const struct tc4x_geth_sigmux_s *mux)
{
  int ret;

  init_pms_supplies();

  ret = init_hsphy_mode(intf, mux);
  if (ret < 0)
    return ret;

#ifdef CONFIG_TRICORE_GETH_IF_SGMII
  ret = tc4x_xpcs1_sgmii_1g_init();
  if (ret < 0)
    {
      nerr("XPCS1 SGMII init failed: %d\n", ret);
      return ret;
    }
#endif

  ret = init_geth(intf);
  if (ret < 0)
    return ret;

  ret = xgmac_initialize(intf);
  if (ret < 0)
    return ret;

  init_geth_bridge(intf);

  ret = irq_attach(CONFIG_TRICORE_GETH_BRIDGE_IRQ, xgmac_mac_bridge_isr, (void *)intf);
  if (ret < 0)
    return ret;

  up_enable_irq(CONFIG_TRICORE_GETH_BRIDGE_IRQ);

  return OK;
}
