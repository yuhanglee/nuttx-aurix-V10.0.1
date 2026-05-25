/****************************************************************************
 * boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_qspi.c
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

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include "tricore_gpio.h"
#include "tc3x_qspi.h"

#ifdef CONFIG_TC3XX_QSPI2

static void tc397_qspi2_pinmux(void)
{
  aurix_config_gpio(AURIX_GPIO(15, 6, GPIO_PERIPH, GPIO_ALT3));   /* MOSI  */
  aurix_config_gpio(AURIX_GPIO(15, 7, GPIO_INPUT,  GPIO_PULL_NONE));
                                                                  /* MISO  */
  aurix_config_gpio(AURIX_GPIO(15, 8, GPIO_PERIPH, GPIO_ALT3));   /* SCLK  */
  aurix_config_gpio(AURIX_GPIO(14, 2, GPIO_PERIPH, GPIO_ALT3));   /* SLSO1 */
}

#endif /* CONFIG_TC3XX_QSPI2 */

#ifdef CONFIG_TC3XX_QSPI3

static void tc397_qspi3_pinmux(void)
{
  aurix_config_gpio(AURIX_GPIO(2, 5, GPIO_INPUT,  GPIO_PULL_NONE));
  aurix_config_gpio(AURIX_GPIO(2, 6, GPIO_PERIPH, GPIO_ALT3));    /* MOSI  */
  aurix_config_gpio(AURIX_GPIO(2, 7, GPIO_PERIPH, GPIO_ALT3));    /* SCLK  */
  aurix_config_gpio(AURIX_GPIO(2, 8, GPIO_INPUT,  GPIO_PULL_UP));
  aurix_config_gpio(AURIX_GPIO(0, 5, GPIO_INPUT,  GPIO_PULL_UP));
  up_mdelay(2);
  aurix_config_gpio(AURIX_GPIO(2, 8, GPIO_PERIPH, GPIO_ALT2));    /* SLSO5 */
  aurix_config_gpio(AURIX_GPIO(0, 5, GPIO_PERIPH, GPIO_ALT3));    /* SLSO3 */
}

#endif /* CONFIG_TC3XX_QSPI3 */

#if defined(CONFIG_TC3XX_QSPI2) || defined(CONFIG_TC3XX_QSPI3)

int tc397_qspi_initialize(void)
{
  static bool s_initialized;
  FAR struct spi_dev_s *dev;

  if (s_initialized)
    {
      return OK;
    }

#ifdef CONFIG_TC3XX_QSPI2
  tc397_qspi2_pinmux();

  dev = tc3xx_qspibus_initialize(2);
  if (dev == NULL)
    {
      _err("QSPI2: bus initialize failed\n");
      return -ENODEV;
    }

#ifdef CONFIG_SPI_DRIVER
  {
    int ret = spi_register(dev, 0);
    if (ret < 0)
      {
        _err("QSPI2: spi_register(0) failed: %d\n", ret);
      }
  }
#endif
#endif /* CONFIG_TC3XX_QSPI2 */

#ifdef CONFIG_TC3XX_QSPI3
  tc397_qspi3_pinmux();

  dev = tc3xx_qspibus_initialize(3);
  if (dev == NULL)
    {
      _err("QSPI3: bus initialize failed\n");
      return -ENODEV;
    }

#ifdef CONFIG_SPI_DRIVER
  {
    int ret = spi_register(dev, 3);
    if (ret < 0)
      {
        _err("QSPI3: spi_register(3) failed: %d\n", ret);
      }

  }
#endif
#endif /* CONFIG_TC3XX_QSPI3 */

  s_initialized = true;
  return OK;
}

#endif /* CONFIG_TC3XX_QSPI2 || CONFIG_TC3XX_QSPI3 */
