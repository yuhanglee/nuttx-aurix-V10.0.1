/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_egtm.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/bits.h>

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "tricore_internal.h"
#include "hardware/tc4x_egtm.h"
#include "tc4x_egtm.h"

extern void aurix_cpu_endinit_enable(bool enable);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct egtm_ch_s
{
  xcpt_t   cb;
  uint32_t remaining;   /* 0 = free-run, else periods until auto-stop */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct egtm_ch_s g_ch[8];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void egtm_clc_enable(void)
{
  int retries = 100000;

  aurix_cpu_endinit_enable(false);
  modreg32(0u, EGTM_CLC_DISR, EGTM_CLC);
  aurix_cpu_endinit_enable(true);
  while ((getreg32(EGTM_CLC) & EGTM_CLC_DISS) && --retries > 0)
    {
    }
}

static void egtm_cmu_init(uint32_t gclk_den)
{
  /* NUM/DEN are PROTE-guarded and writable only while CLK_EN == 0. */

  putreg32(PROT_UNLOCK, EGTM_CFGCMU0_PROTSE);
  putreg32(PROT_UNLOCK, EGTM_CFGCMU0_PROTE);

  putreg32(1u,       EGTM_CMU_GCLK_NUM);
  putreg32(gclk_den, EGTM_CMU_GCLK_DEN);
  putreg32(CMU_CLK_EN_CFGCMU_FXCLK, EGTM_CMU_CLK_EN);

  putreg32(PROT_LOCK_RUN, EGTM_CFGCMU0_PROTE);
  putreg32(PROT_LOCK_RUN, EGTM_CFGCMU0_PROTSE);
}

static void egtm_ch_disable(uint8_t ch)
{
  putreg32(TGC_CH_DISABLE(ch), EGTM_TOM_TGC0_ENDIS_CTRL);
  putreg32(TGC_CH_DISABLE(ch), EGTM_TOM_TGC0_OUTEN_CTRL);
  putreg32(TGC_GLB_CTRL_HOST_TRIG, EGTM_TOM_TGC0_GLB_CTRL);
}

static int egtm_tom_isr(int irq, void *context, void *arg)
{
  uint8_t  ch = (uint8_t)(uintptr_t)arg;
  uint32_t notify;

  (void)getreg32(EGTM_ICM_IRQG_TOM0_CI);

  notify = getreg32(EGTM_TOM_CH_IRQ_NOTIFY(ch));
  putreg32(notify, EGTM_TOM_CH_IRQ_NOTIFY(ch));

  if (g_ch[ch].remaining != 0 && --g_ch[ch].remaining == 0)
    {
      egtm_ch_disable(ch);
    }

  if (g_ch[ch].cb != NULL)
    {
      g_ch[ch].cb(irq, context, arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void tc4x_egtm_init(uint32_t gclk_den)
{
  static bool s_done;

  if (s_done)
    {
      return;
    }

  s_done = true;
  egtm_clc_enable();
  egtm_cmu_init(gclk_den);
}

void tc4x_egtm_pwm_setup(uint8_t ch, uint8_t fxclk_sel,
                          uint32_t period_ticks, uint32_t duty_ticks,
                          int irq, xcpt_t cb, uint32_t stop_after_n)
{
  DEBUGASSERT(ch < 8);

  g_ch[ch].cb        = cb;
  g_ch[ch].remaining = stop_after_n;

  putreg32((uint32_t)fxclk_sel << TOM_CTRL_CLK_SRC_SHIFT,
           EGTM_TOM_CH_CTRL(ch));

  putreg32(period_ticks, EGTM_TOM_CH_SR0(ch));
  putreg32(duty_ticks,   EGTM_TOM_CH_SR1(ch));
  putreg32(0u,           EGTM_TOM_CH_CN0(ch));

  if (irq >= 0)
    {
      putreg32(TOM_IRQ_MODE_PULSE, EGTM_TOM_CH_IRQ_MODE(ch));
      putreg32(TOM_IRQ_EN_CCU0TC,  EGTM_TOM_CH_IRQ_EN(ch));

      irq_attach(irq, egtm_tom_isr, (void *)(uintptr_t)ch);
      up_enable_irq(irq);
    }

  /* FUPD_CTRL must be armed before HOST_TRIG copies SR -> CM. */

  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC0_ENDIS_CTRL);
  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC0_OUTEN_CTRL);
  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC0_FUPD_CTRL);
  putreg32(TGC_GLB_CTRL_HOST_TRIG, EGTM_TOM_TGC0_GLB_CTRL);
}

void tc4x_egtm_pwm_update(uint8_t ch, uint32_t duty_ticks)
{
  DEBUGASSERT(ch < 8);

  putreg32(duty_ticks, EGTM_TOM_CH_SR1(ch));
  putreg32(TGC_GLB_CTRL_HOST_TRIG, EGTM_TOM_TGC0_GLB_CTRL);
}

void tc4x_egtm_pwm_stop(uint8_t ch)
{
  DEBUGASSERT(ch < 8);

  g_ch[ch].remaining = 0;
  egtm_ch_disable(ch);
}

void tc4x_egtm_route_tout(uint8_t tout_n, uint8_t sel_code)
{
  uint32_t x = tout_n % 6u;

  DEBUGASSERT(sel_code <= 0x1Fu);
  modreg32((uint32_t)sel_code << TOUTSEL_SHIFT(x), TOUTSEL_MASK(x),
           EGTM_TOUTSEL(tout_n / 6u));
}
