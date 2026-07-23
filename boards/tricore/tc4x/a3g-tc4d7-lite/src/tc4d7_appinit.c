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

#ifdef CONFIG_USERLED_LOWER
#  include <debug.h>
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <debug.h>
#  include <nuttx/board.h>
#  include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_TC4X_QSPI
int tc4d7_qspi_bringup(void);
#endif

#ifdef CONFIG_TC4X_WDT
#  include <debug.h>
#  include "hardware/tc4x_wdt.h"
#endif

#ifdef CONFIG_TC4X_I2C
#  include <debug.h>
#  include "tc4d7_i2c.h"
#endif

#ifdef CONFIG_CAN_MCAN
#  include <debug.h>
int mcmcan_board_initialize(void);
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
#  include <debug.h>
#  include <nuttx/fs/fs.h>
#  include <nuttx/mtd/mtd.h>

struct mtd_dev_s *tricore_flash_initialize(void);
#endif

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF
#  include <debug.h>
#  include "tc4x_egtm_pwm.h"
#endif

#ifdef CONFIG_TC4X_ASCLIN_SPI
#  include <debug.h>
#  include <nuttx/spi/spi_transfer.h>
#  include "hardware/tc4x_asclin_spi.h"
#endif

#ifdef CONFIG_BOARD_GPIO_TEST
#  include <debug.h>

static const gpio_pinset_t g_test_gpio_pins[] =
{
  AURIX_GPIO(22, 0, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH,
  AURIX_GPIO(22, 2, GPIO_INPUT,  GPIO_PULL_UP),
  AURIX_GPIO(22, 3, GPIO_INPUT,  GPIO_PULL_DOWN),
};
#endif

#ifdef CONFIG_BOARD_USER_LED_GPIO
#  include <debug.h>

static const gpio_pinset_t g_user_led_pin =
  AURIX_GPIO(3, 9, GPIO_OUTPUT, GPIO_ALT0) | GPIO_OUTPUT_HIGH;
#endif

#if defined(CONFIG_TC4X_EGTM_PWM_LOWERHALF) && defined(CONFIG_TC4D7_PWM_ANY)

/* Board PWM map: minor -> (cluster, TOM channel, pad, TOUT, TOUTSEL).
 * Rows are gated by per-channel CONFIG_TC4D7_PWM_CH<N>; the whole
 * block is elided when no channel is enabled (TC4D7_PWM_ANY is auto-
 * selected by any TC4D7_PWM_CHx).
 */

#define TC4D7_PAD(port, padx)                                           \
  (AURIX_GPIO((port), (padx), GPIO_PERIPH, GPIO_ALT9) | GPIO_PERIPH_OWN_PAD)

struct tc4d7_pwm_row_s
{
  int8_t        minor;
  uint8_t       cluster;
  uint8_t       ch;
  gpio_pinset_t pin;
  uint16_t      tout;
  uint8_t       sel;
};

static const struct tc4d7_pwm_row_s g_tc4d7_pwm_map[] =
{
#ifdef CONFIG_TC4D7_PWM_CH0
  { 0, 0,  0, TC4D7_PAD(0,  0),   9u, 0x14u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH1
  { 1, 0,  1, TC4D7_PAD(0,  1),  10u, 0x14u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH2
  { 2, 0,  2, TC4D7_PAD(0,  2),  11u, 0x14u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH3
  { 3, 0,  3, TC4D7_PAD(0,  3),  12u, 0x14u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH4
  { 4, 0, 11, TC4D7_PAD(0,  4),  13u, 0x10u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH5
  { 5, 0, 12, TC4D7_PAD(0,  5),  14u, 0x10u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH6
  { 6, 0, 13, TC4D7_PAD(0,  6),  15u, 0x10u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH7
  { 7, 0, 14, TC4D7_PAD(0,  7),  16u, 0x10u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH8
  { 8, 0, 15, TC4D7_PAD(0,  8),  17u, 0x10u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH9
  { 9, 0,  9, TC4D7_PAD(0,  9),  18u, 0x04u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH10
  {10, 0, 10, TC4D7_PAD(0, 11),  20u, 0x04u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH11
  {11, 2,  9, TC4D7_PAD(3,  3), 274u, 0x06u},
#endif
#ifdef CONFIG_TC4D7_PWM_CH12
  {12, 2, 10, TC4D7_PAD(3,  4), 275u, 0x15u},
#endif
};

static int tc4d7_pwm_bringup(void)
{
  size_t i;
  int ret;

  for (i = 0; i < sizeof(g_tc4d7_pwm_map) / sizeof(g_tc4d7_pwm_map[0]); i++)
    {
      ret = tc4x_egtm_pwm_register(g_tc4d7_pwm_map[i].minor,
                                    g_tc4d7_pwm_map[i].cluster,
                                    g_tc4d7_pwm_map[i].ch,
                                    g_tc4d7_pwm_map[i].pin,
                                    g_tc4d7_pwm_map[i].tout,
                                    g_tc4d7_pwm_map[i].sel);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}
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
#ifdef CONFIG_USERLED_LOWER
  {
    int ret = userled_lower_initialize("/dev/userleds");

    if (ret < 0)
      {
        _err("ERROR: userled_lower_initialize failed: %d\n", ret);
      }
  }
#endif

#ifdef CONFIG_TC4X_QSPI
  tc4d7_qspi_bringup();
#endif

#ifdef CONFIG_TC4X_WDT
  {
    int ret = tc4x_wdt_initialize("/dev/watchdog0", TC4X_WDT_CPU0);

    if (ret < 0)
      {
        _err("WDT init failed: %d\n", ret);
      }
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
        int ret = register_mtddriver("/dev/dflash0", mtd, 0666, NULL);

        if (ret < 0)
          {
            _err("DFLASH MTD register failed: %d\n", ret);
          }
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
  {
    int ret = mcmcan_board_initialize();

    if (ret < 0)
      {
        _err("CAN init failed: %d\n", ret);
      }
  }
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

#ifdef CONFIG_TC4D7_PWM_ANY
  {
    int ret = tc4d7_pwm_bringup();

    if (ret < 0)
      {
        _err("PWM bringup failed: %d\n", ret);
      }
  }
#endif

#if defined(CONFIG_ARCH_BUTTONS) && defined(CONFIG_INPUT_BUTTONS_LOWER)
  {
    int ret;

    board_button_initialize();
    ret = btn_lower_initialize("/dev/buttons");
    if (ret < 0)
      {
        _err("BUTTON register failed: %d\n", ret);
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
  {
    int gret = aurix_gpio_lower_register(g_user_led_pin, 0);
    if (gret < 0)
      {
        _err("USER_LED /dev/gpio0 register failed: %d\n", gret);
      }
  }
#endif
}
