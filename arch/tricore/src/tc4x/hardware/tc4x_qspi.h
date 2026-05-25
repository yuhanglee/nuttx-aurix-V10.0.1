/****************************************************************************
 * arch/tricore/src/tc4x/hardware/tc4x_qspi.h
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

#ifndef __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_QSPI_H
#define __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_QSPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/bits.h>

/* QSPI module base addresses (TC4Dx) */

#define TC4X_QSPI0_BASE         0xF4401000u
#define TC4X_QSPI1_BASE         0xF4401200u
#define TC4X_QSPI2_BASE         0xF4401400u
#define TC4X_QSPI3_BASE         0xF4401600u
#define TC4X_QSPI4_BASE         0xF4401800u
#define TC4X_QSPI5_BASE         0xF4401A00u
#define TC4X_QSPI6_BASE         0xF4401C00u
#define TC4X_QSPI7_BASE         0xF4401E00u

/* QSPI register offsets */

#define TC4X_QSPI_CLC_OFFSET            0x000u
#define TC4X_QSPI_OCS_OFFSET            0x004u
#define TC4X_QSPI_ID_OFFSET             0x008u
#define TC4X_QSPI_RST_CTRLA_OFFSET      0x00Cu
#define TC4X_QSPI_RST_CTRLB_OFFSET      0x010u
#define TC4X_QSPI_RST_STAT_OFFSET       0x014u
#define TC4X_QSPI_PROTE_OFFSET          0x020u
#define TC4X_QSPI_PROTSE_OFFSET         0x024u
#define TC4X_QSPI_ACCEN_WRA_OFFSET      0x040u
#define TC4X_QSPI_ACCEN_WRB_OFFSET      0x044u
#define TC4X_QSPI_ACCEN_RDA_OFFSET      0x048u
#define TC4X_QSPI_ACCEN_RDB_OFFSET      0x04Cu
#define TC4X_QSPI_ACCEN_VM_OFFSET       0x050u
#define TC4X_QSPI_ACCEN_PRS_OFFSET      0x054u
#define TC4X_QSPI_PISEL_OFFSET          0x104u
#define TC4X_QSPI_GLOBALCON_OFFSET      0x110u
#define TC4X_QSPI_GLOBALCON1_OFFSET     0x114u
#define TC4X_QSPI_BACON_OFFSET          0x118u
#define TC4X_QSPI_ECON_OFFSET(n)        (0x120u + ((n) * 4u))
#define TC4X_QSPI_STATUS_OFFSET         0x140u
#define TC4X_QSPI_STATUS1_OFFSET        0x144u
#define TC4X_QSPI_SSOC_OFFSET           0x148u
#define TC4X_QSPI_FLAGSCLEAR_OFFSET     0x150u
#define TC4X_QSPI_XXLCON_OFFSET         0x154u
#define TC4X_QSPI_MIXENTRY_OFFSET       0x158u
#define TC4X_QSPI_BACONENTRY_OFFSET     0x15Cu
#define TC4X_QSPI_DATAENTRY_OFFSET(n)   (0x160u + ((n) * 4u))
#define TC4X_QSPI_RXEXIT_OFFSET         0x180u
#define TC4X_QSPI_RXEXITD_OFFSET        0x1A0u
#define TC4X_QSPI_MC_OFFSET             0x1B4u
#define TC4X_QSPI_MCCON_OFFSET          0x1B8u
#define TC4X_QSPI_MSSMUX_OFFSET         0x1BCu

/* CLC -- Clock Control Register
 * Offset 0x000, Application Reset value: 0x00000003
 */

#define TC4X_QSPI_CLC_DISR              BIT(0)
#define TC4X_QSPI_CLC_DISS              BIT(1)
#define TC4X_QSPI_CLC_EDIS              BIT(3)

/* OCS -- OCDS Control and Status Register
 * Offset 0x004, Debug Reset value: 0x00000000
 */

#define TC4X_QSPI_OCS_SUS_SHIFT         24
#define TC4X_QSPI_OCS_SUS_MASK          GENMASK(27, 24)
#define TC4X_QSPI_OCS_SUS_NONE          (0u << TC4X_QSPI_OCS_SUS_SHIFT)
#define TC4X_QSPI_OCS_SUS_P             BIT(28)
#define TC4X_QSPI_OCS_SUSSTA_MASK       GENMASK(30, 29)

/* RST_CTRLA -- Reset Control Register A
 * Offset 0x00C, Application Reset value: 0x00000000
 */

#define TC4X_QSPI_RST_CTRLA_KRST        BIT(0)
#define TC4X_QSPI_RST_CTRLA_GRSTEN0     BIT(8)
#define TC4X_QSPI_RST_CTRLA_GRSTEN1     BIT(9)
#define TC4X_QSPI_RST_CTRLA_GRSTEN2     BIT(10)
#define TC4X_QSPI_RST_CTRLA_GRSTEN3     BIT(11)

/* RST_CTRLB -- Reset Control Register B
 * Offset 0x010, Application Reset value: 0x00000000
 */

#define TC4X_QSPI_RST_CTRLB_KRST        BIT(0)
#define TC4X_QSPI_RST_CTRLB_STATCLR     BIT(31)

/* RST_STAT -- Reset Status Register
 * Offset 0x014, Application Reset value: 0x00000000
 */

#define TC4X_QSPI_RST_STAT_KRST         BIT(0)

/* PISEL -- Port Input Select Register
 * Offset 0x104, Kernel Reset value: 0x00000000
 */

#define TC4X_QSPI_PISEL_MRIS_SHIFT      0
#define TC4X_QSPI_PISEL_MRIS_MASK       GENMASK(2, 0)
#define TC4X_QSPI_PISEL_MRIS(v) \
        (((uint32_t)(v) << TC4X_QSPI_PISEL_MRIS_SHIFT) & \
         TC4X_QSPI_PISEL_MRIS_MASK)

/* GLOBALCON -- Global Configuration Register
 * Offset 0x110, Kernel Reset value: 0x000F30FFH
 */

#define TC4X_QSPI_GLOBALCON_TQ_SHIFT      0
#define TC4X_QSPI_GLOBALCON_TQ_MASK       GENMASK(7, 0)
#define TC4X_QSPI_GLOBALCON_TQ(v) \
        (((uint32_t)(v) << TC4X_QSPI_GLOBALCON_TQ_SHIFT) & \
         TC4X_QSPI_GLOBALCON_TQ_MASK)
#define TC4X_QSPI_GLOBALCON_SI            BIT(9)
#define TC4X_QSPI_GLOBALCON_EXPECT_SHIFT  10
#define TC4X_QSPI_GLOBALCON_EXPECT_MASK   GENMASK(13, 10)
#define TC4X_QSPI_GLOBALCON_EXPECT(v) \
        (((uint32_t)(v) << TC4X_QSPI_GLOBALCON_EXPECT_SHIFT) & \
         TC4X_QSPI_GLOBALCON_EXPECT_MASK)
#define TC4X_QSPI_GLOBALCON_LB            BIT(14)  /* Internal loopback    */
#define TC4X_QSPI_GLOBALCON_DEL0          BIT(15)  /* Delayed mode         */
#define TC4X_QSPI_GLOBALCON_STROBE_SHIFT  16
#define TC4X_QSPI_GLOBALCON_STROBE_MASK   GENMASK(20, 16)
#define TC4X_QSPI_GLOBALCON_SRF           BIT(21)  /* Stop on RxFIFO full  */
#define TC4X_QSPI_GLOBALCON_STIP          BIT(22)  /* Slave TX idle polar. */
#define TC4X_QSPI_GLOBALCON_HS            BIT(23)  /* High-speed mode      */
#define TC4X_QSPI_GLOBALCON_EN            BIT(24)  /* Enable (RUN/PAUSE)   */
#define TC4X_QSPI_GLOBALCON_MS_SHIFT      25
#define TC4X_QSPI_GLOBALCON_MS_MASK       GENMASK(26, 25)
/* Master Transmit+Receive = MS[26:25] = 00b (documented TC4Dx encoding) */
#define TC4X_QSPI_GLOBALCON_MS_MASTER     0u
#define TC4X_QSPI_GLOBALCON_AREN          BIT(27)  /* Auto reset enable    */
#define TC4X_QSPI_GLOBALCON_HWTREN        BIT(28)  /* HW trigger enable    */
#define TC4X_QSPI_GLOBALCON_CLKSEL        BIT(29)  /* 0=no clk, 1=fPER    */
#define TC4X_QSPI_GLOBALCON_RESETS_SHIFT  30
#define TC4X_QSPI_GLOBALCON_RESETS_MASK   GENMASK(31, 30)
/* RESETS=01b: SM+TXFIFO+RXFIFO reset */
#define TC4X_QSPI_GLOBALCON_RESETS_SM \
        (1u << TC4X_QSPI_GLOBALCON_RESETS_SHIFT)

/* GLOBALCON1 -- Global Configuration Register 1
 * Offset 0x114, Kernel Reset value: 0x001B0000H
 */

#define TC4X_QSPI_GLOBALCON1_ERRORNS_MASK  GENMASK(8, 0)
#define TC4X_QSPI_GLOBALCON1_TXEN          BIT(9)
#define TC4X_QSPI_GLOBALCON1_RXEN          BIT(10)
#define TC4X_QSPI_GLOBALCON1_PT1EN         BIT(11)
#define TC4X_QSPI_GLOBALCON1_PT2EN         BIT(12)
#define TC4X_QSPI_GLOBALCON1_FD            BIT(13)
#define TC4X_QSPI_GLOBALCON1_HWTRSEL       BIT(14)
#define TC4X_QSPI_GLOBALCON1_USREN         BIT(15)
#define TC4X_QSPI_GLOBALCON1_TXFIFOINT_SHIFT 16
#define TC4X_QSPI_GLOBALCON1_TXFIFOINT_MASK  GENMASK(18, 16)
#define TC4X_QSPI_GLOBALCON1_RXFIFOINT_SHIFT 19
#define TC4X_QSPI_GLOBALCON1_RXFIFOINT_MASK  GENMASK(21, 19)
#define TC4X_QSPI_GLOBALCON1_PT1_SHIFT     22
#define TC4X_QSPI_GLOBALCON1_PT1_MASK      GENMASK(24, 22)
#define TC4X_QSPI_GLOBALCON1_PT2_SHIFT     25
#define TC4X_QSPI_GLOBALCON1_PT2_MASK      GENMASK(27, 25)
#define TC4X_QSPI_GLOBALCON1_TXFM_SHIFT    28
#define TC4X_QSPI_GLOBALCON1_TXFM_MASK     GENMASK(29, 28)
#define TC4X_QSPI_GLOBALCON1_RXFM_SHIFT    30
#define TC4X_QSPI_GLOBALCON1_RXFM_MASK     GENMASK(31, 30)

/* BACON -- Basic Configuration Register (read-only shadow)
 * Offset 0x118, Kernel Reset value: 0x0F871C71H
 * Written through BACONENTRY (offset 0x15C).
 */

#define TC4X_QSPI_BACON_LAST              BIT(0)   /* Last word in frame   */
#define TC4X_QSPI_BACON_IPRE_SHIFT        1
#define TC4X_QSPI_BACON_IPRE_MASK         GENMASK(3, 1)
#define TC4X_QSPI_BACON_IDLE_SHIFT        4
#define TC4X_QSPI_BACON_IDLE_MASK         GENMASK(6, 4)
#define TC4X_QSPI_BACON_LPRE_SHIFT        7
#define TC4X_QSPI_BACON_LPRE_MASK         GENMASK(9, 7)
#define TC4X_QSPI_BACON_LEAD_SHIFT        10
#define TC4X_QSPI_BACON_LEAD_MASK         GENMASK(12, 10)
#define TC4X_QSPI_BACON_TPRE_SHIFT        13
#define TC4X_QSPI_BACON_TPRE_MASK         GENMASK(15, 13)
#define TC4X_QSPI_BACON_TRAIL_SHIFT       16
#define TC4X_QSPI_BACON_TRAIL_MASK        GENMASK(18, 16)
#define TC4X_QSPI_BACON_PARTYP            BIT(19)  /* Parity type          */
#define TC4X_QSPI_BACON_UINT              BIT(20)  /* User interrupt enable*/
#define TC4X_QSPI_BACON_MSB               BIT(21)  /* Shift MSB first      */
#define TC4X_QSPI_BACON_BYTE              BIT(22)  /* DL in bytes (=1)     */
#define TC4X_QSPI_BACON_DL_SHIFT          23
#define TC4X_QSPI_BACON_DL_MASK           GENMASK(27, 23)
/* DL: pass bit count; macro subtracts 1 (field encodes bits-1 when BYTE=0) */
#define TC4X_QSPI_BACON_DL(bits) \
        (((uint32_t)((bits) - 1u) << TC4X_QSPI_BACON_DL_SHIFT) & \
         TC4X_QSPI_BACON_DL_MASK)
#define TC4X_QSPI_BACON_CS_SHIFT          28
#define TC4X_QSPI_BACON_CS_MASK           GENMASK(31, 28)
#define TC4X_QSPI_BACON_CS(n) \
        (((uint32_t)(n) << TC4X_QSPI_BACON_CS_SHIFT) & \
         TC4X_QSPI_BACON_CS_MASK)

/* Convenience helpers for timing delay fields */
#define TC4X_QSPI_BACON_LEAD(v) \
        (((uint32_t)(v) << TC4X_QSPI_BACON_LEAD_SHIFT) & \
         TC4X_QSPI_BACON_LEAD_MASK)
#define TC4X_QSPI_BACON_TRAIL(v) \
        (((uint32_t)(v) << TC4X_QSPI_BACON_TRAIL_SHIFT) & \
         TC4X_QSPI_BACON_TRAIL_MASK)
#define TC4X_QSPI_BACON_IDLE(v) \
        (((uint32_t)(v) << TC4X_QSPI_BACON_IDLE_SHIFT) & \
         TC4X_QSPI_BACON_IDLE_MASK)

/* ECONz -- per-CS baud rate and clock timing (z=0..7, offset 0x120+z*4) */

#define TC4X_QSPI_ECON_Q_SHIFT            0
#define TC4X_QSPI_ECON_Q_MASK             GENMASK(5, 0)
#define TC4X_QSPI_ECON_Q(v) \
        (((uint32_t)(v) << TC4X_QSPI_ECON_Q_SHIFT) & TC4X_QSPI_ECON_Q_MASK)
#define TC4X_QSPI_ECON_A_SHIFT            6
#define TC4X_QSPI_ECON_A_MASK             GENMASK(7, 6)
#define TC4X_QSPI_ECON_A(v) \
        (((uint32_t)(v) << TC4X_QSPI_ECON_A_SHIFT) & TC4X_QSPI_ECON_A_MASK)
#define TC4X_QSPI_ECON_B_SHIFT            8
#define TC4X_QSPI_ECON_B_MASK             GENMASK(9, 8)
#define TC4X_QSPI_ECON_B(v) \
        (((uint32_t)(v) << TC4X_QSPI_ECON_B_SHIFT) & TC4X_QSPI_ECON_B_MASK)
#define TC4X_QSPI_ECON_C_SHIFT            10
#define TC4X_QSPI_ECON_C_MASK             GENMASK(11, 10)
#define TC4X_QSPI_ECON_C(v) \
        (((uint32_t)(v) << TC4X_QSPI_ECON_C_SHIFT) & TC4X_QSPI_ECON_C_MASK)
#define TC4X_QSPI_ECON_CPH                BIT(12)  /* Clock phase          */
#define TC4X_QSPI_ECON_CPOL               BIT(13)  /* Clock polarity       */
#define TC4X_QSPI_ECON_PAREN              BIT(14)  /* Parity enable        */

/* D field (bits[17:16]): Bit segment 4, only used when GLOBALCON.HS=1 */
#define TC4X_QSPI_ECON_D_SHIFT            16
#define TC4X_QSPI_ECON_D_MASK             GENMASK(17, 16)
#define TC4X_QSPI_ECON_D(v) \
        (((uint32_t)(v) << TC4X_QSPI_ECON_D_SHIFT) & TC4X_QSPI_ECON_D_MASK)
/* BE field (bits[31:30]): Big-endian byte permutation */
#define TC4X_QSPI_ECON_BE_SHIFT           30
#define TC4X_QSPI_ECON_BE_MASK            GENMASK(31, 30)
#define TC4X_QSPI_ECON_BE_DISABLED        (0u << TC4X_QSPI_ECON_BE_SHIFT)
#define TC4X_QSPI_ECON_BE_16BIT           (1u << TC4X_QSPI_ECON_BE_SHIFT)
#define TC4X_QSPI_ECON_BE_32BIT           (2u << TC4X_QSPI_ECON_BE_SHIFT)

/* STATUS -- Status Register
 * Offset 0x140, Kernel Reset value: 0x00000000H
 */

#define TC4X_QSPI_STATUS_ERRORFLAGS_MASK  GENMASK(8, 0)
#define TC4X_QSPI_STATUS_TXF              BIT(9)   /* TX interrupt flag    */
#define TC4X_QSPI_STATUS_RXF              BIT(10)  /* RX interrupt flag    */
#define TC4X_QSPI_STATUS_PT1F             BIT(11)  /* Phase trans 1 flag   */
#define TC4X_QSPI_STATUS_PT2F             BIT(12)  /* Phase trans 2 flag   */
#define TC4X_QSPI_STATUS_RPV              BIT(13)  /* Received parity val  */
#define TC4X_QSPI_STATUS_TPV              BIT(14)  /* Transmitted parity   */
#define TC4X_QSPI_STATUS_USRF             BIT(15)  /* User interrupt flag  */
#define TC4X_QSPI_STATUS_TXFIFOLEVEL_SHIFT 16
#define TC4X_QSPI_STATUS_TXFIFOLEVEL_MASK  GENMASK(19, 16)
#define TC4X_QSPI_STATUS_RXFIFOLEVEL_SHIFT 20
#define TC4X_QSPI_STATUS_RXFIFOLEVEL_MASK  GENMASK(23, 20)
#define TC4X_QSPI_STATUS_SLAVESEL_SHIFT   24
#define TC4X_QSPI_STATUS_SLAVESEL_MASK    GENMASK(27, 24)
#define TC4X_QSPI_STATUS_PHASE_MASK       GENMASK(31, 28)
#define TC4X_QSPI_STATUS_PHASE_WAIT       (0u << 28)

/* SSOC -- Slave Select Output Control Register
 * Offset 0x148, Kernel Reset value: 0x00000000H
 */

#define TC4X_QSPI_SSOC_AOL_SHIFT          0
#define TC4X_QSPI_SSOC_AOL_MASK           GENMASK(15, 0)
#define TC4X_QSPI_SSOC_OEN_SHIFT          16
#define TC4X_QSPI_SSOC_OEN_MASK           GENMASK(31, 16)
#define TC4X_QSPI_SSOC_AOL_BIT(n)         BIT(TC4X_QSPI_SSOC_AOL_SHIFT + (n))
#define TC4X_QSPI_SSOC_OEN_BIT(n)         BIT(TC4X_QSPI_SSOC_OEN_SHIFT + (n))

/* FLAGSCLEAR -- Event Flag Clear Register
 * Offset 0x150, Kernel Reset value: 0x00000000H
 */

#define TC4X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK  GENMASK(8, 0)
#define TC4X_QSPI_FLAGSCLEAR_TXC              BIT(9)
#define TC4X_QSPI_FLAGSCLEAR_RXC              BIT(10)
#define TC4X_QSPI_FLAGSCLEAR_PT1C             BIT(11)
#define TC4X_QSPI_FLAGSCLEAR_PT2C             BIT(12)
#define TC4X_QSPI_FLAGSCLEAR_USRC             BIT(15)

#endif /* __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_QSPI_H */
