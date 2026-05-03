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
#define BOARD_PHY_ADDR        1
#define BOARD_PHYID1          MII_PHYID1_RTL8211F
#define BOARD_PHY_ANY_ID      0xFFFFu

#define PHY_RESET_TIMEOUT_MS  100
#define PHY_LINK_TIMEOUT      0x4FFFF
#define PHY_ANEG_TIMEOUT      0x4FFFF

struct mdio_bus_s *dwxgmac_mdio_bus_initialize(uint8_t port, uintptr_t base);

int tc4x_geth_init(int intf);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tc4x7_config_geth_pins(void)
{
  gpio_pinset_t pins[] =
    {
      /* MDC: P16.11 (alt10 = MDC_O).  MDIO: P16.14 — open-drain
       * bidirectional; Lite Kit's working pattern is GPIO_INPUT with
       * pull disabled so the input buffer stays armed for PHY ACK.
       */
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN11, GPIO_PERIPH, GPIO_ALT10),
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
  /* Triboard TC4X7 COM has two RTL822x PHY ports.  Per Zephyr DTS,
   * port-A reset is on P00.10 and port-B reset on P00.11.  Cycle
   * both — we only end up using one but releasing the wrong one
   * leaves it stuck in reset (PHYID reads as 0x0000).
   */

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

/* Probe MDIO addresses 0..31 and return the first responder.  -ENODEV
 * if none.  Avoids the "PHY at non-default addr -> wrong BOARD_PHY_ADDR
 * -> autoneg loop hangs board_app_initialize()" failure mode.
 */
static int tc4x7_phy_scan(struct mdio_bus_s *mdio)
{
  uint16_t id1;
  uint16_t id2;

  for (uint8_t a = 0; a < 32; a++)
    {
      id1 = id2 = 0;
      mdio_read(mdio, a, MII_PHYID1, &id1);
      mdio_read(mdio, a, MII_PHYID2, &id2);
      if ((id1 != 0x0000 && id1 != 0xFFFF) ||
          (id2 != 0x0000 && id2 != 0xFFFF))
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

  ret = mdio_write(mdio, phyaddr, MII_MCR, MII_MCR_RESET);
  if (ret < 0)
    {
      printf("ERROR: PHY reset write failed: %d\n", ret);
      return ret;
    }

  for (to = PHY_RESET_TIMEOUT_MS; to > 0; to -= 10)
    {
      up_mdelay(10);
      phyval = 0xFFFF;
      ret = mdio_read(mdio, phyaddr, MII_MCR, &phyval);
      if (ret < 0)
        {
          printf("ERROR: PHY MCR read failed: %d\n", ret);
          return ret;
        }

      if ((phyval & MII_MCR_RESET) == 0)
        break;
      printf("phy val %x\n", phyval);
    }

  if (to <= 0)
    {
      printf("ERROR: PHY reset timeout\n");
      return -ETIMEDOUT;
    }

  printf("PHY reset complete in %d ms\n", PHY_RESET_TIMEOUT_MS - to);

  ret = mdio_read(mdio, phyaddr, MII_PHYID1, &phyval);
  if (ret < 0)
    {
      nerr("ERROR: Failed to read PHYID1: %d\n", ret);
      return ret;
    }

  /* Triboard's RTL8221B reports a different PHYID1 LSB than the
   * RTL8211F that BOARD_PHYID1 still encodes — accept any non-zero
   * ID and just log it, until the audit confirms the exact value.
   * A zero ID is still a hard fail because that's the no-ACK case.
   */

  if (phyval == 0x0000 || phyval == 0xFFFF)
    {
      printf("ERROR: PHYID1 reads as 0x%04x (no PHY ACK on addr %u)\n",
             phyval, (unsigned)phyaddr);
      return -ENXIO;
    }

  printf("%s detected at addr %u, PHYID1=0x%04x (expected ~0x%04x)\n",
         BOARD_PHY_NAME, (unsigned)phyaddr, phyval, BOARD_PHYID1);

  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_PAGSR, 0xd08);
  mdio_read(mdio, phyaddr, 0x11, &phyval);
  mdio_write(mdio, phyaddr, 0x11, (phyval | 0x100));
  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_PAGSR, 0);

  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_PAGSR, 0xd04);
  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_LCR, 0x8170);
  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_EEELCR, 0x0);
  mdio_write(mdio, phyaddr, MII_RTL8211F_MDIO_PAGSR, 0);

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

  ret = tc4x_geth_init(0);
  if (ret < 0)
    {
      nerr("ERROR: xgmac_initialize failed: %d\n", ret);
      return ret;
    }

  mdio = dwxgmac_mdio_bus_initialize(0, CONFIG_DWXGMAC_BASE);
  if (mdio == NULL)
    {
      printf("ERROR: MDIO bus init failed\n");
      return -ENODEV;
    }

  tc4x7_init_phy(mdio);

  return OK;
}
