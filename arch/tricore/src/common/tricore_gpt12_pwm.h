/****************************************************************************
 * arch/tricore/src/common/tricore_gpt12_pwm.h
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

#ifndef __ARCH_TRICORE_SRC_COMMON_TRICORE_GPT12_PWM_H
#define __ARCH_TRICORE_SRC_COMMON_TRICORE_GPT12_PWM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>

#include <nuttx/timers/pwm.h>

/* Include active chip's GPT12 register definitions and GPT12_REG() accessor. */

#if defined(CONFIG_TC3XX_GPT1_PWM) || defined(CONFIG_TC3XX_GPT2_PWM)
#  include "../tc3xx/hardware/tc3xx_gpt12.h"
#  define GPT12_BASE  CONFIG_TC3XX_GPT120_BASE
#  ifndef GPT12_REG
#    define GPT12_REG(off)  (GPT12_BASE + (off))
#  endif
#elif defined(CONFIG_TC4XX_GPT1_PWM) || defined(CONFIG_TC4XX_GPT2_PWM)
#  include "../tc4xx/hardware/tc4xx_gpt12.h"
#  define GPT12_BASE  CONFIG_TC4XX_GPT120_BASE
#  ifndef GPT12_REG
#    define GPT12_REG(off)  TC4XX_GPT12_REG(GPT12_BASE, (off))
#  endif
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GPT12_GPT1_MINOR       0
#define GPT12_GPT2_MINOR       1
#define GPT12_DEVPATH_LEN      12u
#define GPT12_COUNT_MAX        UINT16_MAX
#define GPT12_TI_FIELD_MAX     7u
#define GPT12_PERIOD_MIN_TICKS 2u
#define GPT12_T6_PRIME_VALUE   ((uint32_t)GPT12_COUNT_MAX - 1u)
#define GPT12_CLC_TIMEOUT      100000u

/* Enable aliases — defined when the block is enabled on either chip. */

#if defined(CONFIG_TC3XX_GPT1_PWM) || defined(CONFIG_TC4XX_GPT1_PWM)
#  define GPT12_GPT1_PWM_ENABLED
#endif

#if defined(CONFIG_TC3XX_GPT2_PWM) || defined(CONFIG_TC4XX_GPT2_PWM)
#  define GPT12_GPT2_PWM_ENABLED
#endif

/* SRN aliases — map chip-specific Kconfig SRN names to common symbols. */

#if defined(CONFIG_TC3XX_GPT1_PWM)
#  define GPT12_GPT1_T3_SRN  CONFIG_TC3XX_GPT1_PWM_T3_SRN
#elif defined(CONFIG_TC4XX_GPT1_PWM)
#  define GPT12_GPT1_T3_SRN  CONFIG_TC4XX_GPT1_PWM_T3_SRN
#endif

#if defined(CONFIG_TC3XX_GPT2_PWM)
#  define GPT12_GPT2_T6_SRN  CONFIG_TC3XX_GPT2_PWM_T6_SRN
#elif defined(CONFIG_TC4XX_GPT2_PWM)
#  define GPT12_GPT2_T6_SRN  CONFIG_TC4XX_GPT2_PWM_T6_SRN
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* base must be first for upcast to struct pwm_lowerhalf_s. */

struct gpt12_pwm_s
{
  struct pwm_lowerhalf_s  base;        /* must be first for upcast */
  uint8_t                 block;       /* 0 = GPT1 (T3), 1 = GPT2 (T6) */
  int                     srn;         /* NuttX IRQ number for this block */
  uint32_t                con_offset;  /* TxCON register offset */
  uint32_t                cnt_offset;  /* Tx count register offset */
  bool                    running;
  uint32_t                caprel_high; /* GPT2: CAPREL reload for high phase */
  uint32_t                caprel_low;  /* GPT2: CAPREL reload for low phase */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* State array and ops table defined in tricore_gpt12_pwm.c. */

extern struct gpt12_pwm_s g_gpt12_pwm[2];
extern const struct pwm_ops_s g_gpt12_pwm_ops;

/* Chip-provided: fSPB clock and BPS register values (steps 0..3).
 * BPS1/BPS2 share the same encoding, so one table covers both blocks.
 */

extern const uint32_t g_gpt12_fspb_hz;
extern const uint32_t g_bps_table[4];

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Common implementations in tricore_gpt12_pwm.c. */

void gpt12_calc_ticks(uint32_t period_ticks, uint32_t duty_b16,
                      FAR uint32_t *high_out, FAR uint32_t *low_out);
int  gpt1_t3_isr(int irq, FAR void *context, FAR void *arg);
int  gpt12_do_register(int minor);
int  gpt12_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                     FAR const struct pwm_info_s *info);
int  gpt12_pwm_stop(FAR struct pwm_lowerhalf_s *dev);

#ifdef GPT12_GPT1_PWM_ENABLED
int  gpt12_gpt1_pwm_register(int minor);
#endif

#ifdef GPT12_GPT2_PWM_ENABLED
int  gpt12_gpt2_pwm_register(int minor);
#endif

#endif /* __ARCH_TRICORE_SRC_COMMON_TRICORE_GPT12_PWM_H */
