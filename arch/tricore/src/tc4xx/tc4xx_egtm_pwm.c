/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_egtm_pwm.c
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
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>
#include <stdio.h>

#include <nuttx/timers/pwm.h>

#include "hardware/tc4x_clock.h"
#include "tricore_gpio.h"
#include "tc4xx_egtm.h"
#include "tc4xx_egtm_pwm.h"

#ifdef CONFIG_TC4XX_EGTM_PWM_LOWERHALF

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tc4xx_pwm_dev_s
{
  struct pwm_lowerhalf_s base;   /* must be first for upcast */
  uint8_t  ch;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int tc4xx_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int tc4xx_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int tc4xx_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                           FAR const struct pwm_info_s *info);
static int tc4xx_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int tc4xx_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                           int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pwm_ops_s g_tc4xx_pwm_ops =
{
  .setup    = tc4xx_pwm_setup,
  .shutdown = tc4xx_pwm_shutdown,
  .start    = tc4xx_pwm_start,
  .stop     = tc4xx_pwm_stop,
  .ioctl    = tc4xx_pwm_ioctl,
};

static struct tc4xx_pwm_dev_s g_pwm0 =
{
  .base.ops  = &g_tc4xx_pwm_ops,
  .ch        = CONFIG_TC4XX_EGTM_TOM0_CH,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int tc4xx_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  UNUSED(dev);
  return OK;
}

static int tc4xx_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4xx_pwm_dev_s *p = (FAR struct tc4xx_pwm_dev_s *)dev;

  tc4xx_egtm_pwm_stop(p->ch);
  return OK;
}

static int tc4xx_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                           FAR const struct pwm_info_s *info)
{
  FAR struct tc4xx_pwm_dev_s *p = (FAR struct tc4xx_pwm_dev_s *)dev;
  uint32_t period;
  uint32_t duty;

  if (info->frequency == 0)
    {
      return -EINVAL;
    }

  /* FXCLK tap pinned to 0 (tick = feGTM/2 = 250 MHz). Taps 1..4 need
   * CMU_FXCLK_CTRL programming and will land with the multi-channel MR.
   */

  period = (TC4X_EGTM_TARGET_HZ >> 1) / info->frequency;
  if (period < 2 || period > 0xFFFFu)
    {
      pwmerr("freq %lu Hz out of range\n",
             (unsigned long)info->frequency);
      return -ERANGE;
    }

  duty = ((uint64_t)period * (uint32_t)info->duty) >> 16;

  tc4xx_egtm_pwm_setup(p->ch, 0, period, duty, -1, NULL, 0);
  return OK;
}

static int tc4xx_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4xx_pwm_dev_s *p = (FAR struct tc4xx_pwm_dev_s *)dev;

  tc4xx_egtm_pwm_stop(p->ch);
  return OK;
}

static int tc4xx_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                           int cmd, unsigned long arg)
{
  UNUSED(dev);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int tc4xx_egtm_pwm_register(int minor, gpio_pinset_t pin,
                            uint8_t tout, uint8_t sel_code)
{
  char devpath[12];

  if (minor != 0)
    {
      return -EINVAL;
    }

  tc4xx_egtm_init(CONFIG_TC4XX_EGTM_PWM_GCLK_DEN);
  aurix_config_gpio(pin);
  tc4xx_egtm_route_tout(tout, sel_code);

  snprintf(devpath, sizeof(devpath), "/dev/pwm%d", minor);
  return pwm_register(devpath, &g_pwm0.base);
}

#endif /* CONFIG_TC4XX_EGTM_PWM_LOWERHALF */
