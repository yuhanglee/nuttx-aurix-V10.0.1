/****************************************************************************
 * boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_xspi.c
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
 * Pin assignment for standard SPI mode over port 20 GPIO pads on this
 * board (exact pin numbers per TC4Dx datasheet, P20 signal mapping):
 *
 *   Signal  TC4xx name   Direction   External device side
 *   ------- ------------ ----------- --------------------
 *   SCLK    CLK          out         SCK on NOR flash
 *   MOSI    DQ0 / SIO0   out         SI  on NOR flash
 *   MISO    DQ1 / SIO1   in          SO  on NOR flash
 *   CS      CS0          out (low)   CS# on NOR flash
 *
 *   Flash Vcc = 3.3 V. Max SCLK on port 20 = 25 MHz.
 *   Tie WP# and HOLD# high on the flash during bring-up.
 *   Common GND between MCU and flash mandatory.
 *
 * Pin mux programming is done separately in tc4d7_pinmux_* (board specific).
 *
 ****************************************************************************/


#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>

#include <nuttx/spi/xspi.h>
#include <nuttx/mtd/xspi_nor.h>


#define TC4D7_XSPI_PORT        0
#define TC4D7_XSPI_CS          0
#define TC4D7_XSPI_FREQ_HZ     1000000     /* 1 MHz, safe for bring-up */


int tc4d7_xspi_initialize(void)
{
  FAR struct xspi_dev_s *dev;
  struct xspi_nor_jedec_id_s id;
  uint32_t actual;
  int ret;


  dev = tc4x_xspi_initialize(TC4D7_XSPI_PORT);
  if (dev == NULL)
    {
      spierr("tc4d7_xspi: initialize failed\n");
      return -ENODEV;
    }

  ret = XSPI_LOCK(dev, true);
  if (ret < 0)
    {
      spierr("tc4d7_xspi: lock failed: %d\n", ret);
      return ret;
    }

  ret = XSPI_SETOPMODE(dev, XSPI_OP_MODE_CONTROLLER);
  if (ret < 0)
    {
      goto err_unlock;
    }

  ret = XSPI_SETMODE(dev, XSPI_CPP_MODE_0);
  if (ret < 0)
    {
      goto err_unlock;
    }

  actual = XSPI_SETFREQUENCY(dev, TC4D7_XSPI_FREQ_HZ);
  spiinfo("tc4d7_xspi: freq requested %u Hz, actual %u Hz\n",
          (unsigned)TC4D7_XSPI_FREQ_HZ, (unsigned)actual);

  XSPI_LOCK(dev, false);

  /* Probe the external NOR */

  ret = xspi_nor_probe(dev, TC4D7_XSPI_CS, &id);
  if (ret < 0)
    {
      spierr("tc4d7_xspi: NOR probe failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "xspi_nor0: JEDEC %02x %02x %02x\n",
         id.manufacturer, id.memory_type, id.capacity);

  return OK;

err_unlock:
  XSPI_LOCK(dev, false);
  return ret;
}
