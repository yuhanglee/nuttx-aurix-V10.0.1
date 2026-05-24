/****************************************************************************
 * boards/tricore/tc3x/a2g-tc375-lite/src/tc375_buttons.c
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

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/clock.h>
#include <nuttx/wdog.h>

#include <arch/board/board.h>
#include "tricore_gpio.h"

#ifdef CONFIG_ARCH_BUTTONS

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* one button: P00.7 (AURIX Lite Kit V2, Table 4) */

#define TC375_BUTTON_PORT   BOARD_GPIO_BUTTON_PORT
#define TC375_BUTTON_PIN    BOARD_GPIO_BUTTON_PIN

/* TC375 has no GPIO-EXINT wiring: poll every 50 ms via watchdog */

#define BTN_POLL_MS  50

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wdog_s g_btn_wdog;
static xcpt_t        g_btn_handler;
static FAR void     *g_btn_arg;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_button_initialize
 *
 * Description:
 *   Initialize the button GPIO as input with pull-up.
 ****************************************************************************/

uint32_t board_button_initialize(void)
{
  aurix_config_gpio(AURIX_GPIO(TC375_BUTTON_PORT, TC375_BUTTON_PIN,
                               GPIO_INPUT, GPIO_PULL_UP));
  return NUM_BUTTONS;
}

/****************************************************************************
 * Name: board_buttons
 *
 * Description:
 *   Return the current state of the button (bit 0 set if pressed).
 ****************************************************************************/

uint32_t board_buttons(void)
{
  /* Button is active low: pressed = 0 on GPIO */

  int raw = aurix_gpio_read(AURIX_GPIO(TC375_BUTTON_PORT, TC375_BUTTON_PIN,
                                       GPIO_INPUT, GPIO_PULL_UP));
  bool pressed = (raw == false);
  return (pressed ? BUTTON_USER1_BIT : 0);
}

/****************************************************************************
 * Name: btn_polldog
 *
 * Description:
 *   Watchdog callback: fire the registered handler then re-arm.
 ****************************************************************************/

static void btn_polldog(wdparm_t arg)
{
  if (g_btn_handler)
    {
      g_btn_handler(0, NULL, g_btn_arg);
      wd_start(&g_btn_wdog, MSEC2TICK(BTN_POLL_MS), btn_polldog, 0);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_button_irq
 ****************************************************************************/

#ifdef CONFIG_ARCH_IRQBUTTONS
int board_button_irq(int id, xcpt_t irqhandler, FAR void *arg)
{
  if (irqhandler)
    {
      UNUSED(id);
      g_btn_handler = irqhandler;
      g_btn_arg     = arg;
      wd_start(&g_btn_wdog, MSEC2TICK(BTN_POLL_MS), btn_polldog, 0);
    }
  else
    {
      UNUSED(id);
      wd_cancel(&g_btn_wdog);
      g_btn_handler = NULL;
      g_btn_arg     = NULL;
    }

  return 0;
}
#endif

#endif /* CONFIG_ARCH_BUTTONS */
