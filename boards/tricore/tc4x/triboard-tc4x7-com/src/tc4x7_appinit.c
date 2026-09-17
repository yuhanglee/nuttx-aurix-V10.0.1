/****************************************************************************
 * boards/tricore/tc4x/triboard-tc4x7-com/src/tc4x7_appinit.c
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

#include <sys/types.h>
#include <debug.h>

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/irq.h>

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
#endif

#include "tricore_gpio.h"

#ifdef CONFIG_TC4X_WDT
#include "hardware/tc4x_wdt.h"
#endif

#ifdef CONFIG_TC4X_I2C
#include "tc4x7_i2c.h"
#endif

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_TC4X_QSPI
int tc4x7_qspi_bringup(void);
#endif

#ifdef CONFIG_TC4X_XSPI
int tc4x7_xspi_initialize(void);
#endif

#ifdef CONFIG_CAN_MCAN
int mcmcan_board_initialize(void);
#endif

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF
#  include "tc4x_egtm_pwm.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(15, 4, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;
  gpio_pinset_t sdapin = AURIX_GPIO(15, 5, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;

  aurix_config_gpio(sclpin);
  aurix_config_gpio(sdapin);
}

void board_aurix_setup_serial_pin(int idx)
{
  if (idx != 0)
    {
      return;
    }

  aurix_config_gpio(AURIX_GPIO(14, 0, GPIO_PERIPH, GPIO_ALT2));
  aurix_config_gpio(AURIX_GPIO(14, 1, GPIO_INPUT, GPIO_PULL_UP));
}

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF

/* P00.<ch> ALT9 routes to TOUT(9+ch); OWN_PAD hands pad control to the
 * peripheral. TOUTSEL code 20 selects TOM0 channels onto this group.
 */

#  define EGTM_PWM_PIN  (AURIX_GPIO(0, CONFIG_TC4X_EGTM_TOM0_CH,            \
                                    GPIO_PERIPH, GPIO_ALT9)                \
                         | GPIO_PERIPH_OWN_PAD)
#  define EGTM_PWM_TOUT      (9u + CONFIG_TC4X_EGTM_TOM0_CH)
#  define EGTM_PWM_TOUTSEL   20u

static int tc4x7_pwm_bringup(void)
{
  return tc4x_egtm_pwm_register(0, EGTM_PWM_PIN,
                                EGTM_PWM_TOUT, EGTM_PWM_TOUTSEL);
}
#endif

int board_app_initialize(uintptr_t arg)
{
  return OK;
}

void board_late_initialize(void)
{
  int ret;

#ifdef CONFIG_TC4X_WDT
  ret = tc4x_wdt_initialize("/dev/watchdog0", TC4X_WDT_CPU0);
  if (ret < 0)
    {
      _err("WDT init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4X_I2C
  ret = tc4x7_i2c_bringup();
  if (ret < 0)
    {
      _err("I2C init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_CAN_MCAN
  mcmcan_board_initialize();
#endif

#ifdef CONFIG_TC4X_QSPI
  ret = tc4x7_qspi_bringup();
  if (ret < 0)
    {
      _err("ERROR: tc4x7_qspi_bringup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4X_XSPI
  ret = tc4x7_xspi_initialize();
  if (ret < 0)
    {
      _err("ERROR: tc4x7_xspi_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_USERLED_LOWER
  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("ERROR: userled_lower_initialize failed: %d\n", ret);
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

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF
  ret = tc4x7_pwm_bringup();
  if (ret < 0)
    {
      _err("PWM bringup failed: %d\n", ret);
    }
#endif

  UNUSED(ret);
}
