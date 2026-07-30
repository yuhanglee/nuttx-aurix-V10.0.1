/****************************************************************************
 * arch/tricore/src/tc3x/hardware/tc3x_gpt12.h
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

/* TC3xx GPT12 (General Purpose Timer 12) register definitions.
 *
 * One GPT120 instance per device, base address 0xF0001800.
 * Two independent timer blocks:
 *   GPT1: T2 (auxiliary), T3 (core), T4 (auxiliary)
 *   GPT2: T5 (auxiliary), T6 (core), CAPREL
 *
 * Source clock: fGPT = fSPB (100 MHz on default TC3xx clock config).
 * GPT1 block prescaler: BPS1 in T3CON, then per-timer divider T3I.
 * GPT2 block prescaler: BPS2 in T6CON, then per-timer divider T6I.
 */

#ifndef __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_GPT12_H
#define __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_GPT12_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/bits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPT120 module base address */

#define TC3X_GPT120_BASE          CONFIG_TC3X_GPT120_BASE

/* Register offsets from GPT120 base */

#define GPT12_CLC_OFFSET           0x000u  /* Clock Control Register */
#define GPT12_PISEL_OFFSET         0x004u  /* Port Input Select Register */
#define GPT12_ID_OFFSET            0x008u  /* Module Identification Register */

/* 0x00C: reserved */
#define GPT12_T2CON_OFFSET         0x010u  /* Timer 2 Control Register */
#define GPT12_T3CON_OFFSET         0x014u  /* Timer 3 Control Register */
#define GPT12_T4CON_OFFSET         0x018u  /* Timer 4 Control Register */
#define GPT12_T5CON_OFFSET         0x01Cu  /* Timer 5 Control Register */
#define GPT12_T6CON_OFFSET         0x020u  /* Timer 6 Control Register */

/* 0x024, 0x028, 0x02C: reserved */
#define GPT12_CAPREL_OFFSET        0x030u  /* Capture/Reload Register */
#define GPT12_T2_OFFSET            0x034u  /* Timer 2 Count Register */
#define GPT12_T3_OFFSET            0x038u  /* Timer 3 Count Register */
#define GPT12_T4_OFFSET            0x03Cu  /* Timer 4 Count Register */
#define GPT12_T5_OFFSET            0x040u  /* Timer 5 Count Register */
#define GPT12_T6_OFFSET            0x044u  /* Timer 6 Count Register */

/* 0x048 .. 0x0E4: reserved */
#define GPT12_OCS_OFFSET           0x0E8u  /* OCDS Control and Status Register */
#define GPT12_KRSTCLR_OFFSET       0x0ECu  /* Kernel Reset Status Clear Register */
#define GPT12_KRST1_OFFSET         0x0F0u  /* Kernel Reset Register 1 */
#define GPT12_KRST0_OFFSET         0x0F4u  /* Kernel Reset Register 0 */

/* 0x0F8: reserved */
#define GPT12_ACCEN0_OFFSET        0x0FCu  /* Access Enable Register 0 */

/* Convenience accessor: absolute address of a GPT120 register */

#define GPT12_REG(off)             (TC3X_GPT120_BASE + (off))

/****************************************************************************
 * CLC — Clock Control Register  (offset 0x000, reset value 0x00000003)
 *
 ****************************************************************************/

#define GPT12_CLC_DISR             BIT(0)  /* Module disable request (rw) */
#define GPT12_CLC_DISS             BIT(1)  /* Module disabled status (rh) */
#define GPT12_CLC_EDIS             BIT(3)  /* Sleep mode disable (rw) */

/****************************************************************************
 * T3CON — Timer T3 Control Register (0014H)  Reset value: 0x00000000
 *
 * f_T3 = fGPT / (F(BPS1) × 2^T3I)
 ****************************************************************************/

#define GPT12_T3CON_T3I_SHIFT      0u
#define GPT12_T3CON_T3I_MASK       GENMASK(2, 0)
#define GPT12_T3CON_T3M_SHIFT      3u
#define GPT12_T3CON_T3M_MASK       GENMASK(5, 3)
#define GPT12_T3CON_T3R            BIT(6)   /* Run bit (rw) */
#define GPT12_T3CON_T3UD           BIT(7)   /* Up/Down: 0=up, 1=down (rw) */
#define GPT12_T3CON_T3UDE          BIT(8)   /* External up/down enable (rw) */
#define GPT12_T3CON_T3OE           BIT(9)   /* Output enable: T3OTL→T3OUT (rw) */
#define GPT12_T3CON_T3OTL          BIT(10)  /* Overflow toggle latch (rwh) */
#define GPT12_T3CON_BPS1_SHIFT     11u
#define GPT12_T3CON_BPS1_MASK      GENMASK(12, 11)
#define GPT12_T3CON_T3EDGE         BIT(13)  /* Edge detection flag (rwh) */
#define GPT12_T3CON_T3CHDIR        BIT(14)  /* Direction change flag (rwh) */
#define GPT12_T3CON_T3RDIR         BIT(15)  /* Rotation direction (rh) */

/* T3M mode values (3-bit field, pre-shifted) */

#define GPT12_T3CON_T3M_TIMER      (0x0u << GPT12_T3CON_T3M_SHIFT)  /* Timer mode */
#define GPT12_T3CON_T3M_COUNTER    (0x1u << GPT12_T3CON_T3M_SHIFT)  /* Counter mode */
#define GPT12_T3CON_T3M_GATED_L    (0x2u << GPT12_T3CON_T3M_SHIFT)  /* Gated, gate active low */
#define GPT12_T3CON_T3M_GATED_H    (0x3u << GPT12_T3CON_T3M_SHIFT)  /* Gated, gate active high */
#define GPT12_T3CON_T3M_IIM_ROT    (0x6u << GPT12_T3CON_T3M_SHIFT)  /* IIM rotate */
#define GPT12_T3CON_T3M_IIM_EDGE   (0x7u << GPT12_T3CON_T3M_SHIFT)  /* IIM edge */

/* BPS1 field values (pre-shifted);
 * encoding: 01b=/4, 00b=/8, 11b=/16, 10b=/32
 */

#define GPT12_T3CON_BPS1_DIV4      (0x1u << GPT12_T3CON_BPS1_SHIFT)  /* fGPT/4  = 25 MHz */
#define GPT12_T3CON_BPS1_DIV8      (0x0u << GPT12_T3CON_BPS1_SHIFT)  /* fGPT/8  = 12.5 MHz */
#define GPT12_T3CON_BPS1_DIV16     (0x3u << GPT12_T3CON_BPS1_SHIFT)  /* fGPT/16 = 6.25 MHz */
#define GPT12_T3CON_BPS1_DIV32     (0x2u << GPT12_T3CON_BPS1_SHIFT)  /* fGPT/32 = 3.125 MHz */

/* Bits common to both T2CON and T4CON */

#define GPT12_TXCON_TXI_SHIFT      0u
#define GPT12_TXCON_TXI_MASK       GENMASK(2, 0)
#define GPT12_TXCON_TXM_SHIFT      3u
#define GPT12_TXCON_TXM_MASK       GENMASK(5, 3)
#define GPT12_TXCON_TXR            BIT(6)   /* Run bit (rw) */

/* Bit common to T3CON and T6CON: output toggle latch enable (bit 9). */

#define GPT12_TXCON_TXOE           BIT(9)   /* T3OE / T6OE: route TxOTL to TxOUT */
#define GPT12_TXCON_TXUD           BIT(7)   /* Up/Down: 0=up, 1=down (rw) */
#define GPT12_TXCON_TXUDE          BIT(8)   /* External up/down enable (rw) */
#define GPT12_TXCON_TXRC           BIT(9)   /* Remote control by T3R (rw) */
#define GPT12_TXCON_TXIRDIS        BIT(12)  /* IIM interrupt disable (rw) */
#define GPT12_TXCON_TXEDGE         BIT(13)  /* Edge detection flag (rwh) */
#define GPT12_TXCON_TXCHDIR        BIT(14)  /* Direction change flag (rwh) */
#define GPT12_TXCON_TXRDIR         BIT(15)  /* Rotation direction (rh) */

/* T4CON-only bits */

#define GPT12_T4CON_CLRT2EN        BIT(10)  /* Falling T4EUD clears T2 (rw) */
#define GPT12_T4CON_CLRT3EN        BIT(11)  /* Falling T4IN clears T3 (rw) */

/* TxM mode values (3-bit field, pre-shifted) */

#define GPT12_TXCON_TXM_TIMER      (0x0u << GPT12_TXCON_TXM_SHIFT)  /* Timer mode */
#define GPT12_TXCON_TXM_COUNTER    (0x1u << GPT12_TXCON_TXM_SHIFT)  /* Counter mode */
#define GPT12_TXCON_TXM_GATED_L    (0x2u << GPT12_TXCON_TXM_SHIFT)  /* Gated, gate active low */
#define GPT12_TXCON_TXM_GATED_H    (0x3u << GPT12_TXCON_TXM_SHIFT)  /* Gated, gate active high */
#define GPT12_TXCON_TXM_RELOAD     (0x4u << GPT12_TXCON_TXM_SHIFT)  /* Reload mode */
#define GPT12_TXCON_TXM_CAPTURE    (0x5u << GPT12_TXCON_TXM_SHIFT)  /* Capture mode */
#define GPT12_T3CON_T3M_IIM_ROT    (0x6u << GPT12_T3CON_T3M_SHIFT)  /* IIM rotate */
#define GPT12_T3CON_T3M_IIM_EDGE   (0x7u << GPT12_T3CON_T3M_SHIFT)  /* IIM edge */

/* TxI trigger source values (raw, use with GPT12_TXCON_TXI_SHIFT) */

#define GPT12_TXCON_TXI_DISABLE     0u  /* No trigger */
#define GPT12_TXCON_TXI_PIN_POS     1u  /* Positive edge on TxIN */
#define GPT12_TXCON_TXI_PIN_NEG     2u  /* Negative edge on TxIN */
#define GPT12_TXCON_TXI_PIN_BOTH    3u  /* Any edge on TxIN */
#define GPT12_TXCON_TXI_T3OTL_POS   5u  /* Positive T3OTL transition */
#define GPT12_TXCON_TXI_T3OTL_NEG   6u  /* Negative T3OTL transition */
#define GPT12_TXCON_TXI_T3OTL_BOTH  7u  /* Any T3OTL transition */

/****************************************************************************
 * T6CON — Timer T6 Control Register (0020H)  Reset value: 0x00000000
 *
 * f_T6 = fGPT / (F(BPS2) × 2^T6I)
 * T6SR=1 enables CAPREL reload of T6 on each overflow.
 ****************************************************************************/

#define GPT12_T6CON_T6I_SHIFT      0u
#define GPT12_T6CON_T6I_MASK       GENMASK(2, 0)
#define GPT12_T6CON_T6M_SHIFT      3u
#define GPT12_T6CON_T6M_MASK       GENMASK(5, 3)
#define GPT12_T6CON_T6R            BIT(6)   /* Run bit (rw) */
#define GPT12_T6CON_T6UD           BIT(7)   /* Up/Down: 0=up, 1=down (rw) */
#define GPT12_T6CON_T6UDE          BIT(8)   /* External up/down enable (rw) */
#define GPT12_T6CON_T6OE           BIT(9)   /* Output enable: T6OTL→T6OUT (rw) */
#define GPT12_T6CON_T6OTL          BIT(10)  /* Overflow toggle latch (rwh) */
#define GPT12_T6CON_BPS2_SHIFT     11u
#define GPT12_T6CON_BPS2_MASK      GENMASK(12, 11)
#define GPT12_T6CON_T6CLR          BIT(14)  /* Clear enable: T6 cleared on capture event (rw) */
#define GPT12_T6CON_T6SR           BIT(15)  /* CAPREL reload enable (rw) */

/* T6M modes — GPT2 has no IIM mode */

#define GPT12_T6CON_T6M_TIMER      (0x0u << GPT12_T6CON_T6M_SHIFT)
#define GPT12_T6CON_T6M_COUNTER    (0x1u << GPT12_T6CON_T6M_SHIFT)
#define GPT12_T6CON_T6M_GATED_L    (0x2u << GPT12_T6CON_T6M_SHIFT)
#define GPT12_T6CON_T6M_GATED_H    (0x3u << GPT12_T6CON_T6M_SHIFT)

/* BPS2 field values (pre-shifted); encoding differs from BPS1:
 * 00b=÷4, 01b=÷2, 10b=÷16, 11b=÷8
*/


#define GPT12_T6CON_BPS2_DIV2      (0x1u << GPT12_T6CON_BPS2_SHIFT)  /* fGPT/2  = 50.0 MHz */
#define GPT12_T6CON_BPS2_DIV4      (0x0u << GPT12_T6CON_BPS2_SHIFT)  /* fGPT/4  = 25.0 MHz */
#define GPT12_T6CON_BPS2_DIV8      (0x3u << GPT12_T6CON_BPS2_SHIFT)  /* fGPT/8  = 12.5 MHz */
#define GPT12_T6CON_BPS2_DIV16     (0x2u << GPT12_T6CON_BPS2_SHIFT)  /* fGPT/16 =  6.25 MHz */

/****************************************************************************
 * T5CON — Timer T5 Control Register (001CH)  Reset value: 0x00000000
 *
 ****************************************************************************/

#define GPT12_T5CON_T5I_SHIFT      0u
#define GPT12_T5CON_T5I_MASK       GENMASK(2, 0)
#define GPT12_T5CON_T5M_SHIFT      3u
#define GPT12_T5CON_T5M_MASK       GENMASK(5, 3)
#define GPT12_T5CON_T5R            BIT(6)    /* Run bit (rw) */
#define GPT12_T5CON_T5UD           BIT(7)    /* Up/Down: 0=up, 1=down (rw) */
#define GPT12_T5CON_T5UDE          BIT(8)    /* External up/down enable (rw) */
#define GPT12_T5CON_T5RC           BIT(9)    /* Remote control by T6R (rw) */
#define GPT12_T5CON_CT3            BIT(10)   /* Capture trigger: 0=CAPIN, 1=T3IN/T3EUD (rw) */
#define GPT12_T5CON_CI_SHIFT       12u
#define GPT12_T5CON_CI_MASK        GENMASK(13, 12)
#define GPT12_T5CON_T5CLR          BIT(14)   /* Clear T5 after capture (rw) */
#define GPT12_T5CON_T5SC           BIT(15)   /* Enable capture into CAPREL (rw) */

/* T5M mode values (pre-shifted) — same encoding as T6M */

#define GPT12_T5CON_T5M_TIMER      (0x0u << GPT12_T5CON_T5M_SHIFT)
#define GPT12_T5CON_T5M_COUNTER    (0x1u << GPT12_T5CON_T5M_SHIFT)
#define GPT12_T5CON_T5M_GATED_L    (0x2u << GPT12_T5CON_T5M_SHIFT)
#define GPT12_T5CON_T5M_GATED_H    (0x3u << GPT12_T5CON_T5M_SHIFT)

/* CI field values (pre-shifted): capture trigger edge selection for CAPIN
 *  When CT3=1 the encoding selects T3IN/T3EUD instead.
 */

#define GPT12_T5CON_CI_DISABLE     (0x0u << GPT12_T5CON_CI_SHIFT)  /* Capture disabled */
#define GPT12_T5CON_CI_RISING      (0x1u << GPT12_T5CON_CI_SHIFT)  /* Rising edge on CAPIN */
#define GPT12_T5CON_CI_FALLING     (0x2u << GPT12_T5CON_CI_SHIFT)  /* Falling edge on CAPIN */
#define GPT12_T5CON_CI_BOTH        (0x3u << GPT12_T5CON_CI_SHIFT)  /* Both edges on CAPIN */

/****************************************************************************
 * PISEL — Port Input Select Register (0004H)  Reset value: 0x00000000
 *
 ****************************************************************************/

/* Single-bit selects (0=alt A, 1=alt B) */

#define GPT12_PISEL_IST2IN         BIT(0)    /* 0=T2INA,  1=T2INB  */
#define GPT12_PISEL_IST2EUD        BIT(1)    /* 0=T2EUDA, 1=T2EUDB */

/* Two-bit selects for T3 and T4 inputs */

#define GPT12_PISEL_IST3IN_SHIFT   2u
#define GPT12_PISEL_IST3IN_MASK    GENMASK(3, 2)
#define GPT12_PISEL_IST3EUD_SHIFT  4u
#define GPT12_PISEL_IST3EUD_MASK   GENMASK(5, 4)
#define GPT12_PISEL_IST4IN_SHIFT   6u
#define GPT12_PISEL_IST4IN_MASK    GENMASK(7, 6)
#define GPT12_PISEL_IST4EUD_SHIFT  8u
#define GPT12_PISEL_IST4EUD_MASK   GENMASK(9, 8)

/* Single-bit selects for T5 and T6 inputs */

#define GPT12_PISEL_IST5IN         BIT(10)   /* 0=T5INA,  1=T5INB  */
#define GPT12_PISEL_IST5EUD        BIT(11)   /* 0=T5EUDA, 1=T5EUDB */
#define GPT12_PISEL_IST6IN         BIT(12)   /* 0=T6INA,  1=T6INB  */
#define GPT12_PISEL_IST6EUD        BIT(13)   /* 0=T6EUDA, 1=T6EUDB */

/* Two-bit CAPIN select */

#define GPT12_PISEL_ISCAPIN_SHIFT  14u
#define GPT12_PISEL_ISCAPIN_MASK   GENMASK(15, 14)

/* ISCAPIN field values (pre-shifted) */

#define GPT12_PISEL_ISCAPIN_A      (0x0u << GPT12_PISEL_ISCAPIN_SHIFT)  /* P13.2 (CAPINA) */
#define GPT12_PISEL_ISCAPIN_B      (0x1u << GPT12_PISEL_ISCAPIN_SHIFT)  /* CAPINB */
#define GPT12_PISEL_ISCAPIN_T3RD   (0x2u << GPT12_PISEL_ISCAPIN_SHIFT)  /* T3 read trigger */
#define GPT12_PISEL_ISCAPIN_TXRD   (0x3u << GPT12_PISEL_ISCAPIN_SHIFT)  /* T2/T3/T4 read trigger */

/****************************************************************************
 * CAPREL — Capture/Reload Register (0030H)  Reset value: 0x00000000
 ****************************************************************************/

#define GPT12_CAPREL_SHIFT         0u
#define GPT12_CAPREL_MASK          GENMASK(15, 0)   /* 16-bit capture/reload value */

/****************************************************************************
 * Reload value helper
 *
 * T3/T6 counts up from a preloaded value to 0xFFFF then overflows.
 * To produce exactly N ticks per phase: reload = UINT16_MAX - N.
 ****************************************************************************/

#define GPT12_RELOAD(n)            ((uint16_t)(UINT16_MAX - (n)))

#endif /* __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_GPT12_H */
