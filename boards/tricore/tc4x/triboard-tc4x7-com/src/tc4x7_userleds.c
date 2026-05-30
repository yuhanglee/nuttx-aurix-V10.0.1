/****************************************************************************
 * boards/tricore/tc4x/triboard-tc4x7-com/src/tc4x7_userleds.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

#include "tc4x7.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO pinset array indexed by BOARD_LEDn.
 *
 * TriBoard TC4X7 COM - 8 blue LEDs, all active LOW.
 * GPIO_OUTPUT_HIGH = initial state HIGH (LED off at boot).
 */

static const gpio_pinset_t g_led_map[GPIO_NLEDS] =
{
  GPIO_LED1,  /* BOARD_LED1: D402, P33.0 */
  GPIO_LED2,  /* BOARD_LED2: D403, P33.1 */
  GPIO_LED3,  /* BOARD_LED3: D404, P33.4 */
  GPIO_LED4,  /* BOARD_LED4: D405, P33.5 */
  GPIO_LED5,  /* BOARD_LED5: D406, P13.0 */
  GPIO_LED6,  /* BOARD_LED6: D407, P13.1 */
  GPIO_LED7,  /* BOARD_LED7: D408, P13.2 */
  GPIO_LED8,  /* BOARD_LED8: D409, P13.3 */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_userled_initialize
 *
 * Description:
 *   Configure all eight user LEDs as GPIO outputs, initialised HIGH
 *   (LEDs OFF).
 *
 * Returned Value:
 *   Number of software-controllable LEDs (BOARD_NLEDS).
 *
 ****************************************************************************/

uint32_t board_userled_initialize(void)
{
  int i;

  for (i = 0; i < GPIO_NLEDS; i++)
    {
      aurix_config_gpio(g_led_map[i]);
    }

  return BOARD_NLEDS;
}

/****************************************************************************
 * Name: board_userled
 *
 * Description:
 *   Set the state of a single user LED.
 *
 * Input Parameters:
 *   led   - LED index (BOARD_LED1 .. BOARD_LED8)
 *   ledon - true to illuminate the LED, false to extinguish it
 *
 ****************************************************************************/

void board_userled(int led, bool ledon)
{
  if (led < GPIO_NLEDS)
    {
      /* LEDs are active LOW: ledon=true -> drive pin LOW */

      aurix_gpio_write(g_led_map[led], !ledon);
    }
}

/****************************************************************************
 * Name: board_userled_all
 *
 * Description:
 *   Set all eight user LEDs from a bitmask.
 *   Bit n corresponds to BOARD_LED(n+1).
 *
 * Input Parameters:
 *   ledset - Bitmask of LEDs to illuminate (bit set -> LED on)
 *
 ****************************************************************************/

void board_userled_all(uint32_t ledset)
{
  /* LEDs are active LOW: bit set in ledset -> drive pin LOW */

  aurix_gpio_write(g_led_map[BOARD_LED1], !(ledset & BOARD_LED1_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED2], !(ledset & BOARD_LED2_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED3], !(ledset & BOARD_LED3_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED4], !(ledset & BOARD_LED4_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED5], !(ledset & BOARD_LED5_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED6], !(ledset & BOARD_LED6_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED7], !(ledset & BOARD_LED7_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED8], !(ledset & BOARD_LED8_BIT));
}
