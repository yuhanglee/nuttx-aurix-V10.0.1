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
#include "hardware/tc4xx_egtm.h"
#include "tricore_gpio.h"
#include "tc4xx_egtm.h"
#include "tc4xx_egtm_pwm.h"

#ifdef CONFIG_TC4XX_EGTM_PWM_LOWERHALF

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Hardware upper bound: all TOM + all ATOM channels across 3 clusters. */

#define TC4XX_PWM_MAX_MINORS \
  (EGTM_NCLUSTERS * (EGTM_TOM_NCHANNELS + EGTM_ATOM_NCHANNELS))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tc4xx_pwm_dev_s
{
  struct pwm_lowerhalf_s base;   /* must be first for upcast */
  uint8_t  cluster;
  uint8_t  ch;
  bool     use_atom;             /* false = TOM, true = ATOM */
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

static struct tc4xx_pwm_dev_s g_pwm[TC4XX_PWM_MAX_MINORS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4xx_pwm_setup
 *
 * Description:
 *   Lower-half open hook. No-op: hardware setup happens at register time.
 *
 ****************************************************************************/

static int tc4xx_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  UNUSED(dev);
  return OK;
}

/****************************************************************************
 * Name: tc4xx_pwm_shutdown
 *
 * Description:
 *   Lower-half close hook. Stops the TOM channel.
 *
 ****************************************************************************/

static int tc4xx_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4xx_pwm_dev_s *p = (FAR struct tc4xx_pwm_dev_s *)dev;

  if (p->use_atom)
    {
      tc4xx_egtm_atom_stop(p->cluster, p->ch);
    }
  else
    {
      tc4xx_egtm_pwm_stop(p->cluster, p->ch);
    }

  return OK;
}

/****************************************************************************
 * Name: tc4xx_pwm_start
 *
 * Description:
 *   Program the TOM channel for the requested frequency and duty. FXCLK
 *   tap pinned to 0; effective tick = feGTM/2. Returns -ERANGE if the
 *   frequency falls outside the 16-bit period register.
 *
 ****************************************************************************/

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

  if (p->use_atom)
    {
      uint64_t fegtm   = TC4X_EGTM_TARGET_HZ >> 1;
      uint64_t freq64  = info->frequency;
      uint64_t denom   = freq64 * 0xFFFFFFu;
      uint32_t cmu_div = (fegtm > denom) ?
                         (uint32_t)((fegtm + denom - 1u) / denom) - 1u : 0u;

      if (cmu_div > 0xFFFFFFu)
        {
          cmu_div = 0xFFFFFFu;
        }

      period = (uint32_t)(fegtm / ((uint64_t)(cmu_div + 1u) * freq64));
      if (period < 2u)
        {
          pwmerr("freq %lu Hz out of range\n", (unsigned long)info->frequency);
          return -ERANGE;
        }

      duty = (uint32_t)(((uint64_t)period * (uint32_t)info->duty) >> 16);
      duty = period - duty;
      tc4xx_egtm_atom_setup(p->cluster, p->ch, 0, cmu_div,
                            period, duty, -1, NULL, 0);
    }
  else
    {
      period = (TC4X_EGTM_TARGET_HZ >> 1) / info->frequency;
      if (period < 2 || period > 0xFFFFu)
        {
          pwmerr("freq %lu Hz out of range\n", (unsigned long)info->frequency);
          return -ERANGE;
        }

      duty = ((uint64_t)period * (uint32_t)info->duty) >> 16;
      tc4xx_egtm_pwm_setup(p->cluster, p->ch, 0, period, duty, -1, NULL, 0);
    }

  return OK;
}

/****************************************************************************
 * Name: tc4xx_pwm_stop
 *
 * Description:
 *   Stop the running TOM channel.
 *
 ****************************************************************************/

static int tc4xx_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc4xx_pwm_dev_s *p = (FAR struct tc4xx_pwm_dev_s *)dev;

  if (p->use_atom)
    {
      tc4xx_egtm_atom_stop(p->cluster, p->ch);
    }
  else
    {
      tc4xx_egtm_pwm_stop(p->cluster, p->ch);
    }

  return OK;
}

/****************************************************************************
 * Name: tc4xx_pwm_ioctl
 *
 * Description:
 *   No platform-specific ioctls.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: tc4xx_egtm_pwm_register
 *
 * Description:
 *   Bind TOM (is_atom=false) or ATOM (is_atom=true) channel <ch> of
 *   cluster <cluster> to /dev/pwm<minor>. Initialises eGTM on first call,
 *   configures the pad, routes TOUT, and registers the upper-half PWM
 *   device. Returns -EBUSY if this minor is already bound.
 *
 ****************************************************************************/

int tc4xx_egtm_pwm_register(int minor, bool is_atom,
                            uint8_t cluster, uint8_t ch,
                            gpio_pinset_t pin, uint16_t tout,
                            uint8_t sel_code)
{
  char devpath[12];
  uint8_t nch = is_atom ? EGTM_ATOM_NCHANNELS : EGTM_TOM_NCHANNELS;

  if (minor < 0 || minor >= TC4XX_PWM_MAX_MINORS)
    {
      return -EINVAL;
    }

  if (cluster >= EGTM_NCLUSTERS || ch >= nch)
    {
      return -EINVAL;
    }

  if (g_pwm[minor].base.ops != NULL)
    {
      return -EBUSY;
    }

  tc4xx_egtm_init(CONFIG_TC4XX_EGTM_PWM_GCLK_DEN);

  g_pwm[minor].base.ops = &g_tc4xx_pwm_ops;
  g_pwm[minor].cluster  = cluster;
  g_pwm[minor].ch       = ch;
  g_pwm[minor].use_atom = is_atom;

  aurix_config_gpio(pin);
  tc4xx_egtm_route_tout(tout, sel_code);

  snprintf(devpath, sizeof(devpath), "/dev/pwm%d", minor);
  return pwm_register(devpath, &g_pwm[minor].base);
}

#endif /* CONFIG_TC4XX_EGTM_PWM_LOWERHALF */
