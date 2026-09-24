/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_i2s.h
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

#ifndef __ARCH_TRICORE_SRC_TC4X_TC4X_I2S_H
#define __ARCH_TRICORE_SRC_TC4X_TC4X_I2S_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <nuttx/audio/i2s.h>

#ifdef CONFIG_TC4X_I2S

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Audio PLL interface-clock frequency, in Hz (default 98.304 MHz).  A board
 * may override it before including this header.
 */

#ifndef TC4X_I2S_APLL_FREQUENCY
#  define TC4X_I2S_APLL_FREQUENCY  98304000u
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: tc4x_i2s_initialize
 *
 * Description:
 *   Initialise one AURIX TC48x AUDIO/TDM instance and return its lower-half
 *   I2S device object for registration with an upper-half driver.  Per-instance
 *   settings are taken from the instance's Kconfig options (CONFIG_TC4X_I2Sn_*).
 *
 * Input Parameters:
 *   port - TDM instance number (0 .. TC4X_AUDIO0_NTDM-1).
 *
 * Returned Value:
 *   A pointer to the I2S device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct i2s_dev_s *tc4x_i2s_initialize(int port);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* CONFIG_TC4X_I2S */
#endif /* __ARCH_TRICORE_SRC_TC4X_TC4X_I2S_H */
