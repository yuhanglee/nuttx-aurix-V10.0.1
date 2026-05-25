/****************************************************************************
 * boards/tricore/tc4x/a3g-tc4d7-lite/src/tc4d7_userleds.c
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

#include "tc4d7.h"

#ifndef CONFIG_ARCH_LEDS

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO pinset array indexed by BOARD_LEDn */

static const gpio_pinset_t g_led_map[GPIO_NLEDS] =
{
  GPIO_LED1,  /* BOARD_LED1: P03.9  */
  GPIO_LED2,  /* BOARD_LED2: P03.10 */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_userled_initialize
 *
 * Description:
 *   Configure LED1 (P03.9) and LED2 (P03.10) as GPIO outputs, initialised
 *   HIGH (LEDs OFF, active LOW). LED5 (ESR0) is a hardware fault indicator
 *   and requires no software initialisation.
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
 *   led   - LED index (BOARD_LED1 or BOARD_LED2)
 *   ledon - true to turn LED on, false to turn off
 *
 ****************************************************************************/

void board_userled(int led, bool ledon)
{
  if (led < GPIO_NLEDS)
    {
      /* LEDs are active LOW: ledon=true -> drive LOW */

      aurix_gpio_write(g_led_map[led], !ledon);
    }
}

/****************************************************************************
 * Name: board_userled_all
 *
 * Description:
 *   Set all user LEDs from a bitmask.
 *   Bit 0 = LED1 (P03.9), bit 1 = LED2 (P03.10).
 *
 * Input Parameters:
 *   ledset - Bitmask of LEDs to turn on
 *
 ****************************************************************************/

void board_userled_all(uint32_t ledset)
{
  /* LEDs are active LOW: bit set in ledset → drive LOW */

  aurix_gpio_write(g_led_map[BOARD_LED1], !(ledset & BOARD_LED1_BIT));
  aurix_gpio_write(g_led_map[BOARD_LED2], !(ledset & BOARD_LED2_BIT));
}

#endif /* !CONFIG_ARCH_LEDS */
