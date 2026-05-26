/****************************************************************************
 * boards/tricore/tc4x/triboard-tc4x7-com/src/tc4x7_buttons.c
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

/* One button: P33.11 on the TC4X7 Triboard */

#define TC4X7_BUTTON_PORT  BOARD_GPIO_BUTTON_PORT
#define TC4X7_BUTTON_PIN   BOARD_GPIO_BUTTON_PIN

/* No GPIO external interrupt path yet: poll every 50 ms via watchdog */

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
 * Name: board_button_initialize
 ****************************************************************************/

uint32_t board_button_initialize(void)
{
  aurix_config_gpio(AURIX_GPIO(TC4X7_BUTTON_PORT, TC4X7_BUTTON_PIN,
                               GPIO_INPUT, GPIO_PULL_UP));
  return NUM_BUTTONS;
}

/****************************************************************************
 * Name: board_buttons
 ****************************************************************************/

uint32_t board_buttons(void)
{
  int raw = aurix_gpio_read(AURIX_GPIO(TC4X7_BUTTON_PORT, TC4X7_BUTTON_PIN,
                                       GPIO_INPUT, GPIO_PULL_UP));

  return raw == false ? BUTTON_USER1_BIT : 0;
}

#ifdef CONFIG_ARCH_IRQBUTTONS

/****************************************************************************
 * Name: board_button_irq
 ****************************************************************************/

int board_button_irq(int id, xcpt_t irqhandler, FAR void *arg)
{
  UNUSED(id);

  if (irqhandler)
    {
      g_btn_handler = irqhandler;
      g_btn_arg     = arg;
      wd_start(&g_btn_wdog, MSEC2TICK(BTN_POLL_MS), btn_polldog, 0);
    }
  else
    {
      wd_cancel(&g_btn_wdog);
      g_btn_handler = NULL;
      g_btn_arg     = NULL;
    }

  return 0;
}
#endif

#endif /* CONFIG_ARCH_BUTTONS */