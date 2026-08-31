/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4x_smu.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_SMU_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_SMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/bits.h>

/* Forward declaration */

struct aurix_smu_lowerhalf_s;

/****************************************************************************
 * Base addresses
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMU_SAFE0_BASE
#  define TC4X_SMU_SAFE0_BASE   CONFIG_TC4X_SMU_SAFE0_BASE
#else
#  define TC4X_SMU_SAFE0_BASE   0xF0029000u
#endif

#ifdef CONFIG_TC4X_SMU_SAFE1_BASE
#  define TC4X_SMU_SAFE1_BASE   CONFIG_TC4X_SMU_SAFE1_BASE
#else
#  define TC4X_SMU_SAFE1_BASE   0xF0029500u
#endif

#define TC4X_SMU_NSAFE          2u

#define TC4X_SMU_SAFE_BASE(n) \
  (((n) == 0u) ? TC4X_SMU_SAFE0_BASE : TC4X_SMU_SAFE1_BASE)

/****************************************************************************
 * SMU_SAFE register offsets (per partition)
 ****************************************************************************/

#define TC4X_SMU_CMD_OFFSET     0x000u  /* Command Register                */
#define TC4X_SMU_STS_OFFSET     0x004u  /* Status Register                 */
#define TC4X_SMU_AGC_OFFSET     0x014u  /* Alarm Global Configuration      */
#define TC4X_SMU_RTC_OFFSET     0x018u  /* Recovery Timer Configuration    */
#define TC4X_SMU_KEYS_OFFSET    0x01Cu  /* Key Register (config lock)      */
#define TC4X_SMU_DBG_OFFSET     0x020u  /* Debug Register (SSM state)      */
#define TC4X_SMU_AEXCLR_OFFSET  0x054u  /* Alarm Executed Status Clear     */

#define TC4X_SMU_AGSF_BASE      0x080u
#define TC4X_SMU_AGSF_STRIDE    0x024u

#define TC4X_SMU_AGSF_GROUP_BASE(group) \
  (TC4X_SMU_AGSF_BASE + (uint32_t)(group) * TC4X_SMU_AGSF_STRIDE)

#define TC4X_SMU_AGSF_CON_OFFSET(group, cf) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + (uint32_t)(cf) * 0x004u)

#define TC4X_SMU_AGSF_FSP0_OFFSET(group) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + 0x010u)

#define TC4X_SMU_AGSF_FSP1_OFFSET(group) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + 0x014u)

#define TC4X_SMU_AGSF_STS_OFFSET(group) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + 0x018u)

#define TC4X_SMU_AGSF_DBG_OFFSET(group) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + 0x01Cu)

#define TC4X_SMU_AGSF_CAS_OFFSET(group) \
  (TC4X_SMU_AGSF_GROUP_BASE(group) + 0x020u)

#define TC4X_SMU_AGSF_STS_BASE  (TC4X_SMU_AGSF_BASE + 0x018u)

/****************************************************************************
 * SMU_SAFE dimensions -- 17 alarm groups, 32 bits each, 4 CON regs/grp
 ****************************************************************************/

#define TC4X_SMU_NGROUPS        17u
#define TC4X_SMU_ALARMS_PER_GRP 32u
#define TC4X_SMU_NCON           4u

/* WDT overflow alarms occupy SMU_SAFE0 alarm group 8. */

#define TC4X_SMU_WDT_AG          8u
#define TC4X_SMU_WDT_CPU0_BIT    0u
#define TC4X_SMU_WDT_CPU1_BIT    1u
#define TC4X_SMU_WDT_CPU2_BIT    2u
#define TC4X_SMU_WDT_CPU3_BIT    3u
#define TC4X_SMU_WDT_CPU4_BIT    4u
#define TC4X_SMU_WDT_CPU5_BIT    5u
#define TC4X_SMU_WDT_SAFETY_BIT  6u

/****************************************************************************
 * CMD register (offset 0x000)
 ****************************************************************************/


#define SMU_CMD_CMD_SHIFT       0
#define SMU_CMD_CMD_MASK        GENMASK(3, 0)
#define SMU_CMD_ARG_SHIFT       4
#define SMU_CMD_ARG_MASK        GENMASK(11, 4)

#define SMU_CMD_START           0x0u  /* SMU_Start                        */
#define SMU_CMD_ACTIVATE_FSP    0x1u  /* Activate Fault Signaling Protocol*/
#define SMU_CMD_RELEASE_FSP     0x2u  /* Release Fault Signaling Protocol */
#define SMU_CMD_ACTIVATE_PES    0x3u  /* Activate Prewarning ES           */
#define SMU_CMD_RTSTOP          0x4u  /* SMU_RTStop(ARG=timer_instance)   */
#define SMU_CMD_ASCE            0x5u  /* Enable alarm status clear        */
#define SMU_CMD_ALARM           0x6u  /* SMU_Alarm(ARG=alarm_index)       */
#define SMU_CMD_ALIVE_TEST      0x7u  /* Alive test command               */

/****************************************************************************
 * STS register (offset 0x004)
 ****************************************************************************/

#define SMU_STS_CMD_SHIFT       0
#define SMU_STS_CMD_MASK        GENMASK(3, 0)
#define SMU_STS_ARG_SHIFT       4
#define SMU_STS_ARG_MASK        GENMASK(7, 4)
#define SMU_STS_ASCE            BIT(9)
#define SMU_STS_RTS0            BIT(16)
#define SMU_STS_RTME0           BIT(17)
#define SMU_STS_RTS1            BIT(18)
#define SMU_STS_RTME1           BIT(19)

/****************************************************************************
 * DBG register (offset 0x020)
 ****************************************************************************/

#define SMU_DBG_SSM_SHIFT       0
#define SMU_DBG_SSM_MASK        GENMASK(1, 0)
#define SMU_DBG_SSM_START       0x0u
#define SMU_DBG_SSM_RUN         0x1u
#define SMU_DBG_SSM_FAULT       0x2u

/****************************************************************************
 * KEYS register (offset 0x01C)
 ****************************************************************************/


#define SMU_KEYS_CFGLCK_SHIFT   0
#define SMU_KEYS_CFGLCK_MASK    GENMASK(7, 0)

#define SMU_KEYS_CFGLCK_UNLOCK  0xBCu   /* Temporary unlock                */
#define SMU_KEYS_CFGLCK_LOCK    0xFFu   /* Permanent lock (PERLCK)         */

/****************************************************************************
 * AGC -- Alarm Global Configuration (offset 0x014)
 ****************************************************************************/

#define SMU_AGC_IGCS0_SHIFT     0
#define SMU_AGC_IGCS0_MASK      GENMASK(2, 0)
#define SMU_AGC_IGCS1_SHIFT     4
#define SMU_AGC_IGCS1_MASK      GENMASK(6, 4)
#define SMU_AGC_IGCS2_SHIFT     8
#define SMU_AGC_IGCS2_MASK      GENMASK(10, 8)

/****************************************************************************
 * RTC -- Recovery Timer Configuration (offset 0x030)
 ****************************************************************************/

#define SMU_RTC_RT0E            BIT(0)
#define SMU_RTC_RT1E            BIT(1)
#define SMU_RTC_RTD_SHIFT       8
#define SMU_RTC_RTD_MASK        GENMASK(31, 8)

/****************************************************************************
 * Alarm reaction codes (4-bit, split across CON0..CON3).  Low three
 * bits match the shared aurix_smu upper-half reaction encoding.
 ****************************************************************************/

#define TC4X_SMU_REACT_NONE     0x0u
#define TC4X_SMU_REACT_IGCS0    0x2u  /* Interrupt set 0 (IRQ0) */
#define TC4X_SMU_REACT_IGCS1    0x3u  /* Interrupt set 1 (IRQ1) */
#define TC4X_SMU_REACT_IGCS2    0x4u  /* Interrupt set 2 (IRQ2) */
#define TC4X_SMU_REACT_NMI      0x5u
#define TC4X_SMU_REACT_RESET    0x6u  /* Reset request to SMM   */
#define TC4X_SMU_REACT_CPURST   0x7u  /* CPU reset only         */

/****************************************************************************
 * SMM double-reset guard
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMM_RSTSTATCLR
#  define TC4X_SMM_RSTSTATCLR   CONFIG_TC4X_SMM_RSTSTATCLR
#else
#  define TC4X_SMM_RSTSTATCLR   0xF0060110u
#endif

#define TC4X_SMM_CLRSMUIRF      BIT(16)

/****************************************************************************
 * SMM Reset Trigger Control Register A (RSTTRIGCTRLA)
 ****************************************************************************/

#define TC4X_SMM_RSTTRIGCTRLA       0xF0060114u

#define TC4X_SMM_RSTTRIGCTRLC       0xF006011Cu

#define TC4X_SMM_SMUSAFE0AR_SHIFT   9u
#define TC4X_SMM_SMUSAFE0AR_MASK    (0x7u << TC4X_SMM_SMUSAFE0AR_SHIFT)
#define TC4X_SMM_SMUSAFE0SR_SHIFT   12u
#define TC4X_SMM_SMUSAFE0SR_MASK    (0x7u << TC4X_SMM_SMUSAFE0SR_SHIFT)

#define TC4X_SMM_RSTRIG_NORESET     0x0u
#define TC4X_SMM_RSTRIG_APPRST      0x2u  /* Application Reset encoding */
#define TC4X_SMM_RSTRIG_SYSRST      0x1u  /* System Reset encoding      */

/****************************************************************************
 * SMM PROT registers
 ****************************************************************************/

#define TC4X_SMM_PROTE              0xF006000Cu
#define TC4X_SMM_PROTCSE            0xF0060014u
#define TC4X_SMM_ACCENCS_WRA        0xF0060038u
#define TC4X_SMM_ACCENCS_WRB        0xF006003Cu
#define TC4X_SMM_PROTCE             0xF0060058u

/* PROT state-machine encoding (STATE field is bits 2:0). */

#define TC4X_SMM_PROT_STATE_INIT    0x0u  /* Unlocked */
#define TC4X_SMM_PROT_STATE_CONFIG  0x1u  /* Unlocked */
#define TC4X_SMM_PROT_STATE_RUN     0x4u  /* Locked   */

#define TC4X_SMM_PROT_SWEN          BIT(3)  /* State-write enable */
#define TC4X_SMM_PROT_OWEN          BIT(31) /* Ownership-write enable */

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_TC4X_SMU_IRQ

typedef void (*tc4x_smu_alarm_cb_t)(void *arg);

#endif /* CONFIG_TC4X_SMU_IRQ */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int      tc4x_smu_initialize(void);
int      tc4x_smu_unlock(uint8_t inst);
int      tc4x_smu_lock(uint8_t inst);
int      tc4x_smu_start(uint8_t inst);
bool     tc4x_smu_in_fault_state(uint8_t inst);
uint32_t tc4x_smu_get_alarm_status(uint8_t inst, uint8_t group);
int      tc4x_smu_clear_alarm_bit(uint8_t inst, uint8_t group,
                                   uint8_t alarm);
int      tc4x_smu_config_alarm(uint8_t inst, uint8_t group, uint8_t alarm,
                                uint8_t react);
int      tc4x_smu_rt_stop(uint8_t inst, uint8_t rt_num);
int      tc4x_smu_rt_enable(uint8_t inst, uint8_t rt_num, bool enable);
bool     tc4x_smu_rt_missed(uint8_t inst, uint8_t rt_num);
int      tc4x_smu_emulate_alarm(uint8_t inst, uint8_t alarm_idx);

#ifdef CONFIG_TC4X_SMU_IRQ
int      tc4x_smu_register_callback(tc4x_smu_alarm_cb_t callback, void *arg);
#endif

#ifdef CONFIG_AURIX_SMU
FAR struct aurix_smu_lowerhalf_s *tc4x_smu_lowerhalf(void);
FAR struct aurix_smu_lowerhalf_s *tc4x_smu1_lowerhalf(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4X_SMU_H */
