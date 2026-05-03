/****************************************************************************
 * boards/tricore/tc3x/a2g-tc375-lite/src/tc375_appinit.c
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

#include "tricore_gpio.h"

#ifdef CONFIG_TC3X_I2C
#  include "tc375_i2c.h"
#endif

#ifdef CONFIG_TC3X_WDT
int tc3x_wdt_initialize(const char *devpath, int wdtid);
#endif

#ifdef CONFIG_CAN_MCAN
int mcmcan_board_initialize(void);
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

#ifdef CONFIG_TC3X_I2C
/* SCL/SDA pads inherited from the TC397 board (P15.4/P15.5, open-drain);
 * confirm against the TC375 Lite Kit wiring at board-return.
 */

void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(15, 4, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;
  gpio_pinset_t sdapin = AURIX_GPIO(15, 5, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;

  aurix_config_gpio(sclpin);
  aurix_config_gpio(sdapin);
}
#endif

int board_app_initialize(uintptr_t arg)
{
  return OK;
}

void board_late_initialize(void)
{
#ifdef CONFIG_TC3X_I2C0
  tc375_i2c_bringup();
#endif

#ifdef CONFIG_CAN_MCAN
  mcmcan_board_initialize();
#endif

#ifdef CONFIG_TC3X_WDT
  tc3x_wdt_initialize("/dev/watchdog0", 0);
#endif
}
