/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397-5v-trb/src/tc397_qspi.c
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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include "tricore_gpio.h"
#include "tc3x_qspi.h"
#include "hardware/tc3x_qspi.h"

#ifdef CONFIG_TC3XX_QSPI2



#define TLF30682_REG_DEVCFG0       0x00u   /* reset 0xF3                   */
#define TLF30682_REG_PROTCFG       0x03u   /* protection config / UNLOCK   */
#define TLF30682_REG_SPISF         0x1Fu   /* SPI status flags, rw1c       */
#define TLF30682_REG_DEVSTAT       0x28u   /* device state info, STATE bits*/
#define TLF30682_REG_PROTSTAT      0x29u   /* KEYxOK + LOCK status         */
#define TLF30682_REG_DEVID         0x3Cu   /* reset 0x10 (TLF30682 marker) */
#define TLF30682_DEVID_EXPECTED    0x10u

#define TLF30682_SPI_FREQ_HZ       1000000u
#define TLF30682_SPI_MODE          SPIDEV_MODE1
#define TLF30682_SPI_NBITS         16

#define TLF30682_SPI_DEVID         SPIDEV_USER(1)


static void tc397_qspi2_pinmux(void)
{
  aurix_config_gpio(AURIX_GPIO(15, 6, GPIO_PERIPH, GPIO_ALT3));
  aurix_config_gpio(AURIX_GPIO(15, 7, GPIO_INPUT,  GPIO_PULL_NONE));
  aurix_config_gpio(AURIX_GPIO(15, 8, GPIO_PERIPH, GPIO_ALT3));
  aurix_config_gpio(AURIX_GPIO(14, 2, GPIO_PERIPH, GPIO_ALT3));
}


#define QSPI2_REG(off) \
        (*(volatile uint32_t *)(TC3X_QSPI2_BASE + (off)))

static uint16_t tc397_qspi2_lb_one(uint16_t tx)
{
  uint32_t bacon;
  uint32_t status;
  uint32_t timeout = 100000u;

  /* Drain RX FIFO. */

  while ((QSPI2_REG(TC3X_QSPI_STATUS_OFFSET) &
          TC3X_QSPI_STATUS_RXFIFOLEVEL_MASK) != 0)
    {
      (void)QSPI2_REG(TC3X_QSPI_RXEXIT_OFFSET);
      if (--timeout == 0)
        {
          break;
        }
    }

  /* Clear ERROR + RX/TX flags. */

  QSPI2_REG(TC3X_QSPI_FLAGSCLEAR_OFFSET) =
      TC3X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK |
      TC3X_QSPI_FLAGSCLEAR_TXC | TC3X_QSPI_FLAGSCLEAR_RXC;


  bacon = TC3X_QSPI_BACON_CS(1)
        | (1u << TC3X_QSPI_BACON_DL_SHIFT)
        | TC3X_QSPI_BACON_BYTE
        | TC3X_QSPI_BACON_MSB
        | TC3X_QSPI_BACON_LAST
        | (1u << TC3X_QSPI_BACON_LPRE_SHIFT)
        | TC3X_QSPI_BACON_LEAD(4)
        | (1u << TC3X_QSPI_BACON_TPRE_SHIFT)
        | TC3X_QSPI_BACON_TRAIL(4)
        | (2u << TC3X_QSPI_BACON_IPRE_SHIFT)
        | TC3X_QSPI_BACON_IDLE(4);

  QSPI2_REG(TC3X_QSPI_MIXENTRY_OFFSET)     = bacon;
  QSPI2_REG(TC3X_QSPI_DATAENTRY_OFFSET(0)) = (uint32_t)tx;

  timeout = 100000u;
  while (timeout--)
    {
      status = QSPI2_REG(TC3X_QSPI_STATUS_OFFSET);
      if (((status & TC3X_QSPI_STATUS_RXFIFOLEVEL_MASK) >>
            TC3X_QSPI_STATUS_RXFIFOLEVEL_SHIFT) > 0)
        {
          break;
        }
    }

  if (timeout == 0)
    {
      return 0xDEADu;
    }

  return (uint16_t)QSPI2_REG(TC3X_QSPI_RXEXIT_OFFSET);
}

static void tc397_qspi2_lb_self_test(void)
{
  static const uint16_t patterns[] =
    {
      0x0000u, 0xFFFFu, 0xAAAAu, 0x5555u, 0x1234u, 0x8001u,
      0x00FFu, 0xFF00u,
    };

  uint32_t saved_globalcon = QSPI2_REG(TC3X_QSPI_GLOBALCON_OFFSET);
  uint32_t saved_ssoc      = QSPI2_REG(TC3X_QSPI_SSOC_OFFSET);
  uint32_t pass            = 0;
  uint32_t fail            = 0;

  printf("=== QSPI2 INTERNAL LOOPBACK SELF-TEST (LB=1) ===\n");

  /* Quiet the external bus during the loopback test. */

  QSPI2_REG(TC3X_QSPI_SSOC_OFFSET) =
      saved_ssoc & ~TC3X_QSPI_SSOC_OEN_BIT(1);

  QSPI2_REG(TC3X_QSPI_GLOBALCON_OFFSET) =
      saved_globalcon | TC3X_QSPI_GLOBALCON_LB;

  for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
      uint16_t tx     = patterns[i];
      uint16_t rx     = tc397_qspi2_lb_one(tx);
      uint32_t status1 = QSPI2_REG(TC3X_QSPI_STATUS1_OFFSET);
      uint8_t  bits    = (uint8_t)(status1 & 0xffu);
      uint32_t status  = QSPI2_REG(TC3X_QSPI_STATUS_OFFSET);
      uint32_t errfl   = status & TC3X_QSPI_STATUS_ERRORFLAGS_MASK;

      bool match = (rx == tx);
      printf("  tx=0x%04x rx=0x%04x bitcount=%u err=0x%03lx %s\n",
             tx, rx, bits, (unsigned long)errfl,
             match ? "[PASS]" : "[FAIL]");
      if (match)
        {
          pass++;
        }
      else
        {
          fail++;
        }
    }

  /* Restore. */

  QSPI2_REG(TC3X_QSPI_GLOBALCON_OFFSET) = saved_globalcon;
  QSPI2_REG(TC3X_QSPI_SSOC_OFFSET)      = saved_ssoc;
  QSPI2_REG(TC3X_QSPI_FLAGSCLEAR_OFFSET) =
      TC3X_QSPI_FLAGSCLEAR_ERRORCLEAR_MASK |
      TC3X_QSPI_FLAGSCLEAR_TXC | TC3X_QSPI_FLAGSCLEAR_RXC;

  printf("=== QSPI2 LB result: %u pass / %u fail ===\n", pass, fail);
  fflush(stdout);
}


static uint16_t tlf30682_parity(uint16_t frame_no_parity)
{
  uint16_t v = (uint16_t)(frame_no_parity >> 1);
  uint16_t p = 0;

  while (v != 0)
    {
      p ^= (v & 1u);
      v >>= 1;
    }

  return p;
}


static uint16_t tlf30682_build_read(uint8_t addr)
{
  uint16_t f = (uint16_t)((addr & 0x3fu) << 9);
  return (uint16_t)(f | tlf30682_parity(f));
}

static uint16_t tlf30682_build_write(uint8_t addr, uint8_t data)
{
  uint16_t f = (uint16_t)(0x8000u
                          | ((addr & 0x3fu) << 9)
                          | ((data & 0xffu) << 1));
  return (uint16_t)(f | tlf30682_parity(f));
}


static void tlf30682_decode(const char *tag, uint16_t tx, uint16_t rx)
{
  uint8_t  cmd_resp = (uint8_t)((rx >> 15) & 0x1u);
  uint8_t  status   = (uint8_t)((rx >> 9)  & 0x3fu);
  uint8_t  data     = (uint8_t)((rx >> 1)  & 0xffu);
  uint8_t  par      = (uint8_t)(rx & 0x1u);
  uint16_t expected = tlf30682_parity(rx);

  printf("  %s tx=0x%04x rx=0x%04x  CMD=%u STAT=0x%02x DATA=0x%02x P=%u (exp=%u %s)\n",
         tag, tx, rx, cmd_resp, status, data, par, expected,
         (par == expected) ? "ok" : "BAD");
}


static void tc397_tlf30682_probe(FAR struct spi_dev_s *dev)
{

  uint16_t tx[4];
  uint16_t rx[4] = { 0 };

  tx[0] = tlf30682_build_read(TLF30682_REG_DEVID);
  tx[1] = tlf30682_build_read(TLF30682_REG_DEVCFG0);
  tx[2] = tlf30682_build_read(TLF30682_REG_DEVID);
  tx[3] = tlf30682_build_read(TLF30682_REG_DEVCFG0);

  SPI_LOCK(dev, true);
  SPI_SETMODE(dev, TLF30682_SPI_MODE);
  SPI_SETBITS(dev, TLF30682_SPI_NBITS);
  SPI_SETFREQUENCY(dev, TLF30682_SPI_FREQ_HZ);
  SPI_SELECT(dev, TLF30682_SPI_DEVID, true);
  SPI_EXCHANGE(dev, tx, rx, 4);
  SPI_SELECT(dev, TLF30682_SPI_DEVID, false);
  SPI_LOCK(dev, false);

  printf("=== TLF30682 PMIC PROBE ===\n");
  tlf30682_decode("[0] DEVID  ", tx[0], rx[0]);
  tlf30682_decode("[1] DEVCFG0", tx[1], rx[1]);
  tlf30682_decode("[2] DEVID  ", tx[2], rx[2]);
  tlf30682_decode("[3] DEVCFG0", tx[3], rx[3]);


  uint8_t d0 = (uint8_t)((rx[0] >> 1) & 0xffu);
  uint8_t d1 = (uint8_t)((rx[1] >> 1) & 0xffu);
  uint8_t d2 = (uint8_t)((rx[2] >> 1) & 0xffu);

  bool same_frame = (d0 == TLF30682_DEVID_EXPECTED && d1 == 0xF3u);
  bool next_frame = (d1 == TLF30682_DEVID_EXPECTED && d2 == 0xF3u);

  if (same_frame)
    {
      printf("  RESULT: PASS (same-frame response model). DEVID=0x%02x\n", d0);
    }
  else if (next_frame)
    {
      printf("  RESULT: PASS (next-frame response model). DEVID=0x%02x\n",
             d1);
    }
  else
    {
      printf("  RESULT: FAIL -- DEVID not 0x%02x in any frame\n",
             TLF30682_DEVID_EXPECTED);
    }


  printf("QSPI2 regs: GLOBALCON=0x%08lx GLOBALCON1=0x%08lx PISEL=0x%08lx\n",
         (unsigned long)QSPI2_REG(TC3X_QSPI_GLOBALCON_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_GLOBALCON1_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_PISEL_OFFSET));
  printf("QSPI2 regs: ECON1=0x%08lx SSOC=0x%08lx STATUS=0x%08lx STATUS1=0x%08lx (BITCOUNT=%u)\n",
         (unsigned long)QSPI2_REG(TC3X_QSPI_ECON_OFFSET(1)),
         (unsigned long)QSPI2_REG(TC3X_QSPI_SSOC_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_STATUS_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_STATUS1_OFFSET),
         (unsigned)(QSPI2_REG(TC3X_QSPI_STATUS1_OFFSET) & 0xffu));

  printf("=== TLF30682 PMIC PROBE END ===\n");
  fflush(stdout);
}


static uint16_t tlf30682_one_xfer(FAR struct spi_dev_s *dev, uint16_t tx)
{
  uint16_t rx = 0;

  SPI_LOCK(dev, true);
  SPI_SETMODE(dev, TLF30682_SPI_MODE);
  SPI_SETBITS(dev, TLF30682_SPI_NBITS);
  SPI_SETFREQUENCY(dev, TLF30682_SPI_FREQ_HZ);
  SPI_SELECT(dev, TLF30682_SPI_DEVID, true);
  SPI_EXCHANGE(dev, &tx, &rx, 1);
  SPI_SELECT(dev, TLF30682_SPI_DEVID, false);
  SPI_LOCK(dev, false);
  return rx;
}


static void tc397_tlf30682_write_probe(FAR struct spi_dev_s *dev)
{
  uint16_t tx_rd_spisf  = tlf30682_build_read(TLF30682_REG_SPISF);
  uint16_t tx_rd_devid  = tlf30682_build_read(TLF30682_REG_DEVID);
  uint16_t tx_rd_devcfg = tlf30682_build_read(TLF30682_REG_DEVCFG0);
  uint16_t rx;

  printf("=== TLF30682 WRITE PROBE ===\n");


  uint16_t tx_rd_devstat  = tlf30682_build_read(TLF30682_REG_DEVSTAT);
  uint16_t tx_rd_protstat = tlf30682_build_read(TLF30682_REG_PROTSTAT);
  uint16_t tx_rd_protcfg  = tlf30682_build_read(TLF30682_REG_PROTCFG);

  rx = tlf30682_one_xfer(dev, tx_rd_devstat);
  tlf30682_decode("[01 DEVSTAT    ]", tx_rd_devstat, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_protstat);
  tlf30682_decode("[02 PROTSTAT   ]", tx_rd_protstat, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_protcfg);
  tlf30682_decode("[03 PROTCFG    ]", tx_rd_protcfg, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("[04 base SPISF ]", tx_rd_spisf, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devid);
  tlf30682_decode("[05 base DEVID ]", tx_rd_devid, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devcfg);
  tlf30682_decode("[06 base DEVCFG]", tx_rd_devcfg, rx);


  uint16_t tx_wr_protcfg_aa = tlf30682_build_write(TLF30682_REG_PROTCFG,
                                                   0xAAu);
  rx = tlf30682_one_xfer(dev, tx_wr_protcfg_aa);
  tlf30682_decode("[07 wr PROTCFG=AA]", tx_wr_protcfg_aa, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_protcfg);
  tlf30682_decode("    -> rd PROTCFG", tx_rd_protcfg, rx);


  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("    -> rd SPISF  ", tx_rd_spisf, rx);


  static const struct { uint8_t pat; const char *tag; } spisf_patterns[] =
    {
      { 0xFFu, "[10 wr SPISF=0xFF]" },
      { 0x7Fu, "[20 wr SPISF=0x7F]" },
      { 0x01u, "[30 wr SPISF=0x01 PAR ]" },
      { 0x02u, "[40 wr SPISF=0x02 LEN ]" },
      { 0x04u, "[50 wr SPISF=0x04 ADDR]" },
      { 0x08u, "[60 wr SPISF=0x08 DUR ]" },
    };

  for (size_t i = 0; i < sizeof(spisf_patterns) / sizeof(spisf_patterns[0]);
       i++)
    {
      uint16_t tx_w = tlf30682_build_write(TLF30682_REG_SPISF,
                                           spisf_patterns[i].pat);
      rx = tlf30682_one_xfer(dev, tx_w);
      tlf30682_decode(spisf_patterns[i].tag, tx_w, rx);

      rx = tlf30682_one_xfer(dev, tx_rd_spisf);
      tlf30682_decode("    -> rd SPISF  ", tx_rd_spisf, rx);
    }


  uint16_t tx_w_dcfg = tlf30682_build_write(TLF30682_REG_DEVCFG0, 0x00u);
  rx = tlf30682_one_xfer(dev, tx_w_dcfg);
  tlf30682_decode("[70 wr DEVCFG0=0]", tx_w_dcfg, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devcfg);
  tlf30682_decode("    -> rd DEVCFG0", tx_rd_devcfg, rx);


  printf("--- PROTCFG UNLOCK keys ---\n");

  static const uint8_t unlock_keys[4] = { 0xABu, 0xEFu, 0x56u, 0x12u };
  for (size_t i = 0; i < 4; i++)
    {
      uint16_t tx_k = tlf30682_build_write(0x03u, unlock_keys[i]);
      rx = tlf30682_one_xfer(dev, tx_k);
      char tag[32];
      snprintf(tag, sizeof(tag), "[U%zu wr PROTCFG=0x%02x]",
               i + 1, unlock_keys[i]);
      tlf30682_decode(tag, tx_k, rx);
    }

  /* 4a. Retry SPISF clear with 0xFF after UNLOCK. */

  uint16_t tx_w_unlock = tlf30682_build_write(TLF30682_REG_SPISF, 0xFFu);
  rx = tlf30682_one_xfer(dev, tx_w_unlock);
  tlf30682_decode("[U5 wr SPISF=0xFF]", tx_w_unlock, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("    -> rd SPISF  ", tx_rd_spisf, rx);

  /* 4b. Retry DEVCFG0 = 0x00 after UNLOCK. */

  uint16_t tx_w_dcfg2 = tlf30682_build_write(TLF30682_REG_DEVCFG0, 0x00u);
  rx = tlf30682_one_xfer(dev, tx_w_dcfg2);
  tlf30682_decode("[U6 wr DEVCFG0=0]", tx_w_dcfg2, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devcfg);
  tlf30682_decode("    -> rd DEVCFG0", tx_rd_devcfg, rx);

  /* 4. Final SPISF + DEVID readbacks. */

  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("[80 final SPISF ]", tx_rd_spisf, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devid);
  tlf30682_decode("[81 final DEVID ]", tx_rd_devid, rx);


  printf("--- inverted-parity write experiment ---\n");

  uint16_t tx_w_inv_spisf = (uint16_t)(tlf30682_build_write(
                                          TLF30682_REG_SPISF, 0xFFu) ^ 1u);
  rx = tlf30682_one_xfer(dev, tx_w_inv_spisf);
  tlf30682_decode("[I1 inv-par wr SPISF=0xFF]", tx_w_inv_spisf, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("    -> rd SPISF  ", tx_rd_spisf, rx);

  uint16_t tx_w_inv_devcfg = (uint16_t)(tlf30682_build_write(
                                          TLF30682_REG_DEVCFG0, 0x00u) ^ 1u);
  rx = tlf30682_one_xfer(dev, tx_w_inv_devcfg);
  tlf30682_decode("[I2 inv-par wr DEVCFG0=0]", tx_w_inv_devcfg, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_devcfg);
  tlf30682_decode("    -> rd DEVCFG0", tx_rd_devcfg, rx);

  uint16_t tx_w_inv_protcfg = (uint16_t)(tlf30682_build_write(
                                          TLF30682_REG_PROTCFG, 0xAAu) ^ 1u);
  rx = tlf30682_one_xfer(dev, tx_w_inv_protcfg);
  tlf30682_decode("[I3 inv-par wr PROTCFG=AA]", tx_w_inv_protcfg, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_protcfg);
  tlf30682_decode("    -> rd PROTCFG", tx_rd_protcfg, rx);

  rx = tlf30682_one_xfer(dev, tx_rd_spisf);
  tlf30682_decode("    -> rd SPISF  ", tx_rd_spisf, rx);

  printf("QSPI2 post-probe: STATUS=0x%08lx ECON1=0x%08lx SSOC=0x%08lx STATUS1=0x%08lx (BITCOUNT=%u)\n",
         (unsigned long)QSPI2_REG(TC3X_QSPI_STATUS_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_ECON_OFFSET(1)),
         (unsigned long)QSPI2_REG(TC3X_QSPI_SSOC_OFFSET),
         (unsigned long)QSPI2_REG(TC3X_QSPI_STATUS1_OFFSET),
         (unsigned)(QSPI2_REG(TC3X_QSPI_STATUS1_OFFSET) & 0xffu));

  printf("=== TLF30682 WRITE PROBE END ===\n");
  fflush(stdout);
}


int tc397_qspi_initialize(void)
{
  static bool s_initialized;
  FAR struct spi_dev_s *qspi2;
#ifdef CONFIG_SPI_DRIVER
  int ret;
#endif


  if (s_initialized)
    {
      return OK;
    }

  tc397_qspi2_pinmux();

  qspi2 = tc3xx_qspibus_initialize(2);
  if (qspi2 == NULL)
    {
      printf("QSPI2 init FAILED\n");
      fflush(stdout);
      return -ENODEV;
    }

#ifdef CONFIG_SPI_DRIVER

  ret = spi_register(qspi2, 0);
  if (ret < 0)
    {
      printf("QSPI2 spi_register failed: %d\n", ret);
    }
#endif

  printf("QSPI2 ready, probing TLF30682 PMIC...\n");
  fflush(stdout);
  tc397_qspi2_lb_self_test();
  tc397_tlf30682_probe(qspi2);
  tc397_tlf30682_write_probe(qspi2);

  s_initialized = true;
  return OK;
}

#undef QSPI2_REG

#endif /* CONFIG_TC3XX_QSPI2 */
