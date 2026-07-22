/****************************************************************************
 * drivers/misc/aurix_smu.c
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

/* Upper-half character driver for the Infineon AURIX SMU.
 *
 * Dispatches ioctl commands to a lower-half driver via an ops vtable.
 * The upper half is hardware-agnostic; the lower half implements
 * struct aurix_smu_ops_s and is bound at registration time.
 *
 * Registration:
 *   aurix_smu_register("/dev/smu0", lower);
 *
 * Usage (from NSH or a user task):
 *   int fd = open("/dev/smu0", O_RDWR);
 *   ioctl(fd, SMUIOC_GETSTATE, (unsigned long)&state);
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include "aurix_smu.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aurix_smu_upperhalf_s
{
  uint8_t  crefs;                              /* Open reference count     */
  mutex_t  lock;                               /* Serialises ioctls        */
  FAR struct aurix_smu_lowerhalf_s *lower;     /* Bound lower-half driver  */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     smu_open (FAR struct file *filep);
static int     smu_close(FAR struct file *filep);
static int     smu_ioctl(FAR struct file *filep,
                          int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_smu_fops =
{
  smu_open,   /* open  */
  smu_close,  /* close */
  NULL,       /* read  -- control-only device, no data to read */
  NULL,       /* write -- control-only device, use ioctl */
  NULL,       /* seek  */
  smu_ioctl,  /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int smu_open(FAR struct file *filep)
{
  FAR struct aurix_smu_upperhalf_s *upper =
    (FAR struct aurix_smu_upperhalf_s *)filep->f_inode->i_private;
  int ret;

  ret = nxmutex_lock(&upper->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (upper->crefs == UINT8_MAX)
    {
      ret = -EMFILE;
    }
  else
    {
      upper->crefs++;
      ret = OK;
    }

  nxmutex_unlock(&upper->lock);
  return ret;
}

static int smu_close(FAR struct file *filep)
{
  FAR struct aurix_smu_upperhalf_s *upper =
    (FAR struct aurix_smu_upperhalf_s *)filep->f_inode->i_private;
  int ret;

  ret = nxmutex_lock(&upper->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (upper->crefs > 0)
    {
      upper->crefs--;
    }

  nxmutex_unlock(&upper->lock);
  return OK;
}

static int smu_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct aurix_smu_upperhalf_s *upper =
    (FAR struct aurix_smu_upperhalf_s *)filep->f_inode->i_private;
  FAR struct aurix_smu_lowerhalf_s *lower = upper->lower;
  int ret;

  ret = nxmutex_lock(&upper->lock);
  if (ret < 0)
    {
      return ret;
    }

  switch (cmd)
    {
    case SMUIOC_START:
      ret = lower->ops->start(lower);
      break;

    case SMUIOC_GETSTATE:
      {
        FAR uint32_t *state = (FAR uint32_t *)(uintptr_t)arg;
        if (state == NULL)
          {
            ret = -EINVAL;
            break;
          }

        *state = lower->ops->getstate(lower);
        ret = OK;
      }
      break;

    case SMUIOC_GETALARMSTATUS:
      {
        FAR struct aurix_smu_alarmstatus_s *as =
          (FAR struct aurix_smu_alarmstatus_s *)(uintptr_t)arg;
        if (as == NULL)
          {
            ret = -EINVAL;
            break;
          }

        as->status = lower->ops->getalarmstatus(lower, as->group);
        ret = OK;
      }
      break;

    case SMUIOC_CLEARALARM:
      {
        FAR struct aurix_smu_alarmbit_s *ab =
          (FAR struct aurix_smu_alarmbit_s *)(uintptr_t)arg;
        if (ab == NULL)
          {
            ret = -EINVAL;
            break;
          }

        ret = lower->ops->clearalarm(lower, ab->group, ab->alarm);
      }
      break;

    case SMUIOC_CONFIGALARM:
      {
        FAR struct aurix_smu_alarmcfg_s *ac =
          (FAR struct aurix_smu_alarmcfg_s *)(uintptr_t)arg;
        if (ac == NULL)
          {
            ret = -EINVAL;
            break;
          }

        ret = lower->ops->configalarm(lower, ac->group, ac->alarm,
                                      ac->react);
      }
      break;

    case SMUIOC_RT_STOP:
      ret = lower->ops->rt_stop(lower, (uint8_t)(arg & 0xffu));
      break;

    case SMUIOC_RT_MISSED:
      {
        FAR struct aurix_smu_rtmissed_s *rm =
          (FAR struct aurix_smu_rtmissed_s *)(uintptr_t)arg;
        if (rm == NULL)
          {
            ret = -EINVAL;
            break;
          }

        rm->missed = lower->ops->rt_missed(lower, rm->rt_num);
        ret = OK;
      }
      break;

    case SMUIOC_EMULATEALARM:
      ret = lower->ops->emulatealarm(lower, (uint8_t)(arg & 0xffu));
      break;

    default:
      ret = -ENOTTY;
      break;
    }

  nxmutex_unlock(&upper->lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int aurix_smu_register(FAR const char *path,
                        FAR struct aurix_smu_lowerhalf_s *lower)
{
  FAR struct aurix_smu_upperhalf_s *upper;

  upper = (FAR struct aurix_smu_upperhalf_s *)
    kmm_zalloc(sizeof(struct aurix_smu_upperhalf_s));
  if (upper == NULL)
    {
      return -ENOMEM;
    }

  nxmutex_init(&upper->lock);
  upper->crefs = 0;
  upper->lower = lower;

  return register_driver(path, &g_smu_fops, 0600, upper);
}
