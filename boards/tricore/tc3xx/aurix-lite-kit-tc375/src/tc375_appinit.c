/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375_appinit.c
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
#include <debug.h>

#include <nuttx/board.h>

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
#endif

#include "tricore_gpio.h"

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_CAN
int mcmcan_board_initialize(void);
#endif

#if defined(CONFIG_TC3XX_GPT1_PWM) || defined(CONFIG_TC3XX_GPT2_PWM)
#include "tc3xx_gpt12_pwm.h"
#endif

#ifdef CONFIG_TC3XX_GPT12_CAP
#include "tc3xx_gpt12_cap.h"
#endif

#ifdef CONFIG_TC3XX_GETH
int tc375_ethernet_initialize(void);
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_aurix_setup_serial_pin(int idx)
{
  if (idx != 0)
    {
      return;
    }

  aurix_config_gpio(AURIX_GPIO(14, 0, GPIO_PERIPH, GPIO_ALT2));
  aurix_config_gpio(AURIX_GPIO(14, 1, GPIO_INPUT, GPIO_PULL_UP));
}

int board_app_initialize(uintptr_t arg)
{
  int ret = 0;

#ifdef CONFIG_USERLED_LOWER
  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("ERROR: userled_lower_initialize failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_CAN
  ret = mcmcan_board_initialize();
  if (ret < 0)
    {
      _err("CAN init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GETH
  ret = tc375_ethernet_initialize();
  if (ret < 0)
    {
      _err("eth init failed: %d\n", ret);
    }
#endif

#if defined(CONFIG_ARCH_BUTTONS) && defined(CONFIG_INPUT_BUTTONS_LOWER)
  board_button_initialize();
  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      _err("BUTTON register failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT1_PWM
  /* GPT1 PWM on T3OUT.
   * TC375 Lite Kit: P10.6 O4 = GPT120_T3OUT (TC37x pin list).
   */

  aurix_config_gpio(AURIX_GPIO(10, 6, GPIO_PERIPH, GPIO_ALT4));
  ret = tc3xx_gpt12_pwm_register(0);

  if (ret < 0)
    {
      _err("GPT1 PWM /dev/pwm0 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT2_PWM
  /* GPT2 PWM on T6OUT.
   * TC375 Lite Kit: P10.5 O5 = GPT120_T6OUT (TC37x pin list).
   */

  aurix_config_gpio(AURIX_GPIO(10, 5, GPIO_PERIPH, GPIO_ALT5));
  ret = tc3xx_gpt12_pwm_register(1);

  if (ret < 0)
    {
      _err("GPT2 PWM /dev/pwm1 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT12_CAP
  /* GPT12 capture on CAPINA.
   * TC37x: P13.2 = GPT120_CAPINA.  Connect signal under test to P13.2.
   * Verify pin accessibility on your board before use.
   */

  aurix_config_gpio(AURIX_GPIO(13, 2, GPIO_INPUT, GPIO_PULL_UP));
  ret = tc3xx_gpt12_cap_register(0);
  if (ret < 0)
    {
      _err("GPT12 capture /dev/cap0 init failed: %d\n", ret);
    }
#endif

  return ret;
}
