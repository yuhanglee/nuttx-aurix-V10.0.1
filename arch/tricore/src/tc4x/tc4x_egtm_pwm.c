/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_egtm_pwm.c
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
#include "hardware/tc4x_egtm.h"
#include "tricore_gpio.h"
#include "tc4x_egtm.h"
#include "tc4x_egtm_pwm.h"

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Hardware upper bound: EGTM_NCLUSTERS x EGTM_TOM_NCHANNELS. Board
 * bringup populates only the (minor -> cluster/channel) slots it exposes.
 */

#define TC4X_PWM_MAX_MINORS (EGTM_NCLUSTERS * EGTM_TOM_NCHANNELS)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tc4x_pwm_dev_s
{
  struct pwm_lowerhalf_s base;   /* must be first for upcast */
  uint8_t  cluster;
  uint8_t  ch;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int tc4x_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int tc4x_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int tc4x_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                           FAR const struct pwm_info_s *info);
static int tc4x_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int tc4x_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                           int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pwm_ops_s g_tc4x_pwm_ops =
{
  .setup    = tc4x_pwm_setup,
  .shutdown = tc4x_pwm_shutdown,
  .start    = tc4x_pwm_start,
  .stop     = tc4x_pwm_stop,
  .ioctl    = tc4x_pwm_ioctl,
};

static struct tc4x_pwm_dev_s g_pwm[TC4X_PWM_MAX_MINORS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4x_pwm_setup
 *
 * Description:
 *   Lower-half open hook. No-op: hardware setup happens at register time.
 *
 ****************************************************************************/

static int tc4x_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  UNUSED(dev);
  return OK;
}

/****************************************************************************
 * Name: tc4x_pwm_shutdown
 *
 * Description:
 *   Lower-half close hook. Stops the TOM channel.
 *
 ****************************************************************************/

static int tc4x_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4x_pwm_dev_s *p = (FAR struct tc4x_pwm_dev_s *)dev;

  tc4x_egtm_pwm_stop(p->cluster, p->ch);
  return OK;
}

/****************************************************************************
 * Name: tc4x_pwm_start
 *
 * Description:
 *   Program the TOM channel for the requested frequency and duty. FXCLK
 *   tap pinned to 0; effective tick = feGTM/2. Returns -ERANGE if the
 *   frequency falls outside the 16-bit period register.
 *
 ****************************************************************************/

static int tc4x_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                           FAR const struct pwm_info_s *info)
{
  FAR struct tc4x_pwm_dev_s *p = (FAR struct tc4x_pwm_dev_s *)dev;
  uint32_t period;
  uint32_t duty;

  if (info->frequency == 0)
    {
      return -EINVAL;
    }

  period = (TC4X_EGTM_TARGET_HZ >> 1) / info->frequency;
  if (period < 2 || period > 0xFFFFu)
    {
      pwmerr("freq %lu Hz out of range\n", (unsigned long)info->frequency);
      return -ERANGE;
    }

  duty = ((uint64_t)period * (uint32_t)info->channels[0].duty) >> 16;

  tc4x_egtm_pwm_setup(p->cluster, p->ch, 0, period, duty, -1, NULL, 0);
  return OK;
}

/****************************************************************************
 * Name: tc4x_pwm_stop
 *
 * Description:
 *   Stop the running TOM channel.
 *
 ****************************************************************************/

static int tc4x_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4x_pwm_dev_s *p = (FAR struct tc4x_pwm_dev_s *)dev;

  tc4x_egtm_pwm_stop(p->cluster, p->ch);
  return OK;
}

/****************************************************************************
 * Name: tc4x_pwm_ioctl
 *
 * Description:
 *   No platform-specific ioctls.
 *
 ****************************************************************************/

static int tc4x_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
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

/****************************************************************************
 * Name: tc4x_egtm_pwm_register
 *
 * Description:
 *   Bind TOM channel <ch> of cluster <cluster> to /dev/pwm<minor>.
 *   Initialises eGTM on first call, configures the pad, routes TOUT,
 *   and registers the upper-half PWM device.
 *
 ****************************************************************************/

int tc4x_egtm_pwm_register(int minor, uint8_t cluster, uint8_t ch,
                            gpio_pinset_t pin, uint16_t tout,
                            uint8_t sel_code)
{
  char devpath[12];

  if (minor < 0 || minor >= TC4X_PWM_MAX_MINORS)
    {
      return -EINVAL;
    }

  if (cluster >= EGTM_NCLUSTERS || ch >= EGTM_TOM_NCHANNELS)
    {
      return -EINVAL;
    }

  tc4x_egtm_init(CONFIG_TC4X_EGTM_PWM_GCLK_DEN);

  g_pwm[minor].base.ops = &g_tc4x_pwm_ops;
  g_pwm[minor].cluster  = cluster;
  g_pwm[minor].ch       = ch;

  aurix_config_gpio(pin);
  tc4x_egtm_route_tout(tout, sel_code);

  snprintf(devpath, sizeof(devpath), "/dev/pwm%d", minor);
  return pwm_register(devpath, &g_pwm[minor].base);
}

#endif /* CONFIG_TC4X_EGTM_PWM_LOWERHALF */
