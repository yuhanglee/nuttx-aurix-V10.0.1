/****************************************************************************
 * arch/tricore/include/tc4x/irq.h
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

/* This file should never be included directly but, rather,
 * only indirectly through nuttx/irq.h
 */

#ifndef __ARCH_TRICORE_INCLUDE_TC4X_IRQ_H
#define __ARCH_TRICORE_INCLUDE_TC4X_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define NR_IRQS 2048

/* SMU Service Request nodes (TC4Dx Interrupt Router).
 *   SMU_IR_IRQ[0..2] -> SMU_SAFE0 IGCS0/1/2
 *   SMU_IR_IRQ[3..5] -> SMU_SAFE1 IGCS0/1/2
 *   SMU_CSIRQ        -> cross-signaling IRQ
 */

#define TC4X_IRQ_SMU_SAFE0_IGCS0  1216
#define TC4X_IRQ_SMU_SAFE0_IGCS1  1217
#define TC4X_IRQ_SMU_SAFE0_IGCS2  1218
#define TC4X_IRQ_SMU_SAFE1_IGCS0  1219
#define TC4X_IRQ_SMU_SAFE1_IGCS1  1220
#define TC4X_IRQ_SMU_SAFE1_IGCS2  1221
#define TC4X_IRQ_SMU_CSIRQ        1222

#define TC4X_IRQ_SMM0             1212
#define TC4X_IRQ_SMM1             1213

/* AUDIO TDM SRC nodes (TC48x): 4 contiguous per instance from 1480 (TXDMA,
 * RXDMA, TXERR, RXERR); node = (SRC register addr - 0xF4432000) / 4.
 */

#define TC4X_IRQ_AUDIO_TDM_TXDMA(i)  (1480 + (i) * 4)  /* TX FIFO DMA req     */
#define TC4X_IRQ_AUDIO_TDM_RXDMA(i)  (1481 + (i) * 4)  /* RX FIFO DMA req     */
#define TC4X_IRQ_AUDIO_TDM_TXERR(i)  (1482 + (i) * 4)  /* TX err/trigger (CPU) */
#define TC4X_IRQ_AUDIO_TDM_RXERR(i)  (1483 + (i) * 4)  /* RX err/trigger (CPU) */

/****************************************************************************
 * Public Types
 ****************************************************************************/

#endif /* __ARCH_TRICORE_INCLUDE_TC4X_IRQ_H */
