/****************************************************************************
 * arch/tricore/src/tc4x/hardware/tc4x_i2s.h
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

#ifndef __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_I2S_H
#define __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_I2S_H

/* Register map for the Infineon AURIX TC48x AUDIO TDM / I2S serial interface. */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/bits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AUDIO module (base 0xF4A00000, 64 KiB window) ******/

#define TC4X_AUDIO_BASE           0xF4A00000u
#define TC4X_AUDIO_MODULE_MASK    0xffff0000u
#define TC4X_AUDIO_NTDM           3        /* TC48x has 3 TDM instances       */

/* AUDIO module-level registers (offsets from the module base) ************/

#define TC4X_AUDIO_CLC_OFFSET      0x0000u  /* Clock control register       */
#define TC4X_AUDIO_PROTE_OFFSET    0x0020u  /* PROT register endinit        */
#define TC4X_AUDIO_ACCENGBL_WRA_OFFSET 0x0040u /* Global write access enable */
#define TC4X_AUDIO_ACCENGBL_RDA_OFFSET 0x0048u /* Global read  access enable */

/* Per-TDM-instance access-enable registers */

#define TC4X_AUDIO_ACCENTDM_WRA_OFFSET 0x0060u
#define TC4X_AUDIO_ACCENTDM_RDA_OFFSET 0x0068u
#define TC4X_AUDIO_ACCENTDM_STRIDE     0x0020u

/* TDMy sub-block base within the AUDIO module */

#define TC4X_AUDIO_TDM0_OFFSET     0x2000u
#define TC4X_AUDIO_TDM_STRIDE      0x0200u
#define TC4X_AUDIO_TDM_BASE(y)     (TC4X_AUDIO_BASE + TC4X_AUDIO_TDM0_OFFSET + \
                                    (y) * TC4X_AUDIO_TDM_STRIDE)

/* AUDIO CLC (standard AURIX clock control) */

#define TC4X_AUDIO_CLC_DISR        BIT(0)   /* Module disable request       */
#define TC4X_AUDIO_CLC_DISS        BIT(1)   /* Module disable status        */

/* TDM register offsets (relative to the TDM sub-block base) ***************/

/* TX sub-block (+0x000) */

#define TC4X_TDM_TX_CTL            0x000u   /* TX control                   */
#define TC4X_TDM_TX_IF_CTL         0x010u   /* TX interface control         */
#define TC4X_TDM_TX_CH_CTL         0x014u   /* TX channel control (CH_EN)   */
#define TC4X_TDM_TX_TEST_CTL       0x020u   /* TX test / loopback control   */
#define TC4X_TDM_TX_ROUTE_CTL      0x024u   /* TX route control             */
#define TC4X_TDM_TX_IOMUX_CTL      0x028u   /* TX pad-group IO mux control  */
#define TC4X_TDM_TX_FIFO_CTL       0x080u   /* TX FIFO control              */
#define TC4X_TDM_TX_FIFO_STATUS    0x084u   /* TX FIFO status               */
#define TC4X_TDM_TX_FIFO_WR        0x088u   /* TX FIFO write (WO)           */
#define TC4X_TDM_INTR_TX           0x0c0u   /* TX interrupt (W1C)           */
#define TC4X_TDM_INTR_TX_SET       0x0c4u   /* TX interrupt set             */
#define TC4X_TDM_INTR_TX_MASK      0x0c8u   /* TX interrupt mask (1=enable) */
#define TC4X_TDM_INTR_TX_MASKED    0x0ccu   /* TX interrupt masked (RO)     */

/* RX sub-block (+0x100) */

#define TC4X_TDM_RX_CTL            0x100u   /* RX control                   */
#define TC4X_TDM_RX_IF_CTL         0x110u   /* RX interface control         */
#define TC4X_TDM_RX_CH_CTL         0x114u   /* RX channel control (CH_EN)   */
#define TC4X_TDM_RX_TEST_CTL       0x120u   /* RX test / loopback control   */
#define TC4X_TDM_RX_ROUTE_CTL      0x124u   /* RX route control             */
#define TC4X_TDM_RX_IOMUX_CTL      0x128u   /* RX pad-group IO mux control  */
#define TC4X_TDM_RX_FIFO_CTL       0x180u   /* RX FIFO control              */
#define TC4X_TDM_RX_FIFO_STATUS    0x184u   /* RX FIFO status               */
#define TC4X_TDM_RX_FIFO_RD        0x188u   /* RX FIFO read (RO)            */
#define TC4X_TDM_RX_FIFO_RD_SILENT 0x18cu   /* RX FIFO silent read (RO)     */
#define TC4X_TDM_INTR_RX           0x1c0u   /* RX interrupt (W1C)           */
#define TC4X_TDM_INTR_RX_SET       0x1c4u   /* RX interrupt set             */
#define TC4X_TDM_INTR_RX_MASK      0x1c8u   /* RX interrupt mask (1=enable) */
#define TC4X_TDM_INTR_RX_MASKED    0x1ccu   /* RX interrupt masked (RO)     */

/* TX_CTL (0x000) / RX_CTL (0x100) ************/

#define TDM_CTL_WORD_SIZE_SHIFT    0        /* [3:0]  word-size enum code    */
#define TDM_CTL_WORD_SIZE_MASK     GENMASK(3, 0)
#define TDM_RX_CTL_WORD_SIGN_EXTEND BIT(8)  /* RX only: 1=sign, 0=zero ext  */
#define TDM_CTL_FORMAT_SHIFT       12       /* [13:12] delayed/justified     */
#define TDM_CTL_FORMAT_MASK        GENMASK(13, 12)
#define TDM_CTL_MS                 BIT(16)  /* 1=master (SoC drives clocks) */
#define TDM_CTL_START              BIT(30)  /* RUN bit (set AFTER ACTIVE)    */
#define TDM_CTL_ENABLED            BIT(31)  /* block enable                 */

/* TX_IF_CTL (0x010) ***********************************/

#define TDM_TX_IF_CTL_CLOCK_DIV_SHIFT 0     /* [11:0] SCK = clk_if/(DIV+1)   */
#define TDM_TX_IF_CTL_CLOCK_DIV_MASK  GENMASK(11, 0)
#define TDM_TX_IF_CTL_MCK_DIV_SHIFT   12    /* [13:12] 0=/1 1=/2 2=/4 3=/8   */
#define TDM_TX_IF_CTL_MCK_DIV_MASK    GENMASK(13, 12)
#define TDM_TX_IF_CTL_CLOCK_SEL       BIT(14) /* 0=APLL 98.304 MHz, 1=MCKI  */
#define TDM_TX_IF_CTL_SCK_POLARITY    BIT(17) /* 0=rising sample, 1=inverted */
#define TDM_TX_IF_CTL_FSYNC_POLARITY  BIT(18) /* 0=as-is, 1=inverted (I2S)  */
#define TDM_TX_IF_CTL_FSYNC_FORMAT    BIT(19) /* 0=bit-period, 1=ch-period  */
#define TDM_TX_IF_CTL_CH_NR_SHIFT     20    /* [24:20] channels - 1          */
#define TDM_TX_IF_CTL_CH_NR_MASK      GENMASK(24, 20)
#define TDM_TX_IF_CTL_CH_SIZE_SHIFT   25    /* [29:25] slot width - 1        */
#define TDM_TX_IF_CTL_CH_SIZE_MASK    GENMASK(29, 25)
#define TDM_TX_IF_CTL_I2S_MODE        BIT(31) /* 1=I2S, 0=TDM/PCM           */

/* RX_IF_CTL (0x110) - DIFFERENT bit layout from TX *****/

#define TDM_RX_IF_CTL_CLOCK_DIV_SHIFT 0     /* [11:0]                        */
#define TDM_RX_IF_CTL_CLOCK_DIV_MASK  GENMASK(11, 0)
#define TDM_RX_IF_CTL_MCK_DIV_SHIFT   12    /* [13:12]                       */
#define TDM_RX_IF_CTL_MCK_DIV_MASK    GENMASK(13, 12)
#define TDM_RX_IF_CTL_CLOCK_SEL       BIT(14)
#define TDM_RX_IF_CTL_SCK_POLARITY    BIT(15)
#define TDM_RX_IF_CTL_FSYNC_POLARITY  BIT(16)
#define TDM_RX_IF_CTL_LATE_SAMPLE     BIT(17)
#define TDM_RX_IF_CTL_FSYNC_FORMAT    BIT(18)
#define TDM_RX_IF_CTL_CH_NR_SHIFT     19    /* [23:19] channels - 1          */
#define TDM_RX_IF_CTL_CH_NR_MASK      GENMASK(23, 19)
#define TDM_RX_IF_CTL_CH_SIZE_SHIFT   24    /* [28:24] slot width - 1        */
#define TDM_RX_IF_CTL_CH_SIZE_MASK    GENMASK(28, 24)
#define TDM_RX_IF_CTL_LATE_CAPTURE_SHIFT 29 /* [30:29]                       */
#define TDM_RX_IF_CTL_LATE_CAPTURE_MASK  GENMASK(30, 29)
#define TDM_RX_IF_CTL_I2S_MODE        BIT(31)

/* CH_CTL (0x014 / 0x114) - one bit per slot, write (1<<channels)-1 ********/

#define TDM_CH_CTL_CH_EN_MASK      0xffffffffu

/* TEST_CTL (0x020 / 0x120) - internal TX_SDO -> RX_SDI loopback ***********/

#define TDM_TEST_CTL_ENABLED       BIT(31)

/* ROUTE_CTL (0x024 / 0x124) - slave signalling routing mode **************/

#define TDM_ROUTE_CTL_MODE_SHIFT   0
#define TDM_ROUTE_CTL_MODE_MASK    GENMASK(1, 0)

/* TX_IOMUX_CTL (0x028) - AURIX pad-group mux */

#define TDM_TX_IOMUX_MCK_MUX_SEL_SHIFT   0  /* [1:0]  (unused in master)     */
#define TDM_TX_IOMUX_MCK_MUX_SEL_MASK    GENMASK(1, 0)
#define TDM_TX_IOMUX_SCK_MUX_SEL_SHIFT   2  /* [3:2]                         */
#define TDM_TX_IOMUX_SCK_MUX_SEL_MASK    GENMASK(3, 2)
#define TDM_TX_IOMUX_FSYNC_MUX_SEL_SHIFT 4  /* [5:4]                         */
#define TDM_TX_IOMUX_FSYNC_MUX_SEL_MASK  GENMASK(5, 4)
#define TDM_TX_IOMUX_SDOUT_EN_SEL_SHIFT  6  /* [7:6]                         */
#define TDM_TX_IOMUX_SDOUT_EN_SEL_MASK   GENMASK(7, 6)
#define TDM_TX_IOMUX_SDOUT_DRV_EN        BIT(8)

/* RX_IOMUX_CTL (0x128) - AURIX pad-group mux */

#define TDM_RX_IOMUX_MCK_MUX_SEL_SHIFT   0  /* [1:0]                         */
#define TDM_RX_IOMUX_MCK_MUX_SEL_MASK    GENMASK(1, 0)
#define TDM_RX_IOMUX_SCK_MUX_SEL_SHIFT   2  /* [3:2]                         */
#define TDM_RX_IOMUX_SCK_MUX_SEL_MASK    GENMASK(3, 2)
#define TDM_RX_IOMUX_FSYNC_MUX_SEL_SHIFT 4  /* [5:4]                         */
#define TDM_RX_IOMUX_FSYNC_MUX_SEL_MASK  GENMASK(5, 4)
#define TDM_RX_IOMUX_SD_MUX_SEL_SHIFT    6  /* [7:6]                         */
#define TDM_RX_IOMUX_SD_MUX_SEL_MASK     GENMASK(7, 6)

/* FIFO_CTL (TX 0x080 / RX 0x180) *************/

#define TDM_FIFO_CTL_TRIGGER_LEVEL_SHIFT 0  /* [6:0]                         */
#define TDM_FIFO_CTL_TRIGGER_LEVEL_MASK  GENMASK(6, 0)
#define TDM_TX_FIFO_CTL_MUTE             BIT(16) /* TX only                  */
#define TDM_FIFO_CTL_FREEZE              BIT(17)
#define TDM_FIFO_CTL_ACTIVE              BIT(18) /* serializer consumes/prod */
#define TDM_FIFO_CTL_SDMADONE_SEL_SHIFT  20 /* [27:20] SDMA0 c->c, SDMA1->0x80+c */
#define TDM_FIFO_CTL_SDMADONE_SEL_MASK   GENMASK(27, 20)

/* FIFO_STATUS (TX 0x084 / RX 0x184) - USED range [0,128] *****************/

#define TDM_FIFO_STATUS_USED_SHIFT 0        /* [7:0]                         */
#define TDM_FIFO_STATUS_USED_MASK  GENMASK(7, 0)
#define TDM_FIFO_STATUS_RD_PTR_MASK GENMASK(22, 16)
#define TDM_FIFO_STATUS_WR_PTR_MASK GENMASK(30, 24)

/* INTR_TX (0x0C0) *************************************/

#define TDM_INTR_TX_FIFO_TRIGGER   BIT(0)   /* USED < TRIGGER_LEVEL          */
#define TDM_INTR_TX_FIFO_OVERFLOW  BIT(1)
#define TDM_INTR_TX_FIFO_UNDERFLOW BIT(2)   /* USED < enabled channels       */
#define TDM_INTR_TX_IF_UNDERFLOW   BIT(8)
#define TDM_INTR_TX_ALL            (TDM_INTR_TX_FIFO_TRIGGER  | \
                                    TDM_INTR_TX_FIFO_OVERFLOW | \
                                    TDM_INTR_TX_FIFO_UNDERFLOW | \
                                    TDM_INTR_TX_IF_UNDERFLOW)

/* INTR_RX (0x1C0) ************************************/

#define TDM_INTR_RX_FIFO_TRIGGER   BIT(0)   /* USED > TRIGGER_LEVEL          */
#define TDM_INTR_RX_FIFO_OVERFLOW  BIT(1)
#define TDM_INTR_RX_FIFO_UNDERFLOW BIT(2)
#define TDM_INTR_RX_IF_OVERFLOW    BIT(8)
#define TDM_INTR_RX_ALL            (TDM_INTR_RX_FIFO_TRIGGER  | \
                                    TDM_INTR_RX_FIFO_OVERFLOW | \
                                    TDM_INTR_RX_FIFO_UNDERFLOW | \
                                    TDM_INTR_RX_IF_OVERFLOW)

/* Field value encodings **************************/

/* *_CTL.WORD_SIZE size codes (CH_SIZE instead uses slot_bits - 1) */

#define TDM_SIZE_CODE_8    0u
#define TDM_SIZE_CODE_10   1u
#define TDM_SIZE_CODE_12   2u
#define TDM_SIZE_CODE_14   3u
#define TDM_SIZE_CODE_16   4u
#define TDM_SIZE_CODE_18   5u
#define TDM_SIZE_CODE_20   6u
#define TDM_SIZE_CODE_24   7u
#define TDM_SIZE_CODE_32   8u

/* *_CTL.FORMAT codes */

#define TDM_FMT_LEFT_DELAYED   0u   /* I2S / PCM: MSB one clock after sync   */
#define TDM_FMT_LEFT           1u
#define TDM_FMT_RIGHT_DELAYED  2u
#define TDM_FMT_RIGHT          3u

/* *_CTL.MS codes */

#define TDM_DEVICE_SLAVE   0u
#define TDM_DEVICE_MASTER  1u

/* *_IF_CTL.CLOCK_SEL codes */

#define TDM_SEL_APLL_CLK   0u   /* clk_if from Audio PLL (98.304 MHz)        */
#define TDM_SEL_MCKI       1u   /* clk_if from external TDM_MCKI             */

/* *_IF_CTL.MCK_DIV codes (MCKO = clk_if / MCK_DIV) */

#define TDM_MCK_DIV_1      0u
#define TDM_MCK_DIV_2      1u
#define TDM_MCK_DIV_4      2u
#define TDM_MCK_DIV_8      3u

/* polarity / format codes */

#define TDM_CLK            0u   /* SCK not inverted (sample on rising edge)  */
#define TDM_CLK_INVERTED   1u   /* SCK inverted (sample on falling edge)     */
#define TDM_SIGN           0u   /* FSYNC not inverted                        */
#define TDM_SIGN_INVERTED  1u   /* FSYNC inverted (I2S: WS=0 -> left)        */
#define TDM_BIT_PERIOD     0u   /* FSYNC one bit-clock wide (PCM_SHORT)      */
#define TDM_CH_PERIOD      1u   /* FSYNC one channel wide (I2S / PCM_LONG)   */

/* FIFO geometry */

#define TDM_FIFO_DEPTH     128u  /* entries, each a fixed 32 bits            */

#endif /* __ARCH_TRICORE_SRC_TC4X_HARDWARE_TC4X_I2S_H */
