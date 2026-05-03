/****************************************************************************
 * boards/tricore/tc4dx/tc4d7-lite/src/tc4d7_ethernet.c
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
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/net/mii.h>
#include <nuttx/net/mdio.h>

#include "tc4xx_gpio.h"


/* DP83825I PHY configuration */
#define BOARD_PHY_NAME        "DP83825I"
#define BOARD_PHY_ADDR        0           /* PHY address on MDIO bus */
#define BOARD_PHYID1          MII_PHYID1_DP83825I
#define BOARD_PHYID2          MII_PHYID2_DP83825I

#define PHY_RESET_TIMEOUT_MS  100
#define PHY_LINK_TIMEOUT      5000
#define PHY_ANEG_TIMEOUT      10000

struct mdio_bus_s *dwxgmac_mdio_bus_initialize(uint8_t port, uintptr_t base);


static void tc4d7_config_geth_pins(void)
{
  gpio_pinset_t pins[] =
    {
      /* RMII TX outputs */
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN6, GPIO_PERIPH, GPIO_ALT10),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN8, GPIO_PERIPH, GPIO_ALT10),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN13, GPIO_PERIPH, GPIO_ALT10),

      /* RMII RX inputs */
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN4, GPIO_INPUT, GPIO_PULL_NONE),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN0, GPIO_INPUT, GPIO_PULL_NONE),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN1, GPIO_INPUT, GPIO_PULL_NONE),
      AURIX_GPIO(GPIO_PORT16, GPIO_PIN2, GPIO_INPUT, GPIO_PULL_NONE),

      /* MDIO */
      AURIX_GPIO(GPIO_PORT21, GPIO_PIN2, GPIO_PERIPH, GPIO_ALT8),
      AURIX_GPIO(GPIO_PORT21, GPIO_PIN3, GPIO_INPUT, GPIO_PULL_NONE),
    };

  for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++)
    {
      aurix_config_gpio(pins[i]);
    }
}

static int tc4d7_phy_scan(struct mdio_bus_s *mdio)
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
          ninfo("phy_scan: PHY found at addr %u (id1=0x%04x id2=0x%04x)\n",
                (unsigned)a, id1, id2);
          return a;
        }
    }

  nerr("phy_scan: no PHY responded on MDIO bus\n");
  return -ENODEV;
}

static int tc4d7_init_phy(struct mdio_bus_s *mdio)
{
  uint8_t phyaddr;
  uint16_t phyval;
  int ret;
  int to;
#ifdef CONFIG_BOARD_PHY_AUTONEG
  volatile uint32_t timeout;
#endif

  ret = tc4d7_phy_scan(mdio);
  if (ret < 0)
    {
      return ret;
    }

  phyaddr = (uint8_t)ret;
  if (phyaddr != BOARD_PHY_ADDR)
    {
      ninfo("PHY at addr %u (BOARD_PHY_ADDR=%u) -- using scanned addr\n",
            (unsigned)phyaddr, BOARD_PHY_ADDR);
    }

  ret = mdio_write(mdio, phyaddr, MII_MCR, MII_MCR_RESET);
  if (ret < 0)
    {
      nerr("ERROR: PHY reset write failed: %d\n", ret);
      return ret;
    }

  for (to = PHY_RESET_TIMEOUT_MS; to > 0; to -= 10)
    {
      up_mdelay(10);
      phyval = 0xFFFF;
      ret = mdio_read(mdio, phyaddr, MII_MCR, &phyval);
      if (ret < 0)
        {
          nerr("ERROR: PHY MCR read failed: %d\n", ret);
          return ret;
        }

      if ((phyval & MII_MCR_RESET) == 0)
        break;
    }

  if (to <= 0)
    {
      nerr("ERROR: PHY reset timeout\n");
      return -ETIMEDOUT;
    }

  ninfo("PHY reset complete in %d ms\n", PHY_RESET_TIMEOUT_MS - to);
  ret = mdio_read(mdio, phyaddr, MII_PHYID1, &phyval);
  if (ret < 0)
    {
      nerr("ERROR: Failed to read PHYID1: %d\n", ret);
      return ret;
    }


  ninfo("PHY at addr %u, PHYID1=0x%04x (BOARD_PHYID1=0x%04x)\n",
        (unsigned)phyaddr, phyval, BOARD_PHYID1);

  mdio_write(mdio, phyaddr, MII_DP83825I_RCSR,
             MII_DP83825I_RCSC_ELAST_2);

  mdio_write(mdio, phyaddr, MII_ADVERTISE,
             MII_ADVERTISE_100BASETXFULL |
             MII_ADVERTISE_100BASETXHALF |
             MII_ADVERTISE_10BASETXFULL  |
             MII_ADVERTISE_10BASETXHALF  |
             MII_ADVERTISE_CSMA);

#ifdef CONFIG_BOARD_PHY_AUTONEG


  ret = mdio_write(mdio, phyaddr, MII_MCR,
                   MII_MCR_ANENABLE | MII_MCR_ANRESTART);
  if (ret < 0)
    {
      nerr("ERROR: Failed to (re)start autoneg: %d\n", ret);
      return ret;
    }

  ninfo("%s: waiting for autoneg + link...\n", BOARD_PHY_NAME);


  for (timeout = 0; timeout < PHY_ANEG_TIMEOUT; timeout++)
    {
      ret = mdio_read(mdio, phyaddr, MII_MSR, &phyval);
      if (ret < 0)
        {
          nerr("ERROR: PHY MSR read failed: %d\n", ret);
          return ret;
        }

      if ((phyval & MII_MSR_ANEGCOMPLETE) &&
          (phyval & MII_MSR_LINKSTATUS))
        break;

      nxsig_usleep(100);
    }

  {
    uint16_t mcr = 0;
    uint16_t physts = 0;
    uint16_t lpa = 0;
    uint16_t rcsr = 0;
    mdio_read(mdio, phyaddr, MII_MCR,   &mcr);
    mdio_read(mdio, phyaddr, MII_LPA,   &lpa);
    mdio_read(mdio, phyaddr, MII_DP83825I_PHYSTS, &physts);
    mdio_read(mdio, phyaddr, MII_DP83825I_RCSR,   &rcsr);
    ninfo("PHY status: MCR=0x%04x MSR=0x%04x LPA=0x%04x PHYSTS=0x%04x "
          "RCSR=0x%04x\n", mcr, phyval, lpa, physts, rcsr);
  }

  if (timeout >= PHY_ANEG_TIMEOUT)
    {
      ninfo("Autoneg / link timeout -- proceeding without link\n");
      return OK;
    }

  ninfo("Autoneg + link up (MSR=0x%04x)\n", phyval);

  ret = mdio_read(mdio, phyaddr, MII_DP83825I_PHYSTS, &phyval);
  if (ret == OK)
    {
      bool is100   = ((phyval & MII_DP83825I_PHYSTS_SPEED) == 0);
      bool duplex  = ((phyval & MII_DP83825I_PHYSTS_DUPLEX) != 0);

      ninfo("Link: %s %s-duplex\n",
            is100 ? "100Mbps" : "10Mbps",
            duplex ? "full" : "half");
    }
#else

  /* Force 100Mbps full duplex if autoneg is disabled */
  mdio_write(mdio, phyaddr, MII_MCR,
             MII_MCR_SPEED100 | MII_MCR_FULLDPLX);
  up_mdelay(100);
#endif

  return OK;
}



int tc4d7_ethernet_initialize(void)
{
  int ret;
  struct mdio_bus_s *mdio;

  tc4d7_config_geth_pins();

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

  ret = tc4d7_init_phy(mdio);
  if (ret < 0)
    {
      nerr("ERROR: tc4d7_init_phy failed: %d (eth0 will be link-down)\n", ret);
    }

  return OK;
}
