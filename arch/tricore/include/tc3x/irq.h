/****************************************************************************
 * arch/tricore/include/tc3x/irq.h
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

#ifndef __ARCH_TRICORE_INCLUDE_TC3X_IRQ_H
#define __ARCH_TRICORE_INCLUDE_TC3X_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#define NR_IRQS 2048

/* SMU Service Request nodes (TC3xx UM Part 2 Interrupt Router chapter).
 * SRC_SMUy (y=0..2) base offset = 0x008D0H from IFX_IR_SRC_BASE.
 * IRQ number = offset / 4.
 */

#define TC3X_IRQ_SMU0  564   /* SRC_SMU0: IGCS0 alarm reactions */
#define TC3X_IRQ_SMU1  565   /* SRC_SMU1: IGCS1 alarm reactions */
#define TC3X_IRQ_SMU2  566   /* SRC_SMU2: IGCS2 alarm reactions */

#endif /* __ARCH_TRICORE_INCLUDE_TC3X_IRQ_H */
