/****************************************************************************
 * arch/tricore/src/tc3xx/hardware/tc3xx_smu.h
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

#ifndef __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3XX_SMU_H
#define __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3XX_SMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/bits.h>
#ifdef CONFIG_AURIX_SMU
#  include "aurix_smu.h"
#endif

/****************************************************************************
 * Base addresses
 *
 * SMU_core base is the same across the TC3xx family.
 * SMU_stdby is device-variant specific; the value below is for TC375.
 ****************************************************************************/

#define TC3X_SMU_CORE_BASE      0xF0036800u

/****************************************************************************
 * SMU_core register offsets
 ****************************************************************************/

#define TC3X_SMU_CMD_OFFSET     0x020u  /* Command Register */
#define TC3X_SMU_STS_OFFSET     0x024u  /* Status Register */
#define TC3X_SMU_AGC_OFFSET     0x02Cu  /* Alarm Global Configuration */
#define TC3X_SMU_RTC_OFFSET     0x030u  /* Recovery Timer Configuration */
#define TC3X_SMU_KEYS_OFFSET    0x034u  /* Key Register (config lock/unlock) */
#define TC3X_SMU_DBG_OFFSET     0x038u  /* Debug Register (SSM state) */
#define TC3X_SMU_AEXCLR_OFFSET  0x074u  /* Alarm Executed Status Clear Register */

/* AGiCFj: reaction config for alarm group i (0..11), register j (0..2).
 * Safety ENDINIT protected.
 */

#define TC3X_SMU_AGCF_OFFSET(i, j) \
  (0x100u + (uint32_t)(i) * 0x0Cu + (uint32_t)(j) * 0x004u)

/* AGi: Alarm Status Register for group i (0..11). W1C when ASCE=1. */

#define TC3X_SMU_AG_OFFSET(i)       (0x1C0u + (uint32_t)(i) * 0x004u)

/****************************************************************************
 * SMU_core dimensions — 12 alarm groups, 32 bits each
 ****************************************************************************/

#define TC3X_SMU_NGROUPS        12u
#define TC3X_SMU_ALARMS_PER_GRP 32u

/* SMU alarm group 8: WDT timeout alarms (TC3xx Part1 UM, SMU Alarm Map). */

#define TC3X_SMU_WDT_AG         8u   /* AG8 contains all WDT alarms          */
#define TC3X_SMU_WDT_CPU0_BIT  10u   /* AG8[10]: CPU0 WDT timeout            */
#define TC3X_SMU_WDT_CPU1_BIT  11u   /* AG8[11]: CPU1 WDT timeout            */
#define TC3X_SMU_WDT_CPU2_BIT  12u   /* AG8[12]: CPU2 WDT timeout            */
#define TC3X_SMU_WDT_CPU3_BIT  13u   /* AG8[13]: CPU3 WDT timeout (TC397)    */
#define TC3X_SMU_WDT_CPU4_BIT  14u   /* AG8[14]: CPU4 WDT timeout (TC397)    */
#define TC3X_SMU_WDT_CPU5_BIT  15u   /* AG8[15]: CPU5 WDT timeout (TC397)    */
#define TC3X_SMU_WDT_SAFETY_BIT 16u  /* AG8[16]: Safety WDT timeout          */

/****************************************************************************
 * CLC register (offset 0x000)
 ****************************************************************************/

/****************************************************************************
 * CMD register (offset 0x020)
 ****************************************************************************/

#define SMU_CMD_CMD_SHIFT       0
#define SMU_CMD_CMD_MASK        GENMASK(3, 0)
#define SMU_CMD_ARG_SHIFT       4
#define SMU_CMD_ARG_MASK        GENMASK(7, 4)

#define SMU_CMD_START           0x0u  /* SMU_Start */
#define SMU_CMD_RTSTOP          0x4u  /* SMU_RTStop(ARG=timer_instance) */
#define SMU_CMD_ASCE            0x5u  /* SMU_ASCE -- enable alarm status clear */
#define SMU_CMD_ALARM           0x6u  /* SMU_Alarm(ARG=alarm_index) */

/****************************************************************************
 * STS register (offset 0x024)
 ****************************************************************************/

#define SMU_STS_CMD_SHIFT       0     /* Last command received (rwh) */
#define SMU_STS_CMD_MASK        GENMASK(3, 0)
#define SMU_STS_ARG_SHIFT       4     /* Last command argument (rwh) */
#define SMU_STS_ARG_MASK        GENMASK(7, 4)
#define SMU_STS_ASCE            BIT(9)  /* Alarm Status Clear Enable (rwh) */
#define SMU_STS_RTS0            BIT(16) /* Recovery Timer 0 running (rwh) */
#define SMU_STS_RTME0           BIT(17) /* Recovery Timer 0 missed event (rwh) */
#define SMU_STS_RTS1            BIT(18) /* Recovery Timer 1 running (rwh) */
#define SMU_STS_RTME1           BIT(19) /* Recovery Timer 1 missed event (rwh) */

/****************************************************************************
 * DBG register (offset 0x038)
 ****************************************************************************/

#define SMU_DBG_SSM_SHIFT       0
#define SMU_DBG_SSM_MASK        GENMASK(1, 0)
#define SMU_DBG_SSM_START       0x0u
#define SMU_DBG_SSM_FAULT       0x2u

/****************************************************************************
 * KEYS register (offset 0x034)
 ****************************************************************************/

#define SMU_KEYS_CFGLCK_SHIFT   0
#define SMU_KEYS_CFGLCK_MASK    GENMASK(7, 0)

#define SMU_KEYS_CFGLCK_UNLOCK  0xBCu

/****************************************************************************
 * AGC — Alarm Global Configuration (offset 0x02C)
 ****************************************************************************/

#define SMU_AGC_IGCS0_SHIFT     0
#define SMU_AGC_IGCS0_MASK      GENMASK(2, 0)
#define SMU_AGC_IGCS1_SHIFT     4
#define SMU_AGC_IGCS1_MASK      GENMASK(6, 4)
#define SMU_AGC_IGCS2_SHIFT     8
#define SMU_AGC_IGCS2_MASK      GENMASK(10, 8)

/****************************************************************************
 * RTC — Recovery Timer Configuration (offset 0x030) — Safety ENDINIT
 ****************************************************************************/

#define SMU_RTC_RT0E            BIT(0)
#define SMU_RTC_RT1E            BIT(1)
#define SMU_RTC_RTD_SHIFT       8
#define SMU_RTC_RTD_MASK        GENMASK(31, 8)

/****************************************************************************
 * Alarm reaction codes for AGiCFj
 * 3-bit value split across CF0/CF1/CF2 at the alarm bit position.
 ****************************************************************************/

#define SMU_REACT_IGCS0         0x2u  /* Interrupt set 0 */
#define SMU_REACT_IGCS1         0x3u  /* Interrupt set 1 */
#define SMU_REACT_IGCS2         0x4u  /* Interrupt set 2 */
#define SMU_REACT_RESET         0x6u  /* Reset request to SCU */

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_TC3XX_SMU_IRQ

/* Alarm callback type.  Called from interrupt context for any SMU alarm. */

typedef void (*tc3x_smu_alarm_cb_t)(void *arg);

#endif /* CONFIG_TC3XX_SMU_IRQ */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int      tc3x_smu_initialize(void);
uint32_t tc3x_smu_get_alarm_status(uint8_t group);
int      tc3x_smu_clear_alarm_bit(uint8_t group, uint8_t alarm);
int      tc3x_smu_rt_stop(uint8_t rt_num);

#ifdef CONFIG_TC3XX_SMU_IRQ
int      tc3x_smu_register_callback(tc3x_smu_alarm_cb_t callback, void *arg);
#endif

#ifdef CONFIG_AURIX_SMU
FAR struct aurix_smu_lowerhalf_s *tc3x_smu_lowerhalf(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_TRICORE_SRC_TC3XX_HARDWARE_TC3XX_SMU_H */
