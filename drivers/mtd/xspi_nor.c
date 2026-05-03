/****************************************************************************
 * drivers/mtd/xspi_nor.c
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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/spi/xspi.h>
#include <debug.h>

#include <nuttx/mtd/xspi_nor.h>


#define XSPI_NOR_CMD_RDID    0x9f
#define XSPI_NOR_RDID_LEN    3


int xspi_nor_probe(FAR struct xspi_dev_s *dev, uint8_t cs,
                   FAR struct xspi_nor_jedec_id_s *id)
{
  uint8_t rx[XSPI_NOR_RDID_LEN] = { 0 };
  struct xspi_op_s op;
  int ret;

  if (dev == NULL || id == NULL)
    {
      return -EINVAL;
    }

  memset(&op, 0, sizeof(op));
  op.proto      = XSPI_PROTO_STD;
  op.io_mode    = XSPI_IO_MODE_SINGLE;
  op.data_rate  = XSPI_DATA_RATE_S_S_S;
  op.cs         = cs;
  op.dir        = XSPI_DIR_READ;
  op.cmd_nbits  = 8;
  op.cmdval     = XSPI_NOR_CMD_RDID;
  op.buf        = rx;
  op.len        = XSPI_NOR_RDID_LEN;
  op.timeout_ms = 100;

  ret = XSPI_LOCK(dev, true);
  if (ret < 0)
    {
      spierr("xspi_nor: lock failed: %d\n", ret);
      return ret;
    }

  ret = XSPI_TRANSFER(dev, &op);

  XSPI_LOCK(dev, false);

  if (ret < 0)
    {
      spierr("xspi_nor: RDID transfer failed: %d\n", ret);
      return ret;
    }

  id->manufacturer = rx[0];
  id->memory_type  = rx[1];
  id->capacity     = rx[2];

  spiinfo("xspi_nor: JEDEC ID mfr=0x%02x type=0x%02x cap=0x%02x\n",
          id->manufacturer, id->memory_type, id->capacity);

  if (rx[0] == 0x00 || rx[0] == 0xff)
    {
      spierr("xspi_nor: suspicious JEDEC mfr byte 0x%02x, check wiring\n",
             rx[0]);
      return -ENODEV;
    }

  return OK;
}
