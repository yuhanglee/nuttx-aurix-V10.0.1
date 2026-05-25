/****************************************************************************
 * boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_appinit.c
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
#include <sys/sysinfo.h>

#include <nuttx/config.h>
#include <nuttx/kthread.h>
#include <sched.h>
#include <unistd.h>
#include <nuttx/board.h>
#include <nuttx/irq.h>
#include "tricore_gpio.h"

#ifdef CONFIG_TC4X_WDT
#include "hardware/tc4x_wdt.h"
#endif

#ifdef CONFIG_TC4XX_I2C
#include "tc4xx_i2c.h"
#endif

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(15, 4, GPIO_PERIPH, GPIO_ALT6);
  gpio_pinset_t sdapin = AURIX_GPIO(15, 5, GPIO_PERIPH, GPIO_ALT6);

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

int board_app_initialize(uintptr_t arg)
{
  bool ledon = true;
  uint8_t i = 10;
  int ret;
  gpio_pinset_t led1 = AURIX_GPIO(GPIO_PORT13, GPIO_PIN0,
                                  GPIO_OUTPUT, GPIO_ALT0);

  aurix_config_gpio(led1);
  while (i--)
    {
      aurix_gpio_write(led1, (ledon = !ledon));
      usleep(500 * 1000);
    }

  aurix_gpio_write(led1, false);  /* D406 back ON: solid after startup blink */

#ifdef CONFIG_TC4X_WDT
  ret = tc4x_wdt_initialize("/dev/watchdog0", TC4X_WDT_CPU0);
  if (ret < 0)
    {
      _err("WDT init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4XX_I2C
  ret = tc4xx_i2c_bringup();
  if (ret < 0)
    {
      _err("I2C init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4XX_GETH
  tc4x7_ethernet_initialize();
#endif

#ifdef CONFIG_CAN
  mcmcan_board_initialize();
#endif

#ifdef CONFIG_TC4XX_XSPI
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

  return OK;
}
