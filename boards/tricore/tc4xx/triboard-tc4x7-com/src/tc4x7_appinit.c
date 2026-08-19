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

#ifdef CONFIG_TC4X_SMU
#include "hardware/tc4x_smu.h"
#endif

#ifdef CONFIG_AURIX_SMU
FAR struct aurix_smu_lowerhalf_s *tc4x_smu_lowerhalf(void);
FAR struct aurix_smu_lowerhalf_s *tc4x_smu1_lowerhalf(void);
int aurix_smu_register(const char *path,
                       FAR struct aurix_smu_lowerhalf_s *lower);
#endif

#ifdef CONFIG_TC4XX_I2C
#include "tc4xx_i2c.h"
#endif

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_TC4XX_QSPI
int tc4x7_qspi_bringup(void);
#endif

#ifdef CONFIG_TC4XX_EGTM_PWM_LOWERHALF
#  include "tc4xx_gpio.h"
#  include "tc4xx_egtm_pwm.h"
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

#ifdef CONFIG_TC4X7_PWM_ANY

/* Board PWM map: minor -> (cluster, TOM channel, pad, TOUT, TOUTSEL).
 * Rows are gated by per-channel CONFIG_TC4X7_PWM_CH<N>; the whole
 * block is elided when no channel is enabled (TC4X7_PWM_ANY is auto-
 * selected by any TC4X7_PWM_CHx).
 */

#define TC4X7_PAD(port, padx)                                           \
  (AURIX_GPIO((port), (padx), GPIO_PERIPH, GPIO_ALT9) | GPIO_PERIPH_OWN_PAD)

struct tc4x7_pwm_row_s
{
  int8_t        minor;
  uint8_t       cluster;
  uint8_t       ch;
  gpio_pinset_t pin;
  uint16_t      tout;
  uint8_t       sel;
};

static const struct tc4x7_pwm_row_s g_tc4x7_pwm_map[] =
{
#ifdef CONFIG_TC4X7_PWM_CH0
  { 0, 0,  0, TC4X7_PAD(0,  0),   9u, 0x14u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH1
  { 1, 0,  1, TC4X7_PAD(0,  1),  10u, 0x14u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH2
  { 2, 0,  2, TC4X7_PAD(0,  2),  11u, 0x14u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH3
  { 3, 0,  3, TC4X7_PAD(0,  3),  12u, 0x14u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH4
  { 4, 0, 11, TC4X7_PAD(0,  4),  13u, 0x10u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH5
  { 5, 0, 12, TC4X7_PAD(0,  5),  14u, 0x10u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH6
  { 6, 0, 13, TC4X7_PAD(0,  6),  15u, 0x10u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH7
  { 7, 0, 14, TC4X7_PAD(0,  7),  16u, 0x10u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH8
  { 8, 0, 15, TC4X7_PAD(0,  8),  17u, 0x10u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH9
  { 9, 0,  9, TC4X7_PAD(0,  9),  18u, 0x04u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH10
  {10, 0, 10, TC4X7_PAD(0, 11),  20u, 0x04u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH11
  {11, 1,  8, TC4X7_PAD(1,  4), 112u, 0x05u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH12
  {12, 1,  8, TC4X7_PAD(1,  5), 113u, 0x05u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH13
  {13, 1,  9, TC4X7_PAD(1,  6), 114u, 0x05u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH14
  {14, 1,  9, TC4X7_PAD(1,  7), 115u, 0x05u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH15
  {15, 2,  8, TC4X7_PAD(3,  0), 271u, 0x15u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH16
  {16, 2,  9, TC4X7_PAD(3,  2), 273u, 0x15u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH17
  {17, 2, 10, TC4X7_PAD(3,  4), 275u, 0x15u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH18
  {18, 2, 13, TC4X7_PAD(3, 11), 282u, 0x06u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH19
  {19, 2, 14, TC4X7_PAD(3, 12), 283u, 0x15u},
#endif
#ifdef CONFIG_TC4X7_PWM_CH20
  {20, 2, 15, TC4X7_PAD(3, 14), 285u, 0x15u},
#endif
};

static int tc4x7_pwm_bringup(void)
{
  size_t i;
  int ret;

  for (i = 0; i < sizeof(g_tc4x7_pwm_map) / sizeof(g_tc4x7_pwm_map[0]); i++)
    {
      ret = tc4xx_egtm_pwm_register(g_tc4x7_pwm_map[i].minor,
                                    g_tc4x7_pwm_map[i].cluster,
                                    g_tc4x7_pwm_map[i].ch,
                                    g_tc4x7_pwm_map[i].pin,
                                    g_tc4x7_pwm_map[i].tout,
                                    g_tc4x7_pwm_map[i].sel);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}
#endif

int board_app_initialize(uintptr_t arg)
{
  bool ledon = true;
  uint8_t i = 10;
  int ret = 0;
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

#ifdef CONFIG_TC4X_SMU
  ret = tc4x_smu_initialize();
  if (ret < 0)
    {
      _err("ERROR: tc4x_smu_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_AURIX_SMU
  ret = aurix_smu_register("/dev/smu0", tc4x_smu_lowerhalf());
  if (ret < 0)
    {
      _err("ERROR: aurix_smu_register failed: %d\n", ret);
    }

  ret = aurix_smu_register("/dev/smu1", tc4x_smu1_lowerhalf());
  if (ret < 0)
    {
      _err("ERROR: aurix_smu_register smu1 failed: %d\n", ret);
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

#ifdef CONFIG_TC4XX_QSPI
  ret = tc4x7_qspi_bringup();
  if (ret < 0)
    {
      _err("ERROR: tc4x7_qspi_bringup failed: %d\n", ret);
    }
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

#if defined(CONFIG_ARCH_BUTTONS) && defined(CONFIG_INPUT_BUTTONS_LOWER)
  board_button_initialize();
  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      _err("BUTTON register failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4X7_PWM_ANY
  ret = tc4x7_pwm_bringup();
  if (ret < 0)
    {
      _err("PWM bringup failed: %d\n", ret);
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
