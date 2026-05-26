/****************************************************************************
 * boards/tricore/tc4xx/a3g-tc4d7-lite/include/board.h
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

#ifndef __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_INCLUDE_BOARD_H
#define __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED index values for use with board_userled() ****************************
 *
 * A3G-TC4D7-Lite Kit user LED connections:
 *   LED1 (green): P03.9  - active LOW (pull against GND)
 *   LED2 (green): P03.10 - active LOW (pull against GND)
 *   LED5 (red):   ESR0   - hardware fault indicator, not software-controlled
 *
 ****************************************************************************/

#define BOARD_LED1    0
#define BOARD_LED2    1
#define BOARD_NLEDS   2

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT  (1 << BOARD_LED1)
#define BOARD_LED2_BIT  (1 << BOARD_LED2)

/* Automatic LED state values (CONFIG_ARCH_LEDS) ****************************
 *
 * LED1 (P03.9  green) and LED2 (P03.10 green) encode system state.
 * LED5 (ESR0   red)   is hardware-driven and requires no software init.
 *
 *  SYMBOL               MEANING                      LED1  LED2
 *  -------------------  ---------------------------  ----  ----
 *  LED_STARTED       0  NuttX has been started        ON   OFF
 *  LED_HEAPALLOCATE  1  Heap has been allocated       ON    ON
 *  LED_IRQSENABLED   2  Interrupts enabled            ON    ON
 *  LED_STACKCREATED  3  Idle stack created            ON    ON
 *  LED_INIRQ         4  In an interrupt               TGGL  NC
 *  LED_SIGNAL        5  In a signal handler            NC  TGGL
 *  LED_ASSERTION     6  An assertion failed           ON    ON
 *  LED_PANIC         7  System crash                  ON    ON
 *  LED_IDLE          8  CPU idle (no change)           NC    NC
 *
 ****************************************************************************/

#define LED_STARTED       0
#define LED_HEAPALLOCATE  1
#define LED_IRQSENABLED   2
#define LED_STACKCREATED  3
#define LED_INIRQ         4
#define LED_SIGNAL        5
#define LED_ASSERTION     6
#define LED_PANIC         7
#define LED_IDLE          8

/* Button: P03.11, low-active (pull against GND) */

#define BOARD_GPIO_BUTTON_PORT  3
#define BOARD_GPIO_BUTTON_PIN   11

#define BUTTON_USER1      0
#define NUM_BUTTONS       1
#define BUTTON_USER1_BIT  (1 << BUTTON_USER1)

#endif /* __BOARDS_TRICORE_TC4XX_A3G_TC4D7_LITE_INCLUDE_BOARD_H */
