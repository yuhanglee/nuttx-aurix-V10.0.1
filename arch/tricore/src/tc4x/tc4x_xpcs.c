/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_xpcs.c
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

/* SNPS DesignWare XPCS1 SGMII 1G bringup for the AURIX TC4Dx HSPHY lane
 * that carries GETH0 to the external RTL8221B.
 * 1000BASE-T with auto-negotiation enabled on the PCS side.
 */

#include <nuttx/config.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/bits.h>

#include "tricore_internal.h"
#include "hardware/tc4x_xpcs.h"

static void xpcs_write(uintptr_t reg, uint32_t val)
{
  putreg32(val, reg);
}

static uint32_t xpcs_read(uintptr_t reg)
{
  return getreg32(reg);
}

static void xpcs_modreg(uint32_t setbits, uint32_t mask, uintptr_t reg)
{
  modreg32(setbits, mask, reg);
}

static int xpcs_tx_req_ack(void)
{
  int i;

  xpcs_modreg(XPCS_TX_GENCTRL2_TX_REQ_0, XPCS_TX_GENCTRL2_TX_REQ_0,
              TC4XX_XPCS_PMA_TX_GENCTRL2);

  for (i = 0; i < 1000; i++)
    {
      if (xpcs_read(TC4XX_XPCS_PMA_TX_STS) & XPCS_TX_STS_TX_ACK_0)
        break;
      up_udelay(10);
    }

  if (i == 1000)
    {
      return -ETIMEDOUT;
    }

  xpcs_modreg(0, XPCS_TX_GENCTRL2_TX_REQ_0, TC4XX_XPCS_PMA_TX_GENCTRL2);

  for (i = 0; i < 1000; i++)
    {
      if ((xpcs_read(TC4XX_XPCS_PMA_TX_STS) & XPCS_TX_STS_TX_ACK_0) == 0)
        break;
      up_udelay(10);
    }

  if (i == 1000)
    {
      return -ETIMEDOUT;
    }

  return 0;
}

int tc4x_xpcs1_sgmii_1g_init(void)
{
  int i;
  int ret;

  /* Wait for the XPCS SRAM to finish bootload from ROM. */

  for (i = 0; i < 1000; i++)
    {
      if (xpcs_read(TC4XX_XPCS_PMA_SRAM) & XPCS_SRAM_INIT_DN)
        break;
      up_udelay(100);
    }

  if (i == 1000)
    {
      nerr("XPCS1 SRAM INIT_DN timeout\n");
      return -EIO;
    }

  xpcs_modreg(XPCS_SRAM_EXT_LD_DN, XPCS_SRAM_EXT_LD_DN,
              TC4XX_XPCS_PMA_SRAM);

  for (i = 0; i < 2000; i++)
    {
      if ((xpcs_read(TC4XX_XPCS_PCS_SR_XS_CTRL1) & XPCS_PCS_SR_CTRL1_RST) == 0)
        break;
      up_udelay(100);
    }

  if (i == 2000)
    {
      nerr("XPCS1 PCS reset timeout\n");
      return -EIO;
    }

  /* PCS_TYPE_SEL = 1 (10GBASE-X block reused as SGMII PCS) */

  xpcs_modreg(1u, XPCS_PCS_SR_CTRL2_PCS_TYPE_MASK,
              TC4XX_XPCS_PCS_SR_XS_CTRL2);

  /* EN_2_5G_MODE = 0 (1 Gbps SGMII, not 2.5G) */

  xpcs_modreg(0, XPCS_PCS_VR_DIG_CTRL1_EN_2_5G,
              TC4XX_XPCS_PCS_VR_XS_DIG_CTRL1);

  xpcs_modreg(0, BIT(13), TC4XX_XPCS_PCS_SR_XS_CTRL1);

  /* MII CTRL: AN_ENABLE=1, DUPLEX=1, SS6=1, SS13=0 -> SGMII 1G full duplex */

  xpcs_write(TC4XX_XPCS_MII_CTRL,
             XPCS_MII_CTRL_AN_ENABLE |
             XPCS_MII_CTRL_DUPLEX |
             XPCS_MII_CTRL_SS6);

  /* Disable the MPLL before re-programming clock settings. */

  if (xpcs_read(TC4XX_XPCS_PMA_MPLL_CMN_CTRL) & XPCS_MPLL_CMN_MPLL_EN_0)
    {
      xpcs_modreg(0, XPCS_MPLL_CMN_MPLL_EN_0, TC4XX_XPCS_PMA_MPLL_CMN_CTRL);
      ret = xpcs_tx_req_ack();
      if (ret < 0)
        {
          /* The transmitter does not acknowledge while the MPLL is going
           * down. Take the request away and carry on into the clock setup.
           */

          ninfo("XPCS1 TX req/ack (pre-config) skipped\n");
          xpcs_modreg(0, XPCS_TX_GENCTRL2_TX_REQ_0,
                      TC4XX_XPCS_PMA_TX_GENCTRL2);
        }
    }

  /* SGMII 1G (100 MHz refclk via pad). */

  xpcs_write(TC4XX_XPCS_PMA_REF_CLK_CTRL,
             XPCS_REF_CLK_EN | XPCS_REF_CLK_USE_PAD);
  xpcs_write(TC4XX_XPCS_PMA_MPLLA_CTRL0, 50u);
  xpcs_write(TC4XX_XPCS_PMA_MPLLA_CTRL1, 0u);
  xpcs_write(TC4XX_XPCS_PMA_MPLLA_CTRL2,
             (1u << XPCS_MPLLA_CTRL2_TX_CLK_DIV_SHIFT) |
             XPCS_MPLLA_CTRL2_DIV10_CLK_EN);

  ret = xpcs_tx_req_ack();
  if (ret < 0)
    {
      ninfo("XPCS1 TX req/ack skipped (cold start, no MPLL yet)\n");
      xpcs_modreg(0, XPCS_TX_GENCTRL2_TX_REQ_0,
                  TC4XX_XPCS_PMA_TX_GENCTRL2);
    }

  xpcs_modreg(1000u, XPCS_VCO_CAL_LD0_VAL, TC4XX_XPCS_PMA_VCO_CAL_LD0);
  xpcs_write(TC4XX_XPCS_PMA_VCO_CAL_REF0, 10u);
  xpcs_write(TC4XX_XPCS_PMA_MPLLA_CTRL6,
             2u | (12u << XPCS_MPLLA_CTRL6_CP_PROP_SHIFT));
  xpcs_write(TC4XX_XPCS_PMA_MPLLA_CTRL7,
             1u | (10u << XPCS_MPLLA_CTRL7_CP_PROP_GS_SHIFT));
  xpcs_write(TC4XX_XPCS_PMA_MISC_CTRL2, 1u);
  xpcs_write(TC4XX_XPCS_PMA_RX_MISC_CTRL0, 161u);
  xpcs_write(TC4XX_XPCS_PMA_RX_GENCTRL1, XPCS_RX_GENCTRL1_RX_TERM_ACDC_0);
  xpcs_write(TC4XX_XPCS_PMA_RX_GENCTRL2, 1u << 8);
  xpcs_write(TC4XX_XPCS_PMA_RX_GENCTRL4, 0u);
  xpcs_write(TC4XX_XPCS_PMA_TX_GENCTRL1, 0u);
  xpcs_write(TC4XX_XPCS_PMA_TX_GENCTRL2, 1u << 8);
  xpcs_write(TC4XX_XPCS_PMA_TX_BOOST_CTRL, 15u);
  xpcs_write(TC4XX_XPCS_PMA_RX_RATE_CTRL, 2u);
  xpcs_write(TC4XX_XPCS_PMA_TX_RATE_CTRL, 2u);

  /* Re-enable MPLL, then perform TX and RX handshake to latch config
   * (per HSPHY TRM section 17.3.3.2 SGMII 1G steps 4b-f).
   */

  xpcs_modreg(XPCS_MPLL_CMN_MPLL_EN_0, XPCS_MPLL_CMN_MPLL_EN_0,
              TC4XX_XPCS_PMA_MPLL_CMN_CTRL);

  (void)xpcs_tx_req_ack();

  /* RX_REQ/ACK handshake to latch RX config. */
  {
    xpcs_modreg(XPCS_RX_GENCTRL2_RX_REQ_0, XPCS_RX_GENCTRL2_RX_REQ_0,
                TC4XX_XPCS_PMA_RX_GENCTRL2);
    for (i = 0; i < 1000; i++)
      {
        if ((xpcs_read(TC4XX_XPCS_PMA_RX_GENCTRL2) &
             XPCS_RX_GENCTRL2_RX_REQ_0) == 0)
          break;
        up_udelay(10);
      }
    for (i = 0; i < 1000; i++)
      {
        if ((xpcs_read(TC4XX_XPCS_PMA_RX_STS) & XPCS_RX_STS_RX_ACK_0) == 0)
          break;
        up_udelay(10);
      }
  }

  /* Step 5: Program INIT bit of VR_XS_PCS_DIG_CTRL1 to 1 and wait for
   * it to self-clear. This flushes/initializes the XPCS FIFOs.
   */

  xpcs_modreg(XPCS_PCS_VR_DIG_CTRL1_INIT, XPCS_PCS_VR_DIG_CTRL1_INIT,
              TC4XX_XPCS_PCS_VR_XS_DIG_CTRL1);
  for (i = 0; i < 2000; i++)
    {
      if ((xpcs_read(TC4XX_XPCS_PCS_VR_XS_DIG_CTRL1) &
           XPCS_PCS_VR_DIG_CTRL1_INIT) == 0)
        break;
      up_udelay(100);
    }

  if (i == 2000)
    {
      nerr("XPCS1 INIT bit did not self-clear\n");
      return -EIO;
    }

  /* Step 6: Hold TX clock during LPM cycle. */

  xpcs_modreg(0, XPCS_TX_GENCTRL1_TX_CLK_RDY_0,
              TC4XX_XPCS_PMA_TX_GENCTRL1);

  /* Step 7: Set LPM bit (bit 11) of SR_XS_PCS_CTRL1 to power down XPCS. */

  xpcs_modreg(XPCS_PCS_SR_CTRL1_LPM, XPCS_PCS_SR_CTRL1_LPM,
              TC4XX_XPCS_PCS_SR_XS_CTRL1);

  /* Step 8: Wait until PSEQ_STATE = 110B (power-down complete). */

  for (i = 0; i < 2000; i++)
    {
      uint32_t sts = xpcs_read(TC4XX_XPCS_PCS_VR_XS_DIG_STS);
      if ((sts & XPCS_PCS_VR_DIG_STS_PSEQ_MASK) ==
          XPCS_PCS_VR_DIG_STS_PSEQ_POWERDOWN)
        break;
      up_udelay(100);
    }

  if (i == 2000)
    {
      nwarn("XPCS1 PSEQ_STATE never reached 110B (continuing)\n");
    }

  /* Step 9: Write 0 (no read-modify-write) to SR_XS_PCS_CTRL1 to clear
   * LPM and power up XPCS.
   */

  xpcs_write(TC4XX_XPCS_PCS_SR_XS_CTRL1, 0);

  /* Step 10: Wait PSEQ_STATE = 100B (power-up complete). */

  for (i = 0; i < 2000; i++)
    {
      uint32_t sts = xpcs_read(TC4XX_XPCS_PCS_VR_XS_DIG_STS);
      if ((sts & XPCS_PCS_VR_DIG_STS_PSEQ_MASK) ==
          XPCS_PCS_VR_DIG_STS_PSEQ_POWERUP)
        break;
      up_udelay(100);
    }

  if (i == 2000)
    {
      nerr("XPCS1 PSEQ_STATE never reached 100B\n");
      return -EIO;
    }

  /* Step 11: Enable TX clock — data path unfrozen. */

  xpcs_modreg(XPCS_TX_GENCTRL1_TX_CLK_RDY_0, XPCS_TX_GENCTRL1_TX_CLK_RDY_0,
              TC4XX_XPCS_PMA_TX_GENCTRL1);

  /* Set MAC_AUTO_SW=1 so XPCS auto-switches to the negotiated SGMII
   * speed after CL37 AN completes (per TRM VR_XS_PCS_DIG_CTRL1 desc).
   */

  xpcs_modreg(XPCS_PCS_VR_DIG_CTRL1_MAC_AUTO_SW,
              XPCS_PCS_VR_DIG_CTRL1_MAC_AUTO_SW,
              TC4XX_XPCS_PCS_VR_XS_DIG_CTRL1);

  /* VR_MII_AN_CTRL.PCS_MODE = 2 -> SGMII with auto-negotiation. */

  xpcs_modreg(XPCS_VR_AN_CTRL_PCS_MODE_SGMII,
              XPCS_VR_AN_CTRL_PCS_MODE_MASK,
              TC4XX_XPCS_MII_VR_AN_CTRL);

  return 0;
}
