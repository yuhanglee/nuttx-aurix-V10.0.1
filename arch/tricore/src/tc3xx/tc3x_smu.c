/****************************************************************************
 * arch/tricore/src/tc3xx/tc3x_smu.c
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

/* SMU_core driver for the Infineon AURIX TC3xx family.
 *
 * When CONFIG_TC3XX_SMU_IRQ is selected, IGCS0/1/2 alarms are routed to
 * SRC_SMU0/1/2.  A single per-device callback handles all alarm events.
 *
 * Without CONFIG_TC3XX_SMU_IRQ, WDT alarms directly trigger a chip reset
 * via the SMU hardware with no CPU involvement.
 */

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
#include "hardware/tc3xx_smu.h"
#include "hardware/tc3x_wdt.h"
#include "tricore_smu_common.h"

#ifdef CONFIG_TC3XX_SMU_IRQ
#  include <arch/irq.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AGC IGCS routing values (CPU0 interrupt lines 1/2/3). */

#define SMU_AGC_IGCS0_ROUTE  0x1u
#define SMU_AGC_IGCS1_ROUTE  0x2u
#define SMU_AGC_IGCS2_ROUTE  0x4u

/****************************************************************************
 * Private Functions
 ****************************************************************************/

extern void aurix_safety_endinit_enable(bool enable);

/* Thin wrapper so call sites can keep the flat smu_write_cmd() syntax. */

static inline void smu_write_cmd(uint8_t cmd, uint8_t arg)
{
  aurix_smu_write_cmd(TC3X_SMU_CORE_BASE + TC3X_SMU_CMD_OFFSET, cmd, arg);
}

#ifdef CONFIG_TC3XX_SMU_IRQ

int tc3x_smu_register_callback(tc3x_smu_alarm_cb_t callback, void *arg)
{
  return aurix_smu_register_cb((void (*)(void *))callback, arg);
}

#endif /* CONFIG_TC3XX_SMU_IRQ */

/* Unlock SMU config registers (KEYS=0xBC).
 * Caller must have Safety ENDINIT disabled before calling.
 */

static int tc3x_smu_unlock(void)
{
  putreg32(SMU_KEYS_CFGLCK_UNLOCK,
           TC3X_SMU_CORE_BASE + TC3X_SMU_KEYS_OFFSET);
  return OK;
}

/* Temporarily lock SMU config (KEYS=0x00).
 * Caller must have Safety ENDINIT disabled before calling.
 */

static int tc3x_smu_lock(void)
{
  putreg32(0u, TC3X_SMU_CORE_BASE + TC3X_SMU_KEYS_OFFSET);
  return OK;
}

/* Send SMU_Start() to transition SSM from START to RUN. */

static int tc3x_smu_start(void)
{
  smu_write_cmd(SMU_CMD_START, 0u);
  return OK;
}

static inline bool tc3x_smu_in_fault_state(void)
{
  return aurix_smu_in_fault_state(TC3X_SMU_CORE_BASE + TC3X_SMU_DBG_OFFSET);
}

/* Return the 32-bit AGi alarm status register for the given group. */

uint32_t tc3x_smu_get_alarm_status(uint8_t group)
{
  if (group >= TC3X_SMU_NGROUPS)
    {
      return 0u;
    }

  return getreg32(TC3X_SMU_CORE_BASE + TC3X_SMU_AG_OFFSET(group));
}

/* Clear one alarm status bit (ASCE command + W1C write, Safety ENDINIT
 * disabled around the AG register write as required by TC3xx architecture).
 */

int tc3x_smu_clear_alarm_bit(uint8_t group, uint8_t alarm)
{
  irqstate_t flags;

  if (group >= TC3X_SMU_NGROUPS || alarm >= TC3X_SMU_ALARMS_PER_GRP)
    {
      return -EINVAL;
    }

  /* Clear AEX first so the alarm can re-latch after this clear. */

  putreg32(BIT(alarm), TC3X_SMU_CORE_BASE + TC3X_SMU_AEXCLR_OFFSET);

  flags = enter_critical_section();
  smu_write_cmd(SMU_CMD_ASCE, 0u);
  aurix_safety_endinit_enable(false);
  putreg32(BIT(alarm), TC3X_SMU_CORE_BASE + TC3X_SMU_AG_OFFSET(group));
  aurix_safety_endinit_enable(true);
  leave_critical_section(flags);
  return OK;
}

/* Set the reaction code for one alarm bit in AGiCF0/CF1/CF2.
 * The 3-bit react code is split across three registers at bit position
 * [alarm]: CF0 holds bit0, CF1 holds bit1, CF2 holds bit2 of the code.
 * One Safety ENDINIT window covers unlock, CF writes, optional RT duration
 * update, and lock -- matching the pattern used in the Aurix Zephyr driver.
 */

static int tc3x_smu_config_alarm(uint8_t group, uint8_t alarm, uint8_t react)
{
  uintptr_t cf0_addr;
  uintptr_t cf1_addr;
  uintptr_t cf2_addr;
  uint32_t  mask;
  irqstate_t flags;

  if (group >= TC3X_SMU_NGROUPS || alarm >= TC3X_SMU_ALARMS_PER_GRP)
    {
      return -EINVAL;
    }

  cf0_addr = TC3X_SMU_CORE_BASE + TC3X_SMU_AGCF_OFFSET(group, 0);
  cf1_addr = TC3X_SMU_CORE_BASE + TC3X_SMU_AGCF_OFFSET(group, 1);
  cf2_addr = TC3X_SMU_CORE_BASE + TC3X_SMU_AGCF_OFFSET(group, 2);
  mask = BIT(alarm);

  flags = enter_critical_section();
  aurix_safety_endinit_enable(false);
  tc3x_smu_unlock();

  if (react & BIT(0))
    {
      modreg32(mask, mask, cf0_addr);
    }
  else
    {
      modreg32(0u, mask, cf0_addr);
    }

  if (react & BIT(1))
    {
      modreg32(mask, mask, cf1_addr);
    }
  else
    {
      modreg32(0u, mask, cf1_addr);
    }

  if (react & BIT(2))
    {
      modreg32(mask, mask, cf2_addr);
    }
  else
    {
      modreg32(0u, mask, cf2_addr);
    }

  /* Extend RT duration while KEYS is still unlocked and ENDINIT disabled. */

  if ((react == SMU_REACT_IGCS0 ||
       react == SMU_REACT_IGCS1 ||
       react == SMU_REACT_IGCS2) &&
      !(getreg32(TC3X_SMU_CORE_BASE + TC3X_SMU_STS_OFFSET) &
        (SMU_STS_RTS0 | SMU_STS_RTS1)))
    {
      modreg32((0xfffffu << SMU_RTC_RTD_SHIFT) & SMU_RTC_RTD_MASK,
               SMU_RTC_RTD_MASK,
               TC3X_SMU_CORE_BASE + TC3X_SMU_RTC_OFFSET);
    }

  tc3x_smu_lock();
  aurix_safety_endinit_enable(true);
  leave_critical_section(flags);

  return OK;
}

int tc3x_smu_rt_stop(uint8_t rt_num)
{
  return aurix_smu_rt_stop(TC3X_SMU_CORE_BASE + TC3X_SMU_CMD_OFFSET,
                            rt_num);
}

static inline bool tc3x_smu_rt_missed(uint8_t rt_num)
{
  return aurix_smu_rt_missed(TC3X_SMU_CORE_BASE + TC3X_SMU_STS_OFFSET,
                              rt_num);
}

/* Helper functions for aurix_smu_clear_all_alarms callback interface */

static uint32_t tc3x_clear_get_status(uint8_t group, void *arg)
{
  UNUSED(arg);
  return tc3x_smu_get_alarm_status(group);
}

static void tc3x_clear_write_aexclr(uint32_t mask, void *arg)
{
  UNUSED(arg);
  putreg32(mask, TC3X_SMU_CORE_BASE + TC3X_SMU_AEXCLR_OFFSET);
}

static int tc3x_clear_write_cmd(uint8_t cmd, uint8_t cmdarg, void *arg)
{
  UNUSED(arg);
  smu_write_cmd(cmd, cmdarg);
  return OK;
}

static uint32_t tc3x_clear_read_sts(void *arg)
{
  UNUSED(arg);
  return getreg32(TC3X_SMU_CORE_BASE + TC3X_SMU_STS_OFFSET);
}

static void tc3x_clear_write_status(uint32_t mask, uint8_t group, void *arg)
{
  UNUSED(arg);
  putreg32(mask, TC3X_SMU_CORE_BASE + TC3X_SMU_AG_OFFSET(group));
}

/* Clear all latched alarm bits across all 12 groups. */

static int smu_clear_all_alarms(void)
{
  aurix_smu_clear_all_ops_t ops =
    {
      .ngroups = TC3X_SMU_NGROUPS,
      .get_status = tc3x_clear_get_status,
      .write_aexclr = tc3x_clear_write_aexclr,
      .write_cmd = tc3x_clear_write_cmd,
      .read_sts = tc3x_clear_read_sts,
      .write_status = tc3x_clear_write_status,
      .arg = NULL,
    };

  return aurix_smu_clear_all_alarms(&ops);
}

/* Enable or disable Recovery Timer instance RT0/RT1. */

static int tc3x_smu_rt_enable(uint8_t rt_num, bool enable)
{
  uint32_t bit;
  irqstate_t flags;

  if (rt_num > 1u)
    {
      return -EINVAL;
    }

  bit = (rt_num == 0u) ? SMU_RTC_RT0E : SMU_RTC_RT1E;

  flags = enter_critical_section();
  aurix_safety_endinit_enable(false);
  tc3x_smu_unlock();

  modreg32(enable ? bit : 0u, bit,
           TC3X_SMU_CORE_BASE + TC3X_SMU_RTC_OFFSET);

  tc3x_smu_lock();
  aurix_safety_endinit_enable(true);
  leave_critical_section(flags);
  return OK;
}

/* Inject a software alarm by global index (group * 32 + bit).
 * Used for testing the ISR dispatch path without a real hardware alarm.
 */

static int tc3x_smu_emulate_alarm(uint8_t alarm_idx)
{
  smu_write_cmd(SMU_CMD_ALARM, alarm_idx);
  return OK;
}

#ifdef CONFIG_TC3XX_SMU_IRQ

/* ISR shared by SRC_SMU0/1/2 (IGCS0/1/2). */

static int tc3x_smu_irq_handler(int irq, void *context, void *arg)
{
  aurix_smu_invoke_cb();
  return OK;
}

#endif /* CONFIG_TC3XX_SMU_IRQ */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Initialize SMU_core: ungate clock, clear stale alarms, configure WDT
 * alarm reactions, and attach IRQ handlers.
 */

int tc3x_smu_initialize(void)
{
  irqstate_t flags;

  /* On the second consecutive SMU-triggered Application Reset the
   * hardware enters permanent reset until a cold power cycle.
   * CONFIG_TC3XX_SMU_DISABLE_DOUBLE_RESET_GUARD clears WDTSCON1.CLRIRF
   * to disable this guard.
   */

#ifdef CONFIG_TC3XX_SMU_DISABLE_DOUBLE_RESET_GUARD
  flags = enter_critical_section();
  aurix_safety_endinit_enable(false);

  /* Set CLRIRF (bit 1) in WDTSCON1 to clear the double-reset flag.
   * Read-modify-write to preserve DR (bit 4) and all other bits --
   * a plain write would clear DR and re-enable the Safety WDT.
   */

  modreg32(BIT(1), BIT(1), TC3X_WDTS_CON1);
  aurix_safety_endinit_enable(true);
  leave_critical_section(flags);
#endif

#ifdef CONFIG_TC3XX_WDT

  /* SMU_REACT_RESET (application reset) does not reset the SMU module.
   * After a WDT-triggered reset the SMU stays in FAULT state with alarm
   * bits latched.  Try to clear them; if ASCE is rejected (FAULT state),
   * log the condition and continue -- the alarm reaction is still armed.
   */

  if (smu_clear_all_alarms() < 0)
    {
      syslog(LOG_WARNING,
             "SMU: ASCE command not acknowledged --"
             " some alarm bits may remain latched\n");
    }

  /* Configure WDT alarm reactions from Kconfig (TC3XX_SMU_WDT_REACTION).
   * AG8[10..12] = CPU0..2 WDT timeout (all TC3xx).
   * AG8[13..15] = CPU3..5 WDT timeout (TC397 only, 6-core).
   * AG8[16]     = Safety WDT timeout.
   * Each tc3x_smu_config_alarm() call manages its own ENDINIT+KEYS window.
   */

  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU0_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU1_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU2_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
#  ifdef CONFIG_TC3XX_CHIP_TC397
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU3_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU4_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_CPU5_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);
#  endif
  tc3x_smu_config_alarm(TC3X_SMU_WDT_AG, TC3X_SMU_WDT_SAFETY_BIT,
                         CONFIG_TC3XX_SMU_WDT_REACTION);

#  ifdef CONFIG_TC3XX_SMU_IRQ
  /* Route all three IGCS lines to separate CPU0 interrupt vectors.
   * IGCS0 -> SRC_SMU0 (IRQ 564), IGCS1 -> SRC_SMU1 (565),
   * IGCS2 -> SRC_SMU2 (566).
   */

  flags = enter_critical_section();
  aurix_safety_endinit_enable(false);
  tc3x_smu_unlock();
  putreg32(
    ((SMU_AGC_IGCS0_ROUTE << SMU_AGC_IGCS0_SHIFT) & SMU_AGC_IGCS0_MASK) |
    ((SMU_AGC_IGCS1_ROUTE << SMU_AGC_IGCS1_SHIFT) & SMU_AGC_IGCS1_MASK) |
    ((SMU_AGC_IGCS2_ROUTE << SMU_AGC_IGCS2_SHIFT) & SMU_AGC_IGCS2_MASK),
    TC3X_SMU_CORE_BASE + TC3X_SMU_AGC_OFFSET);
  tc3x_smu_lock();
  aurix_safety_endinit_enable(true);
  leave_critical_section(flags);
#  endif /* CONFIG_TC3XX_SMU_IRQ */

  /* SMU_Start transitions START -> RUN.  After an application reset the
   * SMU remains in RUN or FAULT state -- calling SMU_Start from those
   * states is not valid, so skip it.
   */

  if (((getreg32(TC3X_SMU_CORE_BASE + TC3X_SMU_DBG_OFFSET) &
        SMU_DBG_SSM_MASK) >> SMU_DBG_SSM_SHIFT) == SMU_DBG_SSM_START)
    {
      tc3x_smu_start();
    }

#endif /* CONFIG_TC3XX_WDT */

#ifdef CONFIG_TC3XX_SMU_IRQ
  /* Attach the shared ISR to all three IGCS interrupt lines. */

  irq_attach(TC3XX_IRQ_SMU0, tc3x_smu_irq_handler, NULL);
  irq_attach(TC3XX_IRQ_SMU1, tc3x_smu_irq_handler, NULL);
  irq_attach(TC3XX_IRQ_SMU2, tc3x_smu_irq_handler, NULL);
  up_enable_irq(TC3XX_IRQ_SMU0);
  up_enable_irq(TC3XX_IRQ_SMU1);
  up_enable_irq(TC3XX_IRQ_SMU2);
#endif

  return OK;
}

#ifdef CONFIG_AURIX_SMU

/* TC3x lower-half ops -- thin wrappers over the flat tc3x_smu_* API. */

static int
tc3x_op_start(FAR struct aurix_smu_lowerhalf_s *lower)
{
  return tc3x_smu_start();
}

static uint32_t
tc3x_op_getstate(FAR struct aurix_smu_lowerhalf_s *lower)
{
  return tc3x_smu_in_fault_state() ? SMU_STATE_FAULT : SMU_STATE_RUN;
}

static uint32_t
tc3x_op_getalarmstatus(FAR struct aurix_smu_lowerhalf_s *lower,
                        uint8_t group)
{
  return tc3x_smu_get_alarm_status(group);
}

static int
tc3x_op_clearalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                    uint8_t group, uint8_t alarm)
{
  return tc3x_smu_clear_alarm_bit(group, alarm);
}

static int
tc3x_op_configalarm(FAR struct aurix_smu_lowerhalf_s *lower,
                     uint8_t group, uint8_t alarm, uint8_t react)
{
  return tc3x_smu_config_alarm(group, alarm, react);
}

static int
tc3x_op_rt_stop(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  return tc3x_smu_rt_stop(rt_num);
}

static bool
tc3x_op_rt_missed(FAR struct aurix_smu_lowerhalf_s *lower, uint8_t rt_num)
{
  return tc3x_smu_rt_missed(rt_num);
}

static int
tc3x_op_emulatealarm(FAR struct aurix_smu_lowerhalf_s *lower,
                      uint8_t alarm_idx)
{
  return tc3x_smu_emulate_alarm(alarm_idx);
}

static const struct aurix_smu_ops_s g_tc3x_smu_ops =
{
  .start           = tc3x_op_start,
  .getstate        = tc3x_op_getstate,
  .getalarmstatus  = tc3x_op_getalarmstatus,
  .clearalarm      = tc3x_op_clearalarm,
  .configalarm     = tc3x_op_configalarm,
  .rt_stop         = tc3x_op_rt_stop,
  .rt_missed       = tc3x_op_rt_missed,
  .emulatealarm    = tc3x_op_emulatealarm,
};

static struct aurix_smu_lowerhalf_s g_tc3x_smu_lower =
{
  .ops  = &g_tc3x_smu_ops,
  .priv = NULL,
};

FAR struct aurix_smu_lowerhalf_s *tc3x_smu_lowerhalf(void)
{
  return &g_tc3x_smu_lower;
}

#endif /* CONFIG_AURIX_SMU */