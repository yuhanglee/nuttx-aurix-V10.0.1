/****************************************************************************
 * include/nuttx/spi/xspi.h
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

#ifndef __INCLUDE_NUTTX_SPI_XSPI_H
#define __INCLUDE_NUTTX_SPI_XSPI_H


#include <nuttx/config.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>


/* Protocol family */

enum xspi_proto_e
{
  XSPI_PROTO_STD       = 0,
  XSPI_PROTO_HYPERBUS  = 1,
};

/* Controller role */

enum xspi_op_mode_e
{
  XSPI_OP_MODE_CONTROLLER = 0,
  XSPI_OP_MODE_PERIPHERAL = 1,
};

/* Clock polarity / phase */

enum xspi_cpp_mode_e
{
  XSPI_CPP_MODE_0 = 0,
  XSPI_CPP_MODE_1 = 1,
  XSPI_CPP_MODE_2 = 2,
  XSPI_CPP_MODE_3 = 3,
};

/* Cmd-Addr-Data lane widths */

enum xspi_io_mode_e
{
  XSPI_IO_MODE_SINGLE       = 0,   /* 1-1-1 */
  XSPI_IO_MODE_DUAL         = 1,   /* 2-2-2 */
  XSPI_IO_MODE_DUAL_1_1_2   = 2,
  XSPI_IO_MODE_DUAL_1_2_2   = 3,
  XSPI_IO_MODE_QUAD         = 4,   /* 4-4-4 */
  XSPI_IO_MODE_QUAD_1_1_4   = 5,
  XSPI_IO_MODE_QUAD_1_4_4   = 6,
  XSPI_IO_MODE_OCTAL        = 7,   /* 8-8-8 */
  XSPI_IO_MODE_OCTAL_1_1_8  = 8,
  XSPI_IO_MODE_OCTAL_1_8_8  = 9,
  XSPI_IO_MODE_HEX          = 10,  /* 16-16-16 */
  XSPI_IO_MODE_HEX_8_8_16   = 11,
  XSPI_IO_MODE_HEX_8_16_16  = 12,
};

/* Cmd-Addr-Data rate */

enum xspi_data_rate_e
{
  XSPI_DATA_RATE_S_S_S = 0,
  XSPI_DATA_RATE_S_S_D = 1,
  XSPI_DATA_RATE_S_D_D = 2,
  XSPI_DATA_RATE_D_D_D = 3,
};

/* Transfer direction */

enum xspi_dir_e
{
  XSPI_DIR_NONE  = 0,
  XSPI_DIR_READ  = 1,
  XSPI_DIR_WRITE = 2,
};


#define XSPI_LOCK(d,l)           ((d)->ops->lock(d,l))


#define XSPI_SETOPMODE(d,o)      ((d)->ops->setopmode(d,o))


#define XSPI_SETMODE(d,m)        ((d)->ops->setmode(d,m))


#define XSPI_SETFREQUENCY(d,f)   ((d)->ops->setfrequency(d,f))


#define XSPI_TRANSFER(d,op)      ((d)->ops->transfer(d,op))


#define XSPI_ALLOC(d,b)          ((d)->ops->alloc(d,b))


#define XSPI_FREE(d,b)           ((d)->ops->free(d,b))



struct xspi_op_s
{
  uint8_t  proto;        /* enum xspi_proto_e */
  uint8_t  io_mode;      /* enum xspi_io_mode_e */
  uint8_t  data_rate;    /* enum xspi_data_rate_e */
  uint8_t  cs;

  uint8_t  dir;          /* enum xspi_dir_e */
  uint8_t  dqs;          /* 1 = sample data phase with DQS strobe */
  uint8_t  cmd_nbits;    /* 0, 4, 8, 16 */
  uint8_t  addr_nbits;   /* 0..32, address length in bits */

  uint8_t  dummy;        /* dummy cycles between addr and data */
  uint8_t  reserved0[3];

  uint16_t cmdval;
  uint16_t reserved1;
  uint32_t addrval;

  FAR void *buf;         /* data buffer, NULL if dir == NONE */
  size_t    len;         /* data length in bytes */

  uint32_t timeout_ms;   /* 0 = use driver default */
};

/* Forward reference */

struct xspi_dev_s;

/* Lower half operations */

struct xspi_ops_s
{
  CODE int      (*lock)(FAR struct xspi_dev_s *dev, bool lock);
  CODE int      (*setopmode)(FAR struct xspi_dev_s *dev, uint8_t opmode);
  CODE int      (*setmode)(FAR struct xspi_dev_s *dev, uint8_t cpp_mode);
  CODE uint32_t (*setfrequency)(FAR struct xspi_dev_s *dev, uint32_t hz);
  CODE int      (*transfer)(FAR struct xspi_dev_s *dev,
                            FAR const struct xspi_op_s *op);
  CODE FAR void *(*alloc)(FAR struct xspi_dev_s *dev, size_t len);
  CODE void     (*free)(FAR struct xspi_dev_s *dev, FAR void *buf);
};

struct xspi_dev_s
{
  FAR const struct xspi_ops_s *ops;
};

#endif /* __INCLUDE_NUTTX_SPI_XSPI_H */
