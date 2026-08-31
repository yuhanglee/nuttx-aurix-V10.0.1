/****************************************************************************
 * arch/tricore/src/common/tricore_gpt12_pwm.c
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
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/timers/pwm.h>

#include "tricore_internal.h"
#include "tricore_gpt12_pwm.h"

extern void aurix_cpu_endinit_enable(bool enable);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The pwm example enables CONFIG_PWM_MULTICHAN; read the duty from the
 * first channel in that case, otherwise from the single channel field.
 */

#ifdef CONFIG_PWM_MULTICHAN
#  define PWM_DUTY(info) ((info)->channels[0].duty)
#else
#  define PWM_DUTY(info) ((info)->duty)
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gpt12_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int gpt12_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int gpt12_ioctl(FAR struct pwm_lowerhalf_s *dev,
                       int cmd, unsigned long arg);
static int gpt12_clk_enable(void);
static int gpt12_find_prescaler(int block, uint32_t freq_hz,
                                FAR uint32_t *bps_out,
                                FAR uint32_t *ti_out,
                                FAR uint32_t *period_out);
#ifdef GPT12_GPT2_PWM_ENABLED
static int gpt2_t6_isr(int irq, FAR void *context, FAR void *arg);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct pwm_ops_s g_gpt12_pwm_ops =
{
  .setup    = gpt12_pwm_setup,
  .shutdown = gpt12_pwm_shutdown,
  .start    = gpt12_pwm_start,
  .stop     = gpt12_pwm_stop,
  .ioctl    = gpt12_ioctl,
};

struct gpt12_pwm_s g_gpt12_pwm[2] =
{
  [0] = { .base.ops = &g_gpt12_pwm_ops, .block = 0 },  /* GPT1: T3 */
  [1] = { .base.ops = &g_gpt12_pwm_ops, .block = 1 },  /* GPT2: T6 */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpt12_pwm_setup
 ****************************************************************************/

static int gpt12_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  UNUSED(dev);
  return OK;
}

/****************************************************************************
 * Name: gpt12_pwm_shutdown
 ****************************************************************************/

static int gpt12_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  return gpt12_pwm_stop(dev);
}

/****************************************************************************
 * Name: gpt12_ioctl
 ****************************************************************************/

static int gpt12_ioctl(FAR struct pwm_lowerhalf_s *dev,
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
 * Name: gpt1_t3_isr
 *
 * T3 overflow ISR — toggling and T2/T4 reload are hardware-managed.
 *
 ****************************************************************************/

int gpt1_t3_isr(int irq, FAR void *context, FAR void *arg)
{
  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);
  return OK;
}

/****************************************************************************
 * Name: gpt12_calc_ticks
 *
 * Convert b16 duty cycle to high/low tick counts, clamped to [1, period-1].
 *
 ****************************************************************************/

void gpt12_calc_ticks(uint32_t period_ticks, uint32_t duty_b16,
                      FAR uint32_t *high_out, FAR uint32_t *low_out)
{
  uint32_t high;

  high = (uint32_t)(((uint64_t)period_ticks * duty_b16) >> 16);

  if (high == 0u)
    {
      high = 1u;
    }
  else if (high >= period_ticks)
    {
      high = period_ticks - 1u;
    }

  *high_out = high;
  *low_out  = period_ticks - high;
}

/****************************************************************************
 * Name: gpt12_do_register
 ****************************************************************************/

int gpt12_do_register(int minor)
{
  char devpath[GPT12_DEVPATH_LEN];
  int  ret;

  snprintf(devpath, sizeof(devpath), "/dev/pwm%d", minor);

  ret = pwm_register(devpath, &g_gpt12_pwm[minor].base);
  if (ret < 0)
    {
      pwmerr("ERROR: pwm_register(%s) failed: %d\n", devpath, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: gpt12_clk_enable
 *
 * Enable GPT12 module clock; uses AURIX endinit protection.
 *
 ****************************************************************************/

static int gpt12_clk_enable(void)
{
  uint32_t retries = GPT12_CLC_TIMEOUT;

  if ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) & GPT12_CLC_DISS) == 0u)
    {
      return OK;
    }

  aurix_cpu_endinit_enable(false);
  modreg32(0u, GPT12_CLC_DISR, GPT12_REG(GPT12_CLC_OFFSET));
  aurix_cpu_endinit_enable(true);

  while ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) & GPT12_CLC_DISS) &&
         --retries > 0u)
    {
    }

  if (retries == 0u)
    {
      pwmerr("ERROR: GPT12 clock enable timeout\n");
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: gpt12_find_prescaler
 *
 * Find BPS + TI pair for freq_hz; divisor = (2 << i) * factor
 * (factor=2 GPT1, factor=1 GPT2).
 *
 ****************************************************************************/

static int gpt12_find_prescaler(int block, uint32_t freq_hz,
                                FAR uint32_t *bps_out,
                                FAR uint32_t *ti_out,
                                FAR uint32_t *period_out)
{
  uint32_t factor = 2u;
  uint32_t ti;
  size_t   i;

#ifdef GPT12_GPT2_PWM_ENABLED
  if (block == 1)
    {
      factor = 1u;
    }
#else
  UNUSED(block);
#endif

  for (i = 0; i < sizeof(g_bps_table) / sizeof(g_bps_table[0]); i++)
    {
      uint32_t f_in = g_gpt12_fspb_hz / ((2u << i) * factor);

      for (ti = 0; ti <= GPT12_TI_FIELD_MAX; ti++)
        {
          uint32_t f_tick  = f_in >> ti;
          uint32_t period  = f_tick / freq_hz;

          if (period >= GPT12_PERIOD_MIN_TICKS &&
              period <= (uint32_t)GPT12_COUNT_MAX)
            {
              *bps_out    = g_bps_table[i];
              *ti_out     = ti;
              *period_out = period;
              return OK;
            }
        }
    }

  return -ERANGE;
}

/****************************************************************************
 * Name: gpt12_pwm_start
 *
 * Set prescaler, load reload values, and start the timer.
 * GPT1: T3 core, T2/T4 reload.  GPT2: T6 core, CAPREL reload.
 *
 ****************************************************************************/

int gpt12_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                    FAR const struct pwm_info_s *info)
{
  FAR struct gpt12_pwm_s *priv = (FAR struct gpt12_pwm_s *)dev;
  uint32_t bps;
  uint32_t ti;
  uint32_t period_ticks;
  uint32_t high_ticks;
  uint32_t low_ticks;
  uint32_t txcon;
  int      ret;

  if (info->frequency == 0u)
    {
      pwmerr("ERROR: frequency must be > 0\n");
      return -EINVAL;
    }

  modreg32(0u, GPT12_TXCON_TXR, GPT12_REG(priv->con_offset));

  ret = gpt12_find_prescaler(priv->block, info->frequency,
                             &bps, &ti, &period_ticks);
  if (ret < 0)
    {
      pwmerr("ERROR: %lu Hz out of range for block %d\n",
             (unsigned long)info->frequency, priv->block);
      return ret;
    }

  gpt12_calc_ticks(period_ticks, (uint32_t)PWM_DUTY(info),
                   &high_ticks, &low_ticks);

  pwminfo("GPT12 block=%d freq=%lu period=%lu high=%lu low=%lu"
          " BPS=0x%lx TI=%lu\n",
          priv->block,
          (unsigned long)info->frequency,
          (unsigned long)period_ticks,
          (unsigned long)high_ticks,
          (unsigned long)low_ticks,
          (unsigned long)bps,
          (unsigned long)ti);

  /* TxCON: timer mode, output enable, prescaler; run bit set last. */

  txcon = GPT12_TXCON_TXM_TIMER
        | ((ti << GPT12_T3CON_T3I_SHIFT) & GPT12_T3CON_T3I_MASK)
        | (bps & GPT12_T3CON_BPS1_MASK)
        | GPT12_T3CON_T3OE;

#ifdef GPT12_GPT2_PWM_ENABLED
  if (priv->block == 1)
    {
      txcon |= GPT12_T6CON_T6SR;  /* CAPREL reload on T6 overflow */
    }
#endif

  putreg32(txcon, GPT12_REG(priv->con_offset));

#ifdef GPT12_GPT1_PWM_ENABLED
  if (priv->block == 0)
    {
      uint32_t t2con;
      uint32_t t4con;

      /* T2: reload on positive T3OTL edge */

      t2con = GPT12_TXCON_TXM_RELOAD
            | GPT12_TXCON_TXR
            | ((GPT12_TXCON_TXI_T3OTL_POS << GPT12_TXCON_TXI_SHIFT)
               & GPT12_TXCON_TXI_MASK);
      putreg32(t2con, GPT12_REG(GPT12_T2CON_OFFSET));

      /* T4: reload on negative T3OTL edge */

      t4con = GPT12_TXCON_TXM_RELOAD
            | GPT12_TXCON_TXR
            | ((GPT12_TXCON_TXI_T3OTL_NEG << GPT12_TXCON_TXI_SHIFT)
               & GPT12_TXCON_TXI_MASK);
      putreg32(t4con, GPT12_REG(GPT12_T4CON_OFFSET));

      /* Load T2/T4; prime T3 to overflow on first tick. */

      putreg32((uint32_t)GPT12_RELOAD(high_ticks), GPT12_REG(GPT12_T2_OFFSET));
      putreg32((uint32_t)GPT12_RELOAD(low_ticks),  GPT12_REG(GPT12_T4_OFFSET));
      putreg32((uint32_t)GPT12_COUNT_MAX,           GPT12_REG(priv->cnt_offset));
    }
#endif /* GPT12_GPT1_PWM_ENABLED */

#ifdef GPT12_GPT2_PWM_ENABLED
  if (priv->block == 1)
    {
      /* Store high/low CAPREL preloads for the ISR. */

      priv->caprel_high = (uint32_t)GPT12_RELOAD(high_ticks);
      priv->caprel_low  = (uint32_t)GPT12_RELOAD(low_ticks);

      /* Load CAPREL; prime T6 to overflow in 1 tick. */

      putreg32(priv->caprel_high, GPT12_REG(GPT12_CAPREL_OFFSET));
      putreg32(GPT12_T6_PRIME_VALUE, GPT12_REG(priv->cnt_offset));
    }
#endif /* GPT12_GPT2_PWM_ENABLED */

  up_enable_irq(priv->srn);
  modreg32(GPT12_TXCON_TXR, GPT12_TXCON_TXR, GPT12_REG(priv->con_offset));

  priv->running = true;
  return OK;
}

/****************************************************************************
 * Name: gpt12_pwm_stop
 *
 * Stop the core timer; for GPT1 also halt T2/T4 reload timers.
 *
 ****************************************************************************/

int gpt12_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct gpt12_pwm_s *priv = (FAR struct gpt12_pwm_s *)dev;

  if (!priv->running)
    {
      return OK;
    }

  modreg32(0u, GPT12_TXCON_TXR, GPT12_REG(priv->con_offset));

#ifdef GPT12_GPT1_PWM_ENABLED
  if (priv->block == 0)
    {
      /* Halt T2/T4 reload timers. */

      modreg32(0u, GPT12_TXCON_TXR, GPT12_REG(GPT12_T2CON_OFFSET));
      modreg32(0u, GPT12_TXCON_TXR, GPT12_REG(GPT12_T4CON_OFFSET));
    }
#endif

  up_disable_irq(priv->srn);
  priv->running = false;
  return OK;
}

#ifdef GPT12_GPT2_PWM_ENABLED

/****************************************************************************
 * Name: gpt2_t6_isr
 *
 * T6 overflow ISR — update CAPREL with the opposite-phase reload value.
 *
 ****************************************************************************/

static int gpt2_t6_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct gpt12_pwm_s *priv = &g_gpt12_pwm[GPT12_GPT2_MINOR];

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  if (getreg32(GPT12_REG(GPT12_T6CON_OFFSET)) & GPT12_T6CON_T6OTL)
    {
      putreg32(priv->caprel_low, GPT12_REG(GPT12_CAPREL_OFFSET));
    }
  else
    {
      putreg32(priv->caprel_high, GPT12_REG(GPT12_CAPREL_OFFSET));
    }

  return OK;
}

#endif /* GPT12_GPT2_PWM_ENABLED */

#ifdef GPT12_GPT1_PWM_ENABLED

/****************************************************************************
 * Name: gpt12_gpt1_pwm_register
 *
 * Configure GPT1 (T3) for PWM and register /dev/pwm<minor>.
 *
 ****************************************************************************/

int gpt12_gpt1_pwm_register(int minor)
{
  FAR struct gpt12_pwm_s *priv = &g_gpt12_pwm[minor];
  int ret;

  if (minor != GPT12_GPT1_MINOR)
    {
      pwmerr("ERROR: only minor == %d is supported for GPT1\n",
             GPT12_GPT1_MINOR);
      return -EINVAL;
    }

  ret = gpt12_clk_enable();
  if (ret < 0)
    {
      return ret;
    }

  priv->srn        = GPT12_GPT1_T3_SRN;
  priv->con_offset = GPT12_T3CON_OFFSET;
  priv->cnt_offset = GPT12_T3_OFFSET;
  irq_attach(priv->srn, gpt1_t3_isr, NULL);
  return gpt12_do_register(minor);
}

#endif /* GPT12_GPT1_PWM_ENABLED */

#ifdef GPT12_GPT2_PWM_ENABLED

/****************************************************************************
 * Name: gpt12_gpt2_pwm_register
 *
 * Configure GPT2 (T6/CAPREL) for PWM and register /dev/pwm<minor>.
 *
 ****************************************************************************/

int gpt12_gpt2_pwm_register(int minor)
{
  FAR struct gpt12_pwm_s *priv = &g_gpt12_pwm[minor];
  int ret;

  if (minor != GPT12_GPT2_MINOR)
    {
      pwmerr("ERROR: only minor == %d is supported for GPT2\n",
             GPT12_GPT2_MINOR);
      return -EINVAL;
    }

  ret = gpt12_clk_enable();
  if (ret < 0)
    {
      return ret;
    }

  priv->srn        = GPT12_GPT2_T6_SRN;
  priv->con_offset = GPT12_T6CON_OFFSET;
  priv->cnt_offset = GPT12_T6_OFFSET;
  irq_attach(priv->srn, gpt2_t6_isr, NULL);
  return gpt12_do_register(minor);
}

#endif /* GPT12_GPT2_PWM_ENABLED */
