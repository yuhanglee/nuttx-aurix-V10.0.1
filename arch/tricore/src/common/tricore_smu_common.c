/****************************************************************************
 * arch/tricore/src/common/tricore_smu_common.c
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

#include <nuttx/arch.h>
#include <nuttx/bits.h>
#include <nuttx/irq.h>

#include "tricore_internal.h"
#include "tricore_smu_common.h"

/****************************************************************************
 * Pre-processor Definitions
 *
 * CMD/DBG/STS field layouts are identical across TC3xx and TC4xx.
 ****************************************************************************/

#define SMU_CMD_CMD_MASK   GENMASK(3, 0)
#define SMU_CMD_ARG_SHIFT  4u
#define SMU_CMD_ARG_MASK   GENMASK(11, 4)

#define SMU_CMD_RTSTOP     0x4u
#define SMU_CMD_ASCE       0x5u  /* Enable alarm status clear */

#define SMU_STS_ASCE       BIT(9)  /* Alarm Status Clear Enable */

#define SMU_DBG_SSM_MASK   GENMASK(1, 0)
#define SMU_DBG_SSM_SHIFT  0u
#define SMU_DBG_SSM_FAULT  0x2u

#define SMU_STS_RTME0      BIT(17)
#define SMU_STS_RTME1      BIT(19)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static void (*g_smu_cb)(void *arg);
static void  *g_smu_cb_arg;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void aurix_smu_write_cmd(uintptr_t cmd_reg, uint8_t cmd, uint8_t arg)
{
  uint32_t val;

  val = ((uint32_t)cmd & SMU_CMD_CMD_MASK) |
        (((uint32_t)arg << SMU_CMD_ARG_SHIFT) & SMU_CMD_ARG_MASK);
  putreg32(val, cmd_reg);
}

bool aurix_smu_in_fault_state(uintptr_t dbg_reg)
{
  uint32_t ssm;

  ssm = (getreg32(dbg_reg) & SMU_DBG_SSM_MASK) >> SMU_DBG_SSM_SHIFT;
  return ssm == SMU_DBG_SSM_FAULT;
}

int aurix_smu_rt_stop(uintptr_t cmd_reg, uint8_t rt_num)
{
  if (rt_num > 1u)
    {
      return -EINVAL;
    }

  aurix_smu_write_cmd(cmd_reg, SMU_CMD_RTSTOP, rt_num);
  return OK;
}

bool aurix_smu_rt_missed(uintptr_t sts_reg, uint8_t rt_num)
{
  uint32_t bit;

  if (rt_num > 1u)
    {
      return false;
    }

  bit = (rt_num == 0u) ? SMU_STS_RTME0 : SMU_STS_RTME1;
  return (getreg32(sts_reg) & bit) != 0u;
}

int aurix_smu_register_cb(void (*cb)(void *arg), void *arg)
{
  irqstate_t flags;

  flags = enter_critical_section();
  g_smu_cb     = cb;
  g_smu_cb_arg = arg;
  leave_critical_section(flags);
  return OK;
}

void aurix_smu_invoke_cb(void)
{
  if (g_smu_cb != NULL)
    {
      g_smu_cb(g_smu_cb_arg);
    }
}

int aurix_smu_clear_all_alarms(const aurix_smu_clear_all_ops_t *ops)
{
  uint8_t   group;
  uint32_t  status;
  uint32_t  sts;

  for (group = 0u; group < ops->ngroups; group++)
    {
      status = ops->get_status(group, ops->arg);
      if (status == 0u)
        {
          continue;
        }

      /* Clear AEX bits; alarms can re-latch after startup. */

      ops->write_aexclr(status, ops->arg);

      ops->write_cmd(SMU_CMD_ASCE, 0u, ops->arg);
      sts = ops->read_sts(ops->arg);
      if (!(sts & SMU_STS_ASCE))
        {
          /* ASCE command rejected -- SMU is in FAULT state. */

          return -EPERM;
        }

      ops->write_status(status, group, ops->arg);
    }

  return OK;
}
