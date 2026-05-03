/****************************************************************************
 * arch/tricore/src/tc4xx/tc4x_xspi.h
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

#ifndef __ARCH_TRICORE_SRC_TC4XX_TC4X_XSPI_H
#define __ARCH_TRICORE_SRC_TC4XX_TC4X_XSPI_H


#include <nuttx/config.h>
#include <nuttx/bits.h>
#include <nuttx/spi/xspi.h>


#define TC4X_XSPI0_BASE              0xF9040000u

#define TC4X_XSPI0_CLC_OFFSET        0x0000u

#define TC4X_XSPI0_CLC               (TC4X_XSPI0_BASE + TC4X_XSPI0_CLC_OFFSET)

#define XSPI_CLC_DISR                BIT(0)
#define XSPI_CLC_DISS                BIT(1)
#define XSPI_CLC_EDIS                BIT(3)


#ifdef __cplusplus
extern "C"
{
#endif


FAR struct xspi_dev_s *tc4x_xspi_initialize(int port);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_TRICORE_SRC_TC4XX_TC4X_XSPI_H */
