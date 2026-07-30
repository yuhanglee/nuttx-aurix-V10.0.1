/****************************************************************************
 * arch/tricore/src/tc3x/tc3x_gpt12_cap.c
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
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/bits.h>
#include <nuttx/timers/capture.h>

#include "tricore_internal.h"
#include "tricore_gpio.h"
#include "hardware/tc3x_clock.h"
#include "hardware/tc3x_gpt12.h"
#include "tc3x_gpt12_cap.h"

extern void aurix_cpu_endinit_enable(bool enable);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Peripheral bus clock driving GPT12 */

#define GPT12_CAP_FSPB_HZ        TC3X_FSPB_TARGET_HZ

/* BPS2 prescaler: div2 gives a 50 MHz GPT2 base clock */

#define GPT12_CAP_BPS2_DIV       2u

/* GPT2 base clock fed to T5 before the per-timer prescaler T5I */

#define GPT12_CAP_FGPT_HZ        (GPT12_CAP_FSPB_HZ / GPT12_CAP_BPS2_DIV)

/* Maximum poll count waiting for the CLC.DISS bit to clear */

#define GPT12_CAP_CLC_TIMEOUT    100000u

#define GPT12_CAP_DEVPATH_LEN    16

/* Capture state machine states */

#define CAP_STATE_IDLE   0u  /* Not started */
#define CAP_STATE_RISE1  1u  /* Waiting for first rising edge (t1) */
#define CAP_STATE_FALL   2u  /* Waiting for falling edge (t2) */
#define CAP_STATE_RISE2  3u  /* Waiting for second rising edge (t3) */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Lower-half private state for one GPT12 capture channel */

struct tc3x_gpt12_cap_s
{
  struct cap_lowerhalf_s  base;
  volatile uint32_t       freq_hz;      /* Measured frequency in Hz */
  volatile uint8_t        duty_pct;     /* Duty cycle  */
  volatile uint32_t       edges;        /* Complete period measurements */

  /* Three-point capture state machine */

  volatile uint8_t        cap_state;    /* CAP_STATE_* */
  volatile uint16_t       t1;           /* First rising edge timestamp */
  volatile uint16_t       t2;           /* Falling edge timestamp */

  uint32_t                f_t5_hz;      /* T5 clock in Hz  */
  bool                    running;      /* True between start and stop */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gpt12_cap_start(FAR struct cap_lowerhalf_s *lower);
static int gpt12_cap_stop(FAR struct cap_lowerhalf_s *lower);
static int gpt12_cap_getduty(FAR struct cap_lowerhalf_s *lower,
                              FAR uint8_t *duty);
static int gpt12_cap_getfreq(FAR struct cap_lowerhalf_s *lower,
                              FAR uint32_t *freq);
static int gpt12_cap_getedges(FAR struct cap_lowerhalf_s *lower,
                               FAR uint32_t *edges);
static int gpt12_cap_ioctl(FAR struct cap_lowerhalf_s *lower,
                            int cmd, unsigned long arg);
static int gpt12_cap_isr(int irq, FAR void *context,
                          FAR void *arg);
static int gpt12_cap_clk_enable(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Operations table shared by all capture channels */

static const struct cap_ops_s g_gpt12_cap_ops =
{
  .start    = gpt12_cap_start,
  .stop     = gpt12_cap_stop,
  .getduty  = gpt12_cap_getduty,
  .getfreq  = gpt12_cap_getfreq,
  .getedges = gpt12_cap_getedges,
  .ioctl    = gpt12_cap_ioctl,
};

/* Singleton lower-half instance - GPT12 provides one capture path */

static struct tc3x_gpt12_cap_s g_gpt12_cap0 =
{
  .base.ops = &g_gpt12_cap_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpt12_cap_clk_enable
 *
 *   Enable the GPT120 module clock by writing CLC.DISR=0 under ENDINIT
 *   protection, then poll CLC.DISS until the module is active.
 *
 ****************************************************************************/

static int gpt12_cap_clk_enable(void)
{
  uint32_t i;

  aurix_cpu_endinit_enable(false);
  modreg32(0, GPT12_CLC_DISR, GPT12_REG(GPT12_CLC_OFFSET));
  aurix_cpu_endinit_enable(true);

  for (i = 0u; i < GPT12_CAP_CLC_TIMEOUT; i++)
    {
      if ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) &
           GPT12_CLC_DISS) == 0u)
        {
          return OK;
        }
    }

  cperr("ERROR: GPT120 clock enable timeout\n");
  return -EIO;
}

/****************************************************************************
 * Name: gpt12_cap_isr
 *
 *   CAPREL interrupt handler.  Fires on each captured CAPIN edge.
 *   Reads the T5 count from CAPREL then clears CAPREL.
 *
 ****************************************************************************/

static int gpt12_cap_isr(int irq, FAR void *context,
                          FAR void *arg)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)arg;
  uint16_t caprel;
  uint16_t t3;
  uint16_t period;
  uint16_t pulse;

  UNUSED(irq);
  UNUSED(context);

  /* Read T5 timestamp from CAPREL, then clear CAPREL */

  caprel = (uint16_t)(getreg32(GPT12_REG(GPT12_CAPREL_OFFSET)) &
                      GPT12_CAPREL_MASK);
  putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));

  switch (priv->cap_state)
    {
      case CAP_STATE_RISE1:

        /* First rising edge: record t1, switch to falling edge */

        priv->t1        = caprel;
        modreg32(GPT12_T5CON_CI_FALLING, GPT12_T5CON_CI_MASK,
                 GPT12_REG(GPT12_T5CON_OFFSET));
        priv->cap_state = CAP_STATE_FALL;
        break;

      case CAP_STATE_FALL:

        /* Falling edge: record t2, switch to rising edge */

        priv->t2        = caprel;
        modreg32(GPT12_T5CON_CI_RISING, GPT12_T5CON_CI_MASK,
                 GPT12_REG(GPT12_T5CON_OFFSET));
        priv->cap_state = CAP_STATE_RISE2;
        break;

      case CAP_STATE_RISE2:

        /* Second rising edge: t3 = caprel.
         */

        t3     = caprel;
        period = t3 - priv->t1;
        pulse  = priv->t2 - priv->t1;

        if (period > 0u)
          {
            priv->freq_hz  = priv->f_t5_hz / (uint32_t)period;
            priv->duty_pct =
              (uint8_t)(((uint32_t)pulse * 100u) / (uint32_t)period);
            priv->edges++;
          }

        priv->t1        = t3;
        modreg32(GPT12_T5CON_CI_FALLING, GPT12_T5CON_CI_MASK,
                 GPT12_REG(GPT12_T5CON_OFFSET));
        priv->cap_state = CAP_STATE_FALL;
        break;

      default:
        break;
    }

  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_start
 *
 *   Configure and start T5 capture.  Called on first open() of the device.
 *   Returns -EBUSY if GPT2 T6 PWM is already running (CAPREL conflict).
 *
 ****************************************************************************/

static int gpt12_cap_start(FAR struct cap_lowerhalf_s *lower)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;
  int ret;

  if (priv->running)
    {
      return OK;
    }

  /* CAPREL is shared with GPT2 T6 reload.  Refuse to start if T6 is
   * already counting.
   */

  if (getreg32(GPT12_REG(GPT12_T6CON_OFFSET)) & GPT12_T6CON_T6R)
    {
      cperr("ERROR: T6 is running; T5 capture and GPT2 PWM "
            "cannot be active simultaneously\n");
      return -EBUSY;
    }

  /* Initialise state machine and result fields */

  priv->cap_state = CAP_STATE_RISE1;
  priv->t1        = 0u;
  priv->t2        = 0u;
  priv->freq_hz   = 0u;
  priv->duty_pct  = 0u;
  priv->edges     = 0u;

  /* Reset capture direction and zero counters for a fresh measurement */

  modreg32(GPT12_T5CON_CI_RISING, GPT12_T5CON_CI_MASK,
           GPT12_REG(GPT12_T5CON_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_T5_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));

  /* Attach and enable the CAPREL interrupt */

  ret = irq_attach(CONFIG_TC3X_GPT12_CAP_CAPREL_SRN,
                   gpt12_cap_isr, priv);
  if (ret < 0)
    {
      cperr("ERROR: irq_attach failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(CONFIG_TC3X_GPT12_CAP_CAPREL_SRN);

  /* Start T5 */

  modreg32(GPT12_T5CON_T5R, GPT12_T5CON_T5R,
           GPT12_REG(GPT12_T5CON_OFFSET));

  priv->running = true;
  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_stop
 *
 *   Halt T5 and disable the CAPREL interrupt.
 *
 ****************************************************************************/

static int gpt12_cap_stop(FAR struct cap_lowerhalf_s *lower)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  if (!priv->running)
    {
      return OK;
    }

  /* Halt T5 */

  modreg32(0, GPT12_T5CON_T5R, GPT12_REG(GPT12_T5CON_OFFSET));

  /* Disable and detach the CAPREL interrupt */

  up_disable_irq(CONFIG_TC3X_GPT12_CAP_CAPREL_SRN);
  irq_detach(CONFIG_TC3X_GPT12_CAP_CAPREL_SRN);

  priv->cap_state = CAP_STATE_IDLE;
  priv->running   = false;
  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_getduty
 *
 *   Return the last computed duty cycle percentage.
 *
 ****************************************************************************/

static int gpt12_cap_getduty(FAR struct cap_lowerhalf_s *lower,
                              FAR uint8_t *duty)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  DEBUGASSERT(duty != NULL);

  *duty = priv->duty_pct;
  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_getfreq
 *
 *   Return the last computed signal frequency in Hz.
 *
 ****************************************************************************/

static int gpt12_cap_getfreq(FAR struct cap_lowerhalf_s *lower,
                              FAR uint32_t *freq)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  DEBUGASSERT(freq != NULL);

  *freq = priv->freq_hz;
  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_getedges
 *
 *   Return the number of complete period measurements completed since
 *   the last start().
 *
 ****************************************************************************/

static int gpt12_cap_getedges(FAR struct cap_lowerhalf_s *lower,
                               FAR uint32_t *edges)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  DEBUGASSERT(edges != NULL);

  *edges = priv->edges;
  return OK;
}

/****************************************************************************
 * Name: gpt12_cap_ioctl
 *
 *   No device-specific ioctls supported.
 *
 ****************************************************************************/

static int gpt12_cap_ioctl(FAR struct cap_lowerhalf_s *lower,
                            int cmd, unsigned long arg)
{
  UNUSED(lower);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc3x_gpt12_cap_register
 *
 *   Enable the GPT120 clock, configure T5CON and BPS2, then register
 *   /dev/capN with the NuttX capture upper-half driver.  The board init
 *   layer must configure the CAPIN GPIO pin before calling this function.
 *
 ****************************************************************************/

int tc3x_gpt12_cap_register(int minor)
{
  FAR struct tc3x_gpt12_cap_s *priv = &g_gpt12_cap0;
  char devpath[GPT12_CAP_DEVPATH_LEN];
  int  ret;

  ret = gpt12_cap_clk_enable();
  if (ret < 0)
    {
      return ret;
    }

  /* Select CAPIN input source via PISEL.ISCAPIN */

#if CONFIG_TC3X_GPT12_CAP_CAPIN_SEL == 0
  modreg32(GPT12_PISEL_ISCAPIN_A, GPT12_PISEL_ISCAPIN_MASK,
           GPT12_REG(GPT12_PISEL_OFFSET));
#else
  modreg32(GPT12_PISEL_ISCAPIN_B, GPT12_PISEL_ISCAPIN_MASK,
           GPT12_REG(GPT12_PISEL_OFFSET));
#endif

  /* Set BPS2 in T6CON: T5 uses GPT2 base clock; select div-by-2 = 50 MHz */

  modreg32(GPT12_T6CON_BPS2_DIV2, GPT12_T6CON_BPS2_MASK,
           GPT12_REG(GPT12_T6CON_OFFSET));

  /* Store T5 tick frequency for ISR use */

  priv->f_t5_hz = GPT12_CAP_FGPT_HZ >> CONFIG_TC3X_GPT12_CAP_T5I;

  /* Configure T5CON: timer mode, prescaler, capture to CAPREL */

  putreg32(GPT12_T5CON_T5M_TIMER
           | ((CONFIG_TC3X_GPT12_CAP_T5I << GPT12_T5CON_T5I_SHIFT)
              & GPT12_T5CON_T5I_MASK)
           | GPT12_T5CON_CI_RISING
           | GPT12_T5CON_T5SC,
           GPT12_REG(GPT12_T5CON_OFFSET));

  /* Zero T5 counter and CAPREL at boot */

  putreg32(0u, GPT12_REG(GPT12_T5_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));

  snprintf(devpath, sizeof(devpath), "/dev/cap%d", minor);

  ret = cap_register(devpath, &priv->base);
  if (ret < 0)
    {
      cperr("ERROR: cap_register(%s) failed: %d\n", devpath, ret);
    }

  return ret;
}
