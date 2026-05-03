/****************************************************************************
 * drivers/xspi/xspi_dw_ssi.h
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
 * Synopsys DesignWare DWC_SSI IP core driver. Consumed by SoC glue
 * drivers which own instance storage and provide SoC specific values.
 *
 ****************************************************************************/

#ifndef __DRIVERS_XSPI_XSPI_DW_SSI_H
#define __DRIVERS_XSPI_XSPI_DW_SSI_H


#include <nuttx/config.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/mutex.h>
#include <nuttx/spi/xspi.h>


/* Per instance configuration supplied by SoC glue at init time. */

struct xspi_dw_ssi_config_s
{
  uintptr_t base;            /* IP register base address */
  uint32_t  ssi_clk_hz;      /* SSI_clk feeding the controller */
  bool      use_gpio_pads;   /* true: route via GPIO pads (CTRLR2.PORTSEL=1)
                              * false: route via HSFAST pads (PORTSEL=0)
                              * Ignored on IPs that do not implement CTRLR2.
                              */
};

/* Per instance state. Storage owned by the SoC glue layer. */

struct xspi_dw_ssi_s
{
  struct xspi_dev_s                 dev;
  const struct xspi_dw_ssi_config_s *cfg;
  mutex_t                           lock;
  uint32_t                          frequency;
  uint8_t                           cpp_mode;
  bool                              initialized;
};


#ifdef __cplusplus
extern "C"
{
#endif


FAR struct xspi_dev_s *
xspi_dw_ssi_initialize(FAR struct xspi_dw_ssi_s *priv,
                       FAR const struct xspi_dw_ssi_config_s *cfg);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVERS_XSPI_XSPI_DW_SSI_H */
