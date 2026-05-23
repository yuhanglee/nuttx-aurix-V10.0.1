/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397xa-3v3-tft/src/tc397_appinit.c
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


#include <nuttx/config.h>
#include <debug.h>
#include <errno.h>
#include "tricore_gpio.h"

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_TC3XX_I2C
#include "tc3xx_i2c.h"
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
#  include <nuttx/mtd/mtd.h>
#  include <nuttx/drivers/drivers.h>
struct mtd_dev_s *tricore_flash_initialize(void);
#endif

#ifdef CONFIG_CAN
int mcmcan_board_initialize(void);
#endif

#ifdef CONFIG_TC3XX_QSPI2
int tc397_qspi_initialize(void);
#endif

#ifdef CONFIG_TC3XX_WDT
int tc3x_wdt_initialize(const char *devpath, int wdtid);
#endif


void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(15, 4, GPIO_PERIPH, GPIO_ALT6) | GPIO_OPEN_DRAIN;
  gpio_pinset_t sdapin = AURIX_GPIO(15, 5, GPIO_PERIPH, GPIO_ALT6) | GPIO_OPEN_DRAIN;

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

#ifdef CONFIG_BOARD_GPIO_TEST

static const gpio_pinset_t g_test_gpio_pins[] =
{
  AURIX_GPIO(22, 0, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH,
  AURIX_GPIO(22, 2, GPIO_INPUT,  GPIO_PULL_UP),
  AURIX_GPIO(22, 3, GPIO_INPUT,  GPIO_PULL_DOWN),
};
#endif

int board_app_initialize(uintptr_t arg)
{
  int ret = OK;

#ifdef CONFIG_TC3XX_I2C
  ret = tc3xx_i2c_bringup();
  if (ret < 0)
    {
      _err("I2C init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GETH
  ret = tc397_ethernet_initialize();
  if (ret < 0)
    {
      _err("eth init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_CAN
  ret = mcmcan_board_initialize();
  if (ret < 0)
    {
      _err("CAN init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_QSPI2
  ret = tc397_qspi_initialize();
  if (ret < 0)
    {
      _err("QSPI2/TLF30682 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_WDT
  ret = tc3x_wdt_initialize("/dev/watchdog0", 0);
  if (ret < 0)
    {
      _err("WDT init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
  {
    struct mtd_dev_s *mtd = tricore_flash_initialize();
    if (mtd == NULL)
      {
        _err("DFLASH MTD init failed\n");
      }
    else
      {
        ret = register_mtddriver("/dev/dflash0", mtd, 0666, NULL);
        if (ret < 0)
          {
            _err("DFLASH MTD register failed: %d\n", ret);
          }
      }
  }
#endif

#ifdef CONFIG_BOARD_GPIO_TEST
  for (size_t i = 0; i < sizeof(g_test_gpio_pins) / sizeof(g_test_gpio_pins[0]); i++)
    {
      int gret = aurix_gpio_lower_register(g_test_gpio_pins[i], (int)i);
      if (gret < 0)
        {
          _err("GPIO /dev/gpio%zu register failed: %d\n", i, gret);
        }
    }
#endif

#ifdef CONFIG_USERLED
  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("userled_lower_initialize() failed: %d\n", ret);
    }
#endif

  return ret;
}
