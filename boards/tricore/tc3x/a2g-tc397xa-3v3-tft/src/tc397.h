/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397xa-3v3-tft/src/tc397.h
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

#ifndef __BOARDS_TRICORE_TC3XX_A2G_TC397XA_3V3_TFT_SRC_TC397_H
#define __BOARDS_TRICORE_TC3XX_A2G_TC397XA_3V3_TFT_SRC_TC397_H

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
 * A2G-TC397XA-3V3-TFT user LED connections:
 *   D107 (blue): P13.0 - active LOW
 *   D108 (blue): P13.1 - active LOW
 *   D109 (blue): P13.2 - active LOW
 *   D110 (blue): P13.3 - active LOW
 *
 * GPIO_OUTPUT_HIGH sets the initial output value HIGH (LED OFF, active LOW).
 */

#define GPIO_LED1  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN0, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)
#define GPIO_LED2  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN1, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)
#define GPIO_LED3  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN2, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)
#define GPIO_LED4  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN3, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)

/* Number of GPIO-controlled LEDs */

#define GPIO_NLEDS  4

#endif /* __BOARDS_TRICORE_TC3XX_A2G_TC397XA_3V3_TFT_SRC_TC397_H */
