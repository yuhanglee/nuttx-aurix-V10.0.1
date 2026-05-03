/****************************************************************************
 * arch/tricore/src/tc3xx/hardware/tc3x_qspi.h
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

#ifndef __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3X_QSPI_H
#define __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3X_QSPI_H


#include <nuttx/config.h>
#include <nuttx/bits.h>


/* QSPI module base addresses (TC397 / TC39xB) -----------------------------*/

#define TC3X_QSPI0_BASE         0xF0001C00u
#define TC3X_QSPI1_BASE         0xF0001D00u
#define TC3X_QSPI2_BASE         0xF0001E00u
#define TC3X_QSPI3_BASE         0xF0001F00u
#define TC3X_QSPI4_BASE         0xF0002000u

/* QSPI register offsets ---------------------------------------------------*/

#define TC3X_QSPI_CLC_OFFSET            0x0000
#define TC3X_QSPI_PISEL_OFFSET          0x0004
#define TC3X_QSPI_ID_OFFSET             0x0008
#define TC3X_QSPI_GLOBALCON_OFFSET      0x0010
#define TC3X_QSPI_GLOBALCON1_OFFSET     0x0014
#define TC3X_QSPI_BACON_OFFSET          0x0018
#define TC3X_QSPI_ECON_OFFSET(n)        (0x0020 + ((n) * 4))   /* n = 0..7 */
#define TC3X_QSPI_STATUS_OFFSET         0x0040
#define TC3X_QSPI_STATUS1_OFFSET        0x0044
#define TC3X_QSPI_SSOC_OFFSET           0x0048
#define TC3X_QSPI_FLAGSCLEAR_OFFSET     0x0054
#define TC3X_QSPI_XXLCON_OFFSET         0x0058
#define TC3X_QSPI_MIXENTRY_OFFSET       0x005C
#define TC3X_QSPI_BACONENTRY_OFFSET     0x0060
#define TC3X_QSPI_DATAENTRY_OFFSET(n)   (0x0064 + ((n) * 4))   /* n = 0..7 */
#define TC3X_QSPI_RXEXIT_OFFSET         0x0090
#define TC3X_QSPI_RXEXITD_OFFSET        0x0094
#define TC3X_QSPI_OCS_OFFSET            0x00E8
#define TC3X_QSPI_KRSTCLR_OFFSET        0x00EC
#define TC3X_QSPI_KRST1_OFFSET          0x00F0
#define TC3X_QSPI_KRST0_OFFSET          0x00F4
#define TC3X_QSPI_ACCEN1_OFFSET         0x00F8
#define TC3X_QSPI_ACCEN0_OFFSET         0x00FC


#define TC3X_QSPI_PISEL_MRIS_SHIFT      0
#define TC3X_QSPI_PISEL_MRIS_MASK       GENMASK(2, 0)
#define TC3X_QSPI_PISEL_MRIS(v) \
        (((uint32_t)(v) << TC3X_QSPI_PISEL_MRIS_SHIFT) & \
         TC3X_QSPI_PISEL_MRIS_MASK)

/* CLC -- Clock Control register -------------------------------------------*/

#define TC3X_QSPI_CLC_DISR              BIT(0)   /* Module Disable Request */
#define TC3X_QSPI_CLC_DISS              BIT(1)   /* Module Disable Status  */
#define TC3X_QSPI_CLC_EDIS              BIT(3)
#define TC3X_QSPI_CLC_FSOE              BIT(5)

/* GLOBALCON -- Global Configuration ---------------------------------------*/

#define TC3X_QSPI_GLOBALCON_TQ_SHIFT      0
#define TC3X_QSPI_GLOBALCON_TQ_MASK       GENMASK(7, 0)
#define TC3X_QSPI_GLOBALCON_TQ(v) \
        (((uint32_t)(v) << TC3X_QSPI_GLOBALCON_TQ_SHIFT) & \
         TC3X_QSPI_GLOBALCON_TQ_MASK)
#define TC3X_QSPI_GLOBALCON_SI            BIT(9)
#define TC3X_QSPI_GLOBALCON_EXPECT_SHIFT  10
#define TC3X_QSPI_GLOBALCON_EXPECT_MASK   GENMASK(13, 10)
#define TC3X_QSPI_GLOBALCON_EXPECT(v) \
        (((uint32_t)(v) << TC3X_QSPI_GLOBALCON_EXPECT_SHIFT) & \
         TC3X_QSPI_GLOBALCON_EXPECT_MASK)
#define TC3X_QSPI_GLOBALCON_LB            BIT(14)  /* internal loopback    */
#define TC3X_QSPI_GLOBALCON_DEL0          BIT(15)
#define TC3X_QSPI_GLOBALCON_STROBE_SHIFT  16
#define TC3X_QSPI_GLOBALCON_STROBE_MASK   GENMASK(20, 16)
#define TC3X_QSPI_GLOBALCON_SRF           BIT(21)
#define TC3X_QSPI_GLOBALCON_STIP          BIT(22)
#define TC3X_QSPI_GLOBALCON_EN            BIT(24)
#define TC3X_QSPI_GLOBALCON_MS_SHIFT      25
#define TC3X_QSPI_GLOBALCON_MS_MASK       GENMASK(26, 25)
#define TC3X_QSPI_GLOBALCON_MS_MASTER     (1u << TC3X_QSPI_GLOBALCON_MS_SHIFT)
#define TC3X_QSPI_GLOBALCON_AREN          BIT(27)
#define TC3X_QSPI_GLOBALCON_CLKSEL        BIT(29)
#define TC3X_QSPI_GLOBALCON_RESETS_SHIFT  30
#define TC3X_QSPI_GLOBALCON_RESETS_MASK   GENMASK(31, 30)
#define TC3X_QSPI_GLOBALCON_RESETS_RUN    (1u << TC3X_QSPI_GLOBALCON_RESETS_SHIFT)


#define TC3X_QSPI_GLOBALCON1_TXFM_SHIFT   26
#define TC3X_QSPI_GLOBALCON1_TXFM_MASK    GENMASK(27, 26)
#define TC3X_QSPI_GLOBALCON1_RXFM_SHIFT   28
#define TC3X_QSPI_GLOBALCON1_RXFM_MASK    GENMASK(29, 28)


#define TC3X_QSPI_BACON_LAST              BIT(0)
#define TC3X_QSPI_BACON_IPRE_SHIFT        1
#define TC3X_QSPI_BACON_IPRE_MASK         GENMASK(3, 1)
#define TC3X_QSPI_BACON_IDLE_SHIFT        4
#define TC3X_QSPI_BACON_IDLE_MASK         GENMASK(6, 4)
#define TC3X_QSPI_BACON_LPRE_SHIFT        7
#define TC3X_QSPI_BACON_LPRE_MASK         GENMASK(9, 7)
#define TC3X_QSPI_BACON_LEAD_SHIFT        10
#define TC3X_QSPI_BACON_LEAD_MASK         GENMASK(12, 10)
#define TC3X_QSPI_BACON_TPRE_SHIFT        13
#define TC3X_QSPI_BACON_TPRE_MASK         GENMASK(15, 13)
#define TC3X_QSPI_BACON_TRAIL_SHIFT       16
#define TC3X_QSPI_BACON_TRAIL_MASK        GENMASK(18, 16)
#define TC3X_QSPI_BACON_PARTYP            BIT(19)
#define TC3X_QSPI_BACON_UINT              BIT(20)
#define TC3X_QSPI_BACON_MSB               BIT(21)
#define TC3X_QSPI_BACON_BYTE              BIT(22)
#define TC3X_QSPI_BACON_DL_SHIFT          23
#define TC3X_QSPI_BACON_DL_MASK           GENMASK(27, 23)
#define TC3X_QSPI_BACON_DL(v) \
        (((uint32_t)((v) - 1) << TC3X_QSPI_BACON_DL_SHIFT) & \
         TC3X_QSPI_BACON_DL_MASK)
#define TC3X_QSPI_BACON_CS_SHIFT          28
#define TC3X_QSPI_BACON_CS_MASK           GENMASK(31, 28)
#define TC3X_QSPI_BACON_CS(n) \
        (((uint32_t)(n) << TC3X_QSPI_BACON_CS_SHIFT) & \
         TC3X_QSPI_BACON_CS_MASK)

#define TC3X_QSPI_BACON_LEAD(v)           (((uint32_t)(v) << 10) & GENMASK(12, 10))
#define TC3X_QSPI_BACON_TRAIL(v)          (((uint32_t)(v) << 16) & GENMASK(18, 16))
#define TC3X_QSPI_BACON_IDLE(v)           (((uint32_t)(v) <<  4) & GENMASK( 6,  4))

/* ECONx -- per-CS expansion config ----------------------------------------*/

#define TC3X_QSPI_ECON_Q_SHIFT            0
#define TC3X_QSPI_ECON_Q_MASK             GENMASK(5, 0)
#define TC3X_QSPI_ECON_Q(v) \
        (((uint32_t)(v) << TC3X_QSPI_ECON_Q_SHIFT) & TC3X_QSPI_ECON_Q_MASK)
#define TC3X_QSPI_ECON_A_SHIFT            6
#define TC3X_QSPI_ECON_A_MASK             GENMASK(7, 6)
#define TC3X_QSPI_ECON_A(v) \
        (((uint32_t)(v) << TC3X_QSPI_ECON_A_SHIFT) & TC3X_QSPI_ECON_A_MASK)
#define TC3X_QSPI_ECON_B_SHIFT            8
#define TC3X_QSPI_ECON_B_MASK             GENMASK(9, 8)
#define TC3X_QSPI_ECON_B(v) \
        (((uint32_t)(v) << TC3X_QSPI_ECON_B_SHIFT) & TC3X_QSPI_ECON_B_MASK)
#define TC3X_QSPI_ECON_C_SHIFT            10
#define TC3X_QSPI_ECON_C_MASK             GENMASK(11, 10)
#define TC3X_QSPI_ECON_C(v) \
        (((uint32_t)(v) << TC3X_QSPI_ECON_C_SHIFT) & TC3X_QSPI_ECON_C_MASK)
#define TC3X_QSPI_ECON_CPH                BIT(12)
#define TC3X_QSPI_ECON_CPOL               BIT(13)
#define TC3X_QSPI_ECON_PAREN              BIT(14)

/* STATUS ------------------------------------------------------------------*/

#define TC3X_QSPI_STATUS_ERRORFLAGS_MASK  GENMASK(8, 0)
#define TC3X_QSPI_STATUS_TXF              BIT(9)
#define TC3X_QSPI_STATUS_RXF              BIT(10)
#define TC3X_QSPI_STATUS_USRF             BIT(15)
#define TC3X_QSPI_STATUS_TXFIFOLEVEL_SHIFT 16
#define TC3X_QSPI_STATUS_TXFIFOLEVEL_MASK GENMASK(18, 16)
#define TC3X_QSPI_STATUS_RXFIFOLEVEL_SHIFT 19
#define TC3X_QSPI_STATUS_RXFIFOLEVEL_MASK GENMASK(21, 19)
#define TC3X_QSPI_STATUS_PHASE_MASK       GENMASK(31, 28)

/* SSOC -- Slave Select Output Control -------------------------------------*/

#define TC3X_QSPI_SSOC_AOL_SHIFT          0    /* active-output level (low=1)*/
#define TC3X_QSPI_SSOC_AOL_MASK           GENMASK(15, 0)
#define TC3X_QSPI_SSOC_OEN_SHIFT          16   /* output enable per CS      */
#define TC3X_QSPI_SSOC_OEN_MASK           GENMASK(31, 16)
#define TC3X_QSPI_SSOC_AOL_BIT(n)         BIT(TC3X_QSPI_SSOC_AOL_SHIFT + (n))
#define TC3X_QSPI_SSOC_OEN_BIT(n)         BIT(TC3X_QSPI_SSOC_OEN_SHIFT + (n))

/* FLAGSCLEAR --------------------------------------------------------------*/

#define TC3X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK GENMASK(8, 0)
#define TC3X_QSPI_FLAGSCLEAR_TXC          BIT(9)
#define TC3X_QSPI_FLAGSCLEAR_RXC          BIT(10)
#define TC3X_QSPI_FLAGSCLEAR_USRC         BIT(15)

/* KRST -- kernel reset (write password-protected via WDT) -----------------*/

#define TC3X_QSPI_KRST_RST                BIT(0)
#define TC3X_QSPI_KRST_RSTSTAT            BIT(1)


#define TC3X_QSPI_REG(base, off)        ((base) + (off))

#endif /* __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3X_QSPI_H */
