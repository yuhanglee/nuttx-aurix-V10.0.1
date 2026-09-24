/****************************************************************************
 * arch/tricore/src/aurix/tricore_dma.c
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

/* Aurix (TC3xx/TC4xx) DMA controller driver for the generic NuttX DMA
 * framework (include/nuttx/dma/dma.h).  Supports memory-to-memory, single
 * peripheral copy, and cyclic double-buffer (streaming receive and transmit)
 * transfers.
 * The register layout is identical across the families; the only SoC-specific
 * part is the TC4xx resource partitions, so tricore_dma_initialize() marks RP0
 * valid and assigns every exposed channel to it under a TC4xx chip guard.
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
#include "tricore_dma.h"
#include "hardware/tricore_dma.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TRICORE_DMA_NCHANNELS
#  define CONFIG_TRICORE_DMA_NCHANNELS  8
#endif

#define TRICORE_DMA_NCHAN   CONFIG_TRICORE_DMA_NCHANNELS

#if TRICORE_DMA_NCHAN > TRICORE_DMA_NR_CHANNELS
#  error "CONFIG_TRICORE_DMA_NCHANNELS exceeds the number of DMA channels"
#endif

/* Transaction-end interrupt.  CONFIG_TRICORE_DMA_IRQ_BASE is the interrupt-
 * router service-request-node index of DMA channel 0's transaction-end
 * interrupt; channel c uses IRQ_BASE + c.  The driver is interrupt-driven:
 * completion is reported from the channel ISR.  This mirrors every other NuttX
 * DMA driver - NXP eDMA (imxrt_edma.c), STM32 (stm32_dma_v2.c) and SAM XDMAC
 * (sam_xdmac.c) all invoke the client callback from the DMA interrupt rather
 * than busy-waiting for the transfer to finish.
 */

#ifndef CONFIG_TRICORE_DMA_IRQ_BASE
#  define CONFIG_TRICORE_DMA_IRQ_BASE   0
#endif

#if CONFIG_TRICORE_DMA_IRQ_BASE <= 0
#  error "CONFIG_TRICORE_DMA_IRQ_BASE must be the channel-0 transaction-end SRN"
#endif

#define TRICORE_DMA_CHAN_IRQ(c)    (CONFIG_TRICORE_DMA_IRQ_BASE + (c))

/* Optional resource-partition error interrupt (TC4xx).  All exposed channels
 * are assigned to RP0, so a single node (CONFIG_TRICORE_DMA_ERR_IRQ_BASE)
 * covers them; the handler reads ERRSR.LEC for the faulting channel.
 */

#ifndef CONFIG_TRICORE_DMA_ERR_IRQ_BASE
#  define CONFIG_TRICORE_DMA_ERR_IRQ_BASE  0
#endif

#if CONFIG_TRICORE_DMA_ERR_IRQ_BASE > 0
#  define TRICORE_DMA_HAVE_ERR_IRQ   1
#  define TRICORE_DMA_RP0            0
#endif

/* Internal per-channel state flags (dmach->flags) */

#define TRICORE_DMACH_FIFO_PACED    (1u << 0)  /* FIFO-gated peripheral: RROAT */
#define TRICORE_DMACH_BOOTSTRAP     (1u << 1)  /* Pending one-shot SCH kick    */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tricore_dmach_s
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
  uint8_t              flags;       /* TRICORE_DMACH_* channel state */

  /* Active transfer bookkeeping */

  size_t               xfrlen;      /* Single-shot transfer length in bytes */
  size_t               period;      /* Cyclic period (half-buffer) length */
  dma_callback_t       callback;
  FAR void            *arg;
};

struct tricore_dma_s
{
  struct dma_dev_s       dev;       /* Must be first: framework device */
  bool                   initialised;
  spinlock_t             lock;
  struct tricore_dmach_s channels[TRICORE_DMA_NCHAN];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int      tricore_dma_config(FAR struct dma_chan_s *chan,
                                   FAR const struct dma_config_s *cfg);
static int      tricore_dma_start(FAR struct dma_chan_s *chan,
                                  dma_callback_t callback, FAR void *arg,
                                  uintptr_t dst, uintptr_t src, size_t len);
static int      tricore_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                  dma_callback_t callback, FAR void *arg,
                                  uintptr_t dst, uintptr_t src,
                                  size_t len, size_t period_len);
static int      tricore_dma_stop(FAR struct dma_chan_s *chan);
static int      tricore_dma_pause(FAR struct dma_chan_s *chan);
static int      tricore_dma_resume(FAR struct dma_chan_s *chan);
static size_t   tricore_dma_residual(FAR struct dma_chan_s *chan);

static FAR struct dma_chan_s *tricore_dma_get_chan(FAR struct dma_dev_s *dev,
                                                   unsigned int ident);
static void     tricore_dma_put_chan(FAR struct dma_dev_s *dev,
                                     FAR struct dma_chan_s *chan);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct dma_ops_s g_tricore_dma_ops =
{
  .config       = tricore_dma_config,
  .start        = tricore_dma_start,
  .start_cyclic = tricore_dma_start_cyclic,
  .stop         = tricore_dma_stop,
  .pause        = tricore_dma_pause,
  .resume       = tricore_dma_resume,
  .residual     = tricore_dma_residual,
};

static struct tricore_dma_s g_tricore_dma =
{
  .dev =
    {
      .get_chan = tricore_dma_get_chan,
      .put_chan = tricore_dma_put_chan,
    },
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_dma_width2chdw
 *
 * Description:
 *   Convert a transfer width in bytes (1/2/4/8) to the CHCFGR.CHDW encoding
 *   (log2 of the byte width).  Returns a negative errno on an invalid width.
 *
 ****************************************************************************/

static int tricore_dma_width2chdw(unsigned int width)
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
 * Name: tricore_dma_moves2blkm
 *
 * Description:
 *   Convert a burst length in DMA moves (1/2/3/4/5/8/9/16) to the
 *   CHCFGR.BLKM encoding.  Returns a negative errno on an invalid value.
 *
 ****************************************************************************/

static int tricore_dma_moves2blkm(unsigned int moves)
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
 * Name: tricore_dma_reset
 *
 * Description:
 *   Reset a channel's transaction state and wait for the reset to take.
 *
 ****************************************************************************/

static int tricore_dma_reset(uint8_t channel)
{
  int retries = 100;

  putreg32(DMA_TSR_RST, TRICORE_DMA_TSR(channel));

  while ((getreg32(TRICORE_DMA_TSR(channel)) & DMA_TSR_RST) != 0)
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
 * Name: tricore_dma_config
 *
 * Description:
 *   Capture the transfer attributes for the channel.  This step supports
 *   memory-to-memory transfers only.
 *
 ****************************************************************************/

static int tricore_dma_config(FAR struct dma_chan_s *chan,
                              FAR const struct dma_config_s *cfg)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
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

  if (cfg->src_width && cfg->dst_width && cfg->src_width != cfg->dst_width)
    {
      return -EINVAL;
    }

  width = cfg->src_width ? cfg->src_width :
          cfg->dst_width ? cfg->dst_width : 1;
  if (tricore_dma_width2chdw(width) < 0)
    {
      return -EINVAL;
    }

  moves = (cfg->option & TRICORE_DMA_MOVES_MASK) ?
          (cfg->option & TRICORE_DMA_MOVES_MASK) : 1;
  blkm  = tricore_dma_moves2blkm(moves);
  if (blkm < 0)
    {
      return -EINVAL;
    }

  dmach->width      = width;
  dmach->blkm_moves = (uint8_t)moves;
  dmach->blkm_enc   = (uint8_t)blkm;

  dmach->flags = 0;
  if ((cfg->option & TRICORE_DMA_FIFO_PACED) != 0 &&
      cfg->direction != DMA_MEM_TO_MEM)
    {
      dmach->flags = TRICORE_DMACH_FIFO_PACED | TRICORE_DMACH_BOOTSTRAP;
    }

  return OK;
}

/****************************************************************************
 * Name: tricore_dma_start
 *
 * Description:
 *   Program and run a single DMA transaction.  Memory-to-memory transfers are
 *   software-triggered (CHCSR.SCH, RROAT=1); memory-to/from-peripheral
 *   transfers are armed for hardware requests (TSR.ECH, RROAT=0) and paced by
 *   the peripheral.  Completion is reported from the transaction-end ISR.
 *
 ****************************************************************************/

static int tricore_dma_start(FAR struct dma_chan_s *chan,
                             dma_callback_t callback, FAR void *arg,
                             uintptr_t dst, uintptr_t src, size_t len)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint8_t channel;
  uint32_t adicr;
  uint32_t chcfgr;
  uint32_t moves;
  uint32_t trel;
  int chdw;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  chdw = tricore_dma_width2chdw(dmach->width);
  if (chdw < 0)
    {
      return chdw;
    }

  if (len == 0 ||
      (len % ((size_t)dmach->width * dmach->blkm_moves)) != 0)
    {
      return -EINVAL;
    }

  moves = len / dmach->width;
  trel  = moves / dmach->blkm_moves;
  if (trel > DMA_CHCFGR_TREL_MAX)
    {
      return -EINVAL;
    }

  dmach->cyclic   = false;
  dmach->callback = callback;
  dmach->arg      = arg;
  dmach->xfrlen   = trel * dmach->blkm_moves * dmach->width;

  putreg32(DMA_TSR_DCH, TRICORE_DMA_TSR(channel));
  if (tricore_dma_reset(channel) < 0)
    {
      return -EIO;
    }

  adicr  = (DMA_ADICR_INTCT_TRANSACTION_END << DMA_ADICR_INTCT_SHIFT);
  adicr |= (dmach->src_step > 0) ? DMA_ADICR_INCS : DMA_ADICR_SCBE;
  adicr |= (dmach->dst_step > 0) ? DMA_ADICR_INCD : DMA_ADICR_DCBE;

  chcfgr  = ((uint32_t)chdw << DMA_CHCFGR_CHDW_SHIFT);
  chcfgr |= ((uint32_t)dmach->blkm_enc << DMA_CHCFGR_BLKM_SHIFT);
  chcfgr |= (trel << DMA_CHCFGR_TREL_SHIFT) & DMA_CHCFGR_TREL_MASK;

  if (dmach->dir == DMA_MEM_TO_MEM ||
      (dmach->flags & TRICORE_DMACH_FIFO_PACED) != 0)
    {
      chcfgr |= DMA_CHCFGR_RROAT;
    }

  putreg32(src,    TRICORE_DMA_SADR(channel));
  putreg32(dst,    TRICORE_DMA_DADR(channel));
  putreg32(adicr,  TRICORE_DMA_ADICR(channel));
  putreg32(chcfgr, TRICORE_DMA_CHCFGR(channel));

  dmainfo("ch %u: dir %u SADR 0x%08lx DADR 0x%08lx TREL %lu BLKM %u moves\n",
          channel, dmach->dir, (unsigned long)src, (unsigned long)dst,
          (unsigned long)trel, dmach->blkm_moves);

  if (dmach->dir != DMA_MEM_TO_MEM)
    {
      putreg32(DMA_TSR_ECH, TRICORE_DMA_TSR(channel));
#ifdef TRICORE_DMA_HAVE_ERR_IRQ
      modreg32(DMA_TSR_ETRL, DMA_TSR_ETRL, TRICORE_DMA_TSR(channel));
#endif

      if ((dmach->flags & TRICORE_DMACH_BOOTSTRAP) != 0)
        {
          dmach->flags &= ~TRICORE_DMACH_BOOTSTRAP;
          putreg32(DMA_CHCSR_SCH, TRICORE_DMA_CHCSR(channel));
        }

      return OK;
    }

  putreg32(DMA_CHCSR_SCH, TRICORE_DMA_CHCSR(channel));
  return OK;
}

/****************************************************************************
 * Name: tricore_dma_start_cyclic
 *
 * Description:
 *   Start a continuous streaming transfer that ping-pongs between two half-
 *   buffers with automatic hardware switch.  'len' is the total buffer length
 *   and must be exactly two periods.  DEV_TO_MEM (receive) double-buffers the
 *   destination - the consumer drains the half that just filled; MEM_TO_DEV
 *   (transmit) double-buffers the source - the consumer refills the half that
 *   just drained.  Each transaction end raises the channel interrupt.
 *
 ****************************************************************************/

static int tricore_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                    dma_callback_t callback, FAR void *arg,
                                    uintptr_t dst, uintptr_t src,
                                    size_t len, size_t period_len)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint8_t channel;
  uint32_t adicr;
  uint32_t chcfgr;
  uint32_t shadr;
  uint32_t shct;
  uint32_t moves;
  uint32_t trel;
  int chdw;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  if (dmach->dir != DMA_DEV_TO_MEM && dmach->dir != DMA_MEM_TO_DEV)
    {
      return -ENOTSUP;
    }

  if (period_len == 0 || len != 2 * period_len)
    {
      return -EINVAL;
    }

  chdw = tricore_dma_width2chdw(dmach->width);
  if (chdw < 0)
    {
      return chdw;
    }

  if ((period_len % ((size_t)dmach->width * dmach->blkm_moves)) != 0)
    {
      return -EINVAL;
    }

  moves = period_len / dmach->width;
  trel  = moves / dmach->blkm_moves;
  if (trel == 0 || trel > DMA_CHCFGR_TREL_MAX)
    {
      return -EINVAL;
    }

  dmach->cyclic   = true;
  dmach->callback = callback;
  dmach->arg      = arg;
  dmach->period   = trel * dmach->blkm_moves * dmach->width;

  if (dmach->dir == DMA_DEV_TO_MEM)
    {
      shct  = DMA_ADICR_SHCT_DOUBLE_DEST_AUTOSW;
      shadr = dst + period_len;
    }
  else
    {
      shct  = DMA_ADICR_SHCT_DOUBLE_SRC_AUTOSW;
      shadr = src + period_len;
    }

  putreg32(DMA_TSR_DCH, TRICORE_DMA_TSR(channel));
  if (tricore_dma_reset(channel) < 0)
    {
      return -EIO;
    }

  adicr  = (DMA_ADICR_INTCT_TRANSACTION_END << DMA_ADICR_INTCT_SHIFT);
  adicr |= (dmach->src_step > 0) ? DMA_ADICR_INCS : DMA_ADICR_SCBE;
  adicr |= (dmach->dst_step > 0) ? DMA_ADICR_INCD : DMA_ADICR_DCBE;
  adicr |= (shct << DMA_ADICR_SHCT_SHIFT);

  chcfgr  = ((uint32_t)chdw << DMA_CHCFGR_CHDW_SHIFT);
  chcfgr |= ((uint32_t)dmach->blkm_enc << DMA_CHCFGR_BLKM_SHIFT);
  chcfgr |= (trel << DMA_CHCFGR_TREL_SHIFT) & DMA_CHCFGR_TREL_MASK;
  chcfgr |= DMA_CHCFGR_CHMODE;

  putreg32(src,    TRICORE_DMA_SADR(channel));
  putreg32(dst,    TRICORE_DMA_DADR(channel));
  putreg32(adicr,  TRICORE_DMA_ADICR(channel));
  putreg32(chcfgr, TRICORE_DMA_CHCFGR(channel));

  putreg32(shadr, TRICORE_DMA_SHADR(channel));

  dmainfo("ch %u: cyclic dir %u SADR 0x%08lx DADR 0x%08lx SHADR 0x%08lx "
          "period %lu\n", channel, dmach->dir, (unsigned long)src,
          (unsigned long)dst, (unsigned long)shadr,
          (unsigned long)dmach->period);

  putreg32(DMA_TSR_ECH, TRICORE_DMA_TSR(channel));
#ifdef TRICORE_DMA_HAVE_ERR_IRQ
  modreg32(DMA_TSR_ETRL, DMA_TSR_ETRL, TRICORE_DMA_TSR(channel));
#endif
  return OK;
}

/****************************************************************************
 * Name: tricore_dma_done_isr
 *
 * Description:
 *   Per-channel transaction-end interrupt handler.  Attached on channel
 *   allocation; the handler argument is the channel object.
 *
 ****************************************************************************/

static int tricore_dma_done_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)arg;
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

      modreg32(0, DMA_CHCSR_FROZEN, TRICORE_DMA_CHCSR(channel));
      return OK;
    }

  putreg32(DMA_CHCSR_CICH, TRICORE_DMA_CHCSR(channel));

  if (dmach->callback != NULL)
    {
      dmach->callback(&dmach->chan, dmach->arg, (ssize_t)dmach->xfrlen);
    }

  return OK;
}

/****************************************************************************
 * Name: tricore_dma_geterror
 *
 * Description:
 *   Map an ERRSR status word to a negative errno for the channel callback.
 *
 ****************************************************************************/

#ifdef TRICORE_DMA_HAVE_ERR_IRQ
#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
static int tricore_dma_geterror(uint32_t status)
{
  if ((status & (DMA_ME_ERRSR_SER | DMA_ME_ERRSR_DER | DMA_ME_ERRSR_SPBER |
                 DMA_ME_ERRSR_SRIER | DMA_ME_ERRSR_RAMER)) != 0)
    {
      return -EIO;
    }

  return -EINVAL;
}
#else
static int tricore_dma_geterror(uint32_t status)
{
  if ((status & (DMA_ERRSR_SER | DMA_ERRSR_DER |
                 DMA_ERRSR_TCSCER | DMA_ERRSR_IRTRLER)) != 0)
    {
      return -EIO;
    }

  if ((status & DMA_ERRSR_OVFLER) != 0)
    {
      return -EOVERFLOW;
    }

  if ((status & DMA_ERRSR_CBTRLER) != 0)
    {
      return -EBUSY;
    }

  if ((status & DMA_ERRSR_CDTRLER) != 0)
    {
      return -ENOTSUP;
    }

  return -EINVAL;
}
#endif

/****************************************************************************
 * Name: tricore_dma_err_isr
 *
 * Description:
 *   DMA error interrupt handler.  ERRSR.LEC identifies the faulting channel;
 *   report the error to its callback and clear the status.  TC4xx reports
 *   errors per resource partition (RP0); TC3xx per move engine (ME0/ME1).
 *
 ****************************************************************************/

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
static int tricore_dma_err_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct tricore_dma_s *ctrl = (FAR struct tricore_dma_s *)arg;
  FAR struct tricore_dmach_s *dmach;
  uint32_t status;
  uint32_t channel;
  int m;

  UNUSED(irq);
  UNUSED(context);
  DEBUGASSERT(ctrl != NULL);

  for (m = 0; m < 2; m++)
    {
      status = getreg32(TRICORE_DMA_ME_ERRSR(m));
      if ((status & DMA_ME_ERRSR_ERR_MASK) == 0)
        {
          continue;
        }

      channel = (status & DMA_ME_ERRSR_LEC_MASK) >> DMA_ME_ERRSR_LEC_SHIFT;

      dmaerr("ME%d DMA error: ERRSR 0x%08lx LEC %lu\n",
             m, (unsigned long)status, (unsigned long)channel);

      if (channel < TRICORE_DMA_NCHAN)
        {
          dmach = &ctrl->channels[channel];
          if (dmach->inuse && dmach->callback != NULL)
            {
              dmach->callback(&dmach->chan, dmach->arg,
                              (ssize_t)tricore_dma_geterror(status));
            }
        }

      putreg32(status & DMA_ME_ERRSR_ERR_MASK, TRICORE_DMA_ME_CLRE(m));
    }

  return OK;
}
#else
static int tricore_dma_err_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct tricore_dma_s *ctrl = (FAR struct tricore_dma_s *)arg;
  FAR struct tricore_dmach_s *dmach;
  uint32_t status;
  uint32_t channel;

  UNUSED(irq);
  UNUSED(context);
  DEBUGASSERT(ctrl != NULL);

  status = getreg32(TRICORE_DMA_ERRSR(TRICORE_DMA_RP0));
  if ((status & DMA_ERRSR_ERR_MASK) == 0)
    {
      return OK;
    }

  channel = (status & DMA_ERRSR_LEC_MASK) >> DMA_ERRSR_LEC_SHIFT;

  dmaerr("RP0 DMA error: ERRSR 0x%08lx LEC %lu\n",
         (unsigned long)status, (unsigned long)channel);

  if (channel < TRICORE_DMA_NCHAN)
    {
      dmach = &ctrl->channels[channel];
      if (dmach->inuse && dmach->callback != NULL)
        {
          dmach->callback(&dmach->chan, dmach->arg,
                          (ssize_t)tricore_dma_geterror(status));
        }
    }

  putreg32(status & DMA_ERRSR_ERR_MASK, TRICORE_DMA_CLRE(TRICORE_DMA_RP0));
  return OK;
}
#endif
#endif

/****************************************************************************
 * Name: tricore_dma_stop
 ****************************************************************************/

static int tricore_dma_stop(FAR struct dma_chan_s *chan)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint8_t channel;
  int ret = OK;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  up_disable_irq(TRICORE_DMA_CHAN_IRQ(channel));

  putreg32(DMA_TSR_DCH, TRICORE_DMA_TSR(channel));
  if (tricore_dma_reset(channel) < 0)
    {
      ret = -EIO;
      goto out;
    }

  putreg32(DMA_CHCSR_CICH, TRICORE_DMA_CHCSR(channel));

  dmach->callback  = NULL;
  dmach->cyclic    = false;
  if ((dmach->flags & TRICORE_DMACH_FIFO_PACED) != 0)
    {
      dmach->flags |= TRICORE_DMACH_BOOTSTRAP;
    }

  dmainfo("ch %u: stopped\n", channel);

out:
  up_enable_irq(TRICORE_DMA_CHAN_IRQ(channel));
  return ret;
}

/****************************************************************************
 * Name: tricore_dma_pause
 *
 * Description:
 *   Halt the channel with its transfer state (TCOUNT, addresses) frozen so it
 *   can be resumed.  The channel halts after the current DMA transfer; wait
 *   for the halt acknowledge (TSR.HLTACK).
 *
 ****************************************************************************/

static int tricore_dma_pause(FAR struct dma_chan_s *chan)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint8_t channel;
  int retries = 100;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  modreg32(DMA_TSR_HLTREQ, DMA_TSR_HLTREQ, TRICORE_DMA_TSR(channel));

  while ((getreg32(TRICORE_DMA_TSR(channel)) & DMA_TSR_HLTACK) == 0)
    {
      if (--retries <= 0)
        {
          dmaerr("ch %u: halt not acknowledged\n", channel);
          return -EIO;
        }

      up_udelay(1);
    }

  dmainfo("ch %u: paused\n", channel);
  return OK;
}

/****************************************************************************
 * Name: tricore_dma_resume
 *
 * Description:
 *   Clear the halt request and acknowledge (TSR.HLTCLR) so a paused channel
 *   completes its remaining transfers.
 *
 ****************************************************************************/

static int tricore_dma_resume(FAR struct dma_chan_s *chan)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint8_t channel;

  DEBUGASSERT(dmach != NULL);
  channel = dmach->channel;

  modreg32(DMA_TSR_HLTCLR, DMA_TSR_HLTCLR, TRICORE_DMA_TSR(channel));

  dmainfo("ch %u: resumed\n", channel);
  return OK;
}

/****************************************************************************
 * Name: tricore_dma_residual
 *
 * Description:
 *   Return the number of bytes still to be transferred.  CHCSR.TCOUNT is the
 *   number of remaining DMA transfers; each transfer moves blkm_moves moves
 *   of 'width' bytes, so residual = TCOUNT * blkm_moves * width.
 *
 ****************************************************************************/

static size_t tricore_dma_residual(FAR struct dma_chan_s *chan)
{
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  uint32_t tcount;

  DEBUGASSERT(dmach != NULL);

  tcount = (getreg32(TRICORE_DMA_CHCSR(dmach->channel)) &
            DMA_CHCSR_TCOUNT_MASK) >> DMA_CHCSR_TCOUNT_SHIFT;

  return (size_t)tcount * dmach->blkm_moves * dmach->width;
}

/****************************************************************************
 * Name: tricore_dma_get_chan
 ****************************************************************************/

static FAR struct dma_chan_s *tricore_dma_get_chan(FAR struct dma_dev_s *dev,
                                                   unsigned int ident)
{
  FAR struct tricore_dma_s *ctrl = (FAR struct tricore_dma_s *)dev;
  FAR struct tricore_dmach_s *dmach;
  irqstate_t flags;

  if (ident >= TRICORE_DMA_NCHAN)
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
  dmach->flags      = 0;
  dmach->blkm_moves = 1;
  dmach->blkm_enc   = DMA_BLKM_1_MOVE;
  dmach->src_step   = 1;
  dmach->dst_step   = 1;

  spin_unlock_irqrestore(&ctrl->lock, flags);

  irq_attach(TRICORE_DMA_CHAN_IRQ(ident), tricore_dma_done_isr, dmach);
  up_enable_irq(TRICORE_DMA_CHAN_IRQ(ident));

  dmainfo("acquired channel %u (irq %d)\n", ident,
          TRICORE_DMA_CHAN_IRQ(ident));
  return &dmach->chan;
}

/****************************************************************************
 * Name: tricore_dma_put_chan
 ****************************************************************************/

static void tricore_dma_put_chan(FAR struct dma_dev_s *dev,
                                 FAR struct dma_chan_s *chan)
{
  FAR struct tricore_dma_s *ctrl = (FAR struct tricore_dma_s *)dev;
  FAR struct tricore_dmach_s *dmach = (FAR struct tricore_dmach_s *)chan;
  irqstate_t flags;

  DEBUGASSERT(dmach != NULL);

  tricore_dma_stop(chan);

  up_disable_irq(TRICORE_DMA_CHAN_IRQ(dmach->channel));
  irq_detach(TRICORE_DMA_CHAN_IRQ(dmach->channel));

  flags = spin_lock_irqsave(&ctrl->lock);
  dmach->inuse    = false;
  dmach->callback = NULL;
  dmach->arg      = NULL;
  spin_unlock_irqrestore(&ctrl->lock, flags);

  dmainfo("released channel %u\n", dmach->channel);
}

/****************************************************************************
 * Name: tricore_dma_clk_enable
 *
 * Description:
 *   Request the DMA module clock by clearing CLC.DISR and waiting for
 *   CLC.DISS to fall.
 *
 ****************************************************************************/

static int tricore_dma_clk_enable(void)
{
  int retries = 100;

  modreg32(0, DMA_CLC_DISR, TRICORE_DMA_CLC);

  while ((getreg32(TRICORE_DMA_CLC) & DMA_CLC_DISS) != 0)
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
 * Name: tricore_dma_initialize
 ****************************************************************************/

FAR struct dma_dev_s *tricore_dma_initialize(void)
{
  FAR struct tricore_dma_s *ctrl = &g_tricore_dma;
  int i;

  if (ctrl->initialised)
    {
      return &ctrl->dev;
    }

  if (tricore_dma_clk_enable() < 0)
    {
      dmaerr("DMA module clock enable failed\n");
      return NULL;
    }

#ifdef CONFIG_ARCH_CHIP_FAMILY_TC4X
    {
      uint32_t mode = getreg32(TRICORE_DMA_MODE(0));
      if ((mode & DMA_RP_MODE_VALID) == 0)
        {
          putreg32(DMA_RP_MODE_VALID, TRICORE_DMA_MODE(0));
        }
    }
#endif

  for (i = 0; i < TRICORE_DMA_NCHAN; i++)
    {
      ctrl->channels[i].chan.ops = &g_tricore_dma_ops;
      ctrl->channels[i].channel  = i;
      ctrl->channels[i].inuse    = false;

#ifdef CONFIG_ARCH_CHIP_FAMILY_TC4X
      putreg32(DMA_HRR_HRPV, TRICORE_DMA_HRR(i));
#endif
    }

#ifdef TRICORE_DMA_HAVE_ERR_IRQ
#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
  putreg32(DMA_ME_ERRSR_ERR_MASK, TRICORE_DMA_ME_CLRE(0));
  putreg32(DMA_ME_ERRSR_ERR_MASK, TRICORE_DMA_ME_CLRE(1));
  modreg32(DMA_ME_EER_DEFAULT, DMA_ME_EER_DEFAULT, TRICORE_DMA_ME_EER(0));
  modreg32(DMA_ME_EER_DEFAULT, DMA_ME_EER_DEFAULT, TRICORE_DMA_ME_EER(1));
#else
  putreg32(DMA_ERRSR_ERR_MASK, TRICORE_DMA_CLRE(TRICORE_DMA_RP0));
  modreg32(DMA_EER_DEFAULT, DMA_EER_DEFAULT, TRICORE_DMA_EER(TRICORE_DMA_RP0));
#endif
  irq_attach(CONFIG_TRICORE_DMA_ERR_IRQ_BASE, tricore_dma_err_isr, ctrl);
  up_enable_irq(CONFIG_TRICORE_DMA_ERR_IRQ_BASE);

  dmainfo("DMA error IRQ %d attached\n", CONFIG_TRICORE_DMA_ERR_IRQ_BASE);
#endif

  ctrl->initialised = true;

  dmainfo("Aurix DMA initialised: base 0x%08lx, %d channels exposed\n",
          (unsigned long)TRICORE_DMA_BASE, TRICORE_DMA_NCHAN);
  return &ctrl->dev;
}
