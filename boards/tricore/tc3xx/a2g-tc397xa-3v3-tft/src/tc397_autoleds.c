/****************************************************************************
 * boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_autoleds.c
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

#include "tc397.h"

#ifdef CONFIG_ARCH_LEDS

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LED bit masks for internal state tracking */

#define TC397_LED1_BIT  (1 << 0)
#define TC397_LED2_BIT  (1 << 1)
#define TC397_LED3_BIT  (1 << 2)
#define TC397_LED4_BIT  (1 << 3)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO pinset array indexed by BOARD_LEDn */

static const gpio_pinset_t g_led_map[GPIO_NLEDS] =
{
  GPIO_LED1,  /* BOARD_LED1: D107, P13.0 */
  GPIO_LED2,  /* BOARD_LED2: D108, P13.1 */
  GPIO_LED3,  /* BOARD_LED3: D109, P13.2 */
  GPIO_LED4,  /* BOARD_LED4: D110, P13.3 */
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
 *   ledon=true  drives the pin LOW  (illuminates the LED)
 *   ledon=false drives the pin HIGH (extinguishes the LED)
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
 *   Configure D107-D110 (P13.0-P13.3) as GPIO outputs, initialised HIGH
 *   (LEDs OFF).
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
 *   LED_STARTED (0)      LED1 ON  – NuttX started
 *   LED_HEAPALLOCATE (1) LED2 ON  – heap ready
 *   LED_IRQSENABLED (2)  LED3 ON  – interrupts ready
 *   LED_STACKCREATED (3) LED4 ON  – idle stack created, system running
 *   LED_INIRQ (4)        LED1 toggles – interrupt activity
 *   LED_SIGNAL (5)       LED2 toggles – signal handler
 *   LED_ASSERTION (6)    All LEDs ON  – assertion failed
 *   LED_PANIC (7)        All LEDs ON  – system crash
 *   LED_IDLE (8)         (no change)
 *
 ****************************************************************************/

void board_autoled_on(int led)
{
  switch (led)
    {
      case LED_STARTED:
        g_leds_on |= TC397_LED1_BIT;
        led_set(BOARD_LED1, true);
        break;

      case LED_HEAPALLOCATE:
        g_leds_on |= TC397_LED2_BIT;
        led_set(BOARD_LED2, true);
        break;

      case LED_IRQSENABLED:
        g_leds_on |= TC397_LED3_BIT;
        led_set(BOARD_LED3, true);
        break;

      case LED_STACKCREATED:
        g_leds_on |= TC397_LED4_BIT;
        led_set(BOARD_LED4, true);
        break;

      case LED_INIRQ:

        /* Toggle LED1 on interrupt entry */

        if (g_leds_on & TC397_LED1_BIT)
          {
            led_set(BOARD_LED1, false);
            g_leds_on &= ~TC397_LED1_BIT;
          }
        else
          {
            led_set(BOARD_LED1, true);
            g_leds_on |= TC397_LED1_BIT;
          }
        break;

      case LED_SIGNAL:

        /* Toggle LED2 on signal handler entry */

        if (g_leds_on & TC397_LED2_BIT)
          {
            led_set(BOARD_LED2, false);
            g_leds_on &= ~TC397_LED2_BIT;
          }
        else
          {
            led_set(BOARD_LED2, true);
            g_leds_on |= TC397_LED2_BIT;
          }
        break;

      case LED_ASSERTION:
      case LED_PANIC:
        g_leds_on |= TC397_LED1_BIT | TC397_LED2_BIT |
                     TC397_LED3_BIT | TC397_LED4_BIT;
        led_set(BOARD_LED1, true);
        led_set(BOARD_LED2, true);
        led_set(BOARD_LED3, true);
        led_set(BOARD_LED4, true);
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

        if (g_leds_on & TC397_LED1_BIT)
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

        if (g_leds_on & TC397_LED2_BIT)
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
        led_set(BOARD_LED3, false);
        led_set(BOARD_LED4, false);
        g_leds_on &= ~(TC397_LED1_BIT | TC397_LED2_BIT |
                       TC397_LED3_BIT | TC397_LED4_BIT);
        break;

      case LED_IDLE:
        g_leds_on &= ~TC397_LED4_BIT;
        led_set(BOARD_LED4, false);
        break;

      default:
        break;
    }
}

#endif /* CONFIG_ARCH_LEDS */
