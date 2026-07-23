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

/* Per-(cluster, channel) state */

static struct egtm_ch_s g_ch[EGTM_NCLUSTERS][EGTM_TOM_NCHANNELS];

/* ISR context packs cluster (high byte) and channel (low byte) */

#define EGTM_ISR_CTX_PACK(cl, ch)  ((uintptr_t)(((cl) << 8) | (ch)))
#define EGTM_ISR_CTX_CL(x)         ((uint8_t)(((uintptr_t)(x) >> 8) & 0xFFu))
#define EGTM_ISR_CTX_CH(x)         ((uint8_t)((uintptr_t)(x) & 0xFFu))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: egtm_clc_enable
 *
 * Description:
 *   Release the eGTM module from CLC disable and wait for CLC.DISS to clear.
 *
 ****************************************************************************/

static void egtm_clc_enable(void)
{
  int retries = 100000;

  putreg32(PROT_UNLOCK, EGTM_CTRL_PROTE);
  modreg32(0u, EGTM_CLC_DISR, EGTM_CLC);
  putreg32(PROT_LOCK_RUN, EGTM_CTRL_PROTE);
  while ((getreg32(EGTM_CLC) & EGTM_CLC_DISS) && --retries > 0)
    {
    }
}

/****************************************************************************
 * Name: egtm_cmu_init
 *
 * Description:
 *   Program the CMU to output GCLK = feGTM / gclk_den under PROTE.
 *
 ****************************************************************************/

static void egtm_cmu_init(uint32_t gclk_den)
{
  /* NUM/DEN are writable only while CLK_EN == 0. */

  putreg32(PROT_UNLOCK, EGTM_PROTSE);
  putreg32(PROT_UNLOCK, EGTM_CTRL_PROTE);

  putreg32(1u,       EGTM_CMU_GCLK_NUM);
  putreg32(gclk_den, EGTM_CMU_GCLK_DEN);
  putreg32(CMU_CLK_EN_CFGCMU_FXCLK, EGTM_CMU_CLK_EN);

  putreg32(PROT_LOCK_RUN, EGTM_CTRL_PROTE);
  putreg32(PROT_LOCK_RUN, EGTM_PROTSE);
}

/****************************************************************************
 * Name: egtm_ch_disable
 *
 * Description:
 *   Disable TOM channel <ch> in cluster <cl> at the TGC layer.
 *
 ****************************************************************************/

static void egtm_ch_disable(uint8_t cl, uint8_t ch)
{
  uint8_t g = TGC_GROUP(ch);

  putreg32(TGC_CH_DISABLE(ch), EGTM_TOM_TGC_ENDIS_CTRL(cl, g));
  putreg32(TGC_CH_DISABLE(ch), EGTM_TOM_TGC_OUTEN_CTRL(cl, g));
  putreg32(TGC_GLB_CTRL_HOST_TRIG, EGTM_TOM_TGC_GLB_CTRL(cl, g));
}

/****************************************************************************
 * Name: egtm_tom_isr
 *
 * Description:
 *   TOM channel ISR: ack interrupts, run auto-stop, invoke user callback.
 *
 ****************************************************************************/

static int egtm_tom_isr(int irq, void *context, void *arg)
{
  uint8_t  cl = EGTM_ISR_CTX_CL(arg);
  uint8_t  ch = EGTM_ISR_CTX_CH(arg);
  uint32_t notify;

  (void)getreg32(EGTM_ICM_IRQG_TOM_CI(cl, TGC_GROUP(ch)));

  notify = getreg32(EGTM_TOM_CH_IRQ_NOTIFY(cl, ch));
  putreg32(notify, EGTM_TOM_CH_IRQ_NOTIFY(cl, ch));

  if (g_ch[cl][ch].remaining != 0 && --g_ch[cl][ch].remaining == 0)
    {
      egtm_ch_disable(cl, ch);
    }

  if (g_ch[cl][ch].cb != NULL)
    {
      g_ch[cl][ch].cb(irq, context, arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4x_egtm_init
 *
 * Description:
 *   Enable eGTM (CLC) and program the CMU. Idempotent: first call only.
 *
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

/****************************************************************************
 * Name: tc4x_egtm_pwm_setup
 *
 * Description:
 *   Configure and start TOM channel <ch> of cluster <cl> as a PWM output.
 *
 ****************************************************************************/

void tc4x_egtm_pwm_setup(uint8_t cl, uint8_t ch, uint8_t fxclk_sel,
                          uint32_t period_ticks, uint32_t duty_ticks,
                          int irq, xcpt_t cb, uint32_t stop_after_n)
{
  uint8_t g = TGC_GROUP(ch);

  DEBUGASSERT(cl < EGTM_NCLUSTERS);
  DEBUGASSERT(ch < EGTM_TOM_NCHANNELS);

  g_ch[cl][ch].cb        = cb;
  g_ch[cl][ch].remaining = stop_after_n;

  putreg32((uint32_t)fxclk_sel << TOM_CTRL_CLK_SRC_SHIFT,
           EGTM_TOM_CH_CTRL(cl, ch));

  putreg32(period_ticks, EGTM_TOM_CH_SR0(cl, ch));
  putreg32(duty_ticks,   EGTM_TOM_CH_SR1(cl, ch));
  putreg32(0u,           EGTM_TOM_CH_CN0(cl, ch));

  if (irq >= 0)
    {
      putreg32(TOM_IRQ_MODE_PULSE, EGTM_TOM_CH_IRQ_MODE(cl, ch));
      putreg32(TOM_IRQ_EN_CCU0TC,  EGTM_TOM_CH_IRQ_EN(cl, ch));

      irq_attach(irq, egtm_tom_isr, (void *)EGTM_ISR_CTX_PACK(cl, ch));
      up_enable_irq(irq);
    }

  /* FUPD_CTRL must be armed before HOST_TRIG copies SR -> CM. */

  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC_ENDIS_CTRL(cl, g));
  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC_OUTEN_CTRL(cl, g));
  putreg32(TGC_CH_ENABLE(ch), EGTM_TOM_TGC_FUPD_CTRL(cl, g));
  putreg32(TGC_GLB_CTRL_HOST_TRIG, EGTM_TOM_TGC_GLB_CTRL(cl, g));
}

/****************************************************************************
 * Name: tc4x_egtm_pwm_update
 *
 * Description:
 *   Update duty (SR1) on a running TOM channel and commit via HOST_TRIG.
 *
 ****************************************************************************/

void tc4x_egtm_pwm_update(uint8_t cl, uint8_t ch, uint32_t duty_ticks)
{
  DEBUGASSERT(cl < EGTM_NCLUSTERS);
  DEBUGASSERT(ch < EGTM_TOM_NCHANNELS);

  putreg32(duty_ticks, EGTM_TOM_CH_SR1(cl, ch));
  putreg32(TGC_GLB_CTRL_HOST_TRIG,
           EGTM_TOM_TGC_GLB_CTRL(cl, TGC_GROUP(ch)));
}

/****************************************************************************
 * Name: tc4x_egtm_pwm_stop
 *
 * Description:
 *   Stop a running TOM channel and disable its output.
 *
 ****************************************************************************/

void tc4x_egtm_pwm_stop(uint8_t cl, uint8_t ch)
{
  DEBUGASSERT(cl < EGTM_NCLUSTERS);
  DEBUGASSERT(ch < EGTM_TOM_NCHANNELS);

  g_ch[cl][ch].remaining = 0;
  egtm_ch_disable(cl, ch);
}

/****************************************************************************
 * Name: tc4x_egtm_route_tout
 *
 * Description:
 *   Route pad TOUT<tout_n> to the source selected by <sel_code>.
 *
 ****************************************************************************/

void tc4x_egtm_route_tout(uint16_t tout_n, uint8_t sel_code)
{
  uint32_t x = tout_n % 6u;

  DEBUGASSERT(sel_code <= 0x1Fu);
  modreg32((uint32_t)sel_code << TOUTSEL_SHIFT(x), TOUTSEL_MASK(x),
           EGTM_TOUTSEL(tout_n / 6u));
}
