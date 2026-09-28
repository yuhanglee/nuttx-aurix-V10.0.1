/****************************************************************************
 * boards/tricore/tc4x/a3g-tc487-lite/src/tc487_i2s.c
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
#include <errno.h>

#include <nuttx/audio/i2s.h>

#ifdef CONFIG_AUDIO_TLV320AIC26
#  include <nuttx/spi/spi.h>
#  include <nuttx/audio/audio.h>
#  include <nuttx/audio/tlv320aic26.h>
#endif

#include "tricore_gpio.h"
#include "tc4x_i2s.h"
#include "tc4x_qspi.h"

#ifdef CONFIG_TC4X_I2S

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_TC4X_I2S0
/* AUDIO TDM0 TX pads on KIT_A3G_TC487_LITE (P13.0-3 = SDO/FSYNC/SCK/MCK). */

#  define TC487_TDM0_TXSDO    AURIX_GPIO(13, 0, GPIO_PERIPH, GPIO_ALT15)
#  define TC487_TDM0_TXFSYNCO AURIX_GPIO(13, 1, GPIO_PERIPH, GPIO_ALT15)
#  define TC487_TDM0_TXSCKO   AURIX_GPIO(13, 2, GPIO_PERIPH, GPIO_ALT15)
#  define TC487_TDM0_TXSMCKO  AURIX_GPIO(13, 3, GPIO_PERIPH, GPIO_ALT15)
#endif

#if defined(CONFIG_TC4X_I2S0) && defined(CONFIG_AUDIO_TLV320AIC26)
/* TLV320AIC26 codec on TDM0: control via QSPI4 CS3, MCLK = APLL/4 =
 * 24.576 MHz.  QSPI4 pads (port 22 ALT3): SCLK P22.3, MTSR P22.0,
 * MRST P22.1 (input), CS SLSO3 P22.2.
 */

#  define TC487_AIC26_QSPI_BUS   4
#  define TC487_AIC26_SPI_DEVID  SPIDEV_AUDIO_CTRL(3)
#  define TC487_AIC26_MCLK_HZ    24576000

#  define TC487_AIC26_SCLK    AURIX_GPIO(22, 3, GPIO_PERIPH, GPIO_ALT3)
#  define TC487_AIC26_MTSR    AURIX_GPIO(22, 0, GPIO_PERIPH, GPIO_ALT3)
#  define TC487_AIC26_MRST    AURIX_GPIO(22, 1, GPIO_INPUT, GPIO_PULL_NONE)
#  define TC487_AIC26_SLSO3   AURIX_GPIO(22, 2, GPIO_PERIPH, GPIO_ALT3)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_TC4X_I2S0
static void tc487_i2s0_pinmux(void)
{
  /* TX master drives the codec playback clocks (SDO/FSYNC/SCK/MCK). */

  aurix_config_gpio(TC487_TDM0_TXSDO);
  aurix_config_gpio(TC487_TDM0_TXFSYNCO);
  aurix_config_gpio(TC487_TDM0_TXSCKO);
  aurix_config_gpio(TC487_TDM0_TXSMCKO);
}
#endif

#ifdef CONFIG_TC4X_I2S1
static void tc487_i2s1_pinmux(void)
{
  /* TDM1 is not wired on KIT_A3G_TC487_LITE; add its pads here to use it. */
}
#endif

#ifdef CONFIG_TC4X_I2S2
static void tc487_i2s2_pinmux(void)
{
  /* TDM2 is not wired on KIT_A3G_TC487_LITE; add its pads here to use it. */
}
#endif

/****************************************************************************
 * Name: tc487_i2s_register
 *
 * Description:
 *   Bind an initialised TDM lower half to its upper half.  TDM0 carries the
 *   TLV320AIC26 codec (/dev/audio/pcm0); other instances have no upper half
 *   wired on this board.
 *
 ****************************************************************************/

static int tc487_i2s_register(int port, FAR struct i2s_dev_s *i2s)
{
#if defined(CONFIG_TC4X_I2S0) && defined(CONFIG_AUDIO_TLV320AIC26)
  if (port == 0)
    {
      FAR struct spi_dev_s *spi;
      FAR struct audio_lowerhalf_s *codec;
      int ret;

      /* Route the QSPI4 codec-control pads and bring up the QSPI4 bus. */

      aurix_config_gpio(TC487_AIC26_SCLK);
      aurix_config_gpio(TC487_AIC26_MTSR);
      aurix_config_gpio(TC487_AIC26_MRST);
      aurix_config_gpio(TC487_AIC26_SLSO3);

      spi = tc4x_qspibus_initialize(TC487_AIC26_QSPI_BUS);
      if (spi == NULL)
        {
          ierr("ERROR: QSPI%d init failed\n", TC487_AIC26_QSPI_BUS);
          return -ENODEV;
        }

      codec = tlv320aic26_initialize(spi, TC487_AIC26_SPI_DEVID, i2s,
                                     TC487_AIC26_MCLK_HZ);
      if (codec == NULL)
        {
          ierr("ERROR: tlv320aic26_initialize failed\n");
          return -ENODEV;
        }

      ret = audio_register("pcm0", codec);
      if (ret < 0)
        {
          ierr("ERROR: audio_register(pcm0) failed: %d\n", ret);
        }

      return ret;
    }
#endif

  UNUSED(i2s);
  return OK;
}

/****************************************************************************
 * Name: tc487_i2s_initialize
 *
 * Description:
 *   Route one TDM instance's pads, initialise its I2S lower half (which also
 *   enables the AUDIO CLC and Audio PLL on the first call), and register it.
 *
 ****************************************************************************/

static int tc487_i2s_initialize(int port)
{
  FAR struct i2s_dev_s *i2s;

  switch (port)
    {
#ifdef CONFIG_TC4X_I2S0
      case 0:
        tc487_i2s0_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_I2S1
      case 1:
        tc487_i2s1_pinmux();
        break;
#endif
#ifdef CONFIG_TC4X_I2S2
      case 2:
        tc487_i2s2_pinmux();
        break;
#endif
      default:
        ierr("ERROR: I2S/TDM%d not enabled in config\n", port);
        return -ENODEV;
    }

  i2s = tc4x_i2s_initialize(port);
  if (i2s == NULL)
    {
      ierr("ERROR: tc4x_i2s_initialize(%d) failed\n", port);
      return -ENODEV;
    }

  return tc487_i2s_register(port, i2s);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc487_i2s_bringup
 *
 * Description:
 *   Initialise every enabled AUDIO TDM instance.  Called from
 *   board_app_initialize().
 *
 ****************************************************************************/

int tc487_i2s_bringup(void)
{
  static bool g_done;
  int ret = OK;

  /* board_app_initialize() can run more than once; initialise exactly once. */

  if (g_done)
    {
      return OK;
    }

  g_done = true;

#ifdef CONFIG_TC4X_I2S0
  ret = tc487_i2s_initialize(0);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_I2S1
  ret = tc487_i2s_initialize(1);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_TC4X_I2S2
  ret = tc487_i2s_initialize(2);
  if (ret < 0)
    {
      return ret;
    }
#endif

  return ret;
}

#endif /* CONFIG_TC4X_I2S */
