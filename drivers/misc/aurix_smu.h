/****************************************************************************
 * drivers/misc/aurix_smu.h
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

/* Minimal upper-half interface for the Infineon AURIX SMU. */

#ifndef __INCLUDE_NUTTX_MISC_AURIX_SMU_H
#define __INCLUDE_NUTTX_MISC_AURIX_SMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/fs/ioctl.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_AURIX_SMU

#define _SMUIOCBASE        (0x3b00)
#define _SMUIOC(nr)        _IOC(_SMUIOCBASE, (nr))

#define SMUIOC_START          _SMUIOC(0x01)
#define SMUIOC_GETSTATE       _SMUIOC(0x02)
#define SMUIOC_GETALARMSTATUS _SMUIOC(0x03)
#define SMUIOC_CLEARALARM     _SMUIOC(0x04)
#define SMUIOC_CONFIGALARM    _SMUIOC(0x05)
#define SMUIOC_RT_STOP        _SMUIOC(0x06)
#define SMUIOC_RT_MISSED      _SMUIOC(0x07)
#define SMUIOC_EMULATEALARM   _SMUIOC(0x08)

#define SMU_STATE_RUN    0u
#define SMU_STATE_FAULT  1u

#define SMU_REACT_NONE   0x0u
#define SMU_REACT_IGCS0  0x2u
#define SMU_REACT_IGCS1  0x3u
#define SMU_REACT_IGCS2  0x4u
#define SMU_REACT_NMI    0x5u
#define SMU_REACT_RESET  0x6u
#define SMU_REACT_CPURST 0x7u

struct aurix_smu_alarmstatus_s
{
  uint8_t  group;
  uint32_t status;
};

struct aurix_smu_alarmbit_s
{
  uint8_t group;
  uint8_t alarm;
};

struct aurix_smu_alarmcfg_s
{
  uint8_t group;
  uint8_t alarm;
  uint8_t react;
};

struct aurix_smu_rtmissed_s
{
  uint8_t rt_num;
  bool    missed;
};

/* Lower-half ops -- implemented by the arch-specific driver. */

struct aurix_smu_lowerhalf_s;

struct aurix_smu_ops_s
{
  int      (*start)(FAR struct aurix_smu_lowerhalf_s *lower);
  uint32_t (*getstate)(FAR struct aurix_smu_lowerhalf_s *lower);
  uint32_t (*getalarmstatus)(FAR struct aurix_smu_lowerhalf_s *lower,
                              uint8_t group);
  int      (*clearalarm)(FAR struct aurix_smu_lowerhalf_s *lower,
                          uint8_t group, uint8_t alarm);
  int      (*configalarm)(FAR struct aurix_smu_lowerhalf_s *lower,
                           uint8_t group, uint8_t alarm, uint8_t react);
  int      (*rt_stop)(FAR struct aurix_smu_lowerhalf_s *lower,
                       uint8_t rt_num);
  bool     (*rt_missed)(FAR struct aurix_smu_lowerhalf_s *lower,
                         uint8_t rt_num);
  int      (*emulatealarm)(FAR struct aurix_smu_lowerhalf_s *lower,
                            uint8_t alarm_idx);
};

struct aurix_smu_lowerhalf_s
{
  FAR const struct aurix_smu_ops_s *ops;  /* Must be first */
  FAR void                         *priv; /* Lower-half private data */
};

int aurix_smu_register(FAR const char *path,
                        FAR struct aurix_smu_lowerhalf_s *lower);

#endif /* CONFIG_AURIX_SMU */
#endif /* __INCLUDE_NUTTX_MISC_AURIX_SMU_H */
