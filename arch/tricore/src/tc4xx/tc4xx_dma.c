/****************************************************************************
 * arch/tricore/src/tc4xx/tc4xx_dma.c
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

/* TC4xx SDMA controller driver for the generic NuttX DMA framework
 * (include/nuttx/dma/dma.h).  Supports memory-to-memory, single peripheral
 * copy, and cyclic double-buffer (streaming receive) transfers.  Channels run
 * only when assigned to a valid resource partition, so tc4xx_dma_initialize()
 * marks RP0 valid and assigns every exposed channel to it.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/dma/dma.h>

#include "tricore_internal.h"
#include "tc4xx_dma.h"
#include "hardware/tc4xx_dma.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TC4XX_DMA_NCHANNELS
#  define CONFIG_TC4XX_DMA_NCHANNELS  8
#endif

#define TC4XX_DMA_NCHAN   CONFIG_TC4XX_DMA_NCHANNELS

#if TC4XX_DMA_NCHAN > TC4XX_DMA_NR_CHANNELS
#  error "CONFIG_TC4XX_DMA_NCHANNELS exceeds the number of DMA channels"
#endif

/* Upper bound on the SCH-triggered transaction wait.  A mem-to-mem
 * transaction of at most TREL_MAX moves completes in a few hundred cycles;
 * this is only a safety net against a mis-programmed channel.
 */

#define TC4XX_DMA_DONE_RETRIES   1000000

/* Optional transaction-end interrupt.  Set CONFIG_TC4XX_DMA_IRQ_BASE to the
 * interrupt-router service-request-node index of DMA channel 0's
 * transaction-end interrupt (channel c uses IRQ_BASE + c) to run the driver
 * interrupt-driven.  Left at 0 (the default) the mem-to-mem path completes
 * synchronously by polling TSR.CH and needs no interrupt-router wiring.
 */

#ifndef CONFIG_TC4XX_DMA_IRQ_BASE
#  define CONFIG_TC4XX_DMA_IRQ_BASE   0
#endif

#if CONFIG_TC4XX_DMA_IRQ_BASE > 0
#  define TC4XX_DMA_HAVE_IRQ       1
#  define TC4XX_DMA_CHAN_IRQ(c)    (CONFIG_TC4XX_DMA_IRQ_BASE + (c))
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tc4xx_dmach_s
{
  struct dma_chan_s    chan;        /* Must be first: framework handle */
  uint8_t              channel;     /* Hardware channel number */
  bool                 inuse;       /* Channel allocated */

  /* Attributes captured by DMA_CONFIG() */

  uint8_t              dir;         /* DMA_MEM_TO_MEM / MEM_TO_DEV / DEV_TO_MEM */
  uint8_t              width;       /* Bytes per move (1/2/4/8) */
  uint8_t              blkm_moves;  /* DMA moves per transfer (1..16) */
  uint8_t              blkm_enc;    /* CHCFGR.BLKM encoding of blkm_moves */
  int8_t               src_step;    /* >0 increment source, 0 hold fixed */
  int8_t               dst_step;    /* >0 increment dest, 0 hold fixed */
  bool                 cyclic;      /* Continuous double-buffer transfer */

  /* Active transfer bookkeeping */

  size_t               xfrlen;      /* Single-shot transfer length in bytes */
  size_t               period;      /* Cyclic period (half-buffer) length */
  dma_callback_t       callback;
  FAR void            *arg;
};

struct tc4xx_dma_s
{
  struct dma_dev_s     dev;         /* Must be first: framework device */
  bool                 initialised;
  spinlock_t           lock;
  struct tc4xx_dmach_s channels[TC4XX_DMA_NCHAN];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int      tc4xx_dma_config(FAR struct dma_chan_s *chan,
                                 FAR const struct dma_config_s *cfg);
static int      tc4xx_dma_start(FAR struct dma_chan_s *chan,
                                dma_callback_t callback, FAR void *arg,
                                uintptr_t dst, uintptr_t src, size_t len);
static int      tc4xx_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                dma_callback_t callback, FAR void *arg,
                                uintptr_t dst, uintptr_t src,
                                size_t len, size_t period_len);
static int      tc4xx_dma_stop(FAR struct dma_chan_s *chan);
static size_t   tc4xx_dma_residual(FAR struct dma_chan_s *chan);

static FAR struct dma_chan_s *tc4xx_dma_get_chan(FAR struct dma_dev_s *dev,
                                                 unsigned int ident);
static void     tc4xx_dma_put_chan(FAR struct dma_dev_s *dev,
                                   FAR struct dma_chan_s *chan);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct dma_ops_s g_tc4xx_dma_ops =
{
  .config       = tc4xx_dma_config,
  .start        = tc4xx_dma_start,
  .start_cyclic = tc4xx_dma_start_cyclic,
  .stop         = tc4xx_dma_stop,
  .residual     = tc4xx_dma_residual,
};

static struct tc4xx_dma_s g_tc4xx_dma =
{
  .dev =
    {
      .get_chan = tc4xx_dma_get_chan,
      .put_chan = tc4xx_dma_put_chan,
    },
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4xx_dma_width2chdw
 *
 * Description:
 *   Convert a transfer width in bytes (1/2/4/8) to the CHCFGR.CHDW encoding
 *   (log2 of the byte width).  Returns a negative errno on an invalid width.
 *
 ****************************************************************************/

static int tc4xx_dma_width2chdw(unsigned int width)
{
  switch (width)
    {
      case 1:
        return DMA_CHDW_8BIT;
      case 2:
        return DMA_CHDW_16BIT;
      case 4:
        return DMA_CHDW_32BIT;
      case 8:
        return DMA_CHDW_64BIT;
      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: tc4xx_dma_moves2blkm
 *
 * Description:
 *   Convert a burst length in DMA moves (1/2/3/4/5/8/9/16) to the
 *   CHCFGR.BLKM encoding.  Returns a negative errno on an invalid value.
 *
 ****************************************************************************/

static int tc4xx_dma_moves2blkm(unsigned int moves)
{
  switch (moves)
    {
      case 1:
        return DMA_BLKM_1_MOVE;
      case 2:
        return DMA_BLKM_2_MOVES;
      case 3:
        return DMA_BLKM_3_MOVES;
      case 4:
        return DMA_BLKM_4_MOVES;
      case 5:
        return DMA_BLKM_5_MOVES;
      case 8:
        return DMA_BLKM_8_MOVES;
      case 9:
        return DMA_BLKM_9_MOVES;
      case 16:
        return DMA_BLKM_16_MOVES;
      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: tc4xx_dma_reset
 *
 * Description:
 *   Reset a channel's transaction state and wait for the reset to take.
 *
 ****************************************************************************/

static int tc4xx_dma_reset(uint8_t channel)
{
  int retries = 100;

  putreg32(DMA_TSR_RST, TC4XX_DMA_TSR(channel));

  while ((getreg32(TC4XX_DMA_TSR(channel)) & DMA_TSR_RST) != 0)
    {
      if (--retries <= 0)
        {
          return -EIO;
        }

      up_udelay(1);
    }

  return OK;
}

/****************************************************************************
 * Name: tc4xx_dma_config
 *
 * Description:
 *   Capture the transfer attributes for the channel.  This step supports
 *   memory-to-memory transfers only.
 *
 ****************************************************************************/

static int tc4xx_dma_config(FAR struct dma_chan_s *chan,
                            FAR const struct dma_config_s *cfg)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  unsigned int width;
  unsigned int moves;
  int blkm;

  DEBUGASSERT(dmach != NULL && cfg != NULL);

  switch (cfg->direction)
    {
      case DMA_MEM_TO_MEM:
        dmach->src_step = 1;
        dmach->dst_step = 1;
        break;
      case DMA_MEM_TO_DEV:
        dmach->src_step = 1;
        dmach->dst_step = 0;
        break;
      case DMA_DEV_TO_MEM:
        dmach->src_step = 0;
        dmach->dst_step = 1;
        break;
      default:
        return -ENOTSUP;
    }

  dmach->dir = cfg->direction;

  width = cfg->src_width ? cfg->src_width : cfg->dst_width;
  if (cfg->src_width && cfg->dst_width && cfg->src_width != cfg->dst_width)
    {
      return -EINVAL;
    }

  width = width ? width : 1;
  if (tc4xx_dma_width2chdw(width) < 0)
    {
      return -EINVAL;
    }

  moves = cfg->option ? cfg->option : 1;
  blkm  = tc4xx_dma_moves2blkm(moves);
  if (blkm < 0)
    {
      return -EINVAL;
    }

  dmach->width      = width;
  dmach->blkm_moves = (uint8_t)moves;
  dmach->blkm_enc   = (uint8_t)blkm;
  return OK;
}

/****************************************************************************
 * Name: tc4xx_dma_start
 *
 * Description:
 *   Program and run a memory-to-memory transfer.  The transfer is triggered
 *   by a software request (CHCSR.SCH).  With the transaction-end interrupt
 *   configured the callback runs from the ISR; otherwise this function waits
 *   for completion and invokes the callback synchronously.
 *
 ****************************************************************************/

static int tc4xx_dma_start(FAR struct dma_chan_s *chan,
                           dma_callback_t callback, FAR void *arg,
                           uintptr_t dst, uintptr_t src, size_t len)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  uint8_t channel;
  uint32_t adicr;
  uint32_t chcfgr;
  uint32_t moves;
  uint32_t trel;
  int chdw;
#ifndef TC4XX_DMA_HAVE_IRQ
  int retries;
#endif

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  chdw = tc4xx_dma_width2chdw(dmach->width);
  if (chdw < 0)
    {
      return chdw;
    }

  moves = (len + dmach->width - 1) / dmach->width;
  trel  = (moves + dmach->blkm_moves - 1) / dmach->blkm_moves;
  if (trel == 0 || trel > DMA_CHCFGR_TREL_MAX)
    {
      return -EINVAL;
    }

  dmach->cyclic   = false;
  dmach->callback = callback;
  dmach->arg      = arg;
  dmach->xfrlen   = trel * dmach->blkm_moves * dmach->width;

  if (tc4xx_dma_reset(channel) < 0)
    {
      return -EIO;
    }

  adicr  = (DMA_ADICR_INTCT_TRANSACTION_END << DMA_ADICR_INTCT_SHIFT);
  adicr |= (dmach->src_step > 0) ? DMA_ADICR_INCS : DMA_ADICR_SCBE;
  adicr |= (dmach->dst_step > 0) ? DMA_ADICR_INCD : DMA_ADICR_DCBE;

  chcfgr  = ((uint32_t)chdw << DMA_CHCFGR_CHDW_SHIFT);
  chcfgr |= ((uint32_t)dmach->blkm_enc << DMA_CHCFGR_BLKM_SHIFT);
  chcfgr |= (trel << DMA_CHCFGR_TREL_SHIFT) & DMA_CHCFGR_TREL_MASK;

  if (dmach->dir == DMA_MEM_TO_MEM)
    {
      chcfgr |= DMA_CHCFGR_RROAT;
    }

  putreg32(src,    TC4XX_DMA_SADR(channel));
  putreg32(dst,    TC4XX_DMA_DADR(channel));
  putreg32(adicr,  TC4XX_DMA_ADICR(channel));
  putreg32(chcfgr, TC4XX_DMA_CHCFGR(channel));

  dmainfo("ch %u: dir %u SADR 0x%08lx DADR 0x%08lx TREL %lu BLKM %u moves\n",
          channel, dmach->dir, (unsigned long)src, (unsigned long)dst,
          (unsigned long)trel, dmach->blkm_moves);

  if (dmach->dir != DMA_MEM_TO_MEM)
    {
      putreg32(DMA_TSR_ECH, TC4XX_DMA_TSR(channel));
      return OK;
    }

  putreg32(DMA_CHCSR_SCH, TC4XX_DMA_CHCSR(channel));

#ifdef TC4XX_DMA_HAVE_IRQ
  return OK;
#else
  retries = TC4XX_DMA_DONE_RETRIES;
  while ((getreg32(TC4XX_DMA_TSR(channel)) & DMA_TSR_CH) != 0)
    {
      if (--retries <= 0)
        {
          dmaerr("ch %u: transaction did not complete\n", channel);
          if (callback != NULL)
            {
              callback(&dmach->chan, arg, -ETIMEDOUT);
            }

          return -ETIMEDOUT;
        }
    }

  putreg32(DMA_CHCSR_CICH, TC4XX_DMA_CHCSR(channel));

  if (callback != NULL)
    {
      callback(&dmach->chan, arg, (ssize_t)dmach->xfrlen);
    }

  return OK;
#endif
}

/****************************************************************************
 * Name: tc4xx_dma_start_cyclic
 *
 * Description:
 *   Start a continuous peripheral-to-memory transfer that ping-pongs between
 *   two half-buffers.  'len' is the total buffer length and must be exactly
 *   two periods; the hardware auto-switches between DADR (buffer 0) and SHADR
 *   (buffer 1) at every transaction end, raising the channel interrupt so the
 *   consumer can drain the half that just filled.
 *
 ****************************************************************************/

static int tc4xx_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                  dma_callback_t callback, FAR void *arg,
                                  uintptr_t dst, uintptr_t src,
                                  size_t len, size_t period_len)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  uint8_t channel;
  uint32_t adicr;
  uint32_t chcfgr;
  uint32_t trel;
  int chdw;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  if (dmach->dir != DMA_DEV_TO_MEM)
    {
      return -ENOTSUP;
    }

  if (period_len == 0 || len != 2 * period_len)
    {
      return -EINVAL;
    }

  chdw = tc4xx_dma_width2chdw(dmach->width);
  if (chdw < 0)
    {
      return chdw;
    }

  trel = (period_len + dmach->width - 1) / dmach->width;
  if (trel == 0 || trel > DMA_CHCFGR_TREL_MAX)
    {
      return -EINVAL;
    }

  dmach->cyclic   = true;
  dmach->callback = callback;
  dmach->arg      = arg;
  dmach->period   = trel * dmach->width;

  if (tc4xx_dma_reset(channel) < 0)
    {
      return -EIO;
    }

  adicr  = (DMA_ADICR_INTCT_TRANSACTION_END << DMA_ADICR_INTCT_SHIFT);
  adicr |= (dmach->src_step > 0) ? DMA_ADICR_INCS : DMA_ADICR_SCBE;
  adicr |= (dmach->dst_step > 0) ? DMA_ADICR_INCD : DMA_ADICR_DCBE;
  adicr |= (DMA_ADICR_SHCT_DOUBLE_DEST_AUTOSW << DMA_ADICR_SHCT_SHIFT);

  chcfgr  = ((uint32_t)chdw << DMA_CHCFGR_CHDW_SHIFT);
  chcfgr |= ((uint32_t)dmach->blkm_enc << DMA_CHCFGR_BLKM_SHIFT);
  chcfgr |= (trel << DMA_CHCFGR_TREL_SHIFT) & DMA_CHCFGR_TREL_MASK;
  chcfgr |= DMA_CHCFGR_CHMODE;

  putreg32(src,    TC4XX_DMA_SADR(channel));
  putreg32(dst,    TC4XX_DMA_DADR(channel));
  putreg32(adicr,  TC4XX_DMA_ADICR(channel));
  putreg32(chcfgr, TC4XX_DMA_CHCFGR(channel));

  putreg32(dst + period_len, TC4XX_DMA_SHADR(channel));
  modreg32(DMA_TSR_ETRL, DMA_TSR_ETRL, TC4XX_DMA_TSR(channel));

  dmainfo("ch %u: cyclic DADR 0x%08lx SHADR 0x%08lx period %lu\n",
          channel, (unsigned long)dst,
          (unsigned long)(dst + period_len), (unsigned long)dmach->period);

  putreg32(DMA_TSR_ECH, TC4XX_DMA_TSR(channel));
  return OK;
}

/****************************************************************************
 * Name: tc4xx_dma_done_isr
 *
 * Description:
 *   Per-channel transaction-end interrupt handler.  Attached on channel
 *   allocation; the handler argument is the channel object.
 *
 ****************************************************************************/

#ifdef TC4XX_DMA_HAVE_IRQ
static int tc4xx_dma_done_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)arg;
  uint8_t channel;

  UNUSED(irq);
  UNUSED(context);
  DEBUGASSERT(dmach != NULL);

  channel = dmach->channel;

  if (dmach->cyclic)
    {
      if (dmach->callback != NULL)
        {
          dmach->callback(&dmach->chan, dmach->arg, (ssize_t)dmach->period);
        }

      modreg32(0, DMA_CHCSR_FROZEN, TC4XX_DMA_CHCSR(channel));
      return OK;
    }

  putreg32(DMA_CHCSR_CICH, TC4XX_DMA_CHCSR(channel));

  if (dmach->callback != NULL)
    {
      dmach->callback(&dmach->chan, dmach->arg, (ssize_t)dmach->xfrlen);
    }

  return OK;
}
#endif

/****************************************************************************
 * Name: tc4xx_dma_stop
 ****************************************************************************/

static int tc4xx_dma_stop(FAR struct dma_chan_s *chan)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  uint8_t channel;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  putreg32(DMA_TSR_DCH, TC4XX_DMA_TSR(channel));
  if (tc4xx_dma_reset(channel) < 0)
    {
      return -EIO;
    }

  putreg32(DMA_CHCSR_CICH, TC4XX_DMA_CHCSR(channel));

  dmach->callback = NULL;
  dmach->cyclic   = false;

  dmainfo("ch %u: stopped\n", channel);
  return OK;
}

/****************************************************************************
 * Name: tc4xx_dma_residual
 *
 * Description:
 *   Return the number of bytes still to be transferred.  CHCSR.TCOUNT is the
 *   number of remaining DMA transfers; each transfer moves blkm_moves moves
 *   of 'width' bytes, so residual = TCOUNT * blkm_moves * width.
 *
 ****************************************************************************/

static size_t tc4xx_dma_residual(FAR struct dma_chan_s *chan)
{
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  uint32_t tcount;

  DEBUGASSERT(dmach != NULL);

  tcount = (getreg32(TC4XX_DMA_CHCSR(dmach->channel)) &
            DMA_CHCSR_TCOUNT_MASK) >> DMA_CHCSR_TCOUNT_SHIFT;

  return (size_t)tcount * dmach->blkm_moves * dmach->width;
}

/****************************************************************************
 * Name: tc4xx_dma_get_chan
 ****************************************************************************/

static FAR struct dma_chan_s *tc4xx_dma_get_chan(FAR struct dma_dev_s *dev,
                                                 unsigned int ident)
{
  FAR struct tc4xx_dma_s *ctrl = (FAR struct tc4xx_dma_s *)dev;
  FAR struct tc4xx_dmach_s *dmach;
  irqstate_t flags;

  if (ident >= TC4XX_DMA_NCHAN)
    {
      dmaerr("invalid channel %u\n", ident);
      return NULL;
    }

  flags = spin_lock_irqsave(&ctrl->lock);

  dmach = &ctrl->channels[ident];
  if (dmach->inuse)
    {
      spin_unlock_irqrestore(&ctrl->lock, flags);
      dmaerr("channel %u already in use\n", ident);
      return NULL;
    }

  dmach->inuse      = true;
  dmach->callback   = NULL;
  dmach->arg        = NULL;
  dmach->dir        = DMA_MEM_TO_MEM;
  dmach->cyclic     = false;
  dmach->blkm_moves = 1;
  dmach->blkm_enc   = DMA_BLKM_1_MOVE;
  dmach->src_step   = 1;
  dmach->dst_step   = 1;

  spin_unlock_irqrestore(&ctrl->lock, flags);

#ifdef TC4XX_DMA_HAVE_IRQ
  irq_attach(TC4XX_DMA_CHAN_IRQ(ident), tc4xx_dma_done_isr, dmach);
  up_enable_irq(TC4XX_DMA_CHAN_IRQ(ident));

  dmainfo("acquired channel %u (irq %d)\n", ident, TC4XX_DMA_CHAN_IRQ(ident));
#else
  dmainfo("acquired channel %u\n", ident);
#endif
  return &dmach->chan;
}

/****************************************************************************
 * Name: tc4xx_dma_put_chan
 ****************************************************************************/

static void tc4xx_dma_put_chan(FAR struct dma_dev_s *dev,
                               FAR struct dma_chan_s *chan)
{
  FAR struct tc4xx_dma_s *ctrl = (FAR struct tc4xx_dma_s *)dev;
  FAR struct tc4xx_dmach_s *dmach = (FAR struct tc4xx_dmach_s *)chan;
  irqstate_t flags;

  DEBUGASSERT(dmach != NULL);

  tc4xx_dma_stop(chan);

#ifdef TC4XX_DMA_HAVE_IRQ
  up_disable_irq(TC4XX_DMA_CHAN_IRQ(dmach->channel));
  irq_detach(TC4XX_DMA_CHAN_IRQ(dmach->channel));
#endif

  flags = spin_lock_irqsave(&ctrl->lock);
  dmach->inuse    = false;
  dmach->callback = NULL;
  dmach->arg      = NULL;
  spin_unlock_irqrestore(&ctrl->lock, flags);

  dmainfo("released channel %u\n", dmach->channel);
}

/****************************************************************************
 * Name: tc4xx_dma_clk_enable
 *
 * Description:
 *   Request the DMA module clock by clearing CLC.DISR and waiting for
 *   CLC.DISS to fall.
 *
 ****************************************************************************/

static int tc4xx_dma_clk_enable(void)
{
  int retries = 100;

  modreg32(0, DMA_CLC_DISR, TC4XX_DMA_CLC);

  while ((getreg32(TC4XX_DMA_CLC) & DMA_CLC_DISS) != 0)
    {
      if (--retries <= 0)
        {
          return -EIO;
        }

      up_udelay(1);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4xx_dma_initialize
 ****************************************************************************/

FAR struct dma_dev_s *tc4xx_dma_initialize(void)
{
  FAR struct tc4xx_dma_s *ctrl = &g_tc4xx_dma;
  uint32_t mode;
  int i;

  if (ctrl->initialised)
    {
      return &ctrl->dev;
    }

  if (tc4xx_dma_clk_enable() < 0)
    {
      dmaerr("DMA module clock enable failed\n");
      return NULL;
    }

  mode = getreg32(TC4XX_DMA_MODE(0));
  if ((mode & DMA_RP_MODE_VALID) == 0)
    {
      putreg32(DMA_RP_MODE_VALID, TC4XX_DMA_MODE(0));
    }

  for (i = 0; i < TC4XX_DMA_NCHAN; i++)
    {
      ctrl->channels[i].chan.ops = &g_tc4xx_dma_ops;
      ctrl->channels[i].channel  = i;
      ctrl->channels[i].inuse    = false;

      putreg32(DMA_HRR_HRPV, TC4XX_DMA_HRR(i));
    }

  ctrl->initialised = true;

  dmainfo("TC4xx DMA initialised: base 0x%08lx, %d channels exposed\n",
          (unsigned long)TC4XX_DMA_BASE, TC4XX_DMA_NCHAN);
  return &ctrl->dev;
}
