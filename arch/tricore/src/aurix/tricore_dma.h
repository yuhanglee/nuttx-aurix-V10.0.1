/****************************************************************************
 * arch/tricore/src/aurix/tricore_dma.h
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

#ifndef __ARCH_TRICORE_SRC_AURIX_TRICORE_DMA_H
#define __ARCH_TRICORE_SRC_AURIX_TRICORE_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/dma/dma.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Burst length extension to the generic dma_config_s.
 *
 * The generic framework config carries no burst/block-length field, so the
 * driver reads the number of DMA moves per transfer (the CHCFGR.BLKM "block
 * mode") from dma_config_s.option.  Legal values are 1, 2, 3, 4, 5, 8, 9 and
 * 16 moves; 0 (or an unset option) defaults to 1.
 *
 * Total bytes moved = TREL x moves x width, rounded up to a (moves x width)
 * boundary, where width is src/dst_width.
 */

#define TRICORE_DMA_BURST(moves)   (moves)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_dma_initialize
 *
 * Description:
 *   Initialise the Aurix SDMA controller and return the generic DMA device
 *   object.  A peripheral or board can then acquire a channel with
 *   DMA_GET_CHAN(dev, channel) and drive it through the standard NuttX DMA
 *   framework (DMA_CONFIG / DMA_START / ...).
 *
 *   The call is idempotent: the controller is initialised once and the same
 *   device object is returned on subsequent calls.
 *
 * Returned Value:
 *   Pointer to the DMA device on success, NULL on failure.
 *
 ****************************************************************************/

FAR struct dma_dev_s *tricore_dma_initialize(void);

#endif /* __ARCH_TRICORE_SRC_AURIX_TRICORE_DMA_H */
