/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375_ethernet.c
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
#include <stdio.h>
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/net/mii.h>
#include <nuttx/net/mdio.h>

#include "tc3xx_gpio.h"

/* KIT_A2G_TC375_LITE: DP83825I 10/100 PHY on RMII, MDIO address 0 */

#define BOARD_PHY_NAME        "DP83825I"
#define BOARD_PHY_ADDR        0

#define PHY_RESET_TIMEOUT_MS  500

#define PHY_LINK_TIMEOUT      500
#define PHY_ANEG_TIMEOUT      2000

struct mdio_bus_s *dwxgmac_mdio_bus_initialize(uint8_t port, uintptr_t base);

int tc3x_geth_init(int intf);

static void tc375_config_geth_pins(void)
{
  gpio_pinset_t pins[] =
    {
      /* RMII TX outputs: TXEN P11.6, TXD0 P11.3, TXD1 P11.2 (ALT6) */

      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN6, GPIO_PERIPH, GPIO_ALT6,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN3, GPIO_PERIPH, GPIO_ALT6,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN2, GPIO_PERIPH, GPIO_ALT6,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),

      /* RMII RX inputs: REFCLK P11.12, CRSDV P11.11, RXD0 P11.10,
       * RXD1 P11.9
       */

      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN12, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN11, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN10, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT11, GPIO_PIN9, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 3, 0, GPIO_PERIPH_OWN_PAD),

      /* MDIO bus: MDC P21.2 (ALT5), MDIO P21.3 */

      AURIX_GPIO(GPIO_PORT21, GPIO_PIN2, GPIO_PERIPH, GPIO_ALT5),
      AURIX_GPIO(GPIO_PORT21, GPIO_PIN3, GPIO_INPUT, GPIO_PULL_NONE),
    };

  for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++)
    {
      aurix_config_gpio(pins[i]);
    }
}

static int tc375_phy_scan(struct mdio_bus_s *mdio)
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
          printf("phy_scan: PHY found at addr %u (id1=0x%04x id2=0x%04x)\n",
                 (unsigned)a, id1, id2);
          return a;
        }
    }

  printf("phy_scan: no PHY responded on MDIO bus\n");
  return -ENODEV;
}

static int tc375_init_phy(struct mdio_bus_s *mdio)
{
  uint8_t phyaddr;
  uint16_t phyval;
  int ret;
  int to;
#ifdef CONFIG_BOARD_PHY_AUTONEG
  volatile uint32_t timeout;
#endif

  ret = tc375_phy_scan(mdio);
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
    }

  if (to <= 0)
    {
      printf("ERROR: PHY reset timeout\n");
      return -ETIMEDOUT;
    }

#ifdef CONFIG_BOARD_PHY_AUTONEG
  /* Restart autonegotiation and wait for link + aneg completion */

  ret = mdio_write(mdio, phyaddr, MII_MCR,
                   MII_MCR_ANENABLE | MII_MCR_ANRESTART);
  if (ret < 0)
    {
      printf("ERROR: Failed to enable autoneg: %d\n", ret);
      return ret;
    }

  for (timeout = 0; timeout < PHY_ANEG_TIMEOUT; timeout++)
    {
      ret = mdio_read(mdio, phyaddr, MII_MSR, &phyval);
      if (ret < 0)
        return ret;

      if (phyval & MII_MSR_ANEGCOMPLETE)
        break;

      up_mdelay(1);
    }

  if (timeout >= PHY_ANEG_TIMEOUT)
    {
      printf("%s: autoneg incomplete at boot (MSR=0x%04x); "
             "link will come up asynchronously\n", BOARD_PHY_NAME, phyval);
      return OK;
    }

  ret = mdio_read(mdio, phyaddr, MII_LPA, &phyval);
  if (ret == OK)
    {
      bool is100  = ((phyval & (MII_LPA_100BASETXFULL |
                                MII_LPA_100BASETXHALF)) != 0);
      bool duplex = ((phyval & (MII_LPA_100BASETXFULL |
                                MII_LPA_10BASETXFULL)) != 0);

      printf("%s: link %sMbps %s-duplex\n", BOARD_PHY_NAME,
             is100 ? "100" : "10", duplex ? "full" : "half");
    }

#else
  /* Force 100Mbps full duplex if autoneg is disabled */

  mdio_write(mdio, phyaddr, MII_MCR,
             MII_MCR_SPEED100 | MII_MCR_FULLDPLX);
  up_mdelay(100);
#endif

  return OK;
}

int tc375_ethernet_initialize(void)
{
  int ret;
  struct mdio_bus_s *mdio;

  tc375_config_geth_pins();

  ret = tc3x_geth_init(0);
  if (ret < 0)
    {
      nerr("ERROR: tc3x_geth_init failed: %d\n", ret);
      return ret;
    }

  mdio = dwxgmac_mdio_bus_initialize(0, CONFIG_DWXGMAC_BASE);
  if (mdio == NULL)
    {
      printf("ERROR: MDIO bus init failed\n");
      return -ENODEV;
    }

  tc375_init_phy(mdio);

  return OK;
}
