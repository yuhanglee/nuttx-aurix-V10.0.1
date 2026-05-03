/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4x_hsphy.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_HSPHY_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_HSPHY_H


#include <nuttx/config.h>
#include <nuttx/bits.h>

#include <stdint.h>
#include <stdbool.h>


/* HSPHY FPI base (TRM 17.4.2). */

#define TC4X_HSPHY_BASE              0xF2000000u

/* Register offsets (TRM 17.4 Register overview). */

#define TC4X_HSPHY_CLC_OFFSET        0x0000
#define TC4X_HSPHY_RST_CTRLA_OFFSET  0x000C
#define TC4X_HSPHY_RST_CTRLB_OFFSET  0x0010
#define TC4X_HSPHY_RST_STAT_OFFSET   0x0014
#define TC4X_HSPHY_CMNCFG_OFFSET     0x0200
#define TC4X_HSPHY_DLL_CFG_OFFSET    0x0628

#define TC4X_HSPHY_CLC               (TC4X_HSPHY_BASE + TC4X_HSPHY_CLC_OFFSET)
#define TC4X_HSPHY_RST_CTRLA         (TC4X_HSPHY_BASE + TC4X_HSPHY_RST_CTRLA_OFFSET)
#define TC4X_HSPHY_RST_CTRLB         (TC4X_HSPHY_BASE + TC4X_HSPHY_RST_CTRLB_OFFSET)
#define TC4X_HSPHY_RST_STAT          (TC4X_HSPHY_BASE + TC4X_HSPHY_RST_STAT_OFFSET)
#define TC4X_HSPHY_CMNCFG            (TC4X_HSPHY_BASE + TC4X_HSPHY_CMNCFG_OFFSET)
#define TC4X_HSPHY_DLL_CFG           (TC4X_HSPHY_BASE + TC4X_HSPHY_DLL_CFG_OFFSET)

#define HSPHY_RST_KRST               BIT(0)
#define HSPHY_RST_STATCLR            BIT(31)

/* CLC fields (TRM 17.4.4). Reset value 0x00000003 (DISR=DISS=1). */

#define HSPHY_CLC_DISR               BIT(0)
#define HSPHY_CLC_DISS               BIT(1)
#define HSPHY_CLC_EDIS               BIT(3)


#define HSPHY_CMNCFG_FSR             BIT(4)

/* DLL_CFG fields (TRM 17.4.158). Reset value 0x00280000. */

#define HSPHY_DLL_CFG_TXCFG_SHIFT       0
#define HSPHY_DLL_CFG_TXCFG_MASK        GENMASK(4, 0)
#define HSPHY_DLL_CFG_RXCFG_SHIFT       8
#define HSPHY_DLL_CFG_RXCFG_MASK        GENMASK(12, 8)
#define HSPHY_DLL_CFG_TXEN              BIT(16)
#define HSPHY_DLL_CFG_FMODE             BIT(20)
#define HSPHY_DLL_CFG_REFCLKSE          BIT(21)
#define HSPHY_DLL_CFG_PEN3VX            BIT(22)
#define HSPHY_DLL_CFG_PMODE             BIT(23)
#define HSPHY_DLL_CFG_RXEN              BIT(24)
#define HSPHY_DLL_CFG_TXDATCFG_SHIFT    25
#define HSPHY_DLL_CFG_TXDATCFG_MASK     GENMASK(28, 25)
#define HSPHY_DLL_CFG_RXSAMPLESEL       BIT(29)
#define HSPHY_DLL_CFG_LOCKSTAT          BIT(30)
#define HSPHY_DLL_CFG_POWER             BIT(31)


struct tc4x_hsphy_xspi_cfg_s
{
  bool     pen3vx;        /* false = 1.8V (PEN3VX=0), true = 3V (PEN3VX=1) */
  bool     diff_clock;    /* true = differential CK_P/CK_N (PMODE=1)       */
  bool     fmode_high;    /* false = sclk_out < 166 MHz, true >= 166 MHz   */
  bool     rxsamplesel;   /* true when no RWDS strobe is available         */
  uint8_t  txcfg;         /* 0..31 transmit clock skew                     */
  uint8_t  rxcfg;         /* 0..31 receive clock skew                      */
};


#ifdef __cplusplus
extern "C"
{
#endif


int tc4x_hsphy_xspi_init(const struct tc4x_hsphy_xspi_cfg_s *cfg,
                         uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_HSPHY_H */
