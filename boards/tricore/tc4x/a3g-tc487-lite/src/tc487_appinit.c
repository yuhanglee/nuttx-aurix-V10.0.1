/****************************************************************************
 * boards/tricore/tc4x/a3g-tc487-lite/src/tc487_appinit.c
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
#include <errno.h>

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/irq.h>
#include "tricore_gpio.h"

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_TC4X_QSPI
#  include "tc4x_qspi.h"
#endif

#ifdef CONFIG_TC4X_QSPI
int tc487_qspi_bringup(void);
#endif

#ifdef CONFIG_TC4X_I2S
int tc487_i2s_bringup(void);
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* ASCLIN1 is the on-board USB virtual COM (FT2232HL) and the NSH console
 * (CONFIG_UART1_SERIAL_CONSOLE): P15.0 TX / P15.1 RX (ALT2/ALTI_A).  Per the
 * KIT_A3G_TC487_LITE user guide the USB-C serial COM is wired to ASCLIN1.
 * ASCLIN0 (P14.0/P14.1) is unused here (those pads carry AUDIO TDM0 RX).
 */

void board_aurix_setup_serial_pin(int idx)
{
  switch (idx)
    {
      case 0:
        aurix_config_gpio(AURIX_GPIO(14, 0, GPIO_PERIPH, GPIO_ALT2));
        aurix_config_gpio(AURIX_GPIO(14, 1, GPIO_INPUT, GPIO_PULL_UP));
        break;

      case 1:
        aurix_config_gpio(AURIX_GPIO(15, 0, GPIO_PERIPH, GPIO_ALT2));
        aurix_config_gpio(AURIX_GPIO(15, 1, GPIO_INPUT, GPIO_PULL_UP));
        break;

      default:
        break;
    }
}

#ifdef CONFIG_BOARD_GPIO_TEST

static const gpio_pinset_t g_test_gpio_pins[] =
{
  AURIX_GPIO(22, 0, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH,
  AURIX_GPIO(22, 2, GPIO_INPUT,  GPIO_PULL_UP),
  AURIX_GPIO(22, 3, GPIO_INPUT,  GPIO_PULL_DOWN),
};
#endif

#ifdef CONFIG_BOARD_USER_LED_GPIO

static const gpio_pinset_t g_user_led_pin =
  AURIX_GPIO(3, 9, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH;
#endif

int board_app_initialize(uintptr_t arg)
{
  int ret = OK;

#ifdef CONFIG_USERLED_LOWER
  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("ERROR: userled_lower_initialize failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_QSPI
  ret = tc487_qspi_bringup();
  if (ret < 0)
    {
      _err("ERROR: tc487_qspi_bringup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4X_I2S
  ret = tc487_i2s_bringup();
  if (ret < 0)
    {
      _err("ERROR: tc487_i2s_bringup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BOARD_GPIO_TEST
  for (size_t i = 0;
       i < sizeof(g_test_gpio_pins) / sizeof(g_test_gpio_pins[0]);
       i++)
    {
      int gret = aurix_gpio_lower_register(g_test_gpio_pins[i], (int)i);
      if (gret < 0)
        {
          _err("GPIO /dev/gpio%zu register failed: %d\n", i, gret);
        }
    }
#endif

#ifdef CONFIG_BOARD_USER_LED_GPIO
  ret = aurix_gpio_lower_register(g_user_led_pin, 0);
  if (ret < 0)
    {
      _err("USER_LED /dev/gpio0 register failed: %d\n", ret);
    }
#endif

  return ret;
}

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  board_app_initialize(0);
}
#endif
