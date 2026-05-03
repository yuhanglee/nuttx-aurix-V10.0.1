/****************************************************************************
 * boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_amp_secondary.c
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
 ****************************************************************************/


#include <nuttx/config.h>
#include <stdint.h>

#ifndef CPU
#  define CPU 0
#endif

static const uintptr_t g_cpu_dspr[6] =
{
  0x70000000U,
  0x60000000U,
  0x50000000U,
  0x40000000U,
  0x30000000U,
  0x20000000U,
};

#define AMP_MBOX_OFF       0x30000U
#define AMP_MBOX_SIG_ADDR  (g_cpu_dspr[CPU] + AMP_MBOX_OFF + 0)
#define AMP_MBOX_CTR_ADDR  (g_cpu_dspr[CPU] + AMP_MBOX_OFF + 4)
#define AMP_MBOX_SUM_ADDR  (g_cpu_dspr[CPU] + AMP_MBOX_OFF + 8)

int amp_secondary_main(int argc, char *argv[])
{
  volatile uint32_t *sig = (volatile uint32_t *)AMP_MBOX_SIG_ADDR;
  volatile uint32_t *ctr = (volatile uint32_t *)AMP_MBOX_CTR_ADDR;
  volatile uint32_t *sum = (volatile uint32_t *)AMP_MBOX_SUM_ADDR;
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
