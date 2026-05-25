/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_endinit.c
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

/****************************************************************************
 * TC4xx ENDINIT control
 *
 * Removes or restores ENDINIT protection on peripheral registers by
 * manipulating the per-CPU or safety WDT CTRLA/CTRLB register pair via
 * the hardware password sequence (invert low PW nibble, clear LCK, write
 * CTRLB.DR, then re-lock).
 *
 ****************************************************************************/

#include <stdbool.h>
#include <arch/arch.h>
#include <nuttx/bits.h>
#include "tricore_internal.h"
#include "hardware/tc4x_wdt.h"

#define CTRLA_LCK       BIT(0)
#define CTRLA_PW_MASK   (0x7f << 1)
#define CTRLB_DR        BIT(0)

#define WDT_CPU_STRIDE  0x30u
#define WDT_CTRLA_OFF   TC4X_WDT_CTRLA_OFFSET
#define WDT_CTRLB_OFF   TC4X_WDT_CTRLB_OFFSET

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void endinit_set(uintptr_t ctrla_addr, bool enable)
{
  uint32_t ctrla;

  /* Password access: invert low PW bits, clear LCK */

  ctrla = getreg32(ctrla_addr);
  if (ctrla & CTRLA_LCK)
    {
      ctrla &= ~CTRLA_LCK;
      ctrla ^= CTRLA_PW_MASK;
      putreg32(ctrla, ctrla_addr);
    }

  /* Write CTRLB: DR=1 disables watchdog (removes ENDINIT protection),
   *              DR=0 re-enables watchdog (restores ENDINIT protection).
   */

  putreg32(enable ? 0u : CTRLB_DR,
           ctrla_addr + WDT_CTRLB_OFF - WDT_CTRLA_OFF);

  /* Lock to apply */

  ctrla = getreg32(ctrla_addr);
  ctrla |= CTRLA_LCK;
  putreg32(ctrla, ctrla_addr);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void aurix_cpu_endinit_enable(bool enable)
{
  uint32_t core_id;

  IFX_MFCR(IFX_CPU_CORE_ID, core_id);
  core_id &= IFX_CPU_CORE_ID_MASK;

  endinit_set(TC4X_WDTCPU0_BASE + WDT_CTRLA_OFF + (core_id * WDT_CPU_STRIDE),
             enable);
}

void aurix_safety_endinit_enable(bool enable)
{
  endinit_set(TC4X_WDTSYS_BASE + WDT_CTRLA_OFF, enable);
}
