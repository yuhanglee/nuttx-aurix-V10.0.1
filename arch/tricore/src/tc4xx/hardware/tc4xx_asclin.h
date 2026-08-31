/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4xx_asclin.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_ASCLIN_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_ASCLIN_H

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TRICORE_ASCLIN0_BASE        (0xF02C0000 + CONFIG_TC4XX_PERI_MMAP_OFFSET)
#define TRICORE_ASCLIN_STRIDE       0x200
#define TRICORE_ASCLIN_BASE(n)      (TRICORE_ASCLIN0_BASE + ((uint32_t)(n) * TRICORE_ASCLIN_STRIDE))

#define ASCLIN_CLC_OFFSET           0x000
#define ASCLIN_OCS_OFFSET           0x004
#define ASCLIN_ID_OFFSET            0x008
#define ASCLIN_IOCR_OFFSET          0x100
#define ASCLIN_TXFIFOCON_OFFSET     0x104
#define ASCLIN_RXFIFOCON_OFFSET     0x108
#define ASCLIN_BITCON_OFFSET        0x10C
#define ASCLIN_FRAMECON_OFFSET      0x110
#define ASCLIN_DATCON_OFFSET        0x114
#define ASCLIN_BRG_OFFSET           0x118
#define ASCLIN_BRD_OFFSET           0x11C
#define ASCLIN_LINCON_OFFSET        0x120
#define ASCLIN_LINBTIMER_OFFSET     0x124
#define ASCLIN_LINHTIMER_OFFSET     0x128
#define ASCLIN_FLAGS_OFFSET         0x12C
#define ASCLIN_FLAGSSET_OFFSET      0x130
#define ASCLIN_FLAGSCLEAR_OFFSET    0x134
#define ASCLIN_FLAGSENABLE_OFFSET   0x138
#define ASCLIN_CSR_OFFSET           0x13C
#define ASCLIN_TXDATA_OFFSET        0x140
#define ASCLIN_RXDATA_OFFSET        0x160
#define ASCLIN_RXDATAD_OFFSET       0x180

/* APU-P access protection (TC4Dx).  Peripheral registers such as TXDATA are
 * APU-P protected; a DMA master must be granted write access via ACCEN_WRA
 * (master tags 0-31) / ACCEN_WRB (tags 32-63) or its write to the peripheral
 * raises a destination access-protection error (RP.ERRSR.DER).
 */

#define ASCLIN_PROTE_OFFSET         0x020
#define ASCLIN_ACCEN_WRA_OFFSET     0x040
#define ASCLIN_ACCEN_WRB_OFFSET     0x044
#define ASCLIN_ACCEN_RDA_OFFSET     0x048
#define ASCLIN_ACCEN_RDB_OFFSET     0x04C

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_ASCLIN_H */
