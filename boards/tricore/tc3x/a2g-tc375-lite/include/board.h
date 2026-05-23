/****************************************************************************
 * boards/tricore/tc3x/a2g-tc375-lite/include/board.h
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

#ifndef __BOARDS_TRICORE_TC3X_A2G_TC375_LITE_INCLUDE_BOARD_H
#define __BOARDS_TRICORE_TC3X_A2G_TC375_LITE_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED definitions **********************************************************/

/* The AURIX Lite Kit V2 TC375 has three LEDs:
 *
 *   LED1 (green): P00.5  - active LOW (pull against GND)
 *   LED2 (green): P00.6  - active LOW (pull against GND)
 *   LED3 (red):   ESR0   - active LOW, hardware fault indicator
 *
 * LED1 and LED2 are controlled via the NuttX GPIO driver.
 * LED3 (ESR0) is driven by the System Control Unit (SCU) hardware and
 * reflects the ESR0 reset/fault signal state; it is not software-
 * controlled via the GPIO driver.
 *
 * If CONFIG_ARCH_LEDS is not defined, the user can control LED1 and LED2
 * freely. The following definitions are used to access individual LEDs.
 */

/* LED index values for use with board_userled() */

#define BOARD_LED1        0  /* P00.5, green */
#define BOARD_LED2        1  /* P00.6, green */
#define BOARD_NLEDS       2  /* Software-controllable LEDs (ESR0 is HW) */

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT    (1 << BOARD_LED1)
#define BOARD_LED2_BIT    (1 << BOARD_LED2)

/* If CONFIG_ARCH_LEDS is defined, NuttX will control LED1 and LED2 on
 * the AURIX Lite Kit V2 TC375 to reflect system state:
 *
 *   SYMBOL              Meaning                  LED state
 *   ------------------  -----------------------  --------------------
 *   LED_STARTED         NuttX has been started   LED1 ON
 *   LED_HEAPALLOCATE    Heap has been allocated  LED2 ON
 *   LED_IRQSENABLED     Interrupts enabled       LED2 ON (stays)
 *   LED_STACKCREATED    Idle stack created       LED2 ON (stays)
 *   LED_INIRQ           In an interrupt          LED1 toggles
 *   LED_SIGNAL          In a signal handler      LED2 toggles
 *   LED_ASSERTION       An assertion failed      LED1+LED2 ON
 *   LED_PANIC           The system has crashed   LED1+LED2 blink
 *   LED_IDLE            TC375 is in sleep mode   LED2 OFF
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

#endif /* __BOARDS_TRICORE_TC3X_A2G_TC375_LITE_INCLUDE_BOARD_H */
