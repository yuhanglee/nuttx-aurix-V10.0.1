/****************************************************************************
 * boards/tricore/tc4xx/triboard-tc4x7-com/include/board.h
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

#ifndef __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_INCLUDE_BOARD_H
#define __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED indices for board_userled(): LED1(D402/P33.0)..LED8(D409/P13.3) */

#define BOARD_LED1    0
#define BOARD_LED2    1
#define BOARD_LED3    2
#define BOARD_LED4    3
#define BOARD_LED5    4
#define BOARD_LED6    5
#define BOARD_LED7    6
#define BOARD_LED8    7
#define BOARD_NLEDS   8

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT  (1 << BOARD_LED1)
#define BOARD_LED2_BIT  (1 << BOARD_LED2)
#define BOARD_LED3_BIT  (1 << BOARD_LED3)
#define BOARD_LED4_BIT  (1 << BOARD_LED4)
#define BOARD_LED5_BIT  (1 << BOARD_LED5)
#define BOARD_LED6_BIT  (1 << BOARD_LED6)
#define BOARD_LED7_BIT  (1 << BOARD_LED7)
#define BOARD_LED8_BIT  (1 << BOARD_LED8)

/* Autoled states (CONFIG_ARCH_LEDS): LEDs 1-8 light in sequence at boot;
 * INIRQ/SIGNAL/IDLE are no-ops; PANIC/ASSERTION blinks all 8.
 */

#define LED_STARTED       0
#define LED_HEAPALLOCATE  1
#define LED_IRQSENABLED   2
#define LED_STACKCREATED  3
#define LED_INIRQ         4
#define LED_SIGNAL        5
#define LED_ASSERTION     6
#define LED_PANIC         7
#define LED_IDLE          8

#endif /* __BOARDS_TRICORE_TC4XX_TRIBOARD_TC4X7_COM_INCLUDE_BOARD_H */
