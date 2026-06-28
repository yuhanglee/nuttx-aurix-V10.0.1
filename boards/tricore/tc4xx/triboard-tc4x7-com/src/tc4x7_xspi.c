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

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>

#include <nuttx/arch.h>
#include <nuttx/drivers/drivers.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mtd/xspi_hyperflash.h>
#include <nuttx/spi/xspi.h>

#include "tricore_gpio.h"
#include "hardware/tc4x_xspi.h"

extern uint32_t xspi_read(uintptr_t reg);
extern void     xspi_write(uint32_t value, uintptr_t reg);

#define TC4X7_XSPI_MTD_PATH    "/dev/mtdraw0"
#define TC4X7_XSPI_BLK_PATH    "/dev/mtdblock0"
#define TC4X7_XSPI_CHR_PATH    "/dev/mtd0"

/* 
 *   PD = 2 for clock and chip-select (stronger drive)
 *   PD = 1 for data + strobe lines
 * Without these the IO drives are too weak and the first 16-bit word
 * of every CA-issued burst gets sampled at the wrong phase.
 */

#define XSPI_PAD_CLK(pin) \
  (AURIX_GPIO(GPIO_PORT16, (pin), GPIO_PERIPH, GPIO_ALT0) | \
   GPIO_PERIPH_OWN_PAD | GPIO_PD(2) | GPIO_PL(3))

#define XSPI_PAD_DQ(pin) \
  (AURIX_GPIO(GPIO_PORT16, (pin), GPIO_PERIPH, GPIO_ALT0) | \
   GPIO_PERIPH_OWN_PAD | GPIO_PD(1) | GPIO_PL(3))

#define XSPI_RESET_PAD \
  (AURIX_GPIO(GPIO_PORT16, GPIO_PIN4, GPIO_OUTPUT, GPIO_ALT0) | \
   GPIO_OUTPUT_HIGH)

static int tc4x7_xspi_pinmux(void)
{
  static const gpio_pinset_t pads[] =
  {
    XSPI_PAD_CLK(0),  /* CK_N        */
    XSPI_PAD_CLK(1),  /* CK_P        */
    XSPI_PAD_DQ(2),   /* RWDS / DS   */
    XSPI_PAD_DQ(3),   /* DQ1         */
    XSPI_PAD_DQ(5),   /* DQ2         */
    XSPI_PAD_DQ(6),   /* DQ3         */
    XSPI_PAD_DQ(7),   /* DQ0         */
    XSPI_PAD_DQ(8),   /* DQ4         */
    XSPI_PAD_DQ(9),   /* DQ5         */
    XSPI_PAD_DQ(10),  /* DQ6         */
    XSPI_PAD_DQ(12),  /* DQ7         */
    XSPI_PAD_CLK(13), /* CS0_N       */
    XSPI_RESET_PAD,
  };

  size_t i;
  int    ret;

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

static void tc4x7_xspi_flash_reset(void)
{
  uintptr_t pbase = 0xF003A000u + 16u * 0x400u;

  xspi_write(1u << (4 + 16), pbase + 0x3Cu);  /* OMR.PCL4 -> 0 */
  up_udelay(1000);
  xspi_write(1u << 4,         pbase + 0x3Cu);  /* OMR.PS4  -> 1 */
  up_udelay(10000);
}

int tc4x7_xspi_initialize(void)
{
  FAR struct xspi_dev_s *xspi;
  FAR struct mtd_dev_s  *mtd;
  int ret;

  ret = tc4x7_xspi_pinmux();
  if (ret < 0)
    {
      return ret;
    }

  xspi = tc4x_xspi_initialize(0);
  if (xspi == NULL)
    {
      spierr("xspi: tc4x_xspi_initialize failed\n");
      return -ENODEV;
    }

  XSPI_LOCK(xspi, true);
  XSPI_SETOPMODE(xspi, XSPI_OP_MODE_CONTROLLER);
  XSPI_SETMODE(xspi, XSPI_CPP_MODE_0);
  (void)XSPI_SETFREQUENCY(xspi, 25000000u);
  XSPI_LOCK(xspi, false);

  tc4x7_xspi_flash_reset();
  up_udelay(200);

#ifdef CONFIG_MTD_XSPI_HYPERFLASH
  mtd = xspi_hyperflash_initialize(xspi);
  if (mtd == NULL)
    {
      printf("xspi: hyperflash_initialize failed\n");
      return -ENODEV;
    }

  ret = register_mtddriver(TC4X7_XSPI_MTD_PATH, mtd, 0666, NULL);
  if (ret < 0)
    {
      printf("xspi: register_mtddriver(%s) -> %d\n",
             TC4X7_XSPI_MTD_PATH, ret);
      return ret;
    }

  ret = ftl_initialize_by_path(TC4X7_XSPI_BLK_PATH, mtd, O_RDWR);
  if (ret < 0)
    {
      printf("xspi: ftl_initialize_by_path(%s) -> %d\n",
             TC4X7_XSPI_BLK_PATH, ret);
      return ret;
    }

  ret = bchdev_register(TC4X7_XSPI_BLK_PATH, TC4X7_XSPI_CHR_PATH, O_RDWR);
  if (ret < 0)
    {
      printf("xspi: bchdev_register(%s) -> %d\n",
             TC4X7_XSPI_CHR_PATH, ret);
      return ret;
    }

  printf("xspi: HyperFlash %s -> %s -> %s\n",
         TC4X7_XSPI_MTD_PATH, TC4X7_XSPI_BLK_PATH, TC4X7_XSPI_CHR_PATH);
#else
  UNUSED(mtd);
  printf("xspi: MTD_XSPI_HYPERFLASH not configured\n");
#endif

  return OK;
}
