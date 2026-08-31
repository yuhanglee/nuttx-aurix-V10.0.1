/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_egtm_pwm.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_TC4XX_EGTM_PWM_H
#define __ARCH_TRICORE_SRC_TC4XX_TC4XX_EGTM_PWM_H

#include <nuttx/config.h>

#include "tricore_gpio.h"

#ifdef CONFIG_TC4XX_EGTM_PWM_LOWERHALF

/* Bind TOM (is_atom=false) or ATOM (is_atom=true) channel <ch> of
 * cluster <cluster> to /dev/pwm<minor>.
 */

int tc4xx_egtm_pwm_register(int minor, bool is_atom,
                            uint8_t cluster, uint8_t ch,
                            gpio_pinset_t pin, uint16_t tout,
                            uint8_t sel_code);

#endif /* CONFIG_TC4XX_EGTM_PWM_LOWERHALF */
#endif /* __ARCH_TRICORE_SRC_TC4XX_TC4XX_EGTM_PWM_H */
