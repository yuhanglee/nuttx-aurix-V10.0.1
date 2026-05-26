/****************************************************************************
 * boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_appinit.c
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
#include <nuttx/input/buttons.h>
#include <nuttx/irq.h>
#include "tricore_gpio.h"
#include "tc4xx_gpio.h"

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_TC4X_WDT
#include "hardware/tc4x_wdt.h"
#endif

#ifdef CONFIG_TC4XX_I2C
#include "tc4xx_i2c.h"
#endif

#ifdef CONFIG_TC4XX_ASCLIN_SPI
#  include "hardware/tc4xx_asclin_spi.h"
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
#  include <nuttx/mtd/mtd.h>
#  include <nuttx/drivers/drivers.h>
struct mtd_dev_s *tricore_flash_initialize(void);
#endif

#ifdef CONFIG_CAN
int mcmcan_board_initialize(void);
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_aurix_setup_i2c_pin(int bus)
{
  gpio_pinset_t sclpin = AURIX_GPIO(13, 1, GPIO_PERIPH, GPIO_ALT6);
  gpio_pinset_t sdapin = AURIX_GPIO(13, 2, GPIO_PERIPH, GPIO_ALT6);

  aurix_config_gpio(sclpin);
  aurix_config_gpio(sdapin);
}

/* ASCLIN0 = kit USBCOM console on P14.0 TX / P14.1 RX.
 * ASCLIN1 = mikroBUS pins 13/14 on P15.0 TX / P15.1 RX (ALT2/ALTI_A).
 * AMP secondaries skip serial init, so this hook only ever runs on CPU 0.
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

#if 0
  bool ledon = true;
  uint8_t i = 10;
  gpio_pinset_t led1 = AURIX_GPIO(GPIO_PORT3, GPIO_PIN9, GPIO_OUTPUT,
                                  GPIO_ALT0);

  aurix_config_gpio(led1);
  while (i--)
    {
      aurix_gpio_write(led1, (ledon = !ledon));
      usleep(500 * 1000);
    }
#endif

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
  tc4d7_ethernet_initialize();
#endif

#ifdef CONFIG_CAN
  ret = mcmcan_board_initialize();
  if (ret < 0)
    {
      _err("CAN init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4XX_ASCLIN_SPI
  struct spi_dev_s *spi  = tc4xx_asclin_spibus_initialize(0);
  if (!spi)
    {
      _err("SPI init failed\n");
    }

#ifdef CONFIG_TC4XX_ASCLIN_SPI_CHARDEV
  ret = spi_register(spi, 0);
  if (ret < 0)
    {
      _err("SPI register failed: %d\n", ret);
    }
#endif
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
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

#if defined(CONFIG_ARCH_BUTTONS) && defined(CONFIG_INPUT_BUTTONS_LOWER)
  board_button_initialize();
  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      _err("BUTTON register failed: %d\n", ret);
    }
#endif

  return ret;
}
