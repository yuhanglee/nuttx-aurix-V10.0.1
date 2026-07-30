/****************************************************************************
 * arch/tricore/src/tc3xx/tc3xx_gpt12_pwm.c
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
#include <nuttx/bits.h>
#include <nuttx/timers/pwm.h>

#include "tricore_internal.h"
#include "tricore_gpio.h"
#include "hardware/tc3xx_clock.h"
#include "hardware/tc3xx_gpt12.h"
#include "tc3xx_gpt12_pwm.h"


extern void aurix_cpu_endinit_enable(bool enable);

#define GPT12_FSPB_HZ          TC3X_FSPB_TARGET_HZ /* peripheral bus clock fed to GPT12   */
#define GPT12_CLC_TIMEOUT      100000u             /* max polls waiting for clock enable  */
#define GPT12_COUNT_MAX        UINT16_MAX          /* 16-bit timer top value              */
#define GPT12_TI_FIELD_MAX     7u                  /* T3I/T6I/T5I is 3 bits: max value    */
#define GPT12_PERIOD_MIN_TICKS 2u                  /* minimum ticks/phase (prevents /0)   */
#define GPT12_GPT1_MINOR       0                   /* PWM device minor for the GPT1 block */
#define GPT12_GPT2_MINOR       1                   /* PWM device minor for the GPT2 block */
#define GPT12_DEVPATH_LEN      12u                 /* size of "/dev/pwm%d" buffer */

/* Prime T6 one count below UINT16_MAX so the first overflow fires quickly. */

#define GPT12_T6_PRIME_VALUE   ((uint32_t)GPT12_COUNT_MAX - 1u)

/* Prescaler table entry: BPS register value and fSPB divisor */

struct gpt12_bps_cfg_s
{
  uint32_t bps_val;
  uint32_t f_div;
};

struct tc3xx_gpt12_pwm_s
{
  struct pwm_lowerhalf_s  base;
  uint8_t                 block;      /* 0 = GPT1 (T3), 1 = GPT2 (T6) */
  int                     srn;        /* Timer block SRN; set at registration */
  uint32_t                con_offset; /* TxCON register offset; set at registration */
  uint32_t                cnt_offset; /* Tx count register offset; set at registration */
  bool                    running;
  uint32_t                caprel_high;
  uint32_t                caprel_low;
};

static int  gpt12_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int  gpt12_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int  gpt12_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info);
static int  gpt12_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int  gpt12_ioctl(FAR struct pwm_lowerhalf_s *dev,
                        int cmd, unsigned long arg);

/* GPT1 block prescaler options */

static const struct gpt12_bps_cfg_s g_bps1_table[] =
{
  {GPT12_T3CON_BPS1_DIV4,  4u},   /* fSPB / 4  = 25.000 MHz */
  {GPT12_T3CON_BPS1_DIV8,  8u},   /* fSPB / 8  = 12.500 MHz */
  {GPT12_T3CON_BPS1_DIV16, 16u},  /* fSPB / 16 =  6.250 MHz */
  {GPT12_T3CON_BPS1_DIV32, 32u},  /* fSPB / 32 =  3.125 MHz */
};

#ifdef CONFIG_TC3XX_GPT2_PWM

/* GPT2 (T6/CAPREL) prescaler options */

static const struct gpt12_bps_cfg_s g_bps2_table[] =
{
  {GPT12_T6CON_BPS2_DIV2,  2u},   /* fSPB / 2  = 50.000 MHz */
  {GPT12_T6CON_BPS2_DIV4,  4u},   /* fSPB / 4  = 25.000 MHz */
  {GPT12_T6CON_BPS2_DIV8,  8u},   /* fSPB / 8  = 12.500 MHz */
  {GPT12_T6CON_BPS2_DIV16, 16u},  /* fSPB / 16 =  6.250 MHz */
};

#endif /* CONFIG_TC3XX_GPT2_PWM */

/* Shared ops table for all GPT12 PWM channels */

static const struct pwm_ops_s g_gpt12_pwm_ops =
{
  .setup    = gpt12_pwm_setup,
  .shutdown = gpt12_pwm_shutdown,
  .start    = gpt12_pwm_start,
  .stop     = gpt12_pwm_stop,
  .ioctl    = gpt12_ioctl,
};

static struct tc3xx_gpt12_pwm_s g_gpt12_pwm[2] =
{
  [0] = { .base.ops = &g_gpt12_pwm_ops, .block = 0 },  /* GPT1: T3 */
  [1] = { .base.ops = &g_gpt12_pwm_ops, .block = 1 },  /* GPT2: T6 */
};

/****************************************************************************
 * Name: gpt1_t3_isr
 *
 *   T3 overflow handler.  T3OTL toggling and T2/T4 reloading are
 *   handled in hardware; no software action required.
 *
 ****************************************************************************/

static int gpt1_t3_isr(int irq, FAR void *context, FAR void *arg)
{
  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);
  return OK;
}

/****************************************************************************
 * Name: gpt12_find_prescaler
 *
 *   Select BPS + TI prescaler pair for the requested frequency.
 *   Uses g_bps1_table for block 0 (GPT1) and g_bps2_table for block 1 (GPT2).
 *
 ****************************************************************************/

static int gpt12_find_prescaler(int block, uint32_t freq_hz,
                                FAR uint32_t *bps_out,
                                FAR uint32_t *ti_out,
                                FAR uint32_t *period_out)
{
  const struct gpt12_bps_cfg_s *table;
  size_t   ntable;
  uint32_t ti;
  size_t   i;

#ifdef CONFIG_TC3XX_GPT2_PWM
  if (block == 1)
    {
      table  = g_bps2_table;
      ntable = sizeof(g_bps2_table) / sizeof(g_bps2_table[0]);
    }
  else
#endif
    {
      UNUSED(block);
      table  = g_bps1_table;
      ntable = sizeof(g_bps1_table) / sizeof(g_bps1_table[0]);
    }

  for (i = 0; i < ntable; i++)
    {
      uint32_t f_in = GPT12_FSPB_HZ / table[i].f_div;

      for (ti = 0; ti <= GPT12_TI_FIELD_MAX; ti++)
        {
          uint32_t f_tick = f_in >> ti;
          uint32_t period = f_tick / freq_hz;

          if (period >= GPT12_PERIOD_MIN_TICKS &&
              period <= (uint32_t)GPT12_COUNT_MAX)
            {
              *bps_out    = table[i].bps_val;
              *ti_out     = ti;
              *period_out = period;
              return OK;
            }
        }
    }

  return -ERANGE;
}

/****************************************************************************
 * Name: gpt12_calc_ticks
 *
 *   Compute high- and low-phase tick counts from a b16 duty value.
 *   Clamps to [1, period-1] to guarantee non-zero phases.
 *
 ****************************************************************************/

static void gpt12_calc_ticks(uint32_t period_ticks, uint32_t duty_b16,
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
 * Name: gpt12_clk_enable
 *
 *   Enable GPT120 module clock.  No-op if already running.
 *
 ****************************************************************************/

static int gpt12_clk_enable(void)
{
  uint32_t i;

  /* Already running */

  if ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) & GPT12_CLC_DISS) == 0u)
    {
      return OK;
    }

  aurix_cpu_endinit_enable(false);
  modreg32(0, GPT12_CLC_DISR, GPT12_REG(GPT12_CLC_OFFSET));
  aurix_cpu_endinit_enable(true);

  for (i = 0; i < GPT12_CLC_TIMEOUT; i++)
    {
      if ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) & GPT12_CLC_DISS) == 0u)
        {
          return OK;
        }
    }

  pwmerr("ERROR: GPT120 clock enable timeout\n");
  return -EIO;
}

/****************************************************************************
 * Name: gpt12_ioctl
 *
 *   No device-specific ioctls.
 *
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
 * Name: gpt12_do_register
 *
 *   Register /dev/pwmN with the PWM upper-half driver.
 *
 ****************************************************************************/

static int gpt12_do_register(int minor)
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
 * Name: gpt12_pwm_setup
 ****************************************************************************/

static int gpt12_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  UNUSED(dev);
  return OK;
}

/****************************************************************************
 * Name: gpt12_pwm_shutdown
 *
 *   Stop the active timer when the device is closed.
 *
 ****************************************************************************/

static int gpt12_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  gpt12_pwm_stop(dev);
  return OK;
}

/****************************************************************************
 * Name: gpt12_pwm_start
 *
 *   Configure and start PWM output on the selected timer block.
 *
 ****************************************************************************/

static int gpt12_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                           FAR const struct pwm_info_s *info)
{
  FAR struct tc3xx_gpt12_pwm_s *priv = (FAR struct tc3xx_gpt12_pwm_s *)dev;
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

  /* Halt the timer before reconfiguring */

  modreg32(0, GPT12_TXCON_TXR, GPT12_REG(priv->con_offset));

  ret = gpt12_find_prescaler(priv->block, info->frequency,
                             &bps, &ti, &period_ticks);
  if (ret < 0)
    {
      pwmerr("ERROR: %lu Hz out of range for block %d\n",
             (unsigned long)info->frequency, priv->block);
      return ret;
    }

  gpt12_calc_ticks(period_ticks, (uint32_t)info->duty,
                   &high_ticks, &low_ticks);

  pwminfo("GPT12 PWM: block=%d, freq=%lu Hz, period=%lu ticks, "
          "high=%lu, low=%lu, BPS=0x%lx, TI=%lu\n",
          priv->block,
          (unsigned long)info->frequency,
          (unsigned long)period_ticks,
          (unsigned long)high_ticks,
          (unsigned long)low_ticks,
          (unsigned long)bps,
          (unsigned long)ti);

  /* TxCON: timer mode, output enable, prescaler; TXR cleared until loaded */

  txcon = GPT12_TXCON_TXM_TIMER
        | ((ti << GPT12_T3CON_T3I_SHIFT) & GPT12_T3CON_T3I_MASK)
        | (bps & GPT12_T3CON_BPS1_MASK)
        | GPT12_TXCON_TXOE;

#ifdef CONFIG_TC3XX_GPT2_PWM
  if (priv->block == 1)
    {
      txcon |= GPT12_T6CON_T6SR;  /* CAPREL reload on overflow */
    }
#endif

  putreg32(txcon, GPT12_REG(priv->con_offset));

#ifdef CONFIG_TC3XX_GPT1_PWM
  if (priv->block == 0)
    {
      uint32_t t2con;
      uint32_t t4con;

      /* T2CON: reload mode, active, triggered on rising T3OTL edge */

      t2con = GPT12_TXCON_TXM_RELOAD
            | GPT12_TXCON_TXR
            | ((GPT12_TXCON_TXI_T3OTL_POS << GPT12_TXCON_TXI_SHIFT)
               & GPT12_TXCON_TXI_MASK);
      putreg32(t2con, GPT12_REG(GPT12_T2CON_OFFSET));

      /* T4CON: reload mode, active, triggered on falling T3OTL edge */

      t4con = GPT12_TXCON_TXM_RELOAD
            | GPT12_TXCON_TXR
            | ((GPT12_TXCON_TXI_T3OTL_NEG << GPT12_TXCON_TXI_SHIFT)
               & GPT12_TXCON_TXI_MASK);
      putreg32(t4con, GPT12_REG(GPT12_T4CON_OFFSET));

      /* Load T2/T4 reload values; prime T3 to overflow on first tick */

      putreg32((uint32_t)GPT12_RELOAD(high_ticks), GPT12_REG(GPT12_T2_OFFSET));
      putreg32((uint32_t)GPT12_RELOAD(low_ticks),  GPT12_REG(GPT12_T4_OFFSET));
      putreg32((uint32_t)GPT12_COUNT_MAX,           GPT12_REG(priv->cnt_offset));
    }
#endif /* CONFIG_TC3XX_GPT1_PWM */

#ifdef CONFIG_TC3XX_GPT2_PWM
  if (priv->block == 1)
    {
      /* Store high/low phase CAPREL preloads for the ISR */

      priv->caprel_high = (uint32_t)GPT12_RELOAD(high_ticks);
      priv->caprel_low  = (uint32_t)GPT12_RELOAD(low_ticks);

      /* Load CAPREL; prime T6 to overflow in 1 tick */

      putreg32(priv->caprel_high, GPT12_REG(GPT12_CAPREL_OFFSET));
      putreg32(GPT12_T6_PRIME_VALUE, GPT12_REG(priv->cnt_offset));
    }
#endif /* CONFIG_TC3XX_GPT2_PWM */

  up_enable_irq(priv->srn);

  modreg32(GPT12_TXCON_TXR, GPT12_TXCON_TXR,
           GPT12_REG(priv->con_offset));

  priv->running = true;
  return OK;
}

/****************************************************************************
 * Name: gpt12_pwm_stop
 *
 *   Halt the active timer block and disable its interrupt.
 *
 ****************************************************************************/

static int gpt12_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct tc3xx_gpt12_pwm_s *priv = (FAR struct tc3xx_gpt12_pwm_s *)dev;

  if (!priv->running)
    {
      return OK;
    }

  modreg32(0, GPT12_TXCON_TXR, GPT12_REG(priv->con_offset));

#ifdef CONFIG_TC3XX_GPT1_PWM
  if (priv->block == 0)
    {
      /* Halt auxiliary reload timers */

      modreg32(0, GPT12_TXCON_TXR, GPT12_REG(GPT12_T2CON_OFFSET));
      modreg32(0, GPT12_TXCON_TXR, GPT12_REG(GPT12_T4CON_OFFSET));
    }
#endif

  up_disable_irq(priv->srn);
  priv->running = false;
  return OK;
}

#ifdef CONFIG_TC3XX_GPT1_PWM

/****************************************************************************
 * Name: tc3xx_gpt1_pwm_register
 *
 *   Enable GPT120 clock and register /dev/pwmN.
 *
 ****************************************************************************/

static int tc3xx_gpt1_pwm_register(int minor)
{
  FAR struct tc3xx_gpt12_pwm_s *priv = &g_gpt12_pwm[minor];
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

  priv->srn        = CONFIG_TC3XX_GPT1_PWM_T3_SRN;
  priv->con_offset = GPT12_T3CON_OFFSET;
  priv->cnt_offset = GPT12_T3_OFFSET;
  irq_attach(priv->srn, gpt1_t3_isr, NULL);
  return gpt12_do_register(minor);
}

#endif /* CONFIG_TC3XX_GPT1_PWM */

#ifdef CONFIG_TC3XX_GPT2_PWM

/****************************************************************************
 * Name: gpt2_t6_isr
 *
 *   T6 overflow handler.  Write the opposite-phase CAPREL value for
 *   the next reload.
 *
 ****************************************************************************/

static int gpt2_t6_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct tc3xx_gpt12_pwm_s *priv = &g_gpt12_pwm[1];

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  /* T6OTL=1 (pin HIGH): next phase is low; T6OTL=0 (pin LOW): next is high */

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

/****************************************************************************
 * Name: tc3xx_gpt2_pwm_register
 *
 *   Enable GPT120 clock and register /dev/pwmN.
 *
 ****************************************************************************/

static int tc3xx_gpt2_pwm_register(int minor)
{
  FAR struct tc3xx_gpt12_pwm_s *priv = &g_gpt12_pwm[minor];
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

  priv->srn        = CONFIG_TC3XX_GPT2_PWM_T6_SRN;
  priv->con_offset = GPT12_T6CON_OFFSET;
  priv->cnt_offset = GPT12_T6_OFFSET;
  irq_attach(priv->srn, gpt2_t6_isr, NULL);
  return gpt12_do_register(minor);
}

#endif /* CONFIG_TC3XX_GPT2_PWM */

/****************************************************************************
 * Name: tc3xx_gpt12_pwm_register
 *
 *   minor 0 -> GPT1 (/dev/pwm0), minor 1 -> GPT2 (/dev/pwm1).
 *
 ****************************************************************************/

int tc3xx_gpt12_pwm_register(int minor)
{
#ifdef CONFIG_TC3XX_GPT1_PWM
  if (minor == GPT12_GPT1_MINOR)
    {
      return tc3xx_gpt1_pwm_register(minor);
    }
#endif
#ifdef CONFIG_TC3XX_GPT2_PWM
  if (minor == GPT12_GPT2_MINOR)
    {
      return tc3xx_gpt2_pwm_register(minor);
    }
#endif
  return -EINVAL;
}
