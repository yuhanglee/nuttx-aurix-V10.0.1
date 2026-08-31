/****************************************************************************
 * arch/tricore/src/tc3xx/tc3xx_gpt12_cap.h
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

/* GPT12 T5/CAPREL capture lower-half driver for TC3xx.
 *
 * CAPREL is shared with GPT2 T6 PWM.  Both cannot run simultaneously;
 * start() returns -EBUSY if T6 is already running.
 */

#ifndef __ARCH_TRICORE_SRC_TC3XX_GPT12_CAP_H
#define __ARCH_TRICORE_SRC_TC3XX_GPT12_CAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_TC3XX_GPT2_CAP) || defined(CONFIG_TC3XX_GPT1_CAP)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: tc3xx_gpt12_cap_register
 *
 *   Enable the GPT120 clock and register /dev/capN with the NuttX capture
 *   upper-half driver.  The board init layer must configure the GPIO pin
 *   before calling this function.
 *
 *   minor == 0        ->  CAPIN  (GPT2 T5/CAPREL CT3=0, CONFIG_TC3XX_GPT2_CAP)
 *   minor == 2        ->  T2IN   (GPT1 T3/T2, CONFIG_TC3XX_GPT1_CAP)
 *   minor == 3        ->  T3IN   (GPT2 T5/CAPREL CT3=1, CONFIG_TC3XX_GPT2_CAP_T3IN)
 *   minor == 4        ->  T4IN   (GPT1 T3/T4, CONFIG_TC3XX_GPT1_CAP)
 *
 * Input Parameters:
 *   minor - Device minor number (N in /dev/capN).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int tc3xx_gpt12_cap_register(int minor);

#endif /* CONFIG_TC3XX_GPT2_CAP || CONFIG_TC3XX_GPT1_CAP */

#endif /* __ARCH_TRICORE_SRC_TC3XX_GPT12_CAP_H */
