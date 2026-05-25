/****************************************************************************
 * boards/tricore/tc4x/triboard-tc4x7-com/src/tc4x7_autoleds.c
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

#ifdef CONFIG_ARCH_LEDS

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Internal bitmask for tracking the current on/off state of each LED */

#define TC4X7_LED1_BIT  (1 << 0)
#define TC4X7_LED2_BIT  (1 << 1)
#define TC4X7_LED3_BIT  (1 << 2)
#define TC4X7_LED4_BIT  (1 << 3)
#define TC4X7_LED5_BIT  (1 << 4)
#define TC4X7_LED6_BIT  (1 << 5)
#define TC4X7_LED7_BIT  (1 << 6)
#define TC4X7_LED8_BIT  (1 << 7)

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

/* Current LED on/off state bitmask */

static uint8_t g_leds_on;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: led_set
 *
 * Description:
 *   Drive a single GPIO-controlled LED.  LEDs are active LOW:
 *   ledon=true  -> drive pin LOW  (LED illuminated)
 *   ledon=false -> drive pin HIGH (LED extinguished)
 *
 ****************************************************************************/

static void led_set(int led, bool ledon)
{
  if (led < GPIO_NLEDS)
    {
      aurix_gpio_write(g_led_map[led], !ledon);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_autoled_initialize
 *
 * Description:
 *   Configure all eight LEDs as GPIO outputs initialised HIGH (LEDs OFF).
 *
 ****************************************************************************/

void board_autoled_initialize(void)
{
  int i;

  for (i = 0; i < GPIO_NLEDS; i++)
    {
      aurix_config_gpio(g_led_map[i]);
    }

  g_leds_on = 0;
}

/****************************************************************************
 * Name: board_autoled_on
 *
 * Description:
 *   Turn on LED(s) to reflect the given system state.
 *
 *   Boot progress lights LEDs 1-4 one per step; at LED_STACKCREATED the
 *   remaining four (5-8) are also turned on so all 8 are solid once the
 *   system is fully running.  IRQ, signal, and idle events are no-ops;
 *   LEDs stay solid.  PANIC/ASSERTION blinks all 8 via the panic handler.
 *
 *   LED_STARTED (0)      LED1 ON            - NuttX started
 *   LED_HEAPALLOCATE (1) LED2 ON            - heap ready
 *   LED_IRQSENABLED (2)  LED3 ON            - interrupts enabled
 *   LED_STACKCREATED (3) LED4..LED8 ON      - all 8 solid ON
 *   LED_INIRQ (4)        (no change)        - no-op
 *   LED_SIGNAL (5)       (no change)        - no-op
 *   LED_ASSERTION (6)    All 8 ON           - panic blink (_off clears)
 *   LED_PANIC (7)        All 8 ON           - panic blink (_off clears)
 *   LED_IDLE (8)         (no change)        - no-op
 *
 ****************************************************************************/

void board_autoled_on(int led)
{
  switch (led)
    {
      case LED_STARTED:
        g_leds_on |= TC4X7_LED1_BIT;
        led_set(BOARD_LED1, true);
        break;

      case LED_HEAPALLOCATE:
        g_leds_on |= TC4X7_LED2_BIT;
        led_set(BOARD_LED2, true);
        break;

      case LED_IRQSENABLED:
        g_leds_on |= TC4X7_LED3_BIT;
        led_set(BOARD_LED3, true);
        break;

      case LED_STACKCREATED:
        g_leds_on |= TC4X7_LED4_BIT | TC4X7_LED5_BIT | TC4X7_LED6_BIT |
                     TC4X7_LED7_BIT | TC4X7_LED8_BIT;
        led_set(BOARD_LED4, true);
        led_set(BOARD_LED5, true);
        led_set(BOARD_LED6, true);
        led_set(BOARD_LED7, true);
        led_set(BOARD_LED8, true);
        break;

      case LED_INIRQ:
      case LED_SIGNAL:
      case LED_IDLE:
        break;

      case LED_ASSERTION:
      case LED_PANIC:
        g_leds_on = TC4X7_LED1_BIT | TC4X7_LED2_BIT | TC4X7_LED3_BIT |
                    TC4X7_LED4_BIT | TC4X7_LED5_BIT | TC4X7_LED6_BIT |
                    TC4X7_LED7_BIT | TC4X7_LED8_BIT;
        led_set(BOARD_LED1, true);
        led_set(BOARD_LED2, true);
        led_set(BOARD_LED3, true);
        led_set(BOARD_LED4, true);
        led_set(BOARD_LED5, true);
        led_set(BOARD_LED6, true);
        led_set(BOARD_LED7, true);
        led_set(BOARD_LED8, true);
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: board_autoled_off
 *
 * Description:
 *   Turn off LED(s) when exiting the given system state.
 *
 ****************************************************************************/

void board_autoled_off(int led)
{
  switch (led)
    {
      case LED_INIRQ:
      case LED_SIGNAL:
      case LED_IDLE:
        break;

      case LED_ASSERTION:
      case LED_PANIC:
        led_set(BOARD_LED1, false);
        led_set(BOARD_LED2, false);
        led_set(BOARD_LED3, false);
        led_set(BOARD_LED4, false);
        led_set(BOARD_LED5, false);
        led_set(BOARD_LED6, false);
        led_set(BOARD_LED7, false);
        led_set(BOARD_LED8, false);
        g_leds_on = 0;
        break;

      default:
        break;
    }
}

#endif /* CONFIG_ARCH_LEDS */
