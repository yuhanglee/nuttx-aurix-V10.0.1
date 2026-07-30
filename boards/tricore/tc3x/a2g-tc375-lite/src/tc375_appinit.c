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

#ifdef CONFIG_USERLED_LOWER
#  include <debug.h>
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <debug.h>
#  include <nuttx/board.h>
#  include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
#  include <nuttx/mtd/mtd.h>
#  include <nuttx/fs/fs.h>

struct mtd_dev_s *tricore_flash_initialize(void);
#endif

#ifdef CONFIG_TC3X_WDT
int tc3x_wdt_initialize(const char *devpath, int wdtid);
#endif

#ifdef CONFIG_TC3X_QSPI3
int tc375_qspi_initialize(void);
#endif

#ifdef CONFIG_TC3X_I2C
#  include "tc375_i2c.h"
#endif

#ifdef CONFIG_CAN_MCAN
int mcmcan_board_initialize(void);
#endif

#if defined(CONFIG_TC3X_GPT1_PWM) || defined(CONFIG_TC3X_GPT2_PWM)
#  include "tc3x_gpt12_pwm.h"
#endif

#if defined(CONFIG_TC3X_GPT2_CAP) || defined(CONFIG_TC3X_GPT1_CAP)
#  include "tc3x_gpt12_cap.h"
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
/* Lite Kit routes I2C0 (EEPROM 0x50, mikroBUS, Shield2Go) to P13.1 SCL /
 * P13.2 SDA, input B, which TC3X_I2C0_PISEL=1 selects.
 */

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

#ifdef CONFIG_BOARD_USER_LED_GPIO
  /* LED1 on P00.5 is active-low; init HIGH = LED off. */

  aurix_gpio_lower_register(AURIX_GPIO(0, 5, GPIO_OUTPUT, GPIO_ALT0) |
                            GPIO_OUTPUT_HIGH, 0);
#endif

#ifdef CONFIG_TC3X_WDT
  tc3x_wdt_initialize("/dev/watchdog0", 0);
#endif

#ifdef CONFIG_TRICORE_FLASH_MTD
  {
    struct mtd_dev_s *mtd = tricore_flash_initialize();

    if (mtd != NULL)
      {
        register_mtddriver("/dev/dflash0", mtd, 0666, NULL);
      }
  }
#endif

#ifdef CONFIG_TC3X_QSPI3
  tc375_qspi_initialize();
#endif

#ifdef CONFIG_TC3X_I2C0
  tc375_i2c_bringup();
#endif

#ifdef CONFIG_CAN_MCAN
  mcmcan_board_initialize();
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

#ifdef CONFIG_TC3X_GPT1_PWM
  /* GPT1 PWM on T3OUT.
   * TC375 Lite Kit: P10.6 O4 = GPT120_T3OUT (TC37x pin list).
   */

  {
    int ret;

    aurix_config_gpio(AURIX_GPIO(10, 6, GPIO_PERIPH, GPIO_ALT4));
    ret = tc3x_gpt12_pwm_register(0);
    if (ret < 0)
      {
        _err("GPT1 PWM /dev/pwm0 init failed: %d\n", ret);
      }
  }
#endif

#ifdef CONFIG_TC3X_GPT2_PWM
  /* GPT2 PWM on T6OUT.
   * TC375 Lite Kit: P10.5 O5 = GPT120_T6OUT (TC37x pin list).
   */

  {
    int ret;

    aurix_config_gpio(AURIX_GPIO(10, 5, GPIO_PERIPH, GPIO_ALT5));
    ret = tc3x_gpt12_pwm_register(1);
    if (ret < 0)
      {
        _err("GPT2 PWM /dev/pwm1 init failed: %d\n", ret);
      }
  }
#endif

#ifdef CONFIG_TC3X_GPT12_CAP
  /* GPT12 capture on CAPINA.
   * TC37x: P13.2 = GPT120_CAPINA.  Connect signal under test to P13.2.
   * Verify pin accessibility on your board before use.
   */

  {
    int ret;

    aurix_config_gpio(AURIX_GPIO(13, 2, GPIO_INPUT, GPIO_PULL_UP));
    ret = tc3x_gpt12_cap_register(0);
    if (ret < 0)
      {
        _err("GPT12 capture /dev/cap0 init failed: %d\n", ret);
      }
  }
#endif
}
