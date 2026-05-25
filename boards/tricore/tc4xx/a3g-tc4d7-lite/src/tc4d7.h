/****************************************************************************
 * boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7.h
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

#ifndef __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_SRC_TC4D7_H
#define __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_SRC_TC4D7_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "tc4xx_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED GPIO pin definitions using NuttX AURIX GPIO pinset encoding.
 *
 * A3G-TC4D7-Lite Kit user LED connections (Table 3):
 *   LED1 (green): P03.9  - active LOW (pull against GND)
 *   LED2 (green): P03.10 - active LOW (pull against GND)
 *   LED5 (red):   ESR0   - hardware fault indicator, not GPIO-controlled
 *
 * GPIO_OUTPUT_HIGH sets the initial output value HIGH (LED OFF, active LOW).
 */

#define GPIO_LED1  (AURIX_GPIO(3, 9, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)
#define GPIO_LED2  (AURIX_GPIO(3, 10, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)

/* Number of GPIO-controlled LEDs (LED5/ESR0 is hardware-driven) */

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
 * Name: tc4d7_bringup
 *
 * Description:
 *   Bring up board features
 *
 ****************************************************************************/

#if defined(CONFIG_BOARDCTL) || defined(CONFIG_BOARD_LATE_INITIALIZE)
int tc4d7_bringup(void);
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_SRC_TC4D7_H */
