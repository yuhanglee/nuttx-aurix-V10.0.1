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
#include "hardware/tc3x_port.h"
#include "tc3x_gpt12_cap.h"

/****************************************************************************
 * Private Types (shared by GPT1 and GPT2 capture paths)
 ****************************************************************************/

/* Lower-half private state for one GPT12 capture channel.
 * Shared by all paths (CAPIN, T3IN, T2IN, T4IN); priv->gpt1 selects
 * the hardware path.  GPT2-only and GPT1-only fields are zero otherwise.
 */

struct tc3x_gpt12_cap_s
{
  struct cap_lowerhalf_s  base;
  volatile uint32_t       freq_hz;      /* Measured frequency in Hz */
  volatile uint8_t        duty_pct;     /* Duty cycle  */
  volatile uint32_t       edges;        /* Complete period measurements */

  /* Three-point capture state machine (shared by GPT1 and GPT2 paths) */

  volatile uint8_t        cap_state;    /* CAP_STATE_* */
  volatile uint16_t       t1;           /* Timestamp at first edge */
  volatile uint16_t       t2;           /* Timestamp at second edge */

  bool                    running;      /* True between start() and stop() */
  bool                    gpt1;         /* True = GPT1 path (T2/T4); false = GPT2 (T5) */

  /* GPT2 (T5/CAPREL) specific */

  uint32_t                f_hz;         /* Timer tick frequency in Hz */
  bool                    ct3;          /* True = T3IN trigger (CT3=1); false = CAPIN */
  bool                    t1_rising;    /* T3IN only: first edge was rising */

  /* GPT1 (T2/T4 + T3 timebase) specific */

  uint16_t                con_offset;   /* GPT12_T2CON_OFFSET or T4CON_OFFSET */
  uint16_t                cnt_offset;   /* GPT12_T2_OFFSET    or T4_OFFSET */
  int                     srn;          /* SRN for this channel capture ISR */
  uint16_t                pisel_mask;   /* PISEL bits that select the TxIN pin */
  uint16_t                pisel_val;    /* PISEL value for the configured pin */
};

/****************************************************************************
 * Private Functions (shared read-only ops)
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

static int gpt12_cap_getfreq(FAR struct cap_lowerhalf_s *lower,
                              FAR uint32_t *freq)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  DEBUGASSERT(freq != NULL);
  *freq = priv->freq_hz;
  return OK;
}

static int gpt12_cap_getedges(FAR struct cap_lowerhalf_s *lower,
                               FAR uint32_t *edges)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

  DEBUGASSERT(edges != NULL);
  *edges = priv->edges;
  return OK;
}

static int gpt12_cap_ioctl(FAR struct cap_lowerhalf_s *lower,
                            int cmd, unsigned long arg)
{
  UNUSED(lower);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
}

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GPT12_CAP_DEVPATH_LEN    16

/* Capture state machine states */

#define CAP_STATE_IDLE   0u  /* Not started */
#define CAP_STATE_RISE1  1u  /* Waiting for first rising edge (t1) */
#define CAP_STATE_FALL   2u  /* Waiting for falling edge (t2) */
#define CAP_STATE_RISE2  3u  /* Waiting for second rising edge (t3) */

/* Maximum poll count waiting for the CLC.DISS bit to clear */

#define GPT12_CAP_CLC_TIMEOUT    100000u

extern void aurix_cpu_endinit_enable(bool enable);

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

  /* Already enabled; safe to call from multiple register paths. */

  if ((getreg32(GPT12_REG(GPT12_CLC_OFFSET)) & GPT12_CLC_DISS) == 0u)
    {
      return OK;
    }

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
 * Private Function Prototypes (shared)
 ****************************************************************************/

static int gpt12_cap_start(FAR struct cap_lowerhalf_s *lower);
static int gpt12_cap_stop(FAR struct cap_lowerhalf_s *lower);
static int gpt12_gpt2_cap_start_hw(FAR struct tc3x_gpt12_cap_s *priv);
static int gpt12_gpt2_cap_stop_hw(FAR struct tc3x_gpt12_cap_s *priv);
static int gpt12_gpt1_cap_start_hw(FAR struct tc3x_gpt12_cap_s *priv);
static int gpt12_gpt1_cap_stop_hw(FAR struct tc3x_gpt12_cap_s *priv);
static int gpt12_cap_isr(int irq, FAR void *context,
                          FAR void *arg);

/****************************************************************************
 * Private Data (shared)
 ****************************************************************************/

/* Shared ops table; start()/stop() dispatch via priv->gpt1. */

static const struct cap_ops_s g_gpt12_cap_ops =
{
  .start    = gpt12_cap_start,
  .stop     = gpt12_cap_stop,
  .getduty  = gpt12_cap_getduty,
  .getfreq  = gpt12_cap_getfreq,
  .getedges = gpt12_cap_getedges,
  .ioctl    = gpt12_cap_ioctl,
};

static void gpt12_cap_reset_state(FAR struct tc3x_gpt12_cap_s *priv)
{
  priv->cap_state = CAP_STATE_RISE1;
  priv->t1        = 0u;
  priv->t2        = 0u;
  priv->freq_hz   = 0u;
  priv->duty_pct  = 0u;
  priv->edges     = 0u;
}

static int gpt12_cap_do_register(FAR struct tc3x_gpt12_cap_s *priv,
                                  int minor)
{
  char devpath[GPT12_CAP_DEVPATH_LEN];
  int  ret;

  snprintf(devpath, sizeof(devpath), "/dev/cap%d", minor);
  ret = cap_register(devpath, &priv->base);
  if (ret < 0)
    {
      cperr("ERROR: cap_register(%s) failed: %d\n", devpath, ret);
    }

  return ret;
}

#ifdef CONFIG_TC3X_GPT2_CAP

/****************************************************************************
 * Pre-processor Definitions (GPT2 only)
 ****************************************************************************/

/* Peripheral bus clock driving GPT12 */

#define GPT12_CAP_FSPB_HZ        TC3X_FSPB_TARGET_HZ

/* BPS2 prescaler: div2 gives a 50 MHz GPT2 base clock */

#define GPT12_CAP_BPS2_DIV       2u

/* GPT2 base clock fed to T5 before the per-timer prescaler T5I */

#define GPT12_CAP_FGPT_HZ        (GPT12_CAP_FSPB_HZ / GPT12_CAP_BPS2_DIV)

#ifdef CONFIG_TC3X_GPT2_CAP_T3IN
/* GPIO IN register and pin bit for the T3IN input.
 * Read at RISE1 to determine edge direction for duty correction.
 * IST3IN=0: T3INA = P02.6.  Add entries here for other SEL values.
 */
#  if CONFIG_TC3X_GPT2_CAP_T3IN_SEL == 0
#    define GPT12_T3IN_PORT_IN  (AURIX_PORT_ADDR(2u) + PORT_IN_OFFSET)
#    define GPT12_T3IN_PIN_BIT  6u
#  else
#    error "GPT12_T3IN_PORT_IN not defined for this TC3X_GPT2_CAP_T3IN_SEL value — add the board pin mapping"
#  endif
#endif /* CONFIG_TC3X_GPT2_CAP_T3IN */

/****************************************************************************
 * Private Data (GPT2)
 ****************************************************************************/

/* Singleton lower-half instance - GPT12 provides one capture path */

static struct tc3x_gpt12_cap_s g_gpt12_cap0 =
{
  .base.ops = &g_gpt12_cap_ops,
  .ct3      = false,
};

/* True while T5/CAPREL hardware is in use by any GPT2 capture channel */

static bool g_gpt2_caprel_busy;

#ifdef CONFIG_TC3X_GPT2_CAP_T3IN
static struct tc3x_gpt12_cap_s g_gpt12_t3in_cap =
{
  .base.ops = &g_gpt12_cap_ops,
  .ct3      = true,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpt12_cap_isr
 *
 *   Unified capture ISR for GPT1 (T2IN/T4IN) and GPT2 (CAPIN/T3IN).
 *   Advances the three-point state machine on each captured edge.
 *
 ****************************************************************************/

static int gpt12_cap_isr(int irq, FAR void *context,
                          FAR void *arg)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)arg;
  uint16_t t_cap;
  uint16_t period;
  uint16_t pulse;

  UNUSED(irq);
  UNUSED(context);

  if (priv->gpt1)
    {
      t_cap = (uint16_t)(getreg32(GPT12_REG(priv->cnt_offset)) & 0xffffu);
    }
  else
    {
      /* GPT2: read and clear CAPREL */

      t_cap = (uint16_t)(getreg32(GPT12_REG(GPT12_CAPREL_OFFSET)) &
                         GPT12_CAPREL_MASK);
      putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));
    }

  switch (priv->cap_state)
    {
      case CAP_STATE_RISE1:
        priv->t1 = t_cap;

        if (priv->gpt1)
          {
            modreg32((GPT12_TXCON_TXI_PIN_NEG << GPT12_TXCON_TXI_SHIFT),
                     GPT12_TXCON_TXI_MASK,
                     GPT12_REG(priv->con_offset));
          }
        else if (priv->ct3)
          {
#ifdef CONFIG_TC3X_GPT2_CAP_T3IN
            /* Sample GPIO to determine edge direction */

            priv->t1_rising =
              (bool)((getreg32(GPT12_T3IN_PORT_IN) >> GPT12_T3IN_PIN_BIT)
                     & 1u);
#endif
          }
        else
          {
            modreg32(GPT12_T5CON_CI_FALLING, GPT12_T5CON_CI_MASK,
                     GPT12_REG(GPT12_T5CON_OFFSET));
          }

        priv->cap_state = CAP_STATE_FALL;
        break;

      case CAP_STATE_FALL:
        priv->t2 = t_cap;

        if (priv->gpt1)
          {
            modreg32((GPT12_TXCON_TXI_PIN_POS << GPT12_TXCON_TXI_SHIFT),
                     GPT12_TXCON_TXI_MASK,
                     GPT12_REG(priv->con_offset));
          }
        else if (!priv->ct3)
          {
            modreg32(GPT12_T5CON_CI_RISING, GPT12_T5CON_CI_MASK,
                     GPT12_REG(GPT12_T5CON_OFFSET));
          }

        priv->cap_state = CAP_STATE_RISE2;
        break;

      case CAP_STATE_RISE2:
        period = t_cap    - priv->t1;
        pulse  = priv->t2 - priv->t1;

        /* T3IN: RISE1 was falling — pulse is LOW time; invert. */

        if (!priv->gpt1 && priv->ct3 && !priv->t1_rising && period > 0u)
          {
            pulse = period - pulse;
          }

        if (period > 0u)
          {
            priv->freq_hz  = priv->f_hz / (uint32_t)period;
            priv->duty_pct =
              (uint8_t)(((uint32_t)pulse * 100u) / (uint32_t)period);
            priv->edges++;
          }

        priv->t1 = t_cap;

        if (priv->gpt1)
          {
            modreg32((GPT12_TXCON_TXI_PIN_NEG << GPT12_TXCON_TXI_SHIFT),
                     GPT12_TXCON_TXI_MASK,
                     GPT12_REG(priv->con_offset));
          }
        else if (!priv->ct3)
          {
            modreg32(GPT12_T5CON_CI_FALLING, GPT12_T5CON_CI_MASK,
                     GPT12_REG(GPT12_T5CON_OFFSET));
          }

        priv->cap_state = CAP_STATE_FALL;
        break;

      default:
        break;
    }

  return OK;
}

/****************************************************************************
 * Name: gpt12_gpt2_cap_start_hw
 *
 *   Start T5/CAPREL capture.  Returns -EBUSY if T6 PWM is active or
 *   another GPT2 capture channel already holds CAPREL.
 *
 ****************************************************************************/

static int gpt12_gpt2_cap_start_hw(FAR struct tc3x_gpt12_cap_s *priv)
{
  irqstate_t flags;
  int        ret;

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

  flags = enter_critical_section();

  /* Refuse if another GPT2 capture channel already holds T5/CAPREL */

  if (g_gpt2_caprel_busy)
    {
      leave_critical_section(flags);
      cperr("ERROR: CAPREL already in use by another GPT2 capture "
            "channel\n");
      return -EBUSY;
    }

  gpt12_cap_reset_state(priv);

  /* Select capture trigger source: CT3=0 for CAPIN, CT3=1 for T3IN.
   * This must be done before enabling T5SC capture in T5CON.
   */

  modreg32(priv->ct3 ? GPT12_T5CON_CT3 : 0u,
           GPT12_T5CON_CT3,
           GPT12_REG(GPT12_T5CON_OFFSET));

  /* Set capture edge and zero counters.
   * CAPIN (ct3=false): start on rising edge; ISR toggles CI each capture.
   * T3IN  (ct3=true):  CI_BOTH so every edge fires without mid-stream
   *   CI switching; edge direction is resolved by reading the GPIO in ISR.
   */

  modreg32(priv->ct3 ? GPT12_T5CON_CI_BOTH : GPT12_T5CON_CI_RISING,
           GPT12_T5CON_CI_MASK,
           GPT12_REG(GPT12_T5CON_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_T5_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));

  /* Attach and enable the CAPREL interrupt */

  ret = irq_attach(CONFIG_TC3X_GPT2_CAP_CAPREL_SRN,
                   gpt12_cap_isr, priv);
  if (ret < 0)
    {
      leave_critical_section(flags);
      cperr("ERROR: irq_attach failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(CONFIG_TC3X_GPT2_CAP_CAPREL_SRN);

  /* Start T5 */

  modreg32(GPT12_T5CON_T5R, GPT12_T5CON_T5R,
           GPT12_REG(GPT12_T5CON_OFFSET));

  g_gpt2_caprel_busy = true;
  priv->running      = true;
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: gpt12_gpt2_cap_stop_hw
 *
 *   Halt T5 and disable the CAPREL interrupt.
 *
 ****************************************************************************/

static int gpt12_gpt2_cap_stop_hw(FAR struct tc3x_gpt12_cap_s *priv)
{
  irqstate_t flags;

  if (!priv->running)
    {
      return OK;
    }

  flags = enter_critical_section();

  modreg32(0, GPT12_T5CON_T5R, GPT12_REG(GPT12_T5CON_OFFSET));

  /* Disable and detach the CAPREL interrupt */

  up_disable_irq(CONFIG_TC3X_GPT2_CAP_CAPREL_SRN);
  irq_detach(CONFIG_TC3X_GPT2_CAP_CAPREL_SRN);

  priv->cap_state    = CAP_STATE_IDLE;
  g_gpt2_caprel_busy = false;
  priv->running      = false;

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static void gpt12_gpt2_t5_init(FAR struct tc3x_gpt12_cap_s *priv)
{
  modreg32(GPT12_T6CON_BPS2_DIV2, GPT12_T6CON_BPS2_MASK,
           GPT12_REG(GPT12_T6CON_OFFSET));

  priv->f_hz = GPT12_CAP_FGPT_HZ >> CONFIG_TC3X_GPT2_CAP_T5I;

  putreg32(GPT12_T5CON_T5M_TIMER
           | ((CONFIG_TC3X_GPT2_CAP_T5I << GPT12_T5CON_T5I_SHIFT)
              & GPT12_T5CON_T5I_MASK)
           | GPT12_T5CON_CI_RISING
           | GPT12_T5CON_T5SC,
           GPT12_REG(GPT12_T5CON_OFFSET));

  putreg32(0u, GPT12_REG(GPT12_T5_OFFSET));
  putreg32(0u, GPT12_REG(GPT12_CAPREL_OFFSET));
}

/****************************************************************************
 * Name: gpt12_capin_register
 *
 *   Configure T5/BPS2, register /dev/capN for CAPIN.
 *
 ****************************************************************************/

static int gpt12_capin_register(int minor)
{
  FAR struct tc3x_gpt12_cap_s *priv = &g_gpt12_cap0;

  /* Select CAPIN input source via PISEL.ISCAPIN */

#if CONFIG_TC3X_GPT2_CAP_CAPIN_SEL == 0
  modreg32(GPT12_PISEL_ISCAPIN_A, GPT12_PISEL_ISCAPIN_MASK,
           GPT12_REG(GPT12_PISEL_OFFSET));
#else
  modreg32(GPT12_PISEL_ISCAPIN_B, GPT12_PISEL_ISCAPIN_MASK,
           GPT12_REG(GPT12_PISEL_OFFSET));
#endif

  gpt12_gpt2_t5_init(priv);
  return gpt12_cap_do_register(priv, minor);
}

#ifdef CONFIG_TC3X_GPT2_CAP_T3IN

/****************************************************************************
 * Name: gpt12_t3in_register
 *
 *   Configure T5/BPS2, register /dev/capN for T3IN.  CT3 is applied by
 *   start().  Shares T5/CAPREL with CAPIN; only one channel may run at
 *   a time.
 *
 ****************************************************************************/

static int gpt12_t3in_register(int minor)
{
  FAR struct tc3x_gpt12_cap_s *priv = &g_gpt12_t3in_cap;

  /* Select T3IN input source via PISEL.IST3IN (2-bit field: 0=A, 1=B,
   * 2=C, 3=D).
   */

  modreg32(((uint32_t)CONFIG_TC3X_GPT2_CAP_T3IN_SEL
            << GPT12_PISEL_IST3IN_SHIFT) & GPT12_PISEL_IST3IN_MASK,
           GPT12_PISEL_IST3IN_MASK,
           GPT12_REG(GPT12_PISEL_OFFSET));

  gpt12_gpt2_t5_init(priv);
  return gpt12_cap_do_register(priv, minor);
}

#endif /* CONFIG_TC3X_GPT2_CAP_T3IN */

#endif /* CONFIG_TC3X_GPT2_CAP */

#ifdef CONFIG_TC3X_GPT1_CAP

/****************************************************************************
 * Pre-processor Definitions (GPT1 capture)
 ****************************************************************************/

/* GPT1 base clock: BPS1 hardcoded to DIV4 => fGPT1 = fSPB / 4 = 25 MHz */

#define GPT12_GPT1_CAP_FGPT_HZ   (TC3X_FSPB_TARGET_HZ / 4u)

/****************************************************************************
 * Private Data (GPT1 capture)
 ****************************************************************************/

/* Number of currently-active GPT1 capture channels */

static int g_gpt1_t3_users;

#ifdef CONFIG_TC3X_GPT1_CAP_T2
static struct tc3x_gpt12_cap_s g_gpt1_t2_cap =
{
  .base.ops   = &g_gpt12_cap_ops,
  .gpt1       = true,
  .con_offset = GPT12_T2CON_OFFSET,
  .cnt_offset = GPT12_T2_OFFSET,
  .srn        = CONFIG_TC3X_GPT1_CAP_T2_SRN,

  /* IST2IN is bit 0 of PISEL; value is 0 (T2INA) or 1 (T2INB) */

  .pisel_mask = GPT12_PISEL_IST2IN,
  .pisel_val  = (CONFIG_TC3X_GPT1_CAP_T2IN_SEL != 0) ? GPT12_PISEL_IST2IN : 0u,
};
#endif

#ifdef CONFIG_TC3X_GPT1_CAP_T4
static struct tc3x_gpt12_cap_s g_gpt1_t4_cap =
{
  .base.ops   = &g_gpt12_cap_ops,
  .gpt1       = true,
  .con_offset = GPT12_T4CON_OFFSET,
  .cnt_offset = GPT12_T4_OFFSET,
  .srn        = CONFIG_TC3X_GPT1_CAP_T4_SRN,

  /* IST4IN is bits[7:6] of PISEL; value selects T4INA..T4IND */

  .pisel_mask = GPT12_PISEL_IST4IN_MASK,
  .pisel_val  = (uint32_t)CONFIG_TC3X_GPT1_CAP_T4IN_SEL << GPT12_PISEL_IST4IN_SHIFT,
};
#endif

/****************************************************************************
 * Private Functions (GPT1 capture)
 ****************************************************************************/



/****************************************************************************
 * Name: gpt12_gpt1_cap_start_hw
 *
 *   GPT1 hardware-specific start: configure T3 timebase and TxIN capture.
 *   Called by the unified gpt12_cap_start() dispatcher.
 *
 ****************************************************************************/

static int gpt12_gpt1_cap_start_hw(FAR struct tc3x_gpt12_cap_s *priv)
{
  irqstate_t flags;
  int        ret;

  if (priv->running)
    {
      return OK;
    }

  /* Refuse if GPT1 PWM owns T3 */

  if ((getreg32(GPT12_REG(GPT12_T3CON_OFFSET)) & GPT12_T3CON_T3R) &&
      ((getreg32(GPT12_REG(GPT12_T2CON_OFFSET)) & GPT12_TXCON_TXM_MASK)
       == GPT12_TXCON_TXM_RELOAD))
    {
      cperr("ERROR: GPT1 PWM active; capture cannot start\n");
      return -EBUSY;
    }

  flags = enter_critical_section();

  gpt12_cap_reset_state(priv);

  /* Restore TxCON to Capture mode; GPT1 PWM may have left it in Reload. */

  modreg32(GPT12_TXCON_TXM_CAPTURE
           | ((GPT12_TXCON_TXI_PIN_POS << GPT12_TXCON_TXI_SHIFT)
              & GPT12_TXCON_TXI_MASK),
           GPT12_TXCON_TXM_MASK | GPT12_TXCON_TXI_MASK,
           GPT12_REG(priv->con_offset));

  /* Start T3 on first use; write full T3CON to restore clock config after
   * a GPT1 PWM session may have overwritten it.
   */

  if (g_gpt1_t3_users == 0)
    {
      putreg32(0u, GPT12_REG(GPT12_T3_OFFSET));
      putreg32(GPT12_T3CON_T3M_TIMER
               | GPT12_T3CON_BPS1_DIV4
               | (((CONFIG_TC3X_GPT1_CAP_T3I + 4u) << GPT12_T3CON_T3I_SHIFT)
                  & GPT12_T3CON_T3I_MASK)
               | GPT12_T3CON_T3R,
               GPT12_REG(GPT12_T3CON_OFFSET));
    }

  /* Attach and enable the TxIN capture interrupt */

  ret = irq_attach(priv->srn, gpt12_cap_isr, priv);
  if (ret < 0)
    {
      if (g_gpt1_t3_users == 0)
        {
          modreg32(0, GPT12_T3CON_T3R, GPT12_REG(GPT12_T3CON_OFFSET));
        }

      leave_critical_section(flags);
      cperr("ERROR: irq_attach(Tx cap) failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(priv->srn);
  g_gpt1_t3_users++;
  priv->running = true;

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: gpt12_gpt1_cap_stop_hw
 *
 *   GPT1 hardware-specific stop.  Called by the unified gpt12_cap_stop().
 *
 ****************************************************************************/

static int gpt12_gpt1_cap_stop_hw(FAR struct tc3x_gpt12_cap_s *priv)
{
  irqstate_t flags;

  if (!priv->running)
    {
      return OK;
    }

  flags = enter_critical_section();

  /* Disable and detach the TxIN capture interrupt */

  up_disable_irq(priv->srn);
  irq_detach(priv->srn);

  /* Halt T3 when the last active channel stops */

  g_gpt1_t3_users--;

  if (g_gpt1_t3_users == 0)
    {
      modreg32(0, GPT12_T3CON_T3R, GPT12_REG(GPT12_T3CON_OFFSET));
    }

  priv->cap_state = CAP_STATE_IDLE;
  priv->running   = false;

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions (GPT1 capture)
 ****************************************************************************/

/****************************************************************************
 * Name: gpt12_gpt1_cap_register
 *
 *   Configure T3 timebase and TxIN capture channel, register /dev/capN.
 *   minor: 2=T2IN (/dev/cap2), 4=T4IN (/dev/cap4).
 *
 ****************************************************************************/

static int gpt12_gpt1_cap_register(int minor)
{
  FAR struct tc3x_gpt12_cap_s *priv;

  switch (minor)
    {
#ifdef CONFIG_TC3X_GPT1_CAP_T2
      case 2:
        priv = &g_gpt1_t2_cap;
        break;
#endif
#ifdef CONFIG_TC3X_GPT1_CAP_T4
      case 4:
        priv = &g_gpt1_t4_cap;
        break;
#endif
      default:
        return -EINVAL;
    }

  /* PISEL: select TxIN physical pin (per-channel values in static instance) */

  modreg32(priv->pisel_val, priv->pisel_mask, GPT12_REG(GPT12_PISEL_OFFSET));

  /* T3CON: timer mode, BPS1=div4, T3I prescaler (hw field = T3I_cfg + 4).
   * T3R omitted; set in start().  Idempotent when both channels register.
   */

  putreg32(GPT12_T3CON_T3M_TIMER
           | GPT12_T3CON_BPS1_DIV4
           | (((CONFIG_TC3X_GPT1_CAP_T3I + 4u) << GPT12_T3CON_T3I_SHIFT)
              & GPT12_T3CON_T3I_MASK),
           GPT12_REG(GPT12_T3CON_OFFSET));

  /* TxCON: Capture Mode, armed for rising edge.
   * Full register write keeps CLRT3EN (T4CON bit11) cleared.
   */

  putreg32(GPT12_TXCON_TXM_CAPTURE
           | ((GPT12_TXCON_TXI_PIN_POS << GPT12_TXCON_TXI_SHIFT)
              & GPT12_TXCON_TXI_MASK),
           GPT12_REG(priv->con_offset));

  /* f_T3 = (fSPB/4) >> (T3I + 4); hardware T3I field = T3I_cfg + 4 */

  priv->f_hz = GPT12_GPT1_CAP_FGPT_HZ >> (CONFIG_TC3X_GPT1_CAP_T3I + 4u);

  putreg32(0u, GPT12_REG(GPT12_T3_OFFSET));

  return gpt12_cap_do_register(priv, minor);
}

#endif /* CONFIG_TC3X_GPT1_CAP */

/****************************************************************************
 * Name: gpt12_cap_start
 *
 *   ops.start() dispatcher: routes to GPT1 or GPT2 path via priv->gpt1.
 *
 ****************************************************************************/

static int gpt12_cap_start(FAR struct cap_lowerhalf_s *lower)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

#ifdef CONFIG_TC3X_GPT1_CAP
  if (priv->gpt1)
    {
      return gpt12_gpt1_cap_start_hw(priv);
    }
#endif

#ifdef CONFIG_TC3X_GPT2_CAP
  return gpt12_gpt2_cap_start_hw(priv);
#else
  return -ENODEV;
#endif
}

/****************************************************************************
 * Name: gpt12_cap_stop
 *
 *   ops.stop() dispatcher: mirrors gpt12_cap_start().
 *
 ****************************************************************************/

static int gpt12_cap_stop(FAR struct cap_lowerhalf_s *lower)
{
  FAR struct tc3x_gpt12_cap_s *priv =
    (FAR struct tc3x_gpt12_cap_s *)lower;

#ifdef CONFIG_TC3X_GPT1_CAP
  if (priv->gpt1)
    {
      return gpt12_gpt1_cap_stop_hw(priv);
    }
#endif

#ifdef CONFIG_TC3X_GPT2_CAP
  return gpt12_gpt2_cap_stop_hw(priv);
#else
  return -ENODEV;
#endif
}

/****************************************************************************
 * Name: tc3x_gpt12_cap_register
 *
 *   Unified capture registration entry point.  Dispatches to the
 *   appropriate lower-half based on minor number:
 *
 *     minor == 0          -> CAPIN  (GPT2 T5/CAPREL, CT3=0)
 *     minor == 2 or 4     -> TxIN   (GPT1 T3/T2 or T3/T4)
 *     minor == 3          -> T3IN   (GPT2 T5/CAPREL, CT3=1)
 *
 ****************************************************************************/

int tc3x_gpt12_cap_register(int minor)
{
  int ret;

  ret = gpt12_cap_clk_enable();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_TC3X_GPT1_CAP
  if (minor == 2 || minor == 4)
    {
      return gpt12_gpt1_cap_register(minor);
    }
#endif

#ifdef CONFIG_TC3X_GPT2_CAP
#  ifdef CONFIG_TC3X_GPT2_CAP_T3IN
  if (minor == 3)
    {
      return gpt12_t3in_register(minor);
    }
#  endif
  return gpt12_capin_register(minor);
#else
  cperr("ERROR: CAPIN not enabled; unsupported minor %d\n", minor);
  return -ENODEV;
#endif
}
