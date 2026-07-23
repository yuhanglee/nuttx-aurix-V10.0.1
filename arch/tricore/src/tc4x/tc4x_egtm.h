/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_egtm.h
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

#ifndef __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_H
#define __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_H

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Enable eGTM (CLC) and program CMU GCLK = feGTM / gclk_den. */

void tc4x_egtm_init(uint32_t gclk_den);

/* Configure and start TOM channel <ch> in cluster <cl> as a PWM output.
 *   fxclk_sel    FXCLK_RES tap (0..4)
 *   period/duty  in FXCLK ticks, active-high
 *   irq          SRC interrupt number; <0 skips IRQ setup
 *   cb           period-complete callback, may be NULL
 *   stop_after_n 0 = free-run; >0 disables channel after N periods
 */

void tc4x_egtm_pwm_setup(uint8_t cl, uint8_t ch, uint8_t fxclk_sel,
                          uint32_t period_ticks, uint32_t duty_ticks,
                          int irq, xcpt_t cb, uint32_t stop_after_n);

void tc4x_egtm_pwm_update(uint8_t cl, uint8_t ch, uint32_t duty_ticks);
void tc4x_egtm_pwm_stop(uint8_t cl, uint8_t ch);

/* Route pad TOUT<tout_n> to source <sel_code>. */

void tc4x_egtm_route_tout(uint16_t tout_n, uint8_t sel_code);

#endif /* __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_H */
