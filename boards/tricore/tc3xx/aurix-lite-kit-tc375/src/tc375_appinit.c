/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375_appinit.c
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
#include <debug.h>

#include <nuttx/board.h>

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
#endif

#include "tricore_gpio.h"

#ifdef CONFIG_USERLED_LOWER
#  include <nuttx/leds/userled.h>
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_CAN
int mcmcan_board_initialize(void);
#endif

#if defined(CONFIG_TC3XX_GPT1_PWM) || defined(CONFIG_TC3XX_GPT2_PWM)
#include "tc3xx_gpt12_pwm.h"
#endif

#if defined(CONFIG_TC3XX_GPT2_CAP) || defined(CONFIG_TC3XX_GPT1_CAP)
#include "tc3xx_gpt12_cap.h"
#endif

#ifdef CONFIG_TC3XX_GETH
int tc375_ethernet_initialize(void);
#endif
#ifdef CONFIG_TC3XX_WDT
int tc3x_wdt_initialize(const char *devpath, int wdtid);
#endif

#ifdef CONFIG_TC3XX_SMU
int tc3x_smu_initialize(void);
#endif

#ifdef CONFIG_AURIX_SMU
FAR struct aurix_smu_lowerhalf_s *tc3x_smu_lowerhalf(void);
int aurix_smu_register(const char *path,
                       FAR struct aurix_smu_lowerhalf_s *lower);
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

int board_app_initialize(uintptr_t arg)
{
  int ret = 0;

#ifdef CONFIG_USERLED_LOWER
  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("ERROR: userled_lower_initialize failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_CAN
  ret = mcmcan_board_initialize();
  if (ret < 0)
    {
      _err("CAN init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GETH
  ret = tc375_ethernet_initialize();
  if (ret < 0)
    {
      _err("eth init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_SMU
  ret = tc3x_smu_initialize();
  if (ret < 0)
    {
      _err("ERROR: tc3x_smu_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_WDT
  ret = tc3x_wdt_initialize("/dev/watchdog0", 0);
  if (ret < 0)
    {
      _err("ERROR: tc3x_wdt_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_AURIX_SMU
  /* Register /dev/smu0 after hardware and WDT are fully initialised.
   * Keeping VFS registration separate from hardware init avoids any
   * heap/mutex work in the critical window right after the SMU IRQ
   * is enabled and WDT alarms are armed.
   */

  ret = aurix_smu_register("/dev/smu0", tc3x_smu_lowerhalf());
  if (ret < 0)
    {
      _err("ERROR: aurix_smu_register failed: %d\n", ret);
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

#ifdef CONFIG_TC3XX_GPT1_PWM
  /* GPT1 PWM on T3OUT.
   * TC375 Lite Kit: P10.6 O4 = GPT120_T3OUT (TC37x pin list).
   */

  aurix_config_gpio(AURIX_GPIO(10, 6, GPIO_PERIPH, GPIO_ALT4));
  ret = tc3xx_gpt12_pwm_register(0);

  if (ret < 0)
    {
      _err("GPT1 PWM /dev/pwm0 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT2_PWM
  /* GPT2 PWM on T6OUT.
   * TC375 Lite Kit: P10.5 O5 = GPT120_T6OUT (TC37x pin list).
   */

  aurix_config_gpio(AURIX_GPIO(10, 5, GPIO_PERIPH, GPIO_ALT5));
  ret = tc3xx_gpt12_pwm_register(1);

  if (ret < 0)
    {
      _err("GPT2 PWM /dev/pwm1 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT2_CAP
  /* GPT2 CAPIN capture (/dev/cap0).
   * TC37x: P13.2 = GPT120_CAPINA.  Connect signal under test to P13.2.
   * Verify pin accessibility on your board before use.
   */

  aurix_config_gpio(AURIX_GPIO(13, 2, GPIO_INPUT, GPIO_PULL_UP));
  ret = tc3xx_gpt12_cap_register(0);
  if (ret < 0)
    {
      _err("GPT2 CAPIN capture /dev/cap0 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT1_CAP_T2
  /* GPT1 T2IN capture.
   * TC375 Lite Kit: P00.7 = GPT120_T2INA (PISEL.IST2IN=0).
   * Connect the signal under test to P00.7.
   */

  aurix_config_gpio(AURIX_GPIO(0, 7, GPIO_INPUT, GPIO_PULL_UP));
  ret = tc3xx_gpt12_cap_register(2);         /* /dev/cap2 */
  if (ret < 0)
    {
      _err("GPT1 T2IN capture /dev/cap2 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT2_CAP_T3IN
  /* GPT2 T3IN capture (/dev/cap3).
   * TC375 Lite Kit: P02.6 = GPT120_T3INA (PISEL.IST3IN=0).
   * T3IN shares T5/CAPREL with CAPIN (/dev/cap0); only one may be active
   * at a time.  start() returns -EBUSY if the other channel is running.
   * Connect the signal under test to P02.6.
   */

  aurix_config_gpio(AURIX_GPIO(2, 6, GPIO_INPUT, GPIO_PULL_UP));
  ret = tc3xx_gpt12_cap_register(3);         /* /dev/cap3 */
  if (ret < 0)
    {
      _err("GPT2 T3IN capture /dev/cap3 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC3XX_GPT1_CAP_T4
  /* GPT1 T4IN capture.
   * TC375 Lite Kit: P02.8 = GPT120_T4INA (PISEL.IST4IN=0).
   * NOTE: P02.6 is T3INA, NOT T4INA.  T4INA = P02.8.
   * Connect the signal under test to P02.8.
   */

  aurix_config_gpio(AURIX_GPIO(2, 8, GPIO_INPUT, GPIO_PULL_UP));
  ret = tc3xx_gpt12_cap_register(4);         /* /dev/cap4 */
  if (ret < 0)
    {
      _err("GPT1 T4IN capture /dev/cap4 init failed: %d\n", ret);
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
