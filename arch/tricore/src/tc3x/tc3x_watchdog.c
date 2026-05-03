/****************************************************************************
 * arch/tricore/src/tc3x/tc3x_watchdog.c
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

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <arch/arch.h>
#include <nuttx/bits.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/watchdog.h>

#include "tricore_internal.h"
#include "hardware/tc3x_wdt.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TC3X_WDTCPU0_CON1 (TC3X_SCU_BASE + 0x0250)

#define TC3X_CORE_ID_ALIAS_6  6
#define TC3X_CORE_ID_REMAP_6  5

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void wdt_modify(uintptr_t con0, bool endinit)
{
  uint32_t val = getreg32(con0);
  uint32_t pw = ((val >> 2) & 0x3fff) ^ 0x003f;
  uint32_t want = endinit ? BIT(0) : 0;
  int      i;

  /* Password access: LCK=1, ENDINIT=1 */

  if (val & BIT(1))
    {
      putreg32((val & 0xffff0000) | (pw << 2) | BIT(0), con0);
    }

  /* Modify access: LCK=1, ENDINIT=desired */

  putreg32((val & 0xffff0000) | (pw << 2) | BIT(1) | want, con0);

  for (i = 0; i < 10000; i++)
    {
      if ((getreg32(con0) & BIT(0)) == want)
        {
          break;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void aurix_cpu_endinit_enable(bool enable)
{
  uint32_t core_id;

  TRICORE_MFCR(TRICORE_CPU_CORE_ID, core_id);
  core_id &= TRICORE_CPU_CORE_ID_MASK;

  if (core_id == TC3X_CORE_ID_ALIAS_6)
    {
      core_id = TC3X_CORE_ID_REMAP_6;
    }

  wdt_modify(TC3X_WDTCPU0_BASE + (core_id * 12), enable);
}

void aurix_safety_endinit_enable(bool enable)
{
  wdt_modify(TC3X_WDTS_CON0, enable);
}

void tricore_wdt_disable(void)
{
  uint32_t core_id;

  TRICORE_MFCR(TRICORE_CPU_CORE_ID, core_id);
  core_id &= TRICORE_CPU_CORE_ID_MASK;

  if (core_id == TC3X_CORE_ID_ALIAS_6)
    {
      core_id = TC3X_CORE_ID_REMAP_6;
    }

  if (core_id == 0)
    {
      aurix_safety_endinit_enable(false);
      putreg32(WDT_CON1_DR, TC3X_WDTS_CON1);
      aurix_safety_endinit_enable(true);
    }

  aurix_cpu_endinit_enable(false);
  putreg32(WDT_CON1_DR, TC3X_WDTCPU0_CON1 + (core_id * 12));
  aurix_cpu_endinit_enable(true);
}

#ifdef CONFIG_TC3X_WDT

#define TC3X_WDT_FSPB     CONFIG_TC3X_WDT_FSPB

struct tc3x_wdt_lowerhalf_s
{
  const struct watchdog_ops_s *ops;
  uintptr_t base;
  uint32_t  timeout;
  uint16_t  reload;
  uint8_t   ifs;
  bool      started;
};

static int tc3x_wdt_start(struct watchdog_lowerhalf_s *lower);
static int tc3x_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int tc3x_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int tc3x_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                              struct watchdog_status_s *status);
static int tc3x_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                               uint32_t timeout);

static const struct watchdog_ops_s g_tc3x_wdt_ops =
{
  .start      = tc3x_wdt_start,
  .stop       = tc3x_wdt_stop,
  .keepalive  = tc3x_wdt_keepalive,
  .getstatus  = tc3x_wdt_getstatus,
  .settimeout = tc3x_wdt_settimeout,
  .capture    = NULL,
  .ioctl      = NULL,
};

static const uint32_t g_tc3x_wdt_dividers[4] =
{
  WDT_DIVIDER_16384,
  WDT_DIVIDER_256,
  WDT_DIVIDER_64,
  WDT_DIVIDER_16,
};

static void tc3x_wdt_apply_con0(uintptr_t con0_addr, uint16_t reload,
                                bool endinit)
{
  uint32_t val = getreg32(con0_addr);
  uint32_t pw  = ((val >> WDT_CON0_PW_SHIFT) & 0x3FFFu) ^ 0x003Fu;
  int i;

  if (val & WDT_CON0_LCK)
    {
      putreg32((val & WDT_CON0_REL_MASK) |
               (pw << WDT_CON0_PW_SHIFT) |
               WDT_CON0_ENDINIT,
               con0_addr);
    }

  putreg32(((uint32_t)reload << WDT_CON0_REL_SHIFT) |
           (pw << WDT_CON0_PW_SHIFT) |
           WDT_CON0_LCK |
           (endinit ? WDT_CON0_ENDINIT : 0u),
           con0_addr);

  for (i = 0; i < 10000; i++)
    {
      bool got = (getreg32(con0_addr) & WDT_CON0_ENDINIT) != 0;
      if (got == endinit)
        {
          break;
        }
    }
}

static uint32_t tc3x_wdt_con1_value(struct tc3x_wdt_lowerhalf_s *priv,
                                    bool enabled)
{
  uint32_t con1 = 0;

  if (!enabled)
    {
      con1 |= WDT_CON1_DR;
    }
  if (priv->ifs & 1u)
    {
      con1 |= WDT_CON1_IR0;
    }
  if (priv->ifs & 2u)
    {
      con1 |= WDT_CON1_IR1;
    }

  return con1;
}

static void tc3x_wdt_reconfigure(struct tc3x_wdt_lowerhalf_s *priv,
                                 bool enabled)
{
  uintptr_t con0 = priv->base + TC3X_WDTCPU_CON0_OFFSET;
  uintptr_t con1 = priv->base + TC3X_WDTCPU_CON1_OFFSET;

  aurix_cpu_endinit_enable(false);
  putreg32(tc3x_wdt_con1_value(priv, enabled), con1);
  tc3x_wdt_apply_con0(con0, priv->reload, true);
}

static int tc3x_wdt_calc_timeout(uint32_t timeout_ms, uint16_t *reload,
                                 uint8_t *ifs)
{
  static const uint8_t try_ifs[4] =
  {
    WDT_IFS_DIV16,
    WDT_IFS_DIV64,
    WDT_IFS_DIV256,
    WDT_IFS_DIV16384,
  };
  uint64_t ticks;
  int i;

  for (i = 0; i < 4; i++)
    {
      uint32_t div = g_tc3x_wdt_dividers[try_ifs[i]];

      ticks = (uint64_t)timeout_ms * (TC3X_WDT_FSPB / 1000) / div;
      if (ticks > 0 && ticks <= WDT_TIMER_MAX)
        {
          *reload = (uint16_t)(WDT_TIMER_MAX - ticks);
          *ifs    = try_ifs[i];
          return OK;
        }
    }

  return -ERANGE;
}

static uint32_t tc3x_wdt_calc_timeleft(struct tc3x_wdt_lowerhalf_s *priv)
{
  uint32_t sr  = getreg32(priv->base + TC3X_WDTCPU_SR_OFFSET);
  uint32_t tim = (sr & WDT_SR_TIM_MASK) >> WDT_SR_TIM_SHIFT;
  uint32_t div;
  uint64_t remaining;

  if (priv->ifs >= 4u)
    {
      return 0;
    }

  div = g_tc3x_wdt_dividers[priv->ifs];
  remaining = (uint64_t)(WDT_TIMER_MAX - tim) * div * 1000 / TC3X_WDT_FSPB;
  return (uint32_t)remaining;
}

static int tc3x_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct tc3x_wdt_lowerhalf_s *priv = (struct tc3x_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  tc3x_wdt_reconfigure(priv, true);
  priv->started = true;
  leave_critical_section(flags);
  return OK;
}

static int tc3x_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct tc3x_wdt_lowerhalf_s *priv = (struct tc3x_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  tc3x_wdt_reconfigure(priv, false);
  priv->started = false;
  leave_critical_section(flags);
  return OK;
}

static int tc3x_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  irqstate_t flags;

  UNUSED(lower);

  flags = enter_critical_section();
  aurix_cpu_endinit_enable(false);
  aurix_cpu_endinit_enable(true);
  leave_critical_section(flags);
  return OK;
}

static int tc3x_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                              struct watchdog_status_s *status)
{
  struct tc3x_wdt_lowerhalf_s *priv = (struct tc3x_wdt_lowerhalf_s *)lower;
  uint32_t sr;

  DEBUGASSERT(status != NULL);

  sr = getreg32(priv->base + TC3X_WDTCPU_SR_OFFSET);

  status->flags = 0;
  if (!(sr & WDT_SR_DS))
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->flags |= WDFLAGS_RESET;
  status->timeout  = priv->timeout;
  status->timeleft = tc3x_wdt_calc_timeleft(priv);
  return OK;
}

static int tc3x_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                               uint32_t timeout)
{
  struct tc3x_wdt_lowerhalf_s *priv = (struct tc3x_wdt_lowerhalf_s *)lower;
  uint16_t reload;
  uint8_t  ifs;
  irqstate_t flags;
  int ret;

  ret = tc3x_wdt_calc_timeout(timeout, &reload, &ifs);
  if (ret < 0)
    {
      return ret;
    }

  flags = enter_critical_section();

  priv->reload  = reload;
  priv->ifs     = ifs;
  priv->timeout = timeout;

  if (priv->started)
    {
      tc3x_wdt_reconfigure(priv, true);
    }

  leave_critical_section(flags);
  return OK;
}

int tc3x_wdt_initialize(const char *devpath, int wdtid)
{
  struct tc3x_wdt_lowerhalf_s *priv;
  irqstate_t flags;

  DEBUGASSERT(devpath != NULL);

  if (wdtid != 0)
    {
      return -EINVAL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->ops     = &g_tc3x_wdt_ops;
  priv->base    = TC3X_WDTCPU0_BASE + wdtid * TC3X_WDTCPU_STRIDE;
  priv->started = false;
  priv->reload  = 0;
  priv->ifs     = WDT_IFS_DIV16384;
  priv->timeout = (uint32_t)((uint64_t)WDT_TIMER_MAX * WDT_DIVIDER_16384 *
                              1000 / TC3X_WDT_FSPB);

  flags = enter_critical_section();
  tc3x_wdt_reconfigure(priv, false);
  leave_critical_section(flags);

  if (watchdog_register(devpath,
                        (struct watchdog_lowerhalf_s *)priv) == NULL)
    {
      kmm_free(priv);
      return -ENODEV;
    }

  return OK;
}

#endif /* CONFIG_TC3X_WDT */
