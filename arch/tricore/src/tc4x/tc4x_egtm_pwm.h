/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_egtm_pwm.h
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

#ifndef __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_PWM_H
#define __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_PWM_H

#include <nuttx/config.h>

#include "tricore_gpio.h"

#ifdef CONFIG_TC4X_EGTM_PWM_LOWERHALF

/* Init eGTM (idempotent), configure pad, route TOUT, and register
 * /dev/pwm<minor>. Only minor=0 is supported today.
 */

int tc4x_egtm_pwm_register(int minor, gpio_pinset_t pin,
                            uint8_t tout, uint8_t sel_code);

#endif /* CONFIG_TC4X_EGTM_PWM_LOWERHALF */
#endif /* __ARCH_TRICORE_SRC_TC4X_TC4X_EGTM_PWM_H */
