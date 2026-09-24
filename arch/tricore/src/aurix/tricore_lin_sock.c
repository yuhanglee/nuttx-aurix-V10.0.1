/****************************************************************************
 * arch/tricore/src/aurix/tricore_lin_sock.c
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

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <float.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/wqueue.h>
#include <nuttx/can.h>
#include <nuttx/lin.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/can.h>

#include <sys/param.h>

#include "tricore_internal.h"
#include "tricore_asclin.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if !defined(CONFIG_SCHED_WORKQUEUE)
#  error Work queue support is required
#endif

#define LINWORK                     LPWORK

#define TRICORE_LIN_ID_MASK         0x3f
#define TRICORE_LIN_RX_PID_ECHO     1u

#define TRICORE_LIN_CLK_TIMEOUT     100000u
#define TRICORE_LIN_PRESCALER       CONFIG_TRICORE_LIN_PRESCALER
#define TRICORE_LIN_OVERSAMPLING    16u
#define TRICORE_LIN_SAMPLEPOINT     8u
#define TRICORE_LIN_BRG_MAX         4095
#define TRICORE_LIN_BRG_STEPS_MAX   16u
#define TRICORE_LIN_IDLE_DELAY      2u
#define TRICORE_LIN_LEAD_DELAY      1u
#define TRICORE_LIN_BREAK_BITS      13u
#define TRICORE_LIN_HEADER_TIMEOUT  0xffu
#define TRICORE_LIN_FRAME_TIMEOUT   0xffu

#define TRICORE_LIN_FRAMECON                                             \
  (ASCLIN_FRAMECON_MODE_LIN | ASCLIN_FRAMECON_STOP_1 |                   \
   ((TRICORE_LIN_IDLE_DELAY << ASCLIN_FRAMECON_IDLE_SHIFT) &             \
    ASCLIN_FRAMECON_IDLE_MASK) |                                         \
   ((TRICORE_LIN_LEAD_DELAY << ASCLIN_FRAMECON_LEAD_SHIFT) &             \
    ASCLIN_FRAMECON_LEAD_MASK))

#define TRICORE_LIN_TXFIFO_IDLE     ASCLIN_TXFIFOCON_INW_1
#define TRICORE_LIN_RXFIFO_IDLE     ASCLIN_RXFIFOCON_OUTW_1

#define TRICORE_LIN_TXFIFO_ARM                                           \
  (ASCLIN_TXFIFOCON_INW_1 | ASCLIN_TXFIFOCON_FLUSH | ASCLIN_TXFIFOCON_ENO)

#define TRICORE_LIN_RXFIFO_ARM                                           \
  (ASCLIN_RXFIFOCON_OUTW_1 | ASCLIN_RXFIFOCON_FLUSH | ASCLIN_RXFIFOCON_ENI)

#define TRICORE_LIN_FLAGS_ERR                                            \
  (ASCLIN_FLAGS_LP | ASCLIN_FLAGS_LC | ASCLIN_FLAGS_CE |                 \
   ASCLIN_FLAGS_FE | ASCLIN_FLAGS_HT | ASCLIN_FLAGS_RT |                 \
   ASCLIN_FLAGS_RFO | ASCLIN_FLAGS_TFO)

#define TRICORE_LIN_EN_ERR                                               \
  (ASCLIN_FLAGSENABLE_LPE | ASCLIN_FLAGSENABLE_LCE |                     \
   ASCLIN_FLAGSENABLE_CEE | ASCLIN_FLAGSENABLE_FEE |                     \
   ASCLIN_FLAGSENABLE_HTE | ASCLIN_FLAGSENABLE_RTE |                     \
   ASCLIN_FLAGSENABLE_RFOE | ASCLIN_FLAGSENABLE_TFOE)

#define TRICORE_LIN_EN_IDLE         TRICORE_LIN_EN_ERR

#define TRICORE_LIN_EN_TX                                                \
  (TRICORE_LIN_EN_ERR | ASCLIN_FLAGSENABLE_THE | ASCLIN_FLAGSENABLE_TRE)

#define TRICORE_LIN_EN_RX                                                \
  (TRICORE_LIN_EN_ERR | ASCLIN_FLAGSENABLE_THE | ASCLIN_FLAGSENABLE_RRE)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tricore_lin_s
{
  uint32_t base;
  uint32_t baud;
  uint32_t sr;
  uint32_t stuck;
  uint8_t  port;
  uint8_t  alti;
  uint8_t  rxirq;
  uint8_t  txirq;
  uint8_t  erirq;
  uint8_t  lin_id;
  uint8_t  lin_dlc;
  bool     bifup;
  bool     txbusy;
  bool     lin_ho;

  struct net_driver_s dev;

  struct work_s irqwork;
  struct work_s errwork;

  struct can_frame *txdesc;
  struct can_frame *rxdesc;

  uint8_t tx_pool[sizeof(struct can_frame)];
  uint8_t rx_pool[sizeof(struct can_frame)];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static uint32_t tricore_lin_getreg(struct tricore_lin_s *priv, int offset);
static void tricore_lin_putreg(struct tricore_lin_s *priv, int offset,
                               uint32_t value);
static int  tricore_lin_setclk(struct tricore_lin_s *priv, uint32_t clksel);
static void tricore_lin_setbaud(struct tricore_lin_s *priv);
static int  tricore_lin_setup(struct tricore_lin_s *priv);
static void tricore_lin_shutdown(struct tricore_lin_s *priv);

static int  tricore_lin_transmit(struct tricore_lin_s *priv);
static int  tricore_lin_txpoll(struct net_driver_s *dev);
static void tricore_lin_txdone_work(void *arg);
static void tricore_lin_rxdone_work(void *arg);
#ifdef CONFIG_NET_CAN_ERRORS
static void tricore_lin_err_work(void *arg);
#endif
static int  tricore_lin_interrupt(int irq, void *context, void *arg);

static int  tricore_lin_ifup(struct net_driver_s *dev);
static int  tricore_lin_ifdown(struct net_driver_s *dev);
static int  tricore_lin_txavail(struct net_driver_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define TRICORE_LIN_INSTANCE(n)                                          \
  {                                                                      \
    .port      = (n),                                                    \
    .base      = TRICORE_ASCLIN_BASE(n),                                 \
    .baud      = CONFIG_TRICORE_LIN##n##_BAUD,                           \
    .rxirq     = TRICORE_ASCLIN_RXIRQ(n),                                \
    .txirq     = TRICORE_ASCLIN_TXIRQ(n),                                \
    .erirq     = TRICORE_ASCLIN_ERIRQ(n),                                \
    .dev       =                                                         \
      {                                                                  \
        .d_ifname  = "lin%d",                                            \
        .d_ifup    = tricore_lin_ifup,                                   \
        .d_ifdown  = tricore_lin_ifdown,                                 \
        .d_txavail = tricore_lin_txavail,                                \
      }                                                                  \
  }

static struct tricore_lin_s g_linpriv[] =
{
#ifdef CONFIG_TRICORE_LIN0
  TRICORE_LIN_INSTANCE(0),
#endif
#ifdef CONFIG_TRICORE_LIN1
  TRICORE_LIN_INSTANCE(1),
#endif
#ifdef CONFIG_TRICORE_LIN4
  TRICORE_LIN_INSTANCE(4),
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t tricore_lin_getreg(struct tricore_lin_s *priv, int offset)
{
  return getreg32(priv->base + offset);
}

static void tricore_lin_putreg(struct tricore_lin_s *priv, int offset,
                               uint32_t value)
{
  putreg32(value, priv->base + offset);
}

static int tricore_lin_setclk(struct tricore_lin_s *priv, uint32_t clksel)
{
  uint32_t timeout = TRICORE_LIN_CLK_TIMEOUT;
  uint32_t want = (clksel != ASCLIN_CSR_CLKSEL_NONE) ? ASCLIN_CSR_CON : 0;

  tricore_lin_putreg(priv, ASCLIN_CSR_OFFSET, clksel);

  while (timeout--)
    {
      if ((tricore_lin_getreg(priv, ASCLIN_CSR_OFFSET) & ASCLIN_CSR_CON)
          == want)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

static void tricore_lin_setbaud(struct tricore_lin_s *priv)
{
  uint32_t fpd = CONFIG_TRICORE_ASCLINF_FREQUENCY /
                 TRICORE_LIN_PRESCALER;
  uint32_t fovs = priv->baud * TRICORE_LIN_OVERSAMPLING;
  uint32_t numerator;
  uint32_t denominator;
  int32_t m[2][2];
  int32_t ai;
  int32_t t;
  uint32_t steps = 0;
  float div;

  div = (float)fovs / (float)fpd;

  m[0][0] = 1;
  m[0][1] = 0;
  m[1][0] = 0;
  m[1][1] = 1;

  while (steps++ < TRICORE_LIN_BRG_STEPS_MAX &&
         m[1][0] * (ai = (int32_t)div) + m[1][1] <= TRICORE_LIN_BRG_MAX)
    {
      t = m[0][0] * ai + m[0][1];
      m[0][1] = m[0][0];
      m[0][0] = t;

      t = m[1][0] * ai + m[1][1];
      m[1][1] = m[1][0];
      m[1][0] = t;

      if ((div - (float)ai) < FLT_EPSILON)
        {
          break;
        }

      div = 1.0f / (div - (float)ai);
    }

  numerator = (uint32_t)m[0][0];
  denominator = (uint32_t)m[1][0];

  if (numerator == 0)
    {
      numerator = 1;
    }

  if (denominator == 0)
    {
      denominator = 1;
    }

  tricore_lin_putreg(priv, ASCLIN_BRG_OFFSET,
                     ASCLIN_BRG_VAL(numerator, denominator));
}

static int tricore_lin_setup(struct tricore_lin_s *priv)
{
  uint32_t timeout = TRICORE_LIN_CLK_TIMEOUT;
  uint32_t regval;
  int ret;

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
  aurix_cpu_endinit_enable(false);
#endif
  regval = tricore_lin_getreg(priv, ASCLIN_CLC_OFFSET);
  regval &= ~ASCLIN_CLC_DISR;
  tricore_lin_putreg(priv, ASCLIN_CLC_OFFSET, regval);
#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
  aurix_cpu_endinit_enable(true);
#endif

  while ((tricore_lin_getreg(priv, ASCLIN_CLC_OFFSET) &
          ASCLIN_CLC_DISS) != 0)
    {
      if (timeout-- == 0)
        {
          nerr("ERROR: LIN%d CLC DISS stuck\n", priv->port);

          return -ETIMEDOUT;
        }
    }

  ret = board_aurix_setup_lin_pin(priv->port);
  if (ret < 0)
    {
      nerr("ERROR: LIN%d pins: %d\n", priv->port, ret);

      return ret;
    }

  priv->alti = (uint8_t)ret;

  ret = tricore_lin_setclk(priv, ASCLIN_CSR_CLKSEL_NONE);
  if (ret < 0)
    {
      nerr("ERROR: LIN%d clk stop: %d\n", priv->port, ret);

      return ret;
    }

  tricore_lin_putreg(priv, ASCLIN_FRAMECON_OFFSET, 0);

  regval = ((TRICORE_LIN_PRESCALER - 1u) << ASCLIN_BITCON_PRESCALER_SHIFT)
           & ASCLIN_BITCON_PRESCALER_MASK;
  regval |= ((TRICORE_LIN_OVERSAMPLING - 1u)
             << ASCLIN_BITCON_OVERSAMPLING_SHIFT)
            & ASCLIN_BITCON_OVERSAMPLING_MASK;
  regval |= (TRICORE_LIN_SAMPLEPOINT << ASCLIN_BITCON_SAMPLEPOINT_SHIFT)
            & ASCLIN_BITCON_SAMPLEPOINT_MASK;
  tricore_lin_putreg(priv, ASCLIN_BITCON_OFFSET, regval);

  tricore_lin_setbaud(priv);

  regval = (uint32_t)priv->alti & ASCLIN_IOCR_ALTI_MASK;
  tricore_lin_putreg(priv, ASCLIN_IOCR_OFFSET, regval);

  tricore_lin_putreg(priv, ASCLIN_FRAMECON_OFFSET, TRICORE_LIN_FRAMECON);

  tricore_lin_putreg(priv, ASCLIN_DATCON_OFFSET,
                     ASCLIN_DATCON_RESPONSE(TRICORE_LIN_FRAME_TIMEOUT));

  tricore_lin_putreg(priv, ASCLIN_LINCON_OFFSET,
                     ASCLIN_LINCON_CSEN | ASCLIN_LINCON_MS);

  tricore_lin_putreg(priv, ASCLIN_LINBTIMER_OFFSET,
                     TRICORE_LIN_BREAK_BITS & ASCLIN_LINBTIMER_BREAK_MASK);

  tricore_lin_putreg(priv, ASCLIN_LINHTIMER_OFFSET,
                     TRICORE_LIN_HEADER_TIMEOUT &
                     ASCLIN_LINHTIMER_HEADER_MASK);

  tricore_lin_putreg(priv, ASCLIN_TXFIFOCON_OFFSET,
                     TRICORE_LIN_TXFIFO_IDLE | ASCLIN_TXFIFOCON_FLUSH);
  tricore_lin_putreg(priv, ASCLIN_RXFIFOCON_OFFSET,
                     TRICORE_LIN_RXFIFO_IDLE | ASCLIN_RXFIFOCON_FLUSH);

  tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET, 0);
  tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                     ASCLIN_FLAGSCLEAR_ALL);

  ret = tricore_lin_setclk(priv, CONFIG_TRICORE_LIN_CLK_SRC);
  if (ret < 0)
    {
      nerr("ERROR: LIN%d clk start: %d\n", priv->port, ret);
    }

  return ret;
}

static void tricore_lin_shutdown(struct tricore_lin_s *priv)
{
  uint32_t regval;

  tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET, 0);
  tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                     ASCLIN_FLAGSCLEAR_ALL);

  tricore_lin_setclk(priv, ASCLIN_CSR_CLKSEL_NONE);

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
  aurix_cpu_endinit_enable(false);
#endif
  regval = tricore_lin_getreg(priv, ASCLIN_CLC_OFFSET);
  regval |= ASCLIN_CLC_DISR;
  tricore_lin_putreg(priv, ASCLIN_CLC_OFFSET, regval);
#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
  aurix_cpu_endinit_enable(true);
#endif
}

static int tricore_lin_transmit(struct tricore_lin_s *priv)
{
  struct can_frame *frame = (struct can_frame *)priv->dev.d_buf;
  uint32_t datcon;
  bool lin_rx;
  uint8_t i;

  if (frame == NULL || frame->can_dlc > CAN_MAX_DLEN)
    {
      return -EINVAL;
    }

  if (priv->txbusy)
    {
      return -EBUSY;
    }

  priv->lin_id = frame->can_id & TRICORE_LIN_ID_MASK;
  priv->lin_dlc = frame->can_dlc;
  priv->lin_ho = (priv->lin_dlc == 0);
  lin_rx = (frame->can_id & CAN_RTR_FLAG) != 0;

  datcon = ASCLIN_DATCON_RESPONSE(TRICORE_LIN_FRAME_TIMEOUT);

  if ((frame->can_id & LIN_CHECKSUM_EXTENDED) != 0)
    {
      datcon |= ASCLIN_DATCON_CSM;
    }

  if (priv->lin_ho)
    {
      datcon |= ASCLIN_DATCON_HO;
    }
  else
    {
      datcon |= ASCLIN_DATCON_DATLEN(priv->lin_dlc);
    }

  tricore_lin_putreg(priv, ASCLIN_TXFIFOCON_OFFSET, TRICORE_LIN_TXFIFO_ARM);
  tricore_lin_putreg(priv, ASCLIN_RXFIFOCON_OFFSET, TRICORE_LIN_RXFIFO_ARM);

  tricore_lin_putreg(priv, ASCLIN_DATCON_OFFSET, datcon);
  tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                     ASCLIN_FLAGSCLEAR_ALL);

  tricore_lin_putreg(priv, ASCLIN_TXDATA_OFFSET, priv->lin_id);

  priv->txbusy = true;

  if (lin_rx || (priv->lin_dlc == 0))
    {
      tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                         (priv->lin_dlc ? TRICORE_LIN_EN_RX :
                                          TRICORE_LIN_EN_TX) & ~priv->stuck);
      tricore_lin_putreg(priv, ASCLIN_FLAGSSET_OFFSET,
                         ASCLIN_FLAGSSET_THRQS);

      return OK;
    }

  for (i = 0; i < priv->lin_dlc; i++)
    {
      tricore_lin_putreg(priv, ASCLIN_TXDATA_OFFSET, frame->data[i]);
    }

  tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                     TRICORE_LIN_EN_TX & ~priv->stuck);
  tricore_lin_putreg(priv, ASCLIN_FLAGSSET_OFFSET,
                     ASCLIN_FLAGSSET_THRQS | ASCLIN_FLAGSSET_TRRQS);

  return OK;
}

static int tricore_lin_txpoll(struct net_driver_s *dev)
{
  struct tricore_lin_s *priv;

  if (dev == NULL || dev->d_private == NULL)
    {
      return -EINVAL;
    }

  priv = (struct tricore_lin_s *)dev->d_private;

  if (priv->dev.d_len > 0)
    {
      tricore_lin_transmit(priv);

      return -EBUSY;
    }

  return 0;
}

static void tricore_lin_txdone_work(void *arg)
{
  struct tricore_lin_s *priv = (struct tricore_lin_s *)arg;

  if (priv == NULL)
    {
      return;
    }

  NETDEV_TXDONE(&priv->dev);

  net_lock();
  priv->dev.d_buf = (uint8_t *)priv->txdesc;
  devif_poll(&priv->dev, tricore_lin_txpoll);
  net_unlock();
}

static void tricore_lin_rxdone_work(void *arg)
{
  struct tricore_lin_s *priv = (struct tricore_lin_s *)arg;
  uint32_t fill;
  uint32_t i;

  if (priv == NULL)
    {
      return;
    }

  fill = ASCLIN_RX_FILL(tricore_lin_getreg(priv, ASCLIN_RXFIFOCON_OFFSET));

  if (fill >= TRICORE_LIN_RX_PID_ECHO)
    {
      tricore_lin_getreg(priv, ASCLIN_RXDATA_OFFSET);
      fill -= TRICORE_LIN_RX_PID_ECHO;
    }

  if (fill > priv->lin_dlc)
    {
      fill = priv->lin_dlc;
    }

  for (i = 0; i < fill; i++)
    {
      priv->rxdesc->data[i] =
        tricore_lin_getreg(priv, ASCLIN_RXDATA_OFFSET) & 0xff;
    }

  priv->rxdesc->can_id = priv->lin_id;
  priv->rxdesc->can_dlc = fill;

  net_lock();

  priv->dev.d_len = sizeof(struct can_frame);
  priv->dev.d_buf = (uint8_t *)priv->rxdesc;

  NETDEV_RXPACKETS(&priv->dev);
  can_input(&priv->dev);

  priv->dev.d_buf = (uint8_t *)priv->txdesc;
  devif_poll(&priv->dev, tricore_lin_txpoll);

  net_unlock();
}

#ifdef CONFIG_NET_CAN_ERRORS
static void tricore_lin_err_work(void *arg)
{
  struct tricore_lin_s *priv = (struct tricore_lin_s *)arg;
  struct can_frame *frame;
  uint32_t sr;

  if (priv == NULL)
    {
      return;
    }

  sr = priv->sr;
  frame = priv->rxdesc;

  nerr("ERROR: LIN%d flags=0x%08" PRIx32 " stuck=0x%08" PRIx32 "\n",
       priv->port, sr, priv->stuck);

  memset(frame, 0, sizeof(struct can_frame));

  frame->can_id = LIN_ERR_FLAG;
  frame->can_dlc = CAN_ERR_DLC;

  if ((sr & (ASCLIN_FLAGS_CE | ASCLIN_FLAGS_TFO | ASCLIN_FLAGS_HT)) != 0)
    {
      frame->data[0] |= LIN_ERR_TX;
    }

  if ((sr & (ASCLIN_FLAGS_LC | ASCLIN_FLAGS_RT | ASCLIN_FLAGS_LP)) != 0)
    {
      frame->data[0] |= LIN_ERR_RX;
    }

  if ((sr & (ASCLIN_FLAGS_FE | ASCLIN_FLAGS_RFO)) != 0)
    {
      frame->data[0] |= LIN_ERR_CRTL;
    }

  if ((sr & ASCLIN_FLAGS_HT) != 0)
    {
      frame->data[1] |= LIN_ERR_TX_PID_TMO;
    }

  if ((sr & ASCLIN_FLAGS_RT) != 0)
    {
      frame->data[2] |= LIN_ERR_RX_NO_RESPONSE;
    }

  if ((sr & ASCLIN_FLAGS_LC) != 0)
    {
      frame->data[2] |= LIN_ERR_RX_CKSUM;
    }

  if ((sr & ASCLIN_FLAGS_LP) != 0)
    {
      frame->data[2] |= LIN_ERR_RX_PID_PARITY;
    }

  if ((sr & ASCLIN_FLAGS_CE) != 0)
    {
      frame->data[3] |= LIN_ERR_BUS_DATA;
    }

  if ((sr & ASCLIN_FLAGS_RFO) != 0)
    {
      frame->data[4] |= LIN_ERR_CRTL_RXOVERFLOW;
    }

  if ((sr & ASCLIN_FLAGS_FE) != 0)
    {
      frame->data[4] |= LIN_ERR_CTRL_FRAMEERROR;
    }

  net_lock();

  priv->dev.d_len = sizeof(struct can_frame);
  priv->dev.d_buf = (uint8_t *)frame;

  NETDEV_ERRORS(&priv->dev);
  can_input(&priv->dev);

  priv->dev.d_buf = (uint8_t *)priv->txdesc;
  devif_poll(&priv->dev, tricore_lin_txpoll);

  net_unlock();
}
#endif

static int tricore_lin_interrupt(int irq, void *context, void *arg)
{
  struct tricore_lin_s *priv = (struct tricore_lin_s *)arg;
  uint32_t pending;
  uint32_t stuck;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  pending = tricore_lin_getreg(priv, ASCLIN_FLAGS_OFFSET) &
            tricore_lin_getreg(priv, ASCLIN_FLAGSENABLE_OFFSET);

  if ((pending & TRICORE_LIN_FLAGS_ERR) != 0)
    {
      priv->sr = pending;

      tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                         ASCLIN_FLAGSCLEAR_ALL);

      stuck = tricore_lin_getreg(priv, ASCLIN_FLAGS_OFFSET) & pending &
              TRICORE_LIN_FLAGS_ERR;
      priv->stuck |= stuck;

      tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                         TRICORE_LIN_EN_IDLE & ~priv->stuck);
      priv->txbusy = false;

#ifdef CONFIG_NET_CAN_ERRORS
      work_queue(LINWORK, &priv->errwork, tricore_lin_err_work, priv, 0);
#else
      work_queue(LINWORK, &priv->errwork, tricore_lin_txdone_work, priv, 0);
#endif

      return OK;
    }

  if ((pending & ASCLIN_FLAGS_TR) != 0)
    {
      tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                         ASCLIN_FLAGSCLEAR_ALL);
      tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                         TRICORE_LIN_EN_IDLE & ~priv->stuck);
      priv->txbusy = false;

      work_queue(LINWORK, &priv->irqwork, tricore_lin_txdone_work, priv, 0);

      return OK;
    }

  if ((pending & ASCLIN_FLAGS_RR) != 0)
    {
      tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                         ASCLIN_FLAGSCLEAR_ALL);
      tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                         TRICORE_LIN_EN_IDLE & ~priv->stuck);
      priv->txbusy = false;

      work_queue(LINWORK, &priv->irqwork, tricore_lin_rxdone_work, priv, 0);

      return OK;
    }

  if ((pending & ASCLIN_FLAGS_TH) != 0)
    {
      if (priv->lin_ho)
        {
          tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                             ASCLIN_FLAGSCLEAR_ALL);
          tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                             TRICORE_LIN_EN_IDLE & ~priv->stuck);
          priv->txbusy = false;

          work_queue(LINWORK, &priv->irqwork, tricore_lin_txdone_work,
                     priv, 0);

          return OK;
        }

      tricore_lin_putreg(priv, ASCLIN_FLAGSCLEAR_OFFSET,
                         ASCLIN_FLAGSCLEAR_THC);
    }

  return OK;
}

static int tricore_lin_ifup(struct net_driver_s *dev)
{
  struct tricore_lin_s *priv;
  int ret;

  if (dev == NULL || dev->d_private == NULL)
    {
      return -EINVAL;
    }

  priv = (struct tricore_lin_s *)dev->d_private;

  ret = tricore_lin_setup(priv);
  if (ret < 0)
    {
      nerr("ERROR: LIN%d setup: %d\n", priv->port, ret);

      return ret;
    }

  priv->txdesc = (struct can_frame *)priv->tx_pool;
  priv->rxdesc = (struct can_frame *)priv->rx_pool;
  priv->dev.d_buf = (uint8_t *)priv->txdesc;
  priv->txbusy = false;
  priv->stuck = 0;

  ret = irq_attach(priv->rxirq, tricore_lin_interrupt, priv);
  if (ret < 0)
    {
      goto errout;
    }

  ret = irq_attach(priv->txirq, tricore_lin_interrupt, priv);
  if (ret < 0)
    {
      irq_detach(priv->rxirq);
      goto errout;
    }

  ret = irq_attach(priv->erirq, tricore_lin_interrupt, priv);
  if (ret < 0)
    {
      irq_detach(priv->rxirq);
      irq_detach(priv->txirq);
      goto errout;
    }

  up_enable_irq(priv->rxirq);
  up_enable_irq(priv->txirq);
  up_enable_irq(priv->erirq);

  tricore_lin_putreg(priv, ASCLIN_FLAGSENABLE_OFFSET,
                     TRICORE_LIN_EN_IDLE & ~priv->stuck);

  priv->bifup = true;

  ninfo("LIN%d up, irq rx=%d tx=%d er=%d\n", priv->port,
        priv->rxirq, priv->txirq, priv->erirq);

  return OK;

errout:
  nerr("ERROR: LIN%d irq_attach: %d\n", priv->port, ret);
  tricore_lin_shutdown(priv);

  return ret;
}

static int tricore_lin_ifdown(struct net_driver_s *dev)
{
  struct tricore_lin_s *priv;

  if (dev == NULL || dev->d_private == NULL)
    {
      return -EINVAL;
    }

  priv = (struct tricore_lin_s *)dev->d_private;

  up_disable_irq(priv->rxirq);
  up_disable_irq(priv->txirq);
  up_disable_irq(priv->erirq);

  irq_detach(priv->rxirq);
  irq_detach(priv->txirq);
  irq_detach(priv->erirq);

  tricore_lin_shutdown(priv);

  priv->bifup = false;
  priv->txbusy = false;

  return OK;
}

static int tricore_lin_txavail(struct net_driver_s *dev)
{
  struct tricore_lin_s *priv;

  if (dev == NULL || dev->d_private == NULL)
    {
      return -EINVAL;
    }

  priv = (struct tricore_lin_s *)dev->d_private;

  net_lock();

  if (priv->bifup && !priv->txbusy)
    {
      priv->dev.d_buf = (uint8_t *)priv->txdesc;
      devif_poll(&priv->dev, tricore_lin_txpoll);
    }

  net_unlock();

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int weak_function board_aurix_setup_lin_pin(int idx)
{
  return ASCLIN_IOCR_ALTI_A;
}

int tricore_lin_sock_initialize(void)
{
  struct tricore_lin_s *priv;
  size_t i;
  int ret;

  for (i = 0; i < nitems(g_linpriv); i++)
    {
      priv = &g_linpriv[i];
      priv->dev.d_private = priv;

      ret = netdev_register(&priv->dev, NET_LL_CAN);
      if (ret < 0)
        {
          nerr("ERROR: LIN%d netdev_register: %d\n", priv->port, ret);

          return ret;
        }
    }

  return OK;
}
