/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4xx_dma.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_DMA_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/bits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SDMA module base address (TC4Dx: DMA0 @ 0xF4410000 on the TC4D7, i.e. the
 * nominal 0xF0010000 base shifted by the peripheral memory-map offset).  The
 * per-channel register layout is identical to the TC3xx DMA; the TC4xx
 * additions are the resource-partition (RP) MODE registers and the per-
 * channel HRR (channel-to-RP assignment) registers.
 */

#ifndef CONFIG_TC4XX_PERI_MMAP_OFFSET
#  define CONFIG_TC4XX_PERI_MMAP_OFFSET  0
#endif

#define TC4XX_DMA_BASE             (0xF0010000 + CONFIG_TC4XX_PERI_MMAP_OFFSET)

#define TC4XX_DMA_NR_CHANNELS       128

/* Module / global registers ************************************************/

#define TC4XX_DMA_CLC_OFFSET        0x0000  /* Clock control register */

/* Resource-partition mode registers (RP r, stride 0x80) */

#define TC4XX_DMA_MODE_OFFSET(r)    (0x1030 + ((r) * 0x80))

/* Per-channel arrays (stride 4) */

#define TC4XX_DMA_HRR_OFFSET(c)     (0x1800 + ((c) * 4))   /* Ch -> RP assign */
#define TC4XX_DMA_TSR_OFFSET(c)     (0x1E00 + ((c) * 4))   /* Transaction st. */

/* Per-channel register block (stride 0x20) @ 0x2000 */

#define TC4XX_DMA_CH_OFFSET(c)      (0x2000 + ((c) * 0x20))
#define TC4XX_DMA_CH_SADR_OFFSET    0x08
#define TC4XX_DMA_CH_DADR_OFFSET    0x0c
#define TC4XX_DMA_CH_ADICR_OFFSET   0x10
#define TC4XX_DMA_CH_CHCFGR_OFFSET  0x14
#define TC4XX_DMA_CH_SHADR_OFFSET   0x18
#define TC4XX_DMA_CH_CHCSR_OFFSET   0x1c

/* Absolute register address helpers ****************************************/

#define TC4XX_DMA_CLC              (TC4XX_DMA_BASE + TC4XX_DMA_CLC_OFFSET)
#define TC4XX_DMA_MODE(r)          (TC4XX_DMA_BASE + TC4XX_DMA_MODE_OFFSET(r))
#define TC4XX_DMA_HRR(c)           (TC4XX_DMA_BASE + TC4XX_DMA_HRR_OFFSET(c))
#define TC4XX_DMA_TSR(c)           (TC4XX_DMA_BASE + TC4XX_DMA_TSR_OFFSET(c))

#define TC4XX_DMA_CH_REG(c, r)     (TC4XX_DMA_BASE + TC4XX_DMA_CH_OFFSET(c) + \
                                    (r))
#define TC4XX_DMA_SADR(c)          TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_SADR_OFFSET)
#define TC4XX_DMA_DADR(c)          TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_DADR_OFFSET)
#define TC4XX_DMA_ADICR(c)         TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_ADICR_OFFSET)
#define TC4XX_DMA_CHCFGR(c)        TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_CHCFGR_OFFSET)
#define TC4XX_DMA_SHADR(c)         TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_SHADR_OFFSET)
#define TC4XX_DMA_CHCSR(c)         TC4XX_DMA_CH_REG(c, TC4XX_DMA_CH_CHCSR_OFFSET)

/* CLC - Clock Control register *********************************************/

#define DMA_CLC_DISR               BIT(0)   /* Module disable request */
#define DMA_CLC_DISS               BIT(1)   /* Module disable status */

/* ADICR - Address and Interrupt Control register ***************************/

#define DMA_ADICR_INCS             BIT(3)   /* Increment source address */
#define DMA_ADICR_INCD             BIT(7)   /* Increment dest address */
#define DMA_ADICR_SHCT_SHIFT       16       /* [19:16] Shadow control */
#define DMA_ADICR_SCBE             BIT(20)  /* Source circ buffer enable */
#define DMA_ADICR_DCBE             BIT(21)  /* Dest circ buffer enable */
#define DMA_ADICR_INTCT_SHIFT      26       /* [27:26] Interrupt control */

#define DMA_ADICR_INTCT_TRANSACTION_END    0x2   /* IRQ at transaction end */

/* SHCT shadow-control encoding: double destination buffer, auto-switch */

#define DMA_ADICR_SHCT_DOUBLE_DEST_AUTOSW  0xb

/* CHCFGR - Channel Configuration register **********************************/

#define DMA_CHCFGR_TREL_SHIFT      0        /* [13:0] Transfer reload value */
#define DMA_CHCFGR_TREL_MASK       (0x3fff << DMA_CHCFGR_TREL_SHIFT)
#define DMA_CHCFGR_TREL_MAX        0x3fff
#define DMA_CHCFGR_BLKM_SHIFT      16       /* [18:16] Block mode */
#define DMA_CHCFGR_RROAT           BIT(19)  /* Reset req. only after trans. */
#define DMA_CHCFGR_CHMODE          BIT(20)  /* Channel mode (1 = continuous) */
#define DMA_CHCFGR_CHDW_SHIFT      21       /* [23:21] Channel data width */

/* CHDW channel-data-width encodings (1 << CHDW = bytes per move) */

#define DMA_CHDW_8BIT              0
#define DMA_CHDW_16BIT             1
#define DMA_CHDW_32BIT             2
#define DMA_CHDW_64BIT             3

/* BLKM block-mode encodings (number of moves per transfer) */

#define DMA_BLKM_1_MOVE           0
#define DMA_BLKM_2_MOVES          1
#define DMA_BLKM_4_MOVES          2
#define DMA_BLKM_8_MOVES          3
#define DMA_BLKM_16_MOVES         4
#define DMA_BLKM_3_MOVES          5
#define DMA_BLKM_5_MOVES          6
#define DMA_BLKM_9_MOVES          7

/* CHCSR - Channel Control and Status register ******************************/

#define DMA_CHCSR_TCOUNT_SHIFT     0        /* [13:0] Transfer count */
#define DMA_CHCSR_TCOUNT_MASK      (0x3fff << DMA_CHCSR_TCOUNT_SHIFT)
#define DMA_CHCSR_ICH              BIT(18)  /* Interrupt from channel */
#define DMA_CHCSR_BUFFER           BIT(22)  /* Double-buffer active buffer */
#define DMA_CHCSR_FROZEN           BIT(23)  /* Double-buffer frozen buffer */
#define DMA_CHCSR_CICH             BIT(26)  /* Clear channel IRQ (write-only) */
#define DMA_CHCSR_SCH              BIT(31)  /* Set transaction (write-only) */

/* TSR - Transaction State register *****************************************/

#define DMA_TSR_RST                BIT(0)   /* Channel reset */
#define DMA_TSR_HTRE               BIT(1)   /* Hardware request enable */
#define DMA_TSR_TRL                BIT(2)   /* Transaction request lost */
#define DMA_TSR_CH                 BIT(3)   /* Channel transaction state */
#define DMA_TSR_ETRL               BIT(4)   /* Enable TRL interrupt */
#define DMA_TSR_ECH                BIT(16)  /* Enable HW request (wr-only) */
#define DMA_TSR_DCH                BIT(17)  /* Disable HW request (wr-only) */

/* HRR - channel-to-resource-partition assignment register ******************/

#define DMA_HRR_HRP_SHIFT          0        /* [3:0] Resource partition */
#define DMA_HRR_HRP_MASK           (0xf << DMA_HRR_HRP_SHIFT)
#define DMA_HRR_HRPV               BIT(4)   /* RP assignment valid */

/* MODE - resource-partition mode register **********************************/

#define DMA_RP_MODE_CS             BIT(1)   /* Cyber-security assignment */
#define DMA_RP_MODE_VALID          BIT(31)  /* Resource partition valid */

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_DMA_H */
