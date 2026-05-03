/****************************************************************************
 * drivers/xspi/xspi_dw_ssi_regs.h
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
 * Synopsys DesignWare DWC_SSI register definitions. SoC specific base
 * address and absolute addresses live in the SoC glue layer.
 *
 ****************************************************************************/

#ifndef __DRIVERS_XSPI_XSPI_DW_SSI_REGS_H
#define __DRIVERS_XSPI_XSPI_DW_SSI_REGS_H


/* Register offsets */

#define XSPI_DW_SSI_CLC_OFFSET            0x0000
#define XSPI_DW_SSI_OCS_OFFSET            0x0004
#define XSPI_DW_SSI_ID_OFFSET             0x0008
#define XSPI_DW_SSI_RST_CTRLA_OFFSET      0x000c
#define XSPI_DW_SSI_RST_CTRLB_OFFSET      0x0010
#define XSPI_DW_SSI_RST_STAT_OFFSET       0x0014

#define XSPI_DW_SSI_INTSTAT_OFFSET        0x0160
#define XSPI_DW_SSI_INTCLR_OFFSET         0x0164
#define XSPI_DW_SSI_INTMSK_OFFSET         0x0168
#define XSPI_DW_SSI_CTRLR2_OFFSET         0x016c
#define XSPI_DW_SSI_CTRLR0_OFFSET         0x0200
#define XSPI_DW_SSI_CTRLR1_OFFSET         0x0204
#define XSPI_DW_SSI_SSIENR_OFFSET         0x0208
#define XSPI_DW_SSI_SER_OFFSET            0x0210
#define XSPI_DW_SSI_BAUDR_OFFSET          0x0214
#define XSPI_DW_SSI_TXFTLR_OFFSET         0x0218
#define XSPI_DW_SSI_RXFTLR_OFFSET         0x021c
#define XSPI_DW_SSI_TXFLR_OFFSET          0x0220
#define XSPI_DW_SSI_RXFLR_OFFSET          0x0224
#define XSPI_DW_SSI_SR_OFFSET             0x0228
#define XSPI_DW_SSI_IMR_OFFSET            0x022c
#define XSPI_DW_SSI_ISR_OFFSET            0x0230
#define XSPI_DW_SSI_RISR_OFFSET           0x0234
#define XSPI_DW_SSI_TXEICR_OFFSET         0x0238
#define XSPI_DW_SSI_RXOICR_OFFSET         0x023c
#define XSPI_DW_SSI_RXUICR_OFFSET         0x0240
#define XSPI_DW_SSI_MSTICR_OFFSET         0x0244
#define XSPI_DW_SSI_ICR_OFFSET            0x0248
#define XSPI_DW_SSI_DMACR_OFFSET          0x024c
#define XSPI_DW_SSI_AXIAWLEN_OFFSET       0x0250
#define XSPI_DW_SSI_AXIARLEN_OFFSET       0x0254
#define XSPI_DW_SSI_VERSION_ID_OFFSET     0x025c
#define XSPI_DW_SSI_DR_OFFSET(i)          (0x0260 + ((i) * 4))
#define XSPI_DW_SSI_RX_SAMPLE_DLY_OFFSET  0x02f0
#define XSPI_DW_SSI_SPI_CTRLR0_OFFSET     0x02f4
#define XSPI_DW_SSI_DDR_DRIVE_EDGE_OFFSET 0x02f8

#define XSPI_DW_SSI_SAFETYCR_OFFSET       0x0380
#define XSPI_DW_SSI_SLVIFFSMTOCR_OFFSET   0x0384
#define XSPI_DW_SSI_SSIFSMTOCR_OFFSET     0x0388
#define XSPI_DW_SSI_AXIFSMTOCR_OFFSET     0x038c
#define XSPI_DW_SSI_SAFETYISR_OFFSET      0x03a0
#define XSPI_DW_SSI_SAFETYICR_OFFSET      0x03a4

/* CLC - Clock Control Register */

#define XSPI_DW_SSI_CLC_DISR              (1 << 0)
#define XSPI_DW_SSI_CLC_DISS              (1 << 1)
#define XSPI_DW_SSI_CLC_EDIS              (1 << 3)

/* CTRLR2 - Control Register 2 */

#define XSPI_DW_SSI_CTRLR2_PORTSEL        (1 << 0)
#  define XSPI_DW_SSI_CTRLR2_PORTSEL_P16  (0 << 0)
#  define XSPI_DW_SSI_CTRLR2_PORTSEL_GPIO (1 << 0)


#define XSPI_DW_SSI_CTRLR0_DFS_SHIFT      (0)
#define XSPI_DW_SSI_CTRLR0_DFS_MASK       (0x1f << XSPI_DW_SSI_CTRLR0_DFS_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_DFS(n)       (((n) - 1) << XSPI_DW_SSI_CTRLR0_DFS_SHIFT)
#define XSPI_DW_SSI_CTRLR0_FRF_SHIFT      (6)
#define XSPI_DW_SSI_CTRLR0_FRF_MASK       (0x3 << XSPI_DW_SSI_CTRLR0_FRF_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_FRF_SPI      (0 << XSPI_DW_SSI_CTRLR0_FRF_SHIFT)
#define XSPI_DW_SSI_CTRLR0_SCPH           (1 << 8)
#define XSPI_DW_SSI_CTRLR0_SCPOL          (1 << 9)
#define XSPI_DW_SSI_CTRLR0_TMOD_SHIFT     (10)
#define XSPI_DW_SSI_CTRLR0_TMOD_MASK      (0x3 << XSPI_DW_SSI_CTRLR0_TMOD_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_TMOD_TXRX    (0 << XSPI_DW_SSI_CTRLR0_TMOD_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_TMOD_TX      (1 << XSPI_DW_SSI_CTRLR0_TMOD_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_TMOD_RX      (2 << XSPI_DW_SSI_CTRLR0_TMOD_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_TMOD_EEPROM  (3 << XSPI_DW_SSI_CTRLR0_TMOD_SHIFT)
#define XSPI_DW_SSI_CTRLR0_SRL            (1 << 13)
#define XSPI_DW_SSI_CTRLR0_SSTE           (1 << 14)
#define XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT  (22)
#define XSPI_DW_SSI_CTRLR0_SPI_FRF_MASK   (0x3 << XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_SPI_FRF_STD   (0 << XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_SPI_FRF_DUAL  (1 << XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_SPI_FRF_QUAD  (2 << XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT)
#  define XSPI_DW_SSI_CTRLR0_SPI_FRF_OCTAL (3 << XSPI_DW_SSI_CTRLR0_SPI_FRF_SHIFT)
#define XSPI_DW_SSI_CTRLR0_SPI_HYPERBUS_EN (1 << 24)
#define XSPI_DW_SSI_CTRLR0_CLK_LOOP_EN     (1 << 26)
#define XSPI_DW_SSI_CTRLR0_SSI_IS_MST      (1 << 31)

/* CTRLR1 - Control Register 1 */

#define XSPI_DW_SSI_CTRLR1_NDF_SHIFT      (0)
#define XSPI_DW_SSI_CTRLR1_NDF_MASK       (0xffff << XSPI_DW_SSI_CTRLR1_NDF_SHIFT)


#define XSPI_DW_SSI_SSIENR_SSIC_EN        (1 << 0)

/* SER - Slave Enable Register */

#define XSPI_DW_SSI_SER_SHIFT             (0)
#define XSPI_DW_SSI_SER_MASK              (0x3 << XSPI_DW_SSI_SER_SHIFT)
#  define XSPI_DW_SSI_SER_CS0             (1 << 0)
#  define XSPI_DW_SSI_SER_CS1             (1 << 1)


#define XSPI_DW_SSI_BAUDR_SCKDV_SHIFT     (0)
#define XSPI_DW_SSI_BAUDR_SCKDV_MASK      (0xfffe << XSPI_DW_SSI_BAUDR_SCKDV_SHIFT)

/* TXFTLR / RXFTLR - FIFO Threshold Level */

#define XSPI_DW_SSI_TXFTLR_SHIFT          (0)
#define XSPI_DW_SSI_TXFTLR_MASK           (0xf << XSPI_DW_SSI_TXFTLR_SHIFT)
#define XSPI_DW_SSI_RXFTLR_SHIFT          (0)
#define XSPI_DW_SSI_RXFTLR_MASK           (0xf << XSPI_DW_SSI_RXFTLR_SHIFT)

/* TXFLR / RXFLR - FIFO Level (read only) */

#define XSPI_DW_SSI_TXFLR_SHIFT           (0)
#define XSPI_DW_SSI_TXFLR_MASK            (0x1f << XSPI_DW_SSI_TXFLR_SHIFT)
#define XSPI_DW_SSI_RXFLR_SHIFT           (0)
#define XSPI_DW_SSI_RXFLR_MASK            (0x1f << XSPI_DW_SSI_RXFLR_SHIFT)

/* SR - Status Register */

#define XSPI_DW_SSI_SR_BUSY               (1 << 0)
#define XSPI_DW_SSI_SR_TFNF               (1 << 1)
#define XSPI_DW_SSI_SR_TFE                (1 << 2)
#define XSPI_DW_SSI_SR_RFNE               (1 << 3)
#define XSPI_DW_SSI_SR_RFF                (1 << 4)
#define XSPI_DW_SSI_SR_TXE                (1 << 5)
#define XSPI_DW_SSI_SR_DCOL               (1 << 6)
#define XSPI_DW_SSI_SR_CMPLTD_DF_SHIFT    (15)
#define XSPI_DW_SSI_SR_CMPLTD_DF_MASK     (0x1ffff << XSPI_DW_SSI_SR_CMPLTD_DF_SHIFT)

/* IMR / ISR / RISR / ICR - shared layout */

#define XSPI_DW_SSI_INT_TXEI              (1 << 0)
#define XSPI_DW_SSI_INT_TXOI              (1 << 1)
#define XSPI_DW_SSI_INT_RXUI              (1 << 2)
#define XSPI_DW_SSI_INT_RXOI              (1 << 3)
#define XSPI_DW_SSI_INT_RXFI              (1 << 4)
#define XSPI_DW_SSI_INT_MSTI              (1 << 5)
#define XSPI_DW_SSI_INT_XRXOI             (1 << 6)
#define XSPI_DW_SSI_INT_TXUI              (1 << 7)
#define XSPI_DW_SSI_INT_AXIE              (1 << 8)
#define XSPI_DW_SSI_INT_SPITE             (1 << 10)
#define XSPI_DW_SSI_INT_DONE              (1 << 11)
#define XSPI_DW_SSI_INT_ALL               (0xd7f)


#define XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_SHIFT    (0)
#define XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_MASK     (0x3 << XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT0    (0 << XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT1    (1 << XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT2    (2 << XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_SHIFT)
#define XSPI_DW_SSI_SPI_CTRLR0_ADDR_L_SHIFT        (2)
#define XSPI_DW_SSI_SPI_CTRLR0_ADDR_L_MASK         (0xf << XSPI_DW_SSI_SPI_CTRLR0_ADDR_L_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_ADDR_L(bits)      \
                ((((bits) + 3) / 4) << XSPI_DW_SSI_SPI_CTRLR0_ADDR_L_SHIFT)
#define XSPI_DW_SSI_SPI_CTRLR0_XIP_MD_BIT_EN       (1 << 7)
#define XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT        (8)
#define XSPI_DW_SSI_SPI_CTRLR0_INST_L_MASK         (0x3 << XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_INST_L_0          (0 << XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_INST_L_4          (1 << XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_INST_L_8          (2 << XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT)
#  define XSPI_DW_SSI_SPI_CTRLR0_INST_L_16         (3 << XSPI_DW_SSI_SPI_CTRLR0_INST_L_SHIFT)
#define XSPI_DW_SSI_SPI_CTRLR0_WAIT_CYCLES_SHIFT   (11)
#define XSPI_DW_SSI_SPI_CTRLR0_WAIT_CYCLES_MASK    (0x1f << XSPI_DW_SSI_SPI_CTRLR0_WAIT_CYCLES_SHIFT)
#define XSPI_DW_SSI_SPI_CTRLR0_SPI_DDR_EN          (1 << 16)
#define XSPI_DW_SSI_SPI_CTRLR0_INST_DDR_EN         (1 << 17)
#define XSPI_DW_SSI_SPI_CTRLR0_SPI_RXDS_EN         (1 << 18)
#define XSPI_DW_SSI_SPI_CTRLR0_XIP_DFS_HC          (1 << 19)
#define XSPI_DW_SSI_SPI_CTRLR0_XIP_INST_EN         (1 << 20)
#define XSPI_DW_SSI_SPI_CTRLR0_SSIC_XIP_CONT_EN    (1 << 21)
#define XSPI_DW_SSI_SPI_CTRLR0_SPI_DM_EN           (1 << 24)
#define XSPI_DW_SSI_SPI_CTRLR0_SPI_RXDS_SIG_EN     (1 << 25)
#define XSPI_DW_SSI_SPI_CTRLR0_XIP_MBL_SHIFT       (26)
#define XSPI_DW_SSI_SPI_CTRLR0_XIP_MBL_MASK        (0x3 << XSPI_DW_SSI_SPI_CTRLR0_XIP_MBL_SHIFT)
#define XSPI_DW_SSI_SPI_CTRLR0_CLK_STRETCH_EN      (1 << 30)

#endif /* __DRIVERS_XSPI_XSPI_DW_SSI_REGS_H */
