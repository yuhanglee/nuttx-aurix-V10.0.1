/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397xa-3v3-tft/src/tc397_amp_secondary.c
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


#include <nuttx/config.h>
#include <stdint.h>

#ifndef CPU
#  define CPU 0
#endif

#define AMP_MBOX_BASE      0x900FF100U
#define AMP_MBOX_SLOT(n)   (AMP_MBOX_BASE + ((n) * 16U))
#define AMP_MBOX_SIG(n)    (AMP_MBOX_SLOT(n) + 0)
#define AMP_MBOX_CTR(n)    (AMP_MBOX_SLOT(n) + 4)
#define AMP_MBOX_SUM(n)    (AMP_MBOX_SLOT(n) + 8)

int amp_secondary_main(int argc, char *argv[])
{
  volatile uint32_t *sig = (volatile uint32_t *)AMP_MBOX_SIG(CPU);
  volatile uint32_t *ctr = (volatile uint32_t *)AMP_MBOX_CTR(CPU);
  volatile uint32_t *sum = (volatile uint32_t *)AMP_MBOX_SUM(CPU);
  uint32_t counter = 0;
  uint32_t accum   = 0;

  *sig = 0xCAFE0000U | (uint32_t)CPU;

  for (;;)
    {
      counter++;
      accum += counter * 3U + 7U;

      *ctr = counter;
      *sum = accum;
    }

  return 0;
}
