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
#include <errno.h>
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

#ifdef CONFIG_TC4XX_ASCLIN_SPI
#  include "hardware/tc4xx_asclin_spi.h"
#endif

#ifdef CONFIG_TC4XX_QSPI
#  include "tc4x_qspi.h"
#endif

#ifdef CONFIG_TC4XX_EGTM_PWM_LOWERHALF
#  include "tc4xx_egtm_pwm.h"
#endif

#ifdef CONFIG_TC4XX_QSPI
int tc4d7_qspi_bringup(void);
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

#if defined(CONFIG_TC4XX_EGTM_PWM_LOWERHALF) && \
    (defined(CONFIG_TC4D7_PWM_ANY) || defined(CONFIG_TC4D7_ATOM_ANY))

struct tc4d7_pwm_row_s
{
  int8_t        minor;
  uint8_t       cluster;
  uint8_t       ch;
  gpio_pinset_t pin;
  uint16_t      tout;
  uint8_t       sel;
};

#endif

#if defined(CONFIG_TC4XX_EGTM_PWM_LOWERHALF) && defined(CONFIG_TC4D7_PWM_ANY)

/* Board PWM map: minor -> (cluster, TOM channel, pad, TOUT, TOUTSEL).
 * Rows are gated by per-channel CONFIG_TC4D7_PWM_CH<N>; the whole
 * block is elided when no channel is enabled (TC4D7_PWM_ANY is auto-
 * selected by any TC4D7_PWM_CHx).
 */

#define TC4D7_PAD(port, padx)                                           \
  (AURIX_GPIO((port), (padx), GPIO_PERIPH, GPIO_ALT9) | GPIO_PERIPH_OWN_PAD)

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
      ret = tc4xx_egtm_pwm_register(g_tc4d7_pwm_map[i].minor, false,
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

#if defined(CONFIG_TC4XX_EGTM_PWM_LOWERHALF) && defined(CONFIG_TC4D7_ATOM_ANY)

#define TC4D7_ATOM_PAD(port, padx) \
  (AURIX_GPIO((port), (padx), GPIO_PERIPH, GPIO_ALT9) | GPIO_PERIPH_OWN_PAD)

static const struct tc4d7_pwm_row_s g_tc4d7_atom_map[] =
{
#ifdef CONFIG_TC4D7_ATOM_CH0
  {13, 0, 2, TC4D7_ATOM_PAD(0, 12),  21u, 0x01u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH1
  {14, 0, 0, TC4D7_ATOM_PAD(0,  0),   9u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH2
  {15, 0, 1, TC4D7_ATOM_PAD(0,  1),  10u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH3
  {16, 0, 2, TC4D7_ATOM_PAD(0,  3),  12u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH4
  {17, 0, 3, TC4D7_ATOM_PAD(0,  4),  13u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH5
  {18, 0, 4, TC4D7_ATOM_PAD(0,  5),  14u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH6
  {19, 0, 5, TC4D7_ATOM_PAD(0,  6),  15u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH7
  {20, 0, 6, TC4D7_ATOM_PAD(0,  7),  16u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH8
  {21, 0, 7, TC4D7_ATOM_PAD(0,  8),  17u, 0x12u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH9
  {22, 1, 1, TC4D7_ATOM_PAD(3,  3), 274u, 0x02u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH10
  {23, 1, 1, TC4D7_ATOM_PAD(3,  4), 275u, 0x02u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH11
  {24, 2, 1, TC4D7_ATOM_PAD(3,  3), 274u, 0x03u},
#endif
#ifdef CONFIG_TC4D7_ATOM_CH12
  {25, 2, 1, TC4D7_ATOM_PAD(3,  4), 275u, 0x03u},
#endif
};

static int tc4d7_atom_bringup(void)
{
  size_t i;
  int ret;

  for (i = 0; i < sizeof(g_tc4d7_atom_map) / sizeof(g_tc4d7_atom_map[0]); i++)
    {
      ret = tc4xx_egtm_pwm_register(g_tc4d7_atom_map[i].minor, true,
                                    g_tc4d7_atom_map[i].cluster,
                                    g_tc4d7_atom_map[i].ch,
                                    g_tc4d7_atom_map[i].pin,
                                    g_tc4d7_atom_map[i].tout,
                                    g_tc4d7_atom_map[i].sel);
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
  ret = spi_register(spi, 0);  /* /dev/spi0 */
  if (ret < 0)
    {
      _err("SPI register failed: %d\n", ret);
    }
#endif
#endif

#ifdef CONFIG_TC4XX_QSPI
  ret = tc4d7_qspi_bringup();
  if (ret < 0)
    {
      _err("ERROR: tc4d7_qspi_bringup failed: %d\n", ret);
    }
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

#ifdef CONFIG_TC4D7_PWM_ANY
  ret = tc4d7_pwm_bringup();
  if (ret < 0)
    {
      _err("PWM bringup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_TC4D7_ATOM_ANY
  ret = tc4d7_atom_bringup();
  if (ret < 0)
    {
      _err("ATOM PWM bringup failed: %d\n", ret);
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
