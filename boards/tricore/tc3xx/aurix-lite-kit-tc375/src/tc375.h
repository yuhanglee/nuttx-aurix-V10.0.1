/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375.h
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

#ifndef __BOARDS_TRICORE_TC3XX_AURIX_LITE_KIT_TC375_SRC_TC375_H
#define __BOARDS_TRICORE_TC3XX_AURIX_LITE_KIT_TC375_SRC_TC375_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "tc3xx_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED GPIO pin definitions using NuttX AURIX GPIO pinset encoding.
 *
 * AURIX Lite Kit V2 TC375 user LED connections:
 *   LED1 (green): P00.5 - active LOW
 *   LED2 (green): P00.6 - active LOW
 *   LED3 (red):   ESR0  - hardware fault indicator, not GPIO-controlled
 *
 * GPIO_OUTPUT_HIGH sets the initial output value HIGH (LED OFF, active LOW).
 */

#define GPIO_LED1  (AURIX_GPIO(GPIO_PORT0, GPIO_PIN5, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)
#define GPIO_LED2  (AURIX_GPIO(GPIO_PORT0, GPIO_PIN6, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)

/* Number of GPIO-controlled LEDs (LED3/ESR0 is hardware-driven) */

#define GPIO_NLEDS  2

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Functions Definitions
 ****************************************************************************/

/****************************************************************************
 * Name: tc375_bringup
 *
 * Description:
 *   Bring up board features
 *
 ****************************************************************************/

#if defined(CONFIG_BOARDCTL) || defined(CONFIG_BOARD_LATE_INITIALIZE)
int tc375_bringup(void);
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_TRICORE_TC3XX_AURIX_LITE_KIT_TC375_SRC_TC375_H */
