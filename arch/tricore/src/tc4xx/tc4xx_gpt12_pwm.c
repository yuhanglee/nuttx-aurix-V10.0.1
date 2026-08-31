/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_gpt12_pwm.c
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
#include <errno.h>
#include <debug.h>

#include "hardware/tc4xx_gpt12.h"
#include "tc4xx_gpt12_pwm.h"
#include "tricore_gpt12_pwm.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* fSPB clock for the TC4xx GPT12 module. */

const uint32_t g_gpt12_fspb_hz = 80000000u;

/* BPS register values for steps 0..3.
 * BPS1 (GPT1) and BPS2 (GPT2) share the same encoding, one table covers both.
 * Divisor = (2 << i) * factor (factor=2 GPT1, factor=1 GPT2).
 */

const uint32_t g_bps_table[4] =
{
  GPT12_TCON_BPS_DIV0,
  GPT12_TCON_BPS_DIV1,
  GPT12_TCON_BPS_DIV2,
  GPT12_TCON_BPS_DIV3,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4xx_gpt12_pwm_register
 ****************************************************************************/

int tc4xx_gpt12_pwm_register(int minor)
{
#ifdef GPT12_GPT1_PWM_ENABLED
  if (minor == GPT12_GPT1_MINOR)
    {
      return gpt12_gpt1_pwm_register(minor);
    }
#endif

#ifdef GPT12_GPT2_PWM_ENABLED
  if (minor == GPT12_GPT2_MINOR)
    {
      return gpt12_gpt2_pwm_register(minor);
    }
#endif

  pwmerr("ERROR: minor %d not enabled\n", minor);
  return -EINVAL;
}
