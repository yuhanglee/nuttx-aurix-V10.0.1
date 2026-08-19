/****************************************************************************
 * arch/tricore/src/common/tricore_smu_common.h
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

/* Common AURIX SMU helpers shared by TC3xx and TC4xx lower-half drivers.
 *
 * All functions are parameterised by the resolved register address so the
 * caller selects the correct base address and offset for its hardware
 * variant.
 */

#ifndef __ARCH_TRICORE_SRC_COMMON_TRICORE_SMU_COMMON_H
#define __ARCH_TRICORE_SRC_COMMON_TRICORE_SMU_COMMON_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Write cmd + arg to the SMU CMD register. */

void aurix_smu_write_cmd(uintptr_t cmd_reg, uint8_t cmd, uint8_t arg);

/* Return true when the SSM state machine is in FAULT state. */

bool aurix_smu_in_fault_state(uintptr_t dbg_reg);

/* Send SMU_RTStop for the given timer instance (0 or 1).
 * Returns -EINVAL for rt_num > 1.
 */

int aurix_smu_rt_stop(uintptr_t cmd_reg, uint8_t rt_num);

/* Return true when the RT expired without being acknowledged (RTME set). */

bool aurix_smu_rt_missed(uintptr_t sts_reg, uint8_t rt_num);

/* Register the per-device alarm callback (thread-safe). */

int aurix_smu_register_cb(void (*cb)(void *arg), void *arg);

/* Invoke the registered alarm callback if one is set. */

void aurix_smu_invoke_cb(void);

/* Operations struct for clearing all alarms. */

typedef struct {
  uint8_t ngroups;                                    /* Number of alarm groups */
  uint32_t (*get_status)(uint8_t group, void *arg);   /* Get status for a group */
  void (*write_aexclr)(uint32_t mask, void *arg);     /* Write to AEXCLR register */
  int (*write_cmd)(uint8_t cmd, uint8_t cmdarg,
                   void *arg);                         /* Write command */
  uint32_t (*read_sts)(void *arg);                    /* Read STS register */
  void (*write_status)(uint32_t mask, uint8_t group,
                       void *arg);                     /* Write status register */
  void *arg;                                          /* Private argument for callbacks */
} aurix_smu_clear_all_ops_t;

/* Clear all latched alarm bits.
 * Pulse-type alarms clear after reset; level-type alarms cannot clear
 * while the hardware error condition persists.
 * Returns -EPERM if ASCE is rejected (alarm input still asserted).
 */

int aurix_smu_clear_all_alarms(const aurix_smu_clear_all_ops_t *ops);

#endif /* __ARCH_TRICORE_SRC_COMMON_TRICORE_SMU_COMMON_H */
