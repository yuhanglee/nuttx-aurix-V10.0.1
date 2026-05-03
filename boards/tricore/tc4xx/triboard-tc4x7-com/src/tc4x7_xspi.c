/****************************************************************************
 * boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_xspi.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <string.h>

#include <nuttx/spi/xspi.h>
#include <nuttx/mtd/xspi_nor.h>

#include "tricore_gpio.h"
#include "hardware/tc4x_xspi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Triboard-tc4x7-com xSPI to S26HS512 pinout (user manual Table 17).
 * All five pads handed over to HSPHY via PCSRSEL.PCSR=1 using the
 * GPIO_PERIPH_OWN_PAD flag in aurix_config_gpio().
 *
 *   P16.0  XSPI_CLK_INV       CK_N
 *   P16.1  XSPI_CLK           CK_P
 *   P16.7  XSPI_RXD0/TXD0     DQ0  (MOSI in 1S)
 *   P16.3  XSPI_RXD1/TXD1     DQ1  (MISO in 1S)
 *   P16.13 XSPI_CS0_N         CS_N (also drives flash RESET_N)
 */

#define XSPI_PAD(pin) \
  (AURIX_GPIO(GPIO_PORT16, (pin), GPIO_PERIPH, GPIO_ALT0) | \
   GPIO_PERIPH_OWN_PAD)

#define XSPI_PROBE_FREQ_HZ     5000000u
#define XSPI_PROBE_FREQ_LOW_HZ 1000000u

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int tc4x7_xspi_pinmux(void)
{
  static const gpio_pinset_t pads[] =
  {
    XSPI_PAD(0),    /* CK_N        */
    XSPI_PAD(1),    /* CK_P        */
    XSPI_PAD(7),    /* DQ0 / MOSI  */
    XSPI_PAD(3),    /* DQ1 / MISO  */
    XSPI_PAD(13),   /* CS0_N       */
  };

  size_t i;
  int ret;

  for (i = 0; i < sizeof(pads) / sizeof(pads[0]); i++)
    {
      ret = aurix_config_gpio(pads[i]);
      if (ret < 0)
        {
          spierr("xspi: pad %u config failed: %d\n", (unsigned)i, ret);
          return ret;
        }
    }

  return OK;
}

/* Issue a stand-alone RDID with up to 16 response bytes. Useful when
 * the silicon is suspected to return an extended JEDEC string beyond
 * the canonical 3 byte payload (Cypress / Infineon Semper does).
 */

static int tc4x7_xspi_rdid_raw(FAR struct xspi_dev_s *dev,
                               FAR uint8_t *buf, size_t len)
{
  struct xspi_op_s op;
  int ret;

  memset(&op, 0, sizeof(op));
  op.proto      = XSPI_PROTO_STD;
  op.io_mode    = XSPI_IO_MODE_SINGLE;
  op.data_rate  = XSPI_DATA_RATE_S_S_S;
  op.cs         = 0;
  op.dir        = XSPI_DIR_READ;
  op.cmd_nbits  = 8;
  op.cmdval     = 0x9f;
  op.buf        = buf;
  op.len        = len;
  op.timeout_ms = 100;

  ret = XSPI_LOCK(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = XSPI_TRANSFER(dev, &op);
  XSPI_LOCK(dev, false);
  return ret;
}

static int tc4x7_xspi_rdsr(FAR struct xspi_dev_s *dev, FAR uint8_t *sr)
{
  struct xspi_op_s op;
  int ret;

  memset(&op, 0, sizeof(op));
  op.proto      = XSPI_PROTO_STD;
  op.io_mode    = XSPI_IO_MODE_SINGLE;
  op.data_rate  = XSPI_DATA_RATE_S_S_S;
  op.cs         = 0;
  op.dir        = XSPI_DIR_READ;
  op.cmd_nbits  = 8;
  op.cmdval     = 0x05;
  op.buf        = sr;
  op.len        = 1;
  op.timeout_ms = 100;

  ret = XSPI_LOCK(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = XSPI_TRANSFER(dev, &op);
  XSPI_LOCK(dev, false);
  return ret;
}

static void tc4x7_xspi_dump_id(const char *tag, const uint8_t *buf,
                               size_t len)
{
  size_t i;

  syslog(LOG_INFO, "xspi: %s id[%zu]:", tag, len);
  for (i = 0; i < len; i++)
    {
      syslog(LOG_INFO, " %02x", buf[i]);
    }

  syslog(LOG_INFO, "\n");
}

static int tc4x7_xspi_smoke_test(FAR struct xspi_dev_s *dev)
{
  struct xspi_nor_jedec_id_s id_fast;
  struct xspi_nor_jedec_id_s id_slow;
  uint8_t  rdid16[16];
  uint8_t  sr;
  uint32_t actual;
  int      ret;

  XSPI_LOCK(dev, true);
  XSPI_SETOPMODE(dev, XSPI_OP_MODE_CONTROLLER);
  XSPI_SETMODE(dev, XSPI_CPP_MODE_0);
  actual = XSPI_SETFREQUENCY(dev, XSPI_PROBE_FREQ_HZ);
  XSPI_LOCK(dev, false);
  syslog(LOG_INFO, "xspi: probe sclk=%lu Hz\n", (unsigned long)actual);

  ret = xspi_nor_probe(dev, 0, &id_fast);
  if (ret < 0)
    {
      syslog(LOG_ERR, "xspi: RDID failed at fast clock: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "xspi: RDID fast  mfr=0x%02x type=0x%02x cap=0x%02x\n",
         id_fast.manufacturer, id_fast.memory_type, id_fast.capacity);

  memset(rdid16, 0, sizeof(rdid16));
  ret = tc4x7_xspi_rdid_raw(dev, rdid16, sizeof(rdid16));
  if (ret == OK)
    {
      tc4x7_xspi_dump_id("RDID16", rdid16, sizeof(rdid16));
    }

  ret = tc4x7_xspi_rdsr(dev, &sr);
  if (ret == OK)
    {
      syslog(LOG_INFO, "xspi: RDSR=0x%02x (WIP=%d WEL=%d)\n",
             sr, sr & 1, (sr >> 1) & 1);
    }

  XSPI_LOCK(dev, true);
  actual = XSPI_SETFREQUENCY(dev, XSPI_PROBE_FREQ_LOW_HZ);
  XSPI_LOCK(dev, false);
  syslog(LOG_INFO, "xspi: re-probe sclk=%lu Hz\n", (unsigned long)actual);

  ret = xspi_nor_probe(dev, 0, &id_slow);
  if (ret < 0)
    {
      syslog(LOG_ERR, "xspi: RDID failed at slow clock: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "xspi: RDID slow  mfr=0x%02x type=0x%02x cap=0x%02x\n",
         id_slow.manufacturer, id_slow.memory_type, id_slow.capacity);

  if (id_fast.manufacturer == id_slow.manufacturer &&
      id_fast.memory_type  == id_slow.memory_type  &&
      id_fast.capacity     == id_slow.capacity)
    {
      syslog(LOG_INFO,
             "xspi: JEDEC stable across %lu / %lu Hz\n",
             (unsigned long)XSPI_PROBE_FREQ_HZ,
             (unsigned long)XSPI_PROBE_FREQ_LOW_HZ);
    }
  else
    {
      syslog(LOG_WARNING,
             "xspi: JEDEC mismatch between fast and slow clocks - "
             "timing or noise issue\n");
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int tc4x7_xspi_initialize(void)
{
  FAR struct xspi_dev_s *dev;
  int ret;

  ret = tc4x7_xspi_pinmux();
  if (ret < 0)
    {
      return ret;
    }

  dev = tc4x_xspi_initialize(0);
  if (dev == NULL)
    {
      syslog(LOG_ERR, "xspi: tc4x_xspi_initialize failed\n");
      return -ENODEV;
    }

  /* Allow >= 150 us for flash POR after first CS / RESET_N deassertion. */

  up_udelay(200);

  return tc4x7_xspi_smoke_test(dev);
}
