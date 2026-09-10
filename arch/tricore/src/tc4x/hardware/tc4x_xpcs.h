/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4x_xpcs.h
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
 ****************************************************************************/

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_XPCS_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_XPCS_H

#include <nuttx/bits.h>

/* Synopsys DesignWare XPCS (SNPS xpcs) — one instance per GETH port.
 * XPCS0 at 0xF2800000 pairs with XGMAC0/PHY@1.
 * XPCS1 at 0xF3000000 pairs with XGMAC1/PHY@2.
 * All register offsets are identical between the two — pick the base
 * at compile time via TC4XX_XPCS_BASE.
 */

#ifndef TC4XX_XPCS_BASE
#  define TC4XX_XPCS_BASE                       0xF3000000u
#endif

/* PMA */

#define TC4XX_XPCS_PMA_TX_GENCTRL1              (TC4XX_XPCS_BASE + 0x600C4u)
#define TC4XX_XPCS_PMA_TX_GENCTRL2              (TC4XX_XPCS_BASE + 0x600C8u)
#define TC4XX_XPCS_PMA_TX_BOOST_CTRL            (TC4XX_XPCS_BASE + 0x600CCu)
#define TC4XX_XPCS_PMA_TX_RATE_CTRL             (TC4XX_XPCS_BASE + 0x600D0u)
#define TC4XX_XPCS_PMA_TX_STS                   (TC4XX_XPCS_BASE + 0x60100u)
#define TC4XX_XPCS_PMA_RX_GENCTRL1              (TC4XX_XPCS_BASE + 0x60144u)
#define TC4XX_XPCS_PMA_RX_GENCTRL2              (TC4XX_XPCS_BASE + 0x60148u)
#define TC4XX_XPCS_PMA_RX_RATE_CTRL             (TC4XX_XPCS_BASE + 0x60150u)
#define TC4XX_XPCS_PMA_RX_GENCTRL4              (TC4XX_XPCS_BASE + 0x601A0u)
#define TC4XX_XPCS_PMA_RX_MISC_CTRL0            (TC4XX_XPCS_BASE + 0x601A4u)
#define TC4XX_XPCS_PMA_MPLL_CMN_CTRL            (TC4XX_XPCS_BASE + 0x601C0u)
#define TC4XX_XPCS_PMA_MPLLA_CTRL0              (TC4XX_XPCS_BASE + 0x601C4u)
#define TC4XX_XPCS_PMA_MPLLA_CTRL1              (TC4XX_XPCS_BASE + 0x601C8u)
#define TC4XX_XPCS_PMA_MPLLA_CTRL2              (TC4XX_XPCS_BASE + 0x601CCu)
#define TC4XX_XPCS_PMA_MPLLA_CTRL6              (TC4XX_XPCS_BASE + 0x601F4u)
#define TC4XX_XPCS_PMA_MPLLA_CTRL7              (TC4XX_XPCS_BASE + 0x601FCu)
#define TC4XX_XPCS_PMA_REF_CLK_CTRL             (TC4XX_XPCS_BASE + 0x60244u)
#define TC4XX_XPCS_PMA_VCO_CAL_LD0              (TC4XX_XPCS_BASE + 0x60248u)
#define TC4XX_XPCS_PMA_VCO_CAL_REF0             (TC4XX_XPCS_BASE + 0x60258u)
#define TC4XX_XPCS_PMA_SRAM                     (TC4XX_XPCS_BASE + 0x6026Cu)
#define TC4XX_XPCS_PMA_MISC_CTRL2               (TC4XX_XPCS_BASE + 0x60270u)

/* PCS */

#define TC4XX_XPCS_PCS_SR_XS_CTRL1              (TC4XX_XPCS_BASE + 0xC0000u)
#define TC4XX_XPCS_PCS_SR_XS_CTRL2              (TC4XX_XPCS_BASE + 0xC001Cu)
#define TC4XX_XPCS_PCS_VR_XS_DIG_CTRL1          (TC4XX_XPCS_BASE + 0xE0000u)
#define TC4XX_XPCS_PCS_VR_XS_DIG_STS            (TC4XX_XPCS_BASE + 0xE0040u)
#define TC4XX_XPCS_PMA_RX_STS                   (TC4XX_XPCS_BASE + 0x60180u)

/* MII (MMD_VEND2 emulation) */

#define TC4XX_XPCS_MII_CTRL                     (TC4XX_XPCS_BASE + 0x7C0000u)
#define TC4XX_XPCS_MII_VR_AN_CTRL               (TC4XX_XPCS_BASE + 0x7E0004u)

/* Bit fields */

#define XPCS_MPLL_CMN_MPLL_EN_0                 BIT(0)

#define XPCS_TX_GENCTRL1_TX_CLK_RDY_0           BIT(12)
#define XPCS_TX_GENCTRL2_TX_REQ_0               BIT(0)
#define XPCS_TX_GENCTRL2_TX0_WIDTH              (0x3u << 8)
#define XPCS_TX_STS_TX_ACK_0                    BIT(0)

#define XPCS_RX_GENCTRL1_RX_TERM_ACDC_0         BIT(8)
#define XPCS_RX_GENCTRL2_RX0_WIDTH              (0x3u << 8)

#define XPCS_TX_BOOST_TX0_IBOOST                GENMASK(3, 0)
#define XPCS_TX_RATE_TX0_RATE                   GENMASK(2, 0)
#define XPCS_RX_RATE_RX0_RATE                   GENMASK(1, 0)

#define XPCS_MPLLA_CTRL0_MULTIPLIER             GENMASK(7, 0)
#define XPCS_MPLLA_CTRL2_TX_CLK_DIV_SHIFT       11u
#define XPCS_MPLLA_CTRL2_DIV10_CLK_EN           BIT(9)
#define XPCS_MPLLA_CTRL6_CP_INT                 GENMASK(4, 0)
#define XPCS_MPLLA_CTRL6_CP_PROP_SHIFT          8u
#define XPCS_MPLLA_CTRL7_CP_INT_GS              GENMASK(4, 0)
#define XPCS_MPLLA_CTRL7_CP_PROP_GS_SHIFT       8u

#define XPCS_REF_CLK_EN                         BIT(0)
#define XPCS_REF_CLK_USE_PAD                    BIT(1)

#define XPCS_VCO_CAL_LD0_VAL                    GENMASK(12, 0)
#define XPCS_VCO_CAL_REF0_VAL                   GENMASK(5, 0)

#define XPCS_MISC_CTRL2_SUP_MISC                GENMASK(7, 0)
#define XPCS_RX_MISC_CTRL0_RX0_MISC             GENMASK(7, 0)

#define XPCS_SRAM_INIT_DN                       BIT(0)
#define XPCS_SRAM_EXT_LD_DN                     BIT(1)

#define XPCS_PCS_SR_CTRL1_LPM                   BIT(11)
#define XPCS_PCS_SR_CTRL1_RST                   BIT(15)
#define XPCS_PCS_SR_CTRL2_PCS_TYPE_MASK         GENMASK(3, 0)
#define XPCS_PCS_VR_DIG_CTRL1_INIT              BIT(8)
#define XPCS_PCS_VR_DIG_CTRL1_MAC_AUTO_SW       BIT(9)
#define XPCS_PCS_VR_DIG_CTRL1_VR_RST            BIT(15)
#define XPCS_PCS_VR_DIG_CTRL1_EN_2_5G           BIT(2)
#define XPCS_PCS_VR_DIG_STS_PSEQ_MASK           GENMASK(4, 2)
#define XPCS_PCS_VR_DIG_STS_PSEQ_POWERUP        (0x4u << 2)
#define XPCS_PCS_VR_DIG_STS_PSEQ_POWERDOWN      (0x6u << 2)
#define XPCS_RX_GENCTRL2_RX_REQ_0               BIT(0)
#define XPCS_RX_STS_RX_ACK_0                    BIT(0)

#define XPCS_MII_CTRL_SS5                       BIT(5)
#define XPCS_MII_CTRL_SS6                       BIT(6)
#define XPCS_MII_CTRL_DUPLEX                    BIT(8)
#define XPCS_MII_CTRL_AN_ENABLE                 BIT(12)
#define XPCS_MII_CTRL_SS13                      BIT(13)

#define XPCS_VR_AN_CTRL_PCS_MODE_MASK           GENMASK(2, 1)
#define XPCS_VR_AN_CTRL_PCS_MODE_SGMII          (2u << 1)

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_XPCS_H */
