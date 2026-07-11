/****************************************************************************
 * boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/net/mii.h>
#include <nuttx/net/mdio.h>

#include "tc4x_geth.h"
#include "tc4xx_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Triboard TC4X7 COM places two RTL8221x PHYs at MDIO addresses 1 and
 * 2 (Zephyr DTS `phy@1` / `phy@2`); the boot reset GPIOs P00.10 and
 * P00.11 likewise belong to those addresses, not addr 0.  Reading at
 * 0 returns PHYID1 = 0x0000 because no PHY ACKs and the dwxgmac
 * c22-read silently zero-fills.  Use port-A (addr 1) here.
 *
 * The PHYID1 the RTL8221B actually reports is 0x001c with a vendor-
 * specific lower nibble that differs from the RTL8211F.  Use
 * BOARD_PHY_ANY_ID = 0xFFFF to skip the strict equality check until
 * we know the part's MMD model number; once HSPHY/USXGMII is up we
 * can tighten this back.  See triboard-phy-gaps.md.
 */
#define BOARD_PHY_NAME        "RTL8221B-CG"
#define BOARD_PHY_ADDR        2

#define TC4X7_MDIO_PORT       1

#define TC4X7_GETH_INTF       1
#define BOARD_PHYID1          MII_PHYID1_RTL8211F
#define BOARD_PHY_ANY_ID      0xFFFFu

#define PHY_RESET_TIMEOUT_MS  100
#define PHY_LINK_TIMEOUT      0x4FFFF
#define PHY_ANEG_TIMEOUT      0x4FFFF

struct mdio_bus_s *dwxgmac_mdio_bus_initialize(uint8_t port, uintptr_t base);

int tc4x_geth_init(int intf, const struct tc4x_geth_sigmux_s *mux);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const struct tc4x_geth_sigmux_s g_geth_sigmux =
{
  .mdio = 1,
};

static void tc4x7_config_geth_pins(void)
{
  gpio_pinset_t pins[] =
    {
      /* MDC: P16.11 alt8 (GETH0_P1_MDC_P16_11)
       * MAC1's MDC is at alt8 on the same physical pad as MAC0's alt7).
       * MDIO: P16.14 open-drain bidirectional, input mux B src 15.
       */
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN11, GPIO_PERIPH, GPIO_ALT8),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN14, GPIO_INPUT,  GPIO_PULL_NONE),

      AURIX_GPIO(GPIO_PORT0, GPIO_PIN10, GPIO_OUTPUT, GPIO_ALT0),

      AURIX_GPIO(GPIO_PORT10, GPIO_PIN2, GPIO_INPUT, GPIO_PULL_NONE),
    };

  for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++)
    {
      aurix_config_gpio(pins[i]);
    }
}

static void tc4x7_phy_reset(void)
{
  gpio_pinset_t rst_a = AURIX_GPIO(GPIO_PORT0, GPIO_PIN10,
                                   GPIO_OUTPUT, GPIO_ALT0);
  gpio_pinset_t rst_b = AURIX_GPIO(GPIO_PORT0, GPIO_PIN11,
                                   GPIO_OUTPUT, GPIO_ALT0);

  aurix_config_gpio(rst_b);

  aurix_gpio_write(rst_a, false);
  aurix_gpio_write(rst_b, false);
  up_mdelay(10);
  aurix_gpio_write(rst_a, true);
  aurix_gpio_write(rst_b, true);

  /* RTL822x datasheet: "Reset Configuration Setup Time" >= 70 ms
   * after RST# release before MDIO becomes responsive.
   */

  up_mdelay(100);
}

#define TC4X7_MDIO_SCA(port)        (CONFIG_DWXGMAC_BASE + 0x200 + (port) * 0x2000)
#define TC4X7_MDIO_SCCD(port)       (CONFIG_DWXGMAC_BASE + 0x204 + (port) * 0x2000)
#define TC4X7_MDIO_CLAUSE(port)     (CONFIG_DWXGMAC_BASE + 0x220 + (port) * 0x2000)
#define TC4X7_MDIO_CMD_READ         (0x3u << 16)
#define TC4X7_MDIO_CMD_WRITE        (0x1u << 16)
#define TC4X7_MDIO_SBUSY            BIT(22)
#define TC4X7_MDIO_DATA_MASK        0xFFFFu

#define RTL_MMD_PMAPMD              1
#define RTL_MMD_PCS                 3
#define RTL_MMD_AN                  7
#define RTL_MMD_VEND1               0x1E
#define RTL_MMD_VEND2               0x1F

extern uint32_t xgmac_read(uintptr_t reg);
extern void xgmac_write(uint32_t value, uintptr_t reg);
extern void xgmac_modreg(uint32_t value, uint32_t mask, uintptr_t reg);
extern void dwxgmac_carrier(int intf, bool up);

static int tc4x7_c45_wait_idle(uint8_t port)
{
  int i;

  for (i = 0; i < 100; i++)
    {
      if ((xgmac_read(TC4X7_MDIO_SCCD(port)) & TC4X7_MDIO_SBUSY) == 0)
        return OK;
      up_mdelay(1);
    }

  return -ETIMEDOUT;
}

static void tc4x7_c45_select(uint8_t port, uint8_t phyaddr)
{
  /* CLAUSE bit for phyaddr = 0 -> C45 mode. */

  xgmac_write(0u, TC4X7_MDIO_CLAUSE(port));
  (void)phyaddr;
}

static int tc4x7_c45_write(uint8_t port, uint8_t phyaddr, uint8_t devaddr,
                           uint16_t regaddr, uint16_t value)
{
  int ret;

  ret = tc4x7_c45_wait_idle(port);
  if (ret < 0) return ret;

  tc4x7_c45_select(port, phyaddr);

  xgmac_write(((uint32_t)devaddr << 21) |
              ((uint32_t)phyaddr << 16) |
              (uint32_t)regaddr,
              TC4X7_MDIO_SCA(port));

  xgmac_write(TC4X7_MDIO_CMD_WRITE | TC4X7_MDIO_SBUSY | value,
              TC4X7_MDIO_SCCD(port));

  return tc4x7_c45_wait_idle(port);
}

static int tc4x7_c45_read(uint8_t port, uint8_t phyaddr, uint8_t devaddr,
                          uint16_t regaddr, uint16_t *value)
{
  int ret;

  ret = tc4x7_c45_wait_idle(port);
  if (ret < 0) return ret;

  tc4x7_c45_select(port, phyaddr);

  xgmac_write(((uint32_t)devaddr << 21) |
              ((uint32_t)phyaddr << 16) |
              (uint32_t)regaddr,
              TC4X7_MDIO_SCA(port));

  xgmac_write(TC4X7_MDIO_CMD_READ | TC4X7_MDIO_SBUSY,
              TC4X7_MDIO_SCCD(port));

  ret = tc4x7_c45_wait_idle(port);
  if (ret < 0) return ret;

  *value = (uint16_t)(xgmac_read(TC4X7_MDIO_SCCD(port)) & TC4X7_MDIO_DATA_MASK);
  return OK;
}

static int tc4x7_rtl8221b_serdes_init(uint8_t phyaddr)
{
  const uint8_t port = TC4X7_MDIO_PORT;
  uint16_t v;
  int ret;
  int i;

  /* GPIO reset in tc4x7_phy_reset() has already put the PHY in a
   * known state. Skip the C45 soft-reset fallback that RTL8221B
   * doesn't self-clear reliably.
   */

  /* Enable SerDes */
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x75F3, 0);
  if (ret < 0) return ret;

  /* Select SerDes interface: 2500Base-X + SGMII (mode 0) */
  ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_VEND1, 0x697A, &v);
  if (ret < 0) return ret;
  v &= ~0x3u;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x697A, v);
  if (ret < 0) return ret;

  /* Vendor-magic SerDes tune */
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x6A04, 0x0503);
  if (ret < 0) return ret;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x6F10, 0xD455);
  if (ret < 0) return ret;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x6F11, 0x8020);
  if (ret < 0) return ret;

  /* Force PCS re-init: set VEND2.0xA400 bit 14, poll link, clear bit. */
  ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_VEND2, 0xA400, &v);
  if (ret < 0) return ret;
  v |= 0x4000u;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND2, 0xA400, v);
  if (ret < 0) return ret;

  for (i = 0; i < 1000; i++)
    {
      ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_VEND2, 0xA434, &v);
      if (ret < 0) return ret;
      if (v & 0x4)
        break;
      up_mdelay(1);
    }

  ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_VEND2, 0xA400, &v);
  if (ret < 0) return ret;
  v &= ~0x4000u;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND2, 0xA400, v);
  if (ret < 0) return ret;

  /* SerDes auto-negotiation ON */
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x7588, 0x0002);
  if (ret < 0) return ret;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x7589, 0x70D0);
  if (ret < 0) return ret;
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND1, 0x7587, 0x0003);
  if (ret < 0) return ret;

  /* Copper-side AN: advertise 1000BASE-T full duplex, restart AN. */

  ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_VEND2, 0xA412, &v);
  if (ret < 0) return ret;
  v |= BIT(9); /* 1000BASE-T full-duplex */
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_VEND2, 0xA412, v);
  if (ret < 0) return ret;

  ret = tc4x7_c45_read(port, phyaddr, RTL_MMD_AN, 0x0000, &v);
  if (ret < 0) return ret;
  v |= BIT(12) | BIT(9); /* AN_ENABLE | RESTART_AN */
  ret = tc4x7_c45_write(port, phyaddr, RTL_MMD_AN, 0x0000, v);
  if (ret < 0) return ret;

  return OK;
}

static int tc4x7_phy_scan(struct mdio_bus_s *mdio)
{
  uint16_t id1;
  uint16_t id2;
  uint8_t  order[32];
  int      n = 0;
  int      i;

  order[n++] = BOARD_PHY_ADDR;
  for (uint8_t a = 1; a < 32; a++)
    {
      if (a != BOARD_PHY_ADDR)
        order[n++] = a;
    }

  for (i = 0; i < n; i++)
    {
      uint8_t a = order[i];
      id1 = id2 = 0;
      mdio_read(mdio, a, MII_PHYID1, &id1);
      mdio_read(mdio, a, MII_PHYID2, &id2);
      if (id1 != 0x0000 && id1 != 0xFFFF &&
          id2 != 0x0000 && id2 != 0xFFFF)
        {
          printf("phy_scan: PHY at addr %u (id1=0x%04x id2=0x%04x)\n",
                 (unsigned)a, id1, id2);
          return a;
        }
    }

  printf("phy_scan: no PHY responded on MDIO bus\n");
  return -ENODEV;
}

static int tc4x7_init_phy(struct mdio_bus_s *mdio)
{
  uint8_t phyaddr;
  uint16_t phyval;
  int ret;
  int to;
#ifdef CONFIG_BOARD_PHY_AUTONEG
  volatile uint32_t timeout;
#endif

  ret = tc4x7_phy_scan(mdio);
  if (ret < 0)
    {
      return ret;
    }

  phyaddr = (uint8_t)ret;

  ret = mdio_read(mdio, phyaddr, MII_PHYID1, &phyval);
  if (ret < 0)
    {
      nerr("ERROR: Failed to read PHYID1: %d\n", ret);
      return ret;
    }

  if (phyval == 0x0000 || phyval == 0xFFFF)
    {
      printf("ERROR: PHYID1 reads as 0x%04x (no PHY ACK on addr %u)\n",
             phyval, (unsigned)phyaddr);
      return -ENXIO;
    }

  printf("%s detected at addr %u, PHYID1=0x%04x\n",
         BOARD_PHY_NAME, (unsigned)phyaddr, phyval);

  (void)to;

  ret = tc4x7_rtl8221b_serdes_init(phyaddr);
  if (ret < 0)
    printf("WARN: SerDes MMD writes returned %d (continuing)\n", ret);

  printf("RTL8221B init done; polling link (max 15 s)...\n");

  for (to = 30000; to > 0; to -= 500)
    {
      uint16_t physr = 0;
      tc4x7_c45_read(TC4X7_MDIO_PORT, phyaddr, RTL_MMD_VEND2, 0xA434, &physr);
      if (physr & 0x4)
        {
          printf("RTL8221B copper link UP (PHYSR=0x%04x) after %d ms\n",
                 physr, 30000 - to);
          break;
        }
      if ((30000 - to) % 5000 == 0)
        {
          printf(" t=%d PHYSR=0x%04x\n", 30000 - to, physr);
        }
      up_mdelay(500);
    }

  if (to <= 0)
    {
      printf("RTL8221B copper link DOWN after 30 s\n");
      return -ENOTCONN;
    }

  /* Clear CL37 AN complete interrupt (bit 0 of VR_MII_AN_INTR_STS)
   * and poll for SGMII link.
   */
  *((volatile uint32_t *)0xF37E0008u) = 0;

  printf("XPCS1 already set up (TRM LPM sequence); polling SGMII...\n");

  for (to = 15000; to > 0; to -= 200)
    {
      volatile uint32_t an_intr = *((volatile uint32_t *)0xF37E0008u);
      volatile uint32_t rx_sts  = *((volatile uint32_t *)0xF3060180u);
      volatile uint32_t sr_mii  = *((volatile uint32_t *)0xF3FC0004u);

      /* Check AN_INTR_STS.CL37_ANSGM_STS[3] (bit 4) for link up. */
      if ((an_intr & BIT(4)) || (sr_mii & BIT(2)))
        {
          printf("XPCS1 link UP AN_INTR=0x%04x SR_MII_STS=0x%04x "
                 "RX_STS=0x%04x\n",
                 (unsigned)an_intr, (unsigned)sr_mii, (unsigned)rx_sts);
          dwxgmac_carrier(TC4X7_GETH_INTF, true);
          return OK;
        }
      if ((15000 - to) % 2000 == 0)
        {
          printf(" t=%d AN_INTR=0x%04x SR_MII=0x%04x RX_STS=0x%04x\n",
                 15000 - to, (unsigned)an_intr, (unsigned)sr_mii,
                 (unsigned)rx_sts);
        }
      up_mdelay(200);
    }

  printf("XPCS1 SGMII link DOWN after 15s — SerDes did not sync\n");
  return -ENOTCONN;

#ifdef CONFIG_BOARD_PHY_AUTONEG

  ninfo("%s: Waiting for link...\n", BOARD_PHY_NAME);

  for (timeout = 0; timeout < PHY_LINK_TIMEOUT; timeout++)
    {
      ret = mdio_read(mdio, phyaddr, MII_MSR, &phyval);
      if (ret < 0)
        {
          nerr("ERROR: PHY MSR read failed: %d\n", ret);
          return ret;
        }

      if (phyval & MII_MSR_LINKSTATUS)
        break;

      nxsig_usleep(100);
    }

  if (timeout >= PHY_LINK_TIMEOUT)
    {
      nerr("ERROR: Link timeout (MSR=0x%04x)\n", phyval);
      return -ETIMEDOUT;
    }

  ninfo("Link up (MSR=0x%04x)\n", phyval);

  ret = mdio_write(mdio, phyaddr, MII_MCR, MII_MCR_ANENABLE);
  if (ret < 0)
    {
      nerr("ERROR: Failed to enable autoneg: %d\n", ret);
      return ret;
    }

  for (timeout = 0; timeout < PHY_ANEG_TIMEOUT; timeout++)
    {
      ret = mdio_read(mdio, phyaddr, MII_MSR, &phyval);
      if (ret < 0)
        return ret;

      if (phyval & MII_MSR_ANEGCOMPLETE)
        break;

      nxsig_usleep(100);
    }

  if (timeout >= PHY_ANEG_TIMEOUT)
    {
      nerr("ERROR: Autoneg timeout\n");
      return -ETIMEDOUT;
    }

  ninfo("Autoneg complete (MSR=0x%04x)\n", phyval);

  ret = mdio_read(mdio, phyaddr, MII_RTL8211F_MDIO_PHYSR, &phyval);
  if (ret == OK)
    {
      bool is1000  = ((phyval & MII_RTL8211F_PHYSTS_SPEED1) != 0);
      bool is100   = ((phyval & MII_RTL8211F_PHYSTS_SPEED) != 0);
      bool duplex  = ((phyval & MII_RTL8211F_PHYSTS_DUPLEX) != 0);

      printf("Link: %s %s-duplex\n",
             is1000 ? "1000Mbps" : (is100 ? "100Mbps" : "10Mbps"),
             duplex ? "full" : "half");
    }

#else
  mdio_write(mdio, phyaddr, MII_MCR,
             MII_MCR_SPEED1000 | MII_MCR_FULLDPLX);
  up_mdelay(100);
#endif

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int tc4x7_ethernet_initialize(void)
{
  int ret;
  struct mdio_bus_s *mdio;

  tc4x7_config_geth_pins();
  tc4x7_phy_reset();

  ret = tc4x_geth_init(TC4X7_GETH_INTF, &g_geth_sigmux);
  if (ret < 0)
    {
      nerr("ERROR: xgmac_initialize failed: %d\n", ret);
      return ret;
    }

  dwxgmac_carrier(TC4X7_GETH_INTF, false);

  mdio = dwxgmac_mdio_bus_initialize(0, CONFIG_DWXGMAC_BASE + TC4X7_MDIO_PORT * 0x2000);
  if (mdio == NULL)
    {
      printf("ERROR: MDIO bus init failed\n");
      return -ENODEV;
    }

  tc4x7_init_phy(mdio);

  return OK;
}
