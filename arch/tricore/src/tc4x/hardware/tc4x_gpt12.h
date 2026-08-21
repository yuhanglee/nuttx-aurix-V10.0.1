/****************************************************************************
 * arch/tricore/src/tc4x/hardware/tc4x_gpt12.h
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

/* TC4xx GPT12 register definitions. */

#ifndef __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_GPT12_H
#define __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_GPT12_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/bits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TC4X_GPT120_BASE          0xF0020000u
#define TC4X_GPT121_BASE          0xF0020200u
#define TC4X_GPT122_BASE          0xF0020400u
#define TC4X_GPT123_BASE          0xF0020600u

#define TC4X_GPT12_REG(base, off) ((base) + (off))

/* Register offsets */

#define GPT12_CLC_OFFSET           0x000u  /* Clock Control Register */
#define GPT12_OCS_OFFSET           0x004u  /* OCDS Control and Status Register */
#define GPT12_ID_OFFSET            0x008u  /* Module Identification Register */
#define GPT12_PROTE_OFFSET         0x020u  /* Protection Register Endinit */

#define GPT12_ACCEN_WRA_OFFSET     0x040u
#define GPT12_ACCEN_RDA_OFFSET     0x048u

#define GPT12_PISEL_OFFSET         0x104u  /* Port Input Select Register */
#define GPT12_T2CON_OFFSET         0x110u  /* Timer 2 Control Register */
#define GPT12_T3CON_OFFSET         0x114u  /* Timer 3 Control Register */
#define GPT12_T4CON_OFFSET         0x118u  /* Timer 4 Control Register */
#define GPT12_T5CON_OFFSET         0x11Cu  /* Timer 5 Control Register */
#define GPT12_T6CON_OFFSET         0x120u  /* Timer 6 Control Register */

#define GPT12_CAPREL_OFFSET        0x130u  /* Capture/Reload Register */
#define GPT12_T2_OFFSET            0x134u  /* Timer 2 Count Register */
#define GPT12_T3_OFFSET            0x138u  /* Timer 3 Count Register */
#define GPT12_T4_OFFSET            0x13Cu  /* Timer 4 Count Register */
#define GPT12_T5_OFFSET            0x140u  /* Timer 5 Count Register */
#define GPT12_T6_OFFSET            0x144u  /* Timer 6 Count Register */

/* CLC — Clock Control Register */

#define GPT12_CLC_DISR             BIT(0)  /* Module disable request (rw) */
#define GPT12_CLC_DISS             BIT(1)  /* Module disabled status (rh) */
#define GPT12_CLC_EDIS             BIT(3)  /* Sleep mode disable (rw) */

/* T3CON — Timer T3 Control Register */

#define GPT12_TCON_BPS_SHIFT       11u
#define GPT12_TCON_BPS_MASK        GENMASK(12, 11)
#define GPT12_TCON_BPS_DIV0        (0x1u << GPT12_TCON_BPS_SHIFT)
#define GPT12_TCON_BPS_DIV1        (0x0u << GPT12_TCON_BPS_SHIFT)
#define GPT12_TCON_BPS_DIV2        (0x3u << GPT12_TCON_BPS_SHIFT)
#define GPT12_TCON_BPS_DIV3        (0x2u << GPT12_TCON_BPS_SHIFT)

#define GPT12_T3CON_T3I_SHIFT      0u
#define GPT12_T3CON_T3I_MASK       GENMASK(2, 0)
#define GPT12_T3CON_T3M_SHIFT      3u
#define GPT12_T3CON_T3M_MASK       GENMASK(5, 3)
#define GPT12_T3CON_T3R            BIT(6)
#define GPT12_T3CON_T3UD           BIT(7)   /* Up/Down: 0=up, 1=down */
#define GPT12_T3CON_T3UDE          BIT(8)   /* External up/down enable */
#define GPT12_T3CON_T3OE           BIT(9)   /* T3OTL -> T3OUT output enable */
#define GPT12_T3CON_T3OTL          BIT(10)  /* Overflow toggle latch (rwh) */
#define GPT12_T3CON_BPS1_SHIFT     GPT12_TCON_BPS_SHIFT
#define GPT12_T3CON_BPS1_MASK      GPT12_TCON_BPS_MASK

#define GPT12_T3CON_T3M_TIMER      (0x0u << GPT12_T3CON_T3M_SHIFT)
#define GPT12_T3CON_T3M_COUNTER    (0x1u << GPT12_T3CON_T3M_SHIFT)


#define GPT12_T3CON_BPS1_DIV4      GPT12_TCON_BPS_DIV0
#define GPT12_T3CON_BPS1_DIV8      GPT12_TCON_BPS_DIV1
#define GPT12_T3CON_BPS1_DIV16     GPT12_TCON_BPS_DIV2
#define GPT12_T3CON_BPS1_DIV32     GPT12_TCON_BPS_DIV3

/* T2CON / T4CON — Auxiliary Timer Control */

#define GPT12_TXCON_TXI_SHIFT      0u
#define GPT12_TXCON_TXI_MASK       GENMASK(2, 0)
#define GPT12_TXCON_TXM_SHIFT      3u
#define GPT12_TXCON_TXM_MASK       GENMASK(5, 3)
#define GPT12_TXCON_TXR            BIT(6)   /* Run bit */

#define GPT12_TXCON_TXM_TIMER      (0x0u << GPT12_TXCON_TXM_SHIFT)
#define GPT12_TXCON_TXM_RELOAD     (0x4u << GPT12_TXCON_TXM_SHIFT)
#define GPT12_TXCON_TXM_CAPTURE    (0x5u << GPT12_TXCON_TXM_SHIFT)

#define GPT12_TXCON_TXI_T3OTL_POS  5u
#define GPT12_TXCON_TXI_T3OTL_NEG  6u

/* T6CON — Timer T6 Control Register */

#define GPT12_T6CON_T6I_SHIFT      0u
#define GPT12_T6CON_T6I_MASK       GENMASK(2, 0)
#define GPT12_T6CON_T6M_SHIFT      3u
#define GPT12_T6CON_T6M_MASK       GENMASK(5, 3)
#define GPT12_T6CON_T6R            BIT(6)
#define GPT12_T6CON_T6UD           BIT(7)
#define GPT12_T6CON_T6OE           BIT(9)   /* T6OTL -> T6OUT output enable */
#define GPT12_T6CON_T6OTL          BIT(10)  /* Overflow toggle latch (rwh) */
#define GPT12_T6CON_BPS2_SHIFT     GPT12_TCON_BPS_SHIFT
#define GPT12_T6CON_BPS2_MASK      GPT12_TCON_BPS_MASK
#define GPT12_T6CON_T6SR           BIT(15)  /* CAPREL reload enable */

#define GPT12_T6CON_T6M_TIMER      (0x0u << GPT12_T6CON_T6M_SHIFT)


#define GPT12_T6CON_BPS2_DIV2      GPT12_TCON_BPS_DIV0
#define GPT12_T6CON_BPS2_DIV4      GPT12_TCON_BPS_DIV1
#define GPT12_T6CON_BPS2_DIV8      GPT12_TCON_BPS_DIV2
#define GPT12_T6CON_BPS2_DIV16     GPT12_TCON_BPS_DIV3

/* CAPREL — Capture/Reload Register */

#define GPT12_CAPREL_MASK          GENMASK(15, 0)

/* Reload value: timer overflows after n ticks. */

#define GPT12_RELOAD(n)            ((uint16_t)(UINT16_MAX - (n)))

#endif /* __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_GPT12_H */
