/****************************************************************************
 * boards/tricore/tc4x/triboard-tc4x7-com/src/tc4x7.h
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

#ifndef __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_SRC_TC4X7_H
#define __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_SRC_TC4X7_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include "tricore_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED GPIO pin definitions *************************************************
 *
 * TriBoard TC4X7 COM user LED connections (all blue, active LOW):
 *
 *   D402  P33.0  (SCR: P0.0)  blue  Low-active
 *   D403  P33.1  (SCR: P0.1)  blue  Low-active
 *   D404  P33.4  (SCR: P0.4)  blue  Low-active
 *   D405  P33.5  (SCR: P0.5)  blue  Low-active
 *   D406  P13.0              blue  Low-active
 *   D407  P13.1              blue  Low-active
 *   D408  P13.2              blue  Low-active
 *   D409  P13.3              blue  Low-active
 *
 * GPIO_OUTPUT_HIGH initialises the pin HIGH so the LED is OFF at boot.
 *
 ****************************************************************************/

#define GPIO_LED1  (AURIX_GPIO(GPIO_PORT33, GPIO_PIN0, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D402 P33.0 */
#define GPIO_LED2  (AURIX_GPIO(GPIO_PORT33, GPIO_PIN1, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D403 P33.1 */
#define GPIO_LED3  (AURIX_GPIO(GPIO_PORT33, GPIO_PIN4, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D404 P33.4 */
#define GPIO_LED4  (AURIX_GPIO(GPIO_PORT33, GPIO_PIN5, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D405 P33.5 */
#define GPIO_LED5  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN0, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D406 P13.0 */
#define GPIO_LED6  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN1, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D407 P13.1 */
#define GPIO_LED7  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN2, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D408 P13.2 */
#define GPIO_LED8  (AURIX_GPIO(GPIO_PORT13, GPIO_PIN3, GPIO_OUTPUT, GPIO_ALT0) \
                    | GPIO_OUTPUT_HIGH)   /* D409 P13.3 */

#define GPIO_NLEDS  8

#endif /* __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_SRC_TC4X7_H */
