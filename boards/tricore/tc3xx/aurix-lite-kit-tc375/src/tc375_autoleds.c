/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375_autoleds.c
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

#include "tc375.h"

#ifdef CONFIG_ARCH_LEDS

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED bit masks for internal state tracking */

#define TC375_LED1_BIT  (1 << 0)
#define TC375_LED2_BIT  (1 << 1)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO pinset array indexed by BOARD_LEDn */

static const gpio_pinset_t g_led_map[GPIO_NLEDS] =
{
  GPIO_LED1,  /* BOARD_LED1: P00.5 */
  GPIO_LED2,  /* BOARD_LED2: P00.6 */
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
 *   Drive a single GPIO-controlled LED. LEDs are active LOW:
 *   ledon=true  → drive pin LOW  (LED on)
 *   ledon=false → drive pin HIGH (LED off)
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
 *   Configure LED1 (P00.5) and LED2 (P00.6) as GPIO outputs, initialised
 *   HIGH (LEDs OFF). LED3 (ESR0) is a hardware fault indicator driven by
 *   the SCU and requires no software initialisation.
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
 *   LED_STARTED (0)      LED1 ON  – boot started
 *   LED_HEAPALLOCATE (1) LED2 ON  – heap ready
 *   LED_IRQSENABLED (2)  LED2 ON  – interrupts ready
 *   LED_STACKCREATED (3) LED2 ON  – system running
 *   LED_INIRQ (4)        LED1 toggles – interrupt activity
 *   LED_SIGNAL (5)       LED2 toggles – signal handler
 *   LED_ASSERTION (6)    LED1+LED2 ON – assertion failed
 *   LED_PANIC (7)        LED1+LED2 ON – system crash
 *   LED_IDLE (8)         (no change)
 *
 ****************************************************************************/

void board_autoled_on(int led)
{
  switch (led)
    {
      case LED_STARTED:
        g_leds_on |= TC375_LED1_BIT;
        led_set(BOARD_LED1, true);
        break;

      case LED_HEAPALLOCATE:
      case LED_IRQSENABLED:
      case LED_STACKCREATED:
        g_leds_on |= TC375_LED2_BIT;
        led_set(BOARD_LED2, true);
        break;

      case LED_INIRQ:

        /* Toggle LED1 on interrupt entry */

        if (g_leds_on & TC375_LED1_BIT)
          {
            led_set(BOARD_LED1, false);
            g_leds_on &= ~TC375_LED1_BIT;
          }
        else
          {
            led_set(BOARD_LED1, true);
            g_leds_on |= TC375_LED1_BIT;
          }
        break;

      case LED_SIGNAL:

        /* Toggle LED2 on signal handler entry */

        if (g_leds_on & TC375_LED2_BIT)
          {
            led_set(BOARD_LED2, false);
            g_leds_on &= ~TC375_LED2_BIT;
          }
        else
          {
            led_set(BOARD_LED2, true);
            g_leds_on |= TC375_LED2_BIT;
          }
        break;

      case LED_ASSERTION:
      case LED_PANIC:
        g_leds_on |= TC375_LED1_BIT | TC375_LED2_BIT;
        led_set(BOARD_LED1, true);
        led_set(BOARD_LED2, true);
        break;

      case LED_IDLE:
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

        /* Restore LED1 to its pre-interrupt state */

        if (g_leds_on & TC375_LED1_BIT)
          {
            led_set(BOARD_LED1, true);
          }
        else
          {
            led_set(BOARD_LED1, false);
          }
        break;

      case LED_SIGNAL:

        /* Restore LED2 to its pre-signal state */

        if (g_leds_on & TC375_LED2_BIT)
          {
            led_set(BOARD_LED2, true);
          }
        else
          {
            led_set(BOARD_LED2, false);
          }
        break;

      case LED_ASSERTION:
      case LED_PANIC:
        led_set(BOARD_LED1, false);
        led_set(BOARD_LED2, false);
        g_leds_on &= ~(TC375_LED1_BIT | TC375_LED2_BIT);
        break;

      case LED_IDLE:
        g_leds_on &= ~TC375_LED2_BIT;
        led_set(BOARD_LED2, false);
        break;

      default:
        break;
    }
}

#endif /* CONFIG_ARCH_LEDS */
