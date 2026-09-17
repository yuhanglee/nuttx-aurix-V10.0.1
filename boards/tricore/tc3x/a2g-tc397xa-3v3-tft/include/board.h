/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397xa-3v3-tft/include/board.h
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

#ifndef __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_INCLUDE_BOARD_H
#define __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED definitions **********************************************************/

/* The A2G-TC397XA-3V3-TFT board has four user LEDs (blue, active LOW):
 *
 *   D107 (blue): P13.0 - active LOW
 *   D108 (blue): P13.1 - active LOW
 *   D109 (blue): P13.2 - active LOW
 *   D110 (blue): P13.3 - active LOW
 *
 * All four LEDs are controlled via the NuttX GPIO driver.
 *
 * If CONFIG_ARCH_LEDS is not defined, the user can control all four LEDs
 * freely via board_userled() / board_userled_all().
 */

/* LED index values for use with board_userled() */

#define BOARD_LED1        0  /* D107, P13.0, blue */
#define BOARD_LED2        1  /* D108, P13.1, blue */
#define BOARD_LED3        2  /* D109, P13.2, blue */
#define BOARD_LED4        3  /* D110, P13.3, blue */
#define BOARD_NLEDS       4

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT    (1 << BOARD_LED1)
#define BOARD_LED2_BIT    (1 << BOARD_LED2)
#define BOARD_LED3_BIT    (1 << BOARD_LED3)
#define BOARD_LED4_BIT    (1 << BOARD_LED4)

/* If CONFIG_ARCH_LEDS is defined, NuttX will control the LEDs to reflect
 * system state:
 *
 *   SYMBOL              Meaning                  LED state
 *   ------------------  -----------------------  --------------------
 *   LED_STARTED         NuttX has been started   LED1 ON
 *   LED_HEAPALLOCATE    Heap has been allocated  LED2 ON
 *   LED_IRQSENABLED     Interrupts enabled       LED3 ON
 *   LED_STACKCREATED    Idle stack created       LED4 ON
 *   LED_INIRQ           In an interrupt          LED1 toggles
 *   LED_SIGNAL          In a signal handler      LED2 toggles
 *   LED_ASSERTION       An assertion failed      All LEDs ON
 *   LED_PANIC           The system has crashed   All LEDs ON
 *   LED_IDLE            In sleep mode            LED4 OFF
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

#endif /* __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_INCLUDE_BOARD_H */
