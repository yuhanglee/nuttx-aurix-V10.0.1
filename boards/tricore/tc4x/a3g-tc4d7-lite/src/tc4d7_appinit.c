/****************************************************************************
 * boards/tricore/tc4x/a3g-tc4d7-lite/src/tc4d7_appinit.c
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
#include <sys/types.h>

#include "tricore_gpio.h"

#ifdef CONFIG_TC4X_WDT
#  include <debug.h>
#  include "hardware/tc4x_wdt.h"
#endif

#ifdef CONFIG_TC4X_I2C
#  include <debug.h>
#  include "tc4d7_i2c.h"
#endif

#ifdef CONFIG_CAN_MCAN
int mcmcan_board_initialize(void);
#endif

#ifdef CONFIG_TC4X_ASCLIN_SPI
#  include <debug.h>
#  include <nuttx/spi/spi_transfer.h>
#  include "hardware/tc4x_asclin_spi.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_TC4X_I2C
void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(13, 1, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;
  gpio_pinset_t sdapin = AURIX_GPIO(13, 2, GPIO_PERIPH, GPIO_ALT6) |
                         GPIO_OPEN_DRAIN;

  aurix_config_gpio(sclpin);
  aurix_config_gpio(sdapin);
}
#endif

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

int board_app_initialize(uintptr_t arg)
{
  return OK;
}

void board_late_initialize(void)
{
#ifdef CONFIG_TC4X_WDT
  {
    int ret = tc4x_wdt_initialize("/dev/watchdog0", TC4X_WDT_CPU0);

    if (ret < 0)
      {
        _err("WDT init failed: %d\n", ret);
      }
  }
#endif

#ifdef CONFIG_TC4X_I2C
  {
    int ret = tc4d7_i2c_bringup();

    if (ret < 0)
      {
        _err("I2C init failed: %d\n", ret);
      }
  }
#endif

#ifdef CONFIG_CAN_MCAN
  mcmcan_board_initialize();
#endif

#ifdef CONFIG_TC4X_ASCLIN_SPI
  {
    struct spi_dev_s *spi = tc4x_asclin_spibus_initialize(0);

    if (!spi)
      {
        _err("SPI init failed\n");
      }
#ifdef CONFIG_TC4X_ASCLIN_SPI_CHARDEV
    int ret = spi_register(spi, 0);

    if (ret < 0)
      {
        _err("SPI register failed: %d\n", ret);
      }
#endif
  }
#endif
}
