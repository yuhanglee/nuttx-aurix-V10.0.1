/****************************************************************************
 * drivers/mtd/xspi_nor.h
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
 * Generic JEDEC SPI NOR driver sitting on top of the xSPI subsystem.
 *
 ****************************************************************************/

#ifndef __DRIVERS_MTD_XSPI_NOR_H
#define __DRIVERS_MTD_XSPI_NOR_H


#include <nuttx/config.h>
#include <stdint.h>

#include <nuttx/spi/xspi.h>


struct xspi_nor_jedec_id_s
{
  uint8_t manufacturer;      /* Byte 0 - JEDEC manufacturer ID */
  uint8_t memory_type;       /* Byte 1 - memory type */
  uint8_t capacity;          /* Byte 2 - density code */
};


#ifdef __cplusplus
extern "C"
{
#endif


int xspi_nor_probe(FAR struct xspi_dev_s *dev, uint8_t cs,
                   FAR struct xspi_nor_jedec_id_s *id);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVERS_MTD_XSPI_NOR_H */
