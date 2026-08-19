/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_smu.c
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
#include <inttypes.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/bits.h>
#include <nuttx/irq.h>

#ifdef CONFIG_AURIX_SMU
#  include "aurix_smu.h"
#endif

#include "tricore_internal.h"
#include "hardware/tc4x_smu.h"
#include "hardware/tc4x_wdt.h"
#include "tricore_smu_common.h"

#ifdef CONFIG_TC4X_SMU_IRQ
#  include <arch/irq.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SMU_AGC_IGCS0_ROUTE     0x1u
#define SMU_AGC_IGCS1_ROUTE     0x2u
#define SMU_AGC_IGCS2_ROUTE     0x4u

#ifdef CONFIG_TC4X_SMU_SAFE_INST
#  define TC4X_SMU_DEFAULT_INST   ((uint8_t)CONFIG_TC4X_SMU_SAFE_INST)
#else
#  define TC4X_SMU_DEFAULT_INST   0u
#endif

#if CONFIG_TC4X_SMU_SAFE_INST == 1
#  define TC4X_SMU_IGCS0_IRQ  TC4X_IRQ_SMU_SAFE1_IGCS0
#  define TC4X_SMU_IGCS1_IRQ  TC4X_IRQ_SMU_SAFE1_IGCS1
#  define TC4X_SMU_IGCS2_IRQ  TC4X_IRQ_SMU_SAFE1_IGCS2
#else
#  define TC4X_SMU_IGCS0_IRQ  TC4X_IRQ_SMU_SAFE0_IGCS0
#  define TC4X_SMU_IGCS1_IRQ  TC4X_IRQ_SMU_SAFE0_IGCS1
#  define TC4X_SMU_IGCS2_IRQ  TC4X_IRQ_SMU_SAFE0_IGCS2
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMU_IRQ

/* Callback pointer stored in tricore_smu_common; only the registration
 * wrapper lives here to preserve the tc4x_smu_alarm_cb_t public type.
 */

#endif /* CONFIG_TC4X_SMU_IRQ */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uintptr_t smu_base(uint8_t inst)
{
  return TC4X_SMU_SAFE_BASE(inst);
}

static inline void smu_write_cmd(uint8_t inst, uint8_t cmd, uint8_t arg)
{
  aurix_smu_write_cmd(smu_base(inst) + TC4X_SMU_CMD_OFFSET, cmd, arg);
}

static void tc4x_smu_unlock_inline(uint8_t inst)
{
  putreg32(SMU_KEYS_CFGLCK_UNLOCK,
           smu_base(inst) + TC4X_SMU_KEYS_OFFSET);
}

static void tc4x_smu_lock_inline(uint8_t inst)
{
  putreg32(0u, smu_base(inst) + TC4X_SMU_KEYS_OFFSET);
}

static int tc4x_smu_start_cmd(uint8_t inst)
{
  smu_write_cmd(inst, SMU_CMD_START, 0u);
  return OK;
}

static bool tc4x_smu_in_fault_state_inline(uint8_t inst)
{
  return aurix_smu_in_fault_state(smu_base(inst) + TC4X_SMU_DBG_OFFSET);
}

/****************************************************************************
 * Public Functions -- KEYS wrappers
 ****************************************************************************/

int tc4x_smu_unlock(uint8_t inst)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return -EINVAL;
    }

  tc4x_smu_unlock_inline(inst);
  return OK;
}

int tc4x_smu_lock(uint8_t inst)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return -EINVAL;
    }

  tc4x_smu_lock_inline(inst);
  return OK;
}

/****************************************************************************
 * Public Functions -- Command wrappers
 ****************************************************************************/

int tc4x_smu_start(uint8_t inst)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return -EINVAL;
    }

  return tc4x_smu_start_cmd(inst);
}

int tc4x_smu_emulate_alarm(uint8_t inst, uint8_t alarm_idx)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return -EINVAL;
    }

  smu_write_cmd(inst, SMU_CMD_ALARM, alarm_idx);
  return OK;
}

/****************************************************************************
 * Public Functions -- Alarm status / clear
 ****************************************************************************/

bool tc4x_smu_in_fault_state(uint8_t inst)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return false;
    }

  return tc4x_smu_in_fault_state_inline(inst);
}

uint32_t tc4x_smu_get_alarm_status(uint8_t inst, uint8_t group)
{
  if (inst >= TC4X_SMU_NSAFE || group >= TC4X_SMU_NGROUPS)
    {
      return 0u;
    }

  return getreg32(smu_base(inst) + TC4X_SMU_AGSF_STS_OFFSET(group));
}

int tc4x_smu_clear_alarm_bit(uint8_t inst, uint8_t group, uint8_t alarm)
{
  irqstate_t flags;

  if (inst >= TC4X_SMU_NSAFE ||
      group >= TC4X_SMU_NGROUPS ||
      alarm >= TC4X_SMU_ALARMS_PER_GRP)
    {
      return -EINVAL;
    }

  /* Clear AEX first so the alarm can re-latch after this clear. */

  putreg32(BIT(alarm), smu_base(inst) + TC4X_SMU_AEXCLR_OFFSET);

  flags = enter_critical_section();
  smu_write_cmd(inst, SMU_CMD_ASCE, 0u);
  putreg32(BIT(alarm),
           smu_base(inst) + TC4X_SMU_AGSF_STS_OFFSET(group));
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions -- Alarm reaction configuration
 ****************************************************************************/

int tc4x_smu_config_alarm(uint8_t inst, uint8_t group, uint8_t alarm,
                          uint8_t react)
{
  uintptr_t base;
  uintptr_t con_addr;
  uint32_t  con;
  uint32_t  mask;
  uint8_t   j;
  irqstate_t flags;

  if (inst >= TC4X_SMU_NSAFE ||
      group >= TC4X_SMU_NGROUPS ||
      alarm >= TC4X_SMU_ALARMS_PER_GRP)
    {
      return -EINVAL;
    }

  base = smu_base(inst);
  mask = BIT(alarm);

  flags = enter_critical_section();
  tc4x_smu_unlock_inline(inst);

  for (j = 0u; j < TC4X_SMU_NCON; j++)
    {
      con_addr = base + TC4X_SMU_AGSF_CON_OFFSET(group, j);
      con      = getreg32(con_addr);

      if (((uint32_t)react >> j) & 0x1u)
        {
          con |= mask;
        }
      else
        {
          con &= ~mask;
        }

      putreg32(con, con_addr);
    }

  /* Extend the RT timeout and route IGCS reactions to separate IRQ lines. */

  if (react == TC4X_SMU_REACT_IGCS0 ||
      react == TC4X_SMU_REACT_IGCS1 ||
      react == TC4X_SMU_REACT_IGCS2)
    {
      uint32_t sts = getreg32(base + TC4X_SMU_STS_OFFSET);

      if (!(sts & (SMU_STS_RTS0 | SMU_STS_RTS1)))
        {
          uint32_t rtc = getreg32(base + TC4X_SMU_RTC_OFFSET);

          rtc &= ~SMU_RTC_RTD_MASK;
          rtc |= (0xfffffu << SMU_RTC_RTD_SHIFT) & SMU_RTC_RTD_MASK;
          putreg32(rtc, base + TC4X_SMU_RTC_OFFSET);
        }

      uint32_t agc = getreg32(base + TC4X_SMU_AGC_OFFSET);

      agc &= ~(SMU_AGC_IGCS0_MASK | SMU_AGC_IGCS1_MASK |
               SMU_AGC_IGCS2_MASK);
      agc |= ((SMU_AGC_IGCS0_ROUTE << SMU_AGC_IGCS0_SHIFT) &
              SMU_AGC_IGCS0_MASK) |
             ((SMU_AGC_IGCS1_ROUTE << SMU_AGC_IGCS1_SHIFT) &
              SMU_AGC_IGCS1_MASK) |
             ((SMU_AGC_IGCS2_ROUTE << SMU_AGC_IGCS2_SHIFT) &
              SMU_AGC_IGCS2_MASK);
      putreg32(agc, base + TC4X_SMU_AGC_OFFSET);
    }

  tc4x_smu_lock_inline(inst);
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions -- Recovery Timers
 ****************************************************************************/

int tc4x_smu_rt_stop(uint8_t inst, uint8_t rt_num)
{
  if (inst >= TC4X_SMU_NSAFE)
    {
      return -EINVAL;
    }

  return aurix_smu_rt_stop(smu_base(inst) + TC4X_SMU_CMD_OFFSET, rt_num);
}

bool tc4x_smu_rt_missed(uint8_t inst, uint8_t rt_num)
{
  if (inst >= TC4X_SMU_NSAFE || rt_num > 1u)
    {
      return false;
    }

  return aurix_smu_rt_missed(smu_base(inst) + TC4X_SMU_STS_OFFSET, rt_num);
}

/* Enable or disable Recovery Timer RT0 or RT1 via RTC.RTnE. */

int tc4x_smu_rt_enable(uint8_t inst, uint8_t rt_num, bool enable)
{
  uintptr_t  rtc_addr;
  uint32_t   rtc;
  uint32_t   bit;
  irqstate_t flags;

  if (inst >= TC4X_SMU_NSAFE || rt_num > 1u)
    {
      return -EINVAL;
    }

  bit      = (rt_num == 0u) ? SMU_RTC_RT0E : SMU_RTC_RT1E;
  rtc_addr = smu_base(inst) + TC4X_SMU_RTC_OFFSET;

  flags = enter_critical_section();
  tc4x_smu_unlock_inline(inst);

  rtc = getreg32(rtc_addr);
  if (enable)
    {
      rtc |= bit;
    }
  else
    {
      rtc &= ~bit;
    }

  putreg32(rtc, rtc_addr);

  tc4x_smu_lock_inline(inst);
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions -- Callback registration
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMU_IRQ

int tc4x_smu_register_callback(tc4x_smu_alarm_cb_t callback, void *arg)
{
  return aurix_smu_register_cb((void (*)(void *))callback, arg);
}

#endif /* CONFIG_TC4X_SMU_IRQ */

/****************************************************************************
 * Private Functions -- Bulk alarm clear (used at init)
 ****************************************************************************/

/* Helper functions for aurix_smu_clear_all_alarms callback interface */

static uint32_t tc4x_clear_get_status(uint8_t group, void *arg)
{
  uint8_t inst = (uintptr_t)arg;
  return tc4x_smu_get_alarm_status(inst, group);
}

static void tc4x_clear_write_aexclr(uint32_t mask, void *arg)
{
  uint8_t inst = (uintptr_t)arg;
  putreg32(mask, smu_base(inst) + TC4X_SMU_AEXCLR_OFFSET);
}

static int tc4x_clear_write_cmd(uint8_t cmd, uint8_t cmdarg, void *arg)
{
  uint8_t inst = (uintptr_t)arg;
  smu_write_cmd(inst, cmd, cmdarg);
  return OK;
}

static uint32_t tc4x_clear_read_sts(void *arg)
{
  uint8_t inst = (uintptr_t)arg;
  return getreg32(smu_base(inst) + TC4X_SMU_STS_OFFSET);
}

static void tc4x_clear_write_status(uint32_t mask, uint8_t group, void *arg)
{
  uint8_t inst = (uintptr_t)arg;
  putreg32(mask, smu_base(inst) + TC4X_SMU_AGSF_STS_OFFSET(group));
}

/* Clear all latched alarm bits.
 * Returns -EPERM if ASCE is rejected (SMU still in START state).
 */

static int smu_clear_all_alarms(uint8_t inst)
{
  aurix_smu_clear_all_ops_t ops =
    {
      .ngroups = TC4X_SMU_NGROUPS,
      .get_status = tc4x_clear_get_status,
      .write_aexclr = tc4x_clear_write_aexclr,
      .write_cmd = tc4x_clear_write_cmd,
      .read_sts = tc4x_clear_read_sts,
      .write_status = tc4x_clear_write_status,
      .arg = (void *)(uintptr_t)inst,
    };

  return aurix_smu_clear_all_alarms(&ops);
}

/****************************************************************************
 * Private Functions -- IRQ handling
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMU_IRQ

/* Determine which SAFE instance triggered the IRQ. */
static uint8_t tc4x_irq_to_instance(int irq)
{
  if (irq >= TC4X_IRQ_SMU_SAFE0_IGCS0 && irq <= TC4X_IRQ_SMU_SAFE0_IGCS2)
    {
      return 0u;
    }
  if (irq >= TC4X_IRQ_SMU_SAFE1_IGCS0 && irq <= TC4X_IRQ_SMU_SAFE1_IGCS2)
    {
      return 1u;
    }
  return TC4X_SMU_DEFAULT_INST;
}

static int tc4x_smu_irq_handler(int irq, void *context, void *arg)
{
  uint8_t inst;
  UNUSED(context);
  UNUSED(arg);

  inst = tc4x_irq_to_instance(irq);
  smu_write_cmd(inst, SMU_CMD_RTSTOP, 0u);
  smu_write_cmd(inst, SMU_CMD_RTSTOP, 1u);
  aurix_smu_invoke_cb();
  return OK;
}

#endif /* CONFIG_TC4X_SMU_IRQ */

/****************************************************************************
 * Public Functions -- Driver initialization
 ****************************************************************************/

int tc4x_smu_initialize(void)
{
#ifdef CONFIG_TC4X_WDT
  uint32_t ssm_val;
#endif
#ifdef CONFIG_TC4X_SMU_DISABLE_DOUBLE_RESET_GUARD
  /* Clear SMU reset-in-progress latch (dev/test only). */

  putreg32(TC4X_SMM_CLRSMUIRF, TC4X_SMM_RSTSTATCLR);
#endif

#ifdef CONFIG_TC4X_SMM_ARM_SMUSAFE0AR
  {
    uint32_t val;

    /* PowerOn default SMUSAFE0AR=000b drops RESET reactions; program to Application Reset. */
    val  = getreg32(TC4X_SMM_RSTTRIGCTRLA);
    val &= ~TC4X_SMM_SMUSAFE0AR_MASK;
    val |= ((uint32_t)TC4X_SMM_RSTRIG_APPRST << TC4X_SMM_SMUSAFE0AR_SHIFT);
    putreg32(val, TC4X_SMM_RSTTRIGCTRLA);

    syslog(LOG_INFO,
           "SMM: RSTTRIGCTRLA=0x%08" PRIx32 " (SMUSAFE0AR=AppReset)\n",
           getreg32(TC4X_SMM_RSTTRIGCTRLA));
  }
#endif

#ifdef CONFIG_TC4X_WDT
  /* Application reset does not reset the SMU; alarm bits remain latched. */
  if (smu_clear_all_alarms(TC4X_SMU_DEFAULT_INST) < 0)
    {
      syslog(LOG_WARNING,
             "SMU: ASCE command not acknowledged --"
             " some alarm bits may remain latched\n");
    }

  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU0_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU1_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU2_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU3_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU4_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_CPU5_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);
  tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, TC4X_SMU_WDT_AG,
                        TC4X_SMU_WDT_SAFETY_BIT,
                        CONFIG_TC4X_SMU_WDT_REACTION);

  /* SMU_Start is a no-op in RUN; silently ignored in FAULT (requires power-cycle). */
  ssm_val = (getreg32(smu_base(TC4X_SMU_DEFAULT_INST) +
                      TC4X_SMU_DBG_OFFSET) & SMU_DBG_SSM_MASK) >>
             SMU_DBG_SSM_SHIFT;

  if (ssm_val == SMU_DBG_SSM_START || ssm_val == SMU_DBG_SSM_FAULT)
    {
      tc4x_smu_start_cmd(TC4X_SMU_DEFAULT_INST);
    }

  if (ssm_val == SMU_DBG_SSM_FAULT)
    {
      syslog(LOG_WARNING,
             "SMU: SAFE%u latched in FAULT after prior alarm reset;"
             " full power-cycle required to re-arm alarm reactions\n",
             (unsigned)TC4X_SMU_DEFAULT_INST);
    }

#endif /* CONFIG_TC4X_WDT */

#ifdef CONFIG_TC4X_SMU_IRQ
  irq_attach(TC4X_SMU_IGCS0_IRQ, tc4x_smu_irq_handler, NULL);
  irq_attach(TC4X_SMU_IGCS1_IRQ, tc4x_smu_irq_handler, NULL);
  irq_attach(TC4X_SMU_IGCS2_IRQ, tc4x_smu_irq_handler, NULL);
  up_enable_irq(TC4X_SMU_IGCS0_IRQ);
  up_enable_irq(TC4X_SMU_IGCS1_IRQ);
  up_enable_irq(TC4X_SMU_IGCS2_IRQ);
#endif

  /* Bring SAFE1 to RUN state for software alarm injection on inst=1. */

  tc4x_smu_start_cmd(1u);

  return OK;
}

/****************************************************************************
 * Public Functions -- aurix_smu upper-half lower-half glue
 ****************************************************************************/

#ifdef CONFIG_AURIX_SMU

static int
tc4x_op_start(FAR struct aurix_smu_lowerhalf_s *lower)
{
  UNUSED(lower);
  return tc4x_smu_start(TC4X_SMU_DEFAULT_INST);
}

static uint32_t
tc4x_op_getstate(FAR struct aurix_smu_lowerhalf_s *lower)
{
  UNUSED(lower);
  return tc4x_smu_in_fault_state(TC4X_SMU_DEFAULT_INST) ?
           SMU_STATE_FAULT : SMU_STATE_RUN;
}

static uint32_t
tc4x_op_getalarmstatus(FAR struct aurix_smu_lowerhalf_s *lower,
                       uint8_t group)
{
  UNUSED(lower);
  return tc4x_smu_get_alarm_status(TC4X_SMU_DEFAULT_INST, group);
}

static int
tc4x_op_clearalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                   uint8_t group, uint8_t alarm)
{
  UNUSED(lower);
  return tc4x_smu_clear_alarm_bit(TC4X_SMU_DEFAULT_INST, group, alarm);
}

static int
tc4x_op_configalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                    uint8_t group, uint8_t alarm, uint8_t react)
{
  UNUSED(lower);
  return tc4x_smu_config_alarm(TC4X_SMU_DEFAULT_INST, group, alarm, react);
}

static int
tc4x_op_rt_stop(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  UNUSED(lower);
  return tc4x_smu_rt_stop(TC4X_SMU_DEFAULT_INST, rt_num);
}

static bool
tc4x_op_rt_missed(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  UNUSED(lower);
  return tc4x_smu_rt_missed(TC4X_SMU_DEFAULT_INST, rt_num);
}

static int
tc4x_op_emulatealarm(FAR struct aurix_smu_lowerhalf_s *lower,
                     uint8_t alarm_idx)
{
  UNUSED(lower);
  return tc4x_smu_emulate_alarm(TC4X_SMU_DEFAULT_INST, alarm_idx);
}

static const struct aurix_smu_ops_s g_tc4x_smu_ops =
{
  .start           = tc4x_op_start,
  .getstate        = tc4x_op_getstate,
  .getalarmstatus  = tc4x_op_getalarmstatus,
  .clearalarm      = tc4x_op_clearalarm,
  .configalarm     = tc4x_op_configalarm,
  .rt_stop         = tc4x_op_rt_stop,
  .rt_missed       = tc4x_op_rt_missed,
  .emulatealarm    = tc4x_op_emulatealarm,
};

static struct aurix_smu_lowerhalf_s g_tc4x_smu_lower =
{
  .ops  = &g_tc4x_smu_ops,
  .priv = NULL,
};

FAR struct aurix_smu_lowerhalf_s *tc4x_smu_lowerhalf(void)
{
  return &g_tc4x_smu_lower;
}

/* SAFE1 lower-half -- all ops route to inst=1. */

static int
tc4x1_op_start(FAR struct aurix_smu_lowerhalf_s *lower)
{
  UNUSED(lower);
  return tc4x_smu_start(1u);
}

static uint32_t
tc4x1_op_getstate(FAR struct aurix_smu_lowerhalf_s *lower)
{
  UNUSED(lower);
  return tc4x_smu_in_fault_state(1u) ? SMU_STATE_FAULT : SMU_STATE_RUN;
}

static uint32_t
tc4x1_op_getalarmstatus(FAR struct aurix_smu_lowerhalf_s *lower,
                        uint8_t group)
{
  UNUSED(lower);
  return tc4x_smu_get_alarm_status(1u, group);
}

static int
tc4x1_op_clearalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                    uint8_t group, uint8_t alarm)
{
  UNUSED(lower);
  return tc4x_smu_clear_alarm_bit(1u, group, alarm);
}

static int
tc4x1_op_configalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                     uint8_t group, uint8_t alarm, uint8_t react)
{
  UNUSED(lower);
  return tc4x_smu_config_alarm(1u, group, alarm, react);
}

static int
tc4x1_op_rt_stop(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  UNUSED(lower);
  return tc4x_smu_rt_stop(1u, rt_num);
}

static bool
tc4x1_op_rt_missed(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  UNUSED(lower);
  return tc4x_smu_rt_missed(1u, rt_num);
}

static int
tc4x1_op_emulatealarm(FAR struct aurix_smu_lowerhalf_s *lower,
                      uint8_t alarm_idx)
{
  UNUSED(lower);
  return tc4x_smu_emulate_alarm(1u, alarm_idx);
}

static const struct aurix_smu_ops_s g_tc4x_smu1_ops =
{
  .start           = tc4x1_op_start,
  .getstate        = tc4x1_op_getstate,
  .getalarmstatus  = tc4x1_op_getalarmstatus,
  .clearalarm      = tc4x1_op_clearalarm,
  .configalarm     = tc4x1_op_configalarm,
  .rt_stop         = tc4x1_op_rt_stop,
  .rt_missed       = tc4x1_op_rt_missed,
  .emulatealarm    = tc4x1_op_emulatealarm,
};

static struct aurix_smu_lowerhalf_s g_tc4x_smu1_lower =
{
  .ops  = &g_tc4x_smu1_ops,
  .priv = NULL,
};

FAR struct aurix_smu_lowerhalf_s *tc4x_smu1_lowerhalf(void)
{
  return &g_tc4x_smu1_lower;
}

#endif /* CONFIG_AURIX_SMU */
