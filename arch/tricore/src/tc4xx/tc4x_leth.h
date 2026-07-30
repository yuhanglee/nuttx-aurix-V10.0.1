/****************************************************************************
 * arch/tricore/src/tc4xx/tc4x_leth.h
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

#ifndef ETHERNET_TC4X_LETH_H
#define ETHERNET_TC4X_LETH_H

/* LETH wraps the same DesignWare Ethernet-QoS MAC used by GETH behind an
 * Infineon SFR block.  Port j MAC core sits at base + 0x10000 + j*0x2000;
 * the QOS variant of the shared dwxgmac driver reaches the DMA block at
 * that MAC base + 0xF000, so CONFIG_DWXGMAC_BASE must point at Port 0's
 * core (0xF9410000 for LETH0).
 */

#define TC4XX_LETH_BASE            CONFIG_TRICORE_LETH_BASE
#define TC4XX_LETH_CLC             (TC4XX_LETH_BASE + 0x000)
#define TC4XX_LETH_RST_CTRLA       (TC4XX_LETH_BASE + 0x00C)
#define TC4XX_LETH_RST_CTRLB       (TC4XX_LETH_BASE + 0x010)
#define TC4XX_LETH_RST_STAT        (TC4XX_LETH_BASE + 0x014)
#define TC4XX_LETH_PEN             (TC4XX_LETH_BASE + 0x020)
/* Access-enable (ApApu) groups.  Each 0x20-byte group holds WRA/WRB (write
 * enable, +0x00/+0x04) and RDA/RDB (read enable, +0x08/+0x0C).  There is one
 * GLOBAL group (0x40), one per MAC port (0x60, stride 0x20, 4 ports) and one
 * per DMA channel (0xE0, stride 0x20, 8 channels).  Both read and write must
 * be opened or the CPU takes a bus error touching the MAC/DMA registers.
 */

#define LETH_ACCEN_NMAC            4
#define LETH_ACCEN_NCH             8

#define TC4XX_LETH_ACCEN_GBL_WRA   (TC4XX_LETH_BASE + 0x040)
#define TC4XX_LETH_ACCEN_GBL_WRB   (TC4XX_LETH_BASE + 0x044)
#define TC4XX_LETH_ACCEN_GBL_RDA   (TC4XX_LETH_BASE + 0x048)
#define TC4XX_LETH_ACCEN_GBL_RDB   (TC4XX_LETH_BASE + 0x04C)
#define TC4XX_LETH_ACCEN_MAC_WRA(j) (TC4XX_LETH_BASE + 0x060 + 0x20 * (j))
#define TC4XX_LETH_ACCEN_MAC_WRB(j) (TC4XX_LETH_BASE + 0x064 + 0x20 * (j))
#define TC4XX_LETH_ACCEN_MAC_RDA(j) (TC4XX_LETH_BASE + 0x068 + 0x20 * (j))
#define TC4XX_LETH_ACCEN_MAC_RDB(j) (TC4XX_LETH_BASE + 0x06C + 0x20 * (j))
#define TC4XX_LETH_ACCEN_CH_WRA(k)  (TC4XX_LETH_BASE + 0x0E0 + 0x20 * (k))
#define TC4XX_LETH_ACCEN_CH_WRB(k)  (TC4XX_LETH_BASE + 0x0E4 + 0x20 * (k))
#define TC4XX_LETH_ACCEN_CH_RDA(k)  (TC4XX_LETH_BASE + 0x0E8 + 0x20 * (k))
#define TC4XX_LETH_ACCEN_CH_RDB(k)  (TC4XX_LETH_BASE + 0x0EC + 0x20 * (k))

/* Per-port PHY interface control.  PORTCTRL0 carries the RMII/MII input
 * source muxes and the EPR interface-mode field; PORTCTRL1 the MDIO mux.
 * The board wires every RMII input to source A (code 0 = reset default),
 * so only EPR, the MDIO source (E = 4) and the enables need programming.
 */

#define TC4XX_LETH_PORTCTRL0(j)    (TC4XX_LETH_BASE + 0x320 + 0x08 * (j))
#define TC4XX_LETH_PORTCTRL1(j)    (TC4XX_LETH_BASE + 0x324 + 0x08 * (j))

#define LETH_PORTCTRL0_RXDEN       BIT(3)
#define LETH_PORTCTRL0_RXDV_SHIFT  8    /* CRS_DV alternate input select */
#define LETH_PORTCTRL0_RXDV_MASK   GENMASK(9, 8)
#define LETH_PORTCTRL0_RXD0_SHIFT  14   /* RXD0 alternate input select */
#define LETH_PORTCTRL0_RXD0_MASK   GENMASK(15, 14)
#define LETH_PORTCTRL0_RXD1_SHIFT  16   /* RXD1 alternate input select */
#define LETH_PORTCTRL0_RXD1_MASK   GENMASK(17, 16)
#define LETH_PORTCTRL0_RXCLK_SHIFT 22   /* RXCLK/REFCLK alternate input select */
#define LETH_PORTCTRL0_RXCLK_MASK  GENMASK(23, 22)
#define LETH_PORTCTRL0_TC14EN      BIT(28)
#define LETH_PORTCTRL0_EPR_SHIFT   29
#define LETH_PORTCTRL0_EPR_MASK    GENMASK(31, 29)
#define LETH_PORTCTRL0_EPR_RMII100 1u   /* RMII external PHY at 100 Mbps */
#define LETH_PORTCTRL0_EPR_RMII10  2u   /* RMII external PHY at 10 Mbps  */

/* Alternate-input source for the RMII RX pins (RXD0/RXD1/CRS_DV/REFCLK).
 * The board wires all four to the same source group; 0 = source A.
 */

#ifndef CONFIG_TRICORE_LETH_RMII_RX_SRC
#  define CONFIG_TRICORE_LETH_RMII_RX_SRC 0
#endif

#define LETH_PORTCTRL1_MDIO_MASK   GENMASK(2, 0)
#define LETH_PORTCTRL1_MDIO_SRC_E  4u
#define LETH_PORTCTRL1_MDIOEN      BIT(3)

/* Kernel clock/reset control */

#define LETH_CLC_DISR              BIT(0)
#define LETH_CLC_DISS              BIT(1)
#define LETH_RST_CTRL_RST          BIT(0)
#define LETH_RST_CTRLB_ENDINIT     BIT(31)
#define LETH_RST_STAT_RSTSTAT      BIT(0)
#define LETH_PEN_P0DIS             BIT(0)  /* 0 = Port 0 clock enabled */
#define LETH_PEN_P0STAT            BIT(16) /* Port 0 clock status */

/* Ethernet bridge (four-port instance).  For a single active port the
 * bridge runs in one-port mode: Q_CH_MAPPING_EN = 0 and PORT_SEL picks
 * the port.  Reset FWD_CTRL already selects Port 0 (0x40 = MAX_PKT_LEN).
 */

#define TC4XX_LETH_BRIDGE_BASE     (TC4XX_LETH_BASE + 0x1E000)
#define TC4XX_LETH_BR_FWDCTRL      (TC4XX_LETH_BRIDGE_BASE + 0x000)
#define TC4XX_LETH_BR_FLUSH_LPBK   (TC4XX_LETH_BRIDGE_BASE + 0x004)
#define TC4XX_LETH_BR_PORT_CTRL(j) (TC4XX_LETH_BRIDGE_BASE + 0x010 + 0x10 * (j))
#define TC4XX_LETH_BR_TXQ_MAP(j)   (TC4XX_LETH_BRIDGE_BASE + 0x014 + 0x10 * (j))
#define TC4XX_LETH_BR_RXC_MAP(j)   (TC4XX_LETH_BRIDGE_BASE + 0x018 + 0x10 * (j))
#define TC4XX_LETH_BR_DMA_PORT_SEL (TC4XX_LETH_BRIDGE_BASE + 0x110)

#define LETH_BR_FWDCTRL_PORT_SEL   GENMASK(30, 29)
#define LETH_BR_FWDCTRL_QCHMAP_EN  BIT(31)
#define LETH_BR_FWDCTRL_MAXPKT     GENMASK(15, 0)
#define LETH_BR_MAX_PKT_LEN        1518u   /* full frame incl FCS */
#define LETH_BR_PORT_CTRL_TXQ0     BIT(24) /* TXQ_EN[0] */
#define LETH_BR_PORT_CTRL_RXC0     BIT(8)  /* RXC_EN[0] */

#endif /* ETHERNET_TC4X_LETH_H */
