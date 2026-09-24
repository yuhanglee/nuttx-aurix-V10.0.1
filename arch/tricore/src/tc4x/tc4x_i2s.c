/****************************************************************************
 * arch/tricore/src/tc4x/tc4x_i2s.c
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

/* Lower-half I2S driver for the Infineon AURIX TC48x AUDIO TDM interface.
 * The SoC is the I2S master (BCLK/WS/MCLK from the 98.304 MHz Audio PLL);
 * framing is stereo I2S.  Each direction runs either the CPU FIFO fill/drain
 * path (default, any word size) or SDMA offload (CONFIG_TC4X_I2S_{TX,RX}_DMA,
 * 32-bit samples only).  The AUDIO subsystem exists only on TC48x devices, so
 * the driver is gated by CONFIG_TC4X_I2S.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/queue.h>
#include <nuttx/wqueue.h>
#include <nuttx/spinlock.h>
#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>

#include "tricore_internal.h"
#include "tc4x_i2s.h"
#include "hardware/tc4x_i2s.h"
#include "hardware/tc4x_clock.h"

/* DMA offload is compiled in when the SoC DMA infrastructure is present; a
 * given instance/direction uses it per cfg->tx_dma / cfg->rx_dma at runtime.
 */

#ifdef CONFIG_TRICORE_DMA
#  define TC4X_I2S_HAVE_DMA 1
#  include <nuttx/dma/dma.h>
#  include "tricore_dma.h"
#endif

#ifdef CONFIG_TC4X_I2S

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TC4X_I2S_MAXINFLIGHT
#  define CONFIG_TC4X_I2S_MAXINFLIGHT  16
#endif

/* CPU-fill high-water mark: keep the 128-entry FIFO fed to well below full. */

#define TC4X_I2S_TX_FIFO_HWM   112u

/* FIFO-trigger levels.  TX rule: transfer-samples < DEPTH - (TRIGGER - 1);
 * trigger 48 keeps the 64-sample DMA chunk below the 81 bound.
 */

#define TC4X_I2S_TX_TRIGGER    48u
#define TC4X_I2S_RX_TRIGGER    64u

/* Audio PLL policy: P, K and N configured from scratch (nothing trusted from
 * boot).  Field = divider-1: PDIV=0 -> P=1, final KDIV=9 -> K=10, safe
 * intermediate KDIV=1 (K=2) before power-up.  fDCO = fAUDIO * K must stay in
 * the 600-1000 MHz window; lock poll is a 100us real-time timeout.
 */

#define TC4X_APLL_P                1u
#define TC4X_APLL_K                10u
#define TC4X_APLL_PDIV             0u
#define TC4X_APLL_KDIV_SAFE        1u
#define TC4X_APLL_KDIV_FINAL       9u
#define TC4X_APLL_FDCO_MIN         600000000u
#define TC4X_APLL_FDCO_MAX         1000000000u
#define TC4X_APLL_LOCK_TIMEOUT_US  100

#ifdef TC4X_I2S_HAVE_DMA
/* Samples per hardware-triggered SDMA transaction (32-bit each); 64 < 81. */

#  define TC4X_I2S_DMA_SAMPLES  64u
#  define TC4X_I2S_DMA_CHUNK    (TC4X_I2S_DMA_SAMPLES * (uint32_t)sizeof(uint32_t))

/* APU master TAG-ID of SDMA0 in resource-partition 0. */

#  define TC4X_I2S_DMA0_TAGID   0x10u

/* AUDIO APU PROT state-machine states / bits (soc_prot.h). */

#  define TC4X_PROT_STATE_INIT   0u
#  define TC4X_PROT_STATE_CONFIG 1u
#  define TC4X_PROT_STATE_RUN    4u
#  define TC4X_PROT_STATE_MASK   0x7u
#  define TC4X_PROT_SWEN         BIT(3)

#endif /* TC4X_I2S_HAVE_DMA */

/* FIELD_PREP(mask, shift, val): pack val into the masked field. */

#define FIELD_PREP(mask, shift, val)  (((uint32_t)(val) << (shift)) & (mask))

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-instance configuration, resolved from the CONFIG_TC4X_I2Sn_* options. */

struct tc4x_i2s_config_s
{
  uint8_t  port;         /* TDM instance 0..2                                 */
  uint8_t  mck_div;      /* TX/RX MCK_DIV code: 0=/1 1=/2 2=/4 3=/8           */
  uint8_t  tx_iomux;     /* TX pad group A/B/C/D = 0..3 (TX_IOMUX_CTL)        */
  uint8_t  rx_iomux;     /* RX pad group A/B/C/D = 0..3 (RX_IOMUX_CTL)        */
  uint8_t  tx_dma_chan;  /* SDMA0 channel serving the TX FIFO (if tx_dma)     */
  uint8_t  rx_dma_chan;  /* SDMA0 channel serving the RX FIFO (if rx_dma)     */
  bool     tx_dma;       /* Offload TX FIFO fill to the SDMA                  */
  bool     rx_dma;       /* Offload RX FIFO drain to the SDMA                 */
  bool     loopback;     /* Internal TX_SDO->RX_SDI self-test (no pins/codec) */
  bool     tx_slave;     /* TX is a clock slave (MS=0), clock from the pads   */
  bool     rx_slave;     /* RX is a clock slave (MS=0), clock from the pads   */
};

/* A container that holds one queued audio buffer plus its callback. */

struct tc4x_i2s_buffer_s
{
  struct tc4x_i2s_buffer_s *flink;      /* Singly linked list support        */
  i2s_callback_t            callback;   /* Completion callback               */
  FAR void                 *arg;        /* Callback argument                 */
  FAR struct ap_buffer_s   *apb;        /* The audio buffer                  */
  int                       result;     /* Transfer result                   */
};

/* Per-direction (TX or RX) transport state. */

struct tc4x_i2s_xport_s
{
  sq_queue_t                pend;       /* Queued, not yet active            */
  sq_queue_t                act;        /* Currently being transferred       */
  sq_queue_t                done;       /* Completed, awaiting callback      */
  struct work_s             work;       /* Worker for completion callbacks   */
  FAR struct ap_buffer_s   *apb;        /* Active buffer being (de)serialized */
  uint32_t                  samplerate; /* Sample rate in Hz                 */
  uint32_t                  offset;     /* Byte offset into the active apb   */
#ifdef TC4X_I2S_HAVE_DMA
  FAR struct dma_chan_s    *dmach;      /* SDMA channel handle                */
  unsigned int              dmachan;    /* SDMA channel number (0..127)       */
#endif
  uint8_t                   datawidth;  /* Word width in bits                */
  uint8_t                   channels;   /* Channel/slot count                */
  bool                      running;    /* Serializer active                 */
#ifdef TC4X_I2S_HAVE_DMA
  bool                      dma;        /* This direction uses SDMA offload   */
  bool                      wait_start; /* Defer-activate / defer-bootstrap    */
#endif
};

/* The I2S device.  i2s_dev_s must be the first member. */

struct tc4x_i2s_s
{
  struct i2s_dev_s          dev;        /* Externally visible I2S interface  */
  uintptr_t                 base;       /* TDM sub-block base (module+0x2000) */
  uint32_t                  clk_if;     /* APLL interface clock (Hz)         */
  int                       tx_irq;     /* TXERR/trigger SRC node (this inst) */
  int                       rx_irq;     /* RXERR/trigger SRC node (this inst) */
  mutex_t                   lock;       /* API mutual exclusion              */
  sem_t                     bufsem;     /* Counts free containers            */
  spinlock_t                spin;       /* Protects the queues vs the ISR    */
  FAR struct tc4x_i2s_buffer_s *freelist;
  struct tc4x_i2s_buffer_s  containers[CONFIG_TC4X_I2S_MAXINFLIGHT];
  struct tc4x_i2s_xport_s   tx;
  struct tc4x_i2s_xport_s   rx;
  uint8_t                   tdm_index;  /* TDM instance index (y = 0..2)     */
  uint8_t                   mck_div;    /* MCK_DIV code (0=/1..3=/8)         */
  uint8_t                   tx_iomux;   /* TX pad-group A/B/C/D = 0..3       */
  uint8_t                   rx_iomux;   /* RX pad-group A/B/C/D = 0..3       */
  bool                      loopback;   /* Internal TX_SDO->RX_SDI self-test */
  bool                      tx_slave;   /* TX clock slave (MS=0, clock from pad)*/
  bool                      rx_slave;   /* RX clock slave (MS=0, clock from pad)*/
  bool                      txenab;     /* TX supported                      */
  bool                      rxenab;     /* RX supported                      */
  bool                      configured; /* TDM registers programmed          */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Register helpers */

static inline uint32_t tc4x_getreg(struct tc4x_i2s_s *priv, uintptr_t off);
static inline void tc4x_putreg(struct tc4x_i2s_s *priv, uintptr_t off,
                               uint32_t val);
static inline void tc4x_setbits(struct tc4x_i2s_s *priv, uintptr_t off,
                                uint32_t bits);
static inline void tc4x_clrbits(struct tc4x_i2s_s *priv, uintptr_t off,
                                uint32_t bits);

/* Buffer container pool */

static FAR struct tc4x_i2s_buffer_s *
                tc4x_i2s_buf_allocate(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_buf_free(struct tc4x_i2s_s *priv,
                                  FAR struct tc4x_i2s_buffer_s *bfc);
static void     tc4x_i2s_buf_initialize(struct tc4x_i2s_s *priv);

/* Transfer engine */

static int      tc4x_i2s_tdm_config(struct tc4x_i2s_s *priv, bool tx);
static void     tc4x_i2s_tx_fill(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_rx_drain(struct tc4x_i2s_s *priv);
static int      tc4x_i2s_tx_start(struct tc4x_i2s_s *priv);
static int      tc4x_i2s_rx_start(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_tx_stop(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_rx_stop(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_tx_schedule(struct tc4x_i2s_s *priv, int result);
static void     tc4x_i2s_rx_schedule(struct tc4x_i2s_s *priv, int result);
static void     tc4x_i2s_tx_worker(FAR void *arg);
static void     tc4x_i2s_rx_worker(FAR void *arg);
static int      tc4x_i2s_tx_isr(int irq, FAR void *context, FAR void *arg);
static int      tc4x_i2s_rx_isr(int irq, FAR void *context, FAR void *arg);

#ifdef TC4X_I2S_HAVE_DMA
static int      tc4x_i2s_tx_dma_arm(struct tc4x_i2s_s *priv);
static int      tc4x_i2s_rx_dma_arm(struct tc4x_i2s_s *priv);
static void     tc4x_i2s_tx_dma_cb(FAR struct dma_chan_s *chan,
                                   FAR void *arg, ssize_t result);
static void     tc4x_i2s_rx_dma_cb(FAR struct dma_chan_s *chan,
                                   FAR void *arg, ssize_t result);
#endif

/* I2S methods */

static int32_t  tc4x_i2s_txsamplerate(struct i2s_dev_s *dev, uint32_t rate);
static int32_t  tc4x_i2s_txdatawidth(struct i2s_dev_s *dev, int bits);
static int      tc4x_i2s_txchannels(struct i2s_dev_s *dev, uint8_t channels);
static int      tc4x_i2s_send(struct i2s_dev_s *dev,
                              FAR struct ap_buffer_s *apb,
                              i2s_callback_t callback, FAR void *arg,
                              uint32_t timeout);
static int32_t  tc4x_i2s_rxsamplerate(struct i2s_dev_s *dev, uint32_t rate);
static int32_t  tc4x_i2s_rxdatawidth(struct i2s_dev_s *dev, int bits);
static int      tc4x_i2s_rxchannels(struct i2s_dev_s *dev, uint8_t channels);
static int      tc4x_i2s_receive(struct i2s_dev_s *dev,
                                 FAR struct ap_buffer_s *apb,
                                 i2s_callback_t callback, FAR void *arg,
                                 uint32_t timeout);
static int      tc4x_i2s_ioctl(struct i2s_dev_s *dev, int cmd,
                               unsigned long arg);
static int32_t  tc4x_i2s_getmclkfrequency(struct i2s_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2s_ops_s g_tc4x_i2s_ops =
{
  .i2s_txsamplerate = tc4x_i2s_txsamplerate,
  .i2s_txdatawidth  = tc4x_i2s_txdatawidth,
  .i2s_txchannels   = tc4x_i2s_txchannels,
  .i2s_send         = tc4x_i2s_send,
  .i2s_rxsamplerate = tc4x_i2s_rxsamplerate,
  .i2s_rxdatawidth  = tc4x_i2s_rxdatawidth,
  .i2s_rxchannels   = tc4x_i2s_rxchannels,
  .i2s_receive      = tc4x_i2s_receive,
  .i2s_getmclkfrequency = tc4x_i2s_getmclkfrequency,
  .i2s_ioctl        = tc4x_i2s_ioctl,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t tc4x_getreg(struct tc4x_i2s_s *priv, uintptr_t off)
{
  return getreg32(priv->base + off);
}

static inline void tc4x_putreg(struct tc4x_i2s_s *priv, uintptr_t off,
                               uint32_t val)
{
  putreg32(val, priv->base + off);
}

static inline void tc4x_setbits(struct tc4x_i2s_s *priv, uintptr_t off,
                                uint32_t bits)
{
  putreg32(getreg32(priv->base + off) | bits, priv->base + off);
}

static inline void tc4x_clrbits(struct tc4x_i2s_s *priv, uintptr_t off,
                                uint32_t bits)
{
  putreg32(getreg32(priv->base + off) & ~bits, priv->base + off);
}

/****************************************************************************
 * Name: tc4x_tdm_word_size_code
 *
 * Description:
 *   Return the WORD_SIZE enum code for a PCM word width in bits, or a negated
 *   errno if the width is not one of 8/10/12/14/16/18/20/24/32.
 *
 ****************************************************************************/

static int tc4x_tdm_word_size_code(int bits)
{
  switch (bits)
    {
      case 8:
        return TDM_SIZE_CODE_8;

      case 10:
        return TDM_SIZE_CODE_10;

      case 12:
        return TDM_SIZE_CODE_12;

      case 14:
        return TDM_SIZE_CODE_14;

      case 16:
        return TDM_SIZE_CODE_16;

      case 18:
        return TDM_SIZE_CODE_18;

      case 20:
        return TDM_SIZE_CODE_20;

      case 24:
        return TDM_SIZE_CODE_24;

      case 32:
        return TDM_SIZE_CODE_32;

      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: tc4x_audio_clk_enable
 *
 * Description:
 *   Ungate the AUDIO module clock (CLC) before any TDM register access.  The
 *   Audio PLL is configured separately per sample rate in
 *   tc4x_i2s_apll_set_rate.  TC48x only.
 *
 ****************************************************************************/

static int tc4x_audio_clk_enable(struct tc4x_i2s_s *priv)
{
  int retries;

  /* AUDIO module clock: clear CLC.DISR and wait for CLC.DISS to fall. */

  if ((getreg32(TC4X_AUDIO_BASE + TC4X_AUDIO_CLC_OFFSET) &
       TC4X_AUDIO_CLC_DISS) != 0)
    {
      putreg32(getreg32(TC4X_AUDIO_BASE + TC4X_AUDIO_CLC_OFFSET) &
               ~TC4X_AUDIO_CLC_DISR, TC4X_AUDIO_BASE + TC4X_AUDIO_CLC_OFFSET);

      for (retries = 1000; retries > 0; retries--)
        {
          if ((getreg32(TC4X_AUDIO_BASE + TC4X_AUDIO_CLC_OFFSET) &
               TC4X_AUDIO_CLC_DISS) == 0)
            {
              break;
            }

          up_udelay(1);
        }

      if (retries == 0)
        {
          i2serr("ERROR: AUDIO CLC enable timed out\n");
          return -ETIMEDOUT;
        }
    }

  return OK;
}

#ifdef TC4X_I2S_HAVE_DMA
/****************************************************************************
 * Name: tc4x_apu_enable
 *
 * Description:
 *   Grant a DMA master TAG-ID access to an AUDIO access-enable register through
 *   the APU PROT state machine.  A raw ACCEN write is ignored once PROT is
 *   locked, so in the RUN state the sequence is CONFIG -> set bit -> RUN.
 *
 ****************************************************************************/

static void tc4x_apu_enable(uintptr_t prot, uintptr_t accen, uint8_t tag)
{
  if ((getreg32(prot) & TC4X_PROT_STATE_MASK) == TC4X_PROT_STATE_INIT)
    {
      putreg32(getreg32(accen) | BIT(tag), accen);
      return;
    }

  putreg32((getreg32(prot) & ~TC4X_PROT_STATE_MASK) | TC4X_PROT_SWEN |
           TC4X_PROT_STATE_CONFIG, prot);
  putreg32(getreg32(accen) | BIT(tag), accen);
  putreg32((getreg32(prot) & ~TC4X_PROT_STATE_MASK) | TC4X_PROT_SWEN |
           TC4X_PROT_STATE_RUN, prot);
}

/****************************************************************************
 * Name: tc4x_i2s_apu_grant
 *
 * Description:
 *   Grant the SDMA0 master access to this TDM's FIFOs (TX=write, RX=read) in
 *   both the global and per-TDM access-enable registers; without it the SDMA
 *   FIFO access is denied.
 *
 ****************************************************************************/

static void tc4x_i2s_apu_grant(struct tc4x_i2s_s *priv, bool tx, uint8_t tag)
{
  uintptr_t prot = TC4X_AUDIO_BASE + TC4X_AUDIO_PROTE_OFFSET;
  uintptr_t accen_tdm = TC4X_AUDIO_BASE +
                        priv->tdm_index * TC4X_AUDIO_ACCENTDM_STRIDE;

  if (tx)
    {
      tc4x_apu_enable(prot, TC4X_AUDIO_BASE + TC4X_AUDIO_ACCENGBL_WRA_OFFSET, tag);
      tc4x_apu_enable(prot, accen_tdm + TC4X_AUDIO_ACCENTDM_WRA_OFFSET, tag);
    }
  else
    {
      tc4x_apu_enable(prot, TC4X_AUDIO_BASE + TC4X_AUDIO_ACCENGBL_RDA_OFFSET, tag);
      tc4x_apu_enable(prot, accen_tdm + TC4X_AUDIO_ACCENTDM_RDA_OFFSET, tag);
    }
}

/****************************************************************************
 * Name: tc4x_i2s_tx_dma_arm
 *
 * Description:
 *   Arm one hardware-triggered MEMORY->PERIPHERAL SDMA transaction streaming
 *   the next <=64-sample chunk into TX_FIFO_WR; the transaction-end callback
 *   re-arms.  32-bit only.  Returns -ENODATA when nothing is left to send.
 *
 ****************************************************************************/

static int tc4x_i2s_tx_dma_arm(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->tx;
  FAR struct tc4x_i2s_buffer_s *bfc;
  uint32_t chunk;
  FAR uint8_t *src;

  if (xport->apb == NULL || xport->offset >= xport->apb->nbytes)
    {
      if (xport->apb != NULL)
        {
          tc4x_i2s_tx_schedule(priv, OK);
          xport->apb = NULL;
        }

      bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->pend);
      if (bfc == NULL)
        {
          return -ENODATA;
        }

      sq_addlast((sq_entry_t *)bfc, &xport->act);
      xport->apb    = bfc->apb;
      xport->offset = bfc->apb->curbyte;
    }

  chunk = xport->apb->nbytes - xport->offset;
  if (chunk > TC4X_I2S_DMA_CHUNK)
    {
      chunk = TC4X_I2S_DMA_CHUNK;
    }

  src = xport->apb->samp + xport->offset;
  xport->offset += chunk;

  /* Flush the chunk out of the D-cache so the SDMA reads current data. */

  up_clean_dcache((uintptr_t)src, (uintptr_t)src + chunk);

  return DMA_START(xport->dmach, tc4x_i2s_tx_dma_cb, priv,
                   priv->base + TC4X_TDM_TX_FIFO_WR, (uintptr_t)src, chunk);
}

/****************************************************************************
 * Name: tc4x_i2s_rx_dma_arm
 *
 * Description:
 *   RX mirror of tc4x_i2s_tx_dma_arm: arm a PERIPHERAL->MEMORY transaction
 *   draining RX_FIFO_RD into the active buffer.  Returns -ENOMEM (overrun)
 *   when no free buffer is queued.  32-bit samples only.
 *
 ****************************************************************************/

static int tc4x_i2s_rx_dma_arm(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->rx;
  FAR struct tc4x_i2s_buffer_s *bfc;
  uint32_t chunk;
  FAR uint8_t *dst;

  if (xport->apb == NULL || xport->offset >= xport->apb->nmaxbytes)
    {
      if (xport->apb != NULL)
        {
          xport->apb->nbytes = xport->offset;
          tc4x_i2s_rx_schedule(priv, OK);
          xport->apb = NULL;
        }

      bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->pend);
      if (bfc == NULL)
        {
          return -ENOMEM;
        }

      sq_addlast((sq_entry_t *)bfc, &xport->act);
      xport->apb    = bfc->apb;
      xport->offset = bfc->apb->curbyte;
    }

  chunk = xport->apb->nmaxbytes - xport->offset;
  if (chunk > TC4X_I2S_DMA_CHUNK)
    {
      chunk = TC4X_I2S_DMA_CHUNK;
    }

  dst = xport->apb->samp + xport->offset;
  xport->offset += chunk;

  /* Invalidate so a later CPU read sees SDMA data, not stale cache lines. */

  up_invalidate_dcache((uintptr_t)dst, (uintptr_t)dst + chunk);

  return DMA_START(xport->dmach, tc4x_i2s_rx_dma_cb, priv,
                   (uintptr_t)dst, priv->base + TC4X_TDM_RX_FIFO_RD, chunk);
}

/****************************************************************************
 * Name: tc4x_i2s_tx_dma_cb  (SDMA transaction-end, ISR context)
 *
 * Description:
 *   Defer-activate: the first completed transaction has primed the FIFO from
 *   RAM (with the transmitter still off), so turn the serializer on now -
 *   ACTIVE then START - and re-arm the next chunk.  A raw errno (result < 0)
 *   is a denied FIFO write (RP source/destination error).
 *
 ****************************************************************************/

static void tc4x_i2s_tx_dma_cb(FAR struct dma_chan_s *chan,
                               FAR void *arg, ssize_t result)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;
  int i;

  UNUSED(chan);

  if (result < 0)
    {
      i2serr("ERROR: TX DMA error %zd\n", result);
      tc4x_i2s_tx_schedule(priv, (int)result);
      tc4x_i2s_tx_stop(priv);
      return;
    }

  if (priv->tx.wait_start)
    {
      priv->tx.wait_start = false;
      tc4x_setbits(priv, TC4X_TDM_TX_FIFO_CTL, TDM_FIFO_CTL_ACTIVE);
      tc4x_setbits(priv, TC4X_TDM_TX_CTL, TDM_CTL_START);
    }

  if (tc4x_i2s_tx_dma_arm(priv) != 0)
    {
      /* Nothing left: feed a little silence so the transmitter does not
       * underflow before the FIFO-underflow ISR stops the stream.
       */

      for (i = 0; i < 6; i++)
        {
          tc4x_putreg(priv, TC4X_TDM_TX_FIFO_WR, 0);
        }
    }
}

/****************************************************************************
 * Name: tc4x_i2s_rx_dma_cb  (SDMA transaction-end, ISR context)
 ****************************************************************************/

static void tc4x_i2s_rx_dma_cb(FAR struct dma_chan_s *chan,
                               FAR void *arg, ssize_t result)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;

  UNUSED(chan);

  if (result < 0)
    {
      i2serr("ERROR: RX DMA error %zd\n", result);
      tc4x_i2s_rx_schedule(priv, (int)result);
      tc4x_i2s_rx_stop(priv);
      return;
    }

  if (tc4x_i2s_rx_dma_arm(priv) != 0)
    {
      /* No free buffer (consumer overrun): report on the active container and
       * stop.  An overrun is a real error in continuous capture.
       */

      i2serr("ERROR: RX overrun (no free buffer)\n");
      tc4x_i2s_rx_schedule(priv, -EIO);
      tc4x_i2s_rx_stop(priv);
    }
}
#endif /* TC4X_I2S_HAVE_DMA */

/****************************************************************************
 * Name: tc4x_i2s_buf_allocate / _free / _initialize
 ****************************************************************************/

static FAR struct tc4x_i2s_buffer_s *
tc4x_i2s_buf_allocate(struct tc4x_i2s_s *priv)
{
  FAR struct tc4x_i2s_buffer_s *bfc;
  irqstate_t flags;
  int ret;

  ret = nxsem_wait_uninterruptible(&priv->bufsem);
  if (ret < 0)
    {
      return NULL;
    }

  flags = spin_lock_irqsave(&priv->spin);
  bfc = priv->freelist;
  DEBUGASSERT(bfc != NULL);
  priv->freelist = bfc->flink;
  spin_unlock_irqrestore(&priv->spin, flags);

  return bfc;
}

static void tc4x_i2s_buf_free(struct tc4x_i2s_s *priv,
                              FAR struct tc4x_i2s_buffer_s *bfc)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->spin);
  bfc->flink     = priv->freelist;
  priv->freelist = bfc;
  spin_unlock_irqrestore(&priv->spin, flags);

  nxsem_post(&priv->bufsem);
}

static void tc4x_i2s_buf_initialize(struct tc4x_i2s_s *priv)
{
  int i;

  priv->freelist = NULL;
  for (i = 0; i < CONFIG_TC4X_I2S_MAXINFLIGHT; i++)
    {
      priv->containers[i].flink = priv->freelist;
      priv->freelist = &priv->containers[i];
    }
}

/****************************************************************************
 * Name: tc4x_i2s_tdm_config
 *
 * Description:
 *   Validate rate/width/channels and program the TDM TX or RX interface for
 *   master operation.  channels <= 2 use I2S framing, > 2 use TDM long-frame.
 *   Returns -EINVAL on an invalid or unachievable configuration.
 *
 ****************************************************************************/

static int tc4x_i2s_tdm_config(struct tc4x_i2s_s *priv, bool tx)
{
  struct tc4x_i2s_xport_s *xport = tx ? &priv->tx : &priv->rx;
  const uint32_t clk_if = priv->clk_if;
  uint32_t bclk;
  uint32_t ratio;
  uint32_t ifc;
  uint32_t fifoctl;
  uint8_t sdmadonesel = 0;
  int wcode = tc4x_tdm_word_size_code(xport->datawidth);

  /* Reject an unconfigured or out-of-range setup before touching registers. */

  if (xport->samplerate == 0 || xport->datawidth == 0 ||
      xport->channels < 2 || xport->channels > 32 || wcode < 0)
    {
      i2serr("ERROR: %s invalid config: rate=%lu width=%u channels=%u\n",
             tx ? "TX" : "RX", (unsigned long)xport->samplerate,
             xport->datawidth, xport->channels);
      return -EINVAL;
    }

  bclk  = xport->samplerate * xport->datawidth * xport->channels;
  ratio = clk_if / bclk;

  if ((clk_if % bclk) != 0 || (ratio & 1u) != 0 || ratio < 2 || ratio > 3072)
    {
      i2serr("ERROR: %s rate %lu (channels=%u, %u-bit) not achievable from "
             "clk_if=%lu Hz (bclk=%lu, ratio=%lu)\n",
             tx ? "TX" : "RX", (unsigned long)xport->samplerate,
             xport->channels, xport->datawidth, (unsigned long)clk_if,
             (unsigned long)bclk, (unsigned long)ratio);
      return -EINVAL;
    }

#ifdef TC4X_I2S_HAVE_DMA
  /* Bind this FIFO's dmaRequest to its SDMA channel (SDMA0 ch c -> c). */

  if (xport->dma)
    {
      sdmadonesel = (uint8_t)xport->dmachan;
    }
#endif

  if (tx)
    {
      /* CLOCK_SEL=0 (APLL), FSYNC_FORMAT=channel-period; channels<=2 add
       * I2S_MODE + FSYNC inverted, >2 use TDM long-frame.
       */

      ifc = FIELD_PREP(TDM_TX_IF_CTL_CLOCK_DIV_MASK,
                       TDM_TX_IF_CTL_CLOCK_DIV_SHIFT, ratio - 1) |
            FIELD_PREP(TDM_TX_IF_CTL_MCK_DIV_MASK,
                       TDM_TX_IF_CTL_MCK_DIV_SHIFT, priv->mck_div) |
            TDM_TX_IF_CTL_FSYNC_FORMAT   |
            FIELD_PREP(TDM_TX_IF_CTL_CH_NR_MASK,
                       TDM_TX_IF_CTL_CH_NR_SHIFT, xport->channels - 1) |
            FIELD_PREP(TDM_TX_IF_CTL_CH_SIZE_MASK,
                       TDM_TX_IF_CTL_CH_SIZE_SHIFT, xport->datawidth - 1);

      if (xport->channels <= 2)
        {
          ifc |= TDM_TX_IF_CTL_FSYNC_POLARITY | TDM_TX_IF_CTL_I2S_MODE;
        }

      tc4x_putreg(priv, TC4X_TDM_TX_IF_CTL, ifc);

      tc4x_putreg(priv, TC4X_TDM_TX_CH_CTL,
                  (uint32_t)((1ull << xport->channels) - 1u));

      fifoctl = FIELD_PREP(TDM_FIFO_CTL_TRIGGER_LEVEL_MASK,
                           TDM_FIFO_CTL_TRIGGER_LEVEL_SHIFT,
                           TC4X_I2S_TX_TRIGGER) |
                FIELD_PREP(TDM_FIFO_CTL_SDMADONE_SEL_MASK,
                           TDM_FIFO_CTL_SDMADONE_SEL_SHIFT, sdmadonesel);
      tc4x_putreg(priv, TC4X_TDM_TX_FIFO_CTL, fifoctl);

      tc4x_putreg(priv, TC4X_TDM_TX_CTL,
                  FIELD_PREP(TDM_CTL_WORD_SIZE_MASK,
                             TDM_CTL_WORD_SIZE_SHIFT, wcode) |
                  FIELD_PREP(TDM_CTL_FORMAT_MASK,
                             TDM_CTL_FORMAT_SHIFT, TDM_FMT_LEFT_DELAYED) |
                  (priv->tx_slave ? 0u : TDM_CTL_MS));

      tc4x_setbits(priv, TC4X_TDM_TX_CTL, TDM_CTL_ENABLED);
    }
  else
    {
      ifc = FIELD_PREP(TDM_RX_IF_CTL_CLOCK_DIV_MASK,
                       TDM_RX_IF_CTL_CLOCK_DIV_SHIFT, ratio - 1) |
            FIELD_PREP(TDM_RX_IF_CTL_MCK_DIV_MASK,
                       TDM_RX_IF_CTL_MCK_DIV_SHIFT, priv->mck_div) |
            TDM_RX_IF_CTL_FSYNC_FORMAT   |
            FIELD_PREP(TDM_RX_IF_CTL_CH_NR_MASK,
                       TDM_RX_IF_CTL_CH_NR_SHIFT, xport->channels - 1) |
            FIELD_PREP(TDM_RX_IF_CTL_CH_SIZE_MASK,
                       TDM_RX_IF_CTL_CH_SIZE_SHIFT, xport->datawidth - 1);

      if (xport->channels <= 2)
        {
          ifc |= TDM_RX_IF_CTL_FSYNC_POLARITY | TDM_RX_IF_CTL_I2S_MODE;
        }

      tc4x_putreg(priv, TC4X_TDM_RX_IF_CTL, ifc);

      tc4x_putreg(priv, TC4X_TDM_RX_CH_CTL,
                  (uint32_t)((1ull << xport->channels) - 1u));

      fifoctl = FIELD_PREP(TDM_FIFO_CTL_TRIGGER_LEVEL_MASK,
                           TDM_FIFO_CTL_TRIGGER_LEVEL_SHIFT,
                           TC4X_I2S_RX_TRIGGER) |
                FIELD_PREP(TDM_FIFO_CTL_SDMADONE_SEL_MASK,
                           TDM_FIFO_CTL_SDMADONE_SEL_SHIFT, sdmadonesel);
      tc4x_putreg(priv, TC4X_TDM_RX_FIFO_CTL, fifoctl);

      if (priv->loopback)
        {
          tc4x_putreg(priv, TC4X_TDM_RX_TEST_CTL, TDM_TEST_CTL_ENABLED);
        }

      tc4x_putreg(priv, TC4X_TDM_RX_CTL,
                  FIELD_PREP(TDM_CTL_WORD_SIZE_MASK,
                             TDM_CTL_WORD_SIZE_SHIFT, wcode) |
                  FIELD_PREP(TDM_CTL_FORMAT_MASK,
                             TDM_CTL_FORMAT_SHIFT, TDM_FMT_LEFT_DELAYED) |
                  ((priv->loopback || priv->rx_slave) ? 0u : TDM_CTL_MS));

      tc4x_setbits(priv, TC4X_TDM_RX_CTL, TDM_CTL_ENABLED);
    }

  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_tx_fill
 *
 * Description:
 *   CPU-fill the TX FIFO from the active audio buffer.  The FIFO write is
 *   32-bit; the memory read narrows to the sample width.
 *
 ****************************************************************************/

static void tc4x_i2s_tx_fill(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->tx;
  FAR struct ap_buffer_s *apb;
  FAR struct tc4x_i2s_buffer_s *bfc;
  uint8_t sbytes = (xport->datawidth > 16) ? 4 : (xport->datawidth > 8) ? 2 : 1;

  while ((tc4x_getreg(priv, TC4X_TDM_TX_FIFO_STATUS) &
          TDM_FIFO_STATUS_USED_MASK) < TC4X_I2S_TX_FIFO_HWM)
    {
      apb = xport->apb;

      if (apb == NULL || xport->offset >= apb->nbytes)
        {
          if (apb != NULL)
            {
              tc4x_i2s_tx_schedule(priv, OK);
              xport->apb = NULL;
            }

          bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->pend);
          if (bfc == NULL)
            {
              break;    /* Nothing queued: let the FIFO drain */
            }

          sq_addlast((sq_entry_t *)bfc, &xport->act);
          xport->apb    = bfc->apb;
          xport->offset = bfc->apb->curbyte;
          apb           = xport->apb;

          if (xport->offset >= apb->nbytes)
            {
              continue;
            }
        }

      switch (sbytes)
        {
          case 4:
            tc4x_putreg(priv, TC4X_TDM_TX_FIFO_WR,
                        *(FAR uint32_t *)(apb->samp + xport->offset));
            break;

          case 2:
            tc4x_putreg(priv, TC4X_TDM_TX_FIFO_WR,
                        (uint32_t)(*(FAR uint16_t *)(apb->samp +
                                                     xport->offset)));
            break;

          default:  /* 1 byte */
            tc4x_putreg(priv, TC4X_TDM_TX_FIFO_WR,
                        (uint32_t)(*(FAR uint8_t *)(apb->samp +
                                                    xport->offset)));
            break;
        }

      xport->offset += sbytes;
    }
}

/****************************************************************************
 * Name: tc4x_i2s_rx_drain
 *
 * Description:
 *   CPU-drain the RX FIFO into the active audio buffer (mirror of tx_fill).
 *   The FIFO read is 32-bit; a narrower sample takes the low bits.
 *
 ****************************************************************************/

static void tc4x_i2s_rx_drain(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->rx;
  FAR struct ap_buffer_s *apb;
  FAR struct tc4x_i2s_buffer_s *bfc;
  uint32_t sample;
  uint8_t sbytes = (xport->datawidth > 16) ? 4 : (xport->datawidth > 8) ? 2 : 1;

  while ((tc4x_getreg(priv, TC4X_TDM_RX_FIFO_STATUS) &
          TDM_FIFO_STATUS_USED_MASK) > 0)
    {
      apb = xport->apb;

      if (apb == NULL)
        {
          bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->pend);
          if (bfc == NULL)
            {
              break;    /* No buffer to receive into: leave data in FIFO */
            }

          sq_addlast((sq_entry_t *)bfc, &xport->act);
          xport->apb    = bfc->apb;
          xport->offset = bfc->apb->curbyte;
          apb           = xport->apb;
        }

      sample = tc4x_getreg(priv, TC4X_TDM_RX_FIFO_RD);

      switch (sbytes)
        {
          case 4:
            *(FAR uint32_t *)(apb->samp + xport->offset) = sample;
            break;

          case 2:
            *(FAR uint16_t *)(apb->samp + xport->offset) = (uint16_t)sample;
            break;

          default:  /* 1 byte */
            *(FAR uint8_t *)(apb->samp + xport->offset) = (uint8_t)sample;
            break;
        }

      xport->offset  += sbytes;
      apb->nbytes     = xport->offset;

      if (xport->offset >= apb->nmaxbytes)
        {
          tc4x_i2s_rx_schedule(priv, OK);
          xport->apb = NULL;
        }
    }
}

/****************************************************************************
 * Name: tc4x_i2s_tx_start
 ****************************************************************************/

static int tc4x_i2s_tx_start(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->tx;
  int ret;

  if (xport->running || sq_peek(&xport->pend) == NULL)
    {
      return OK;
    }

  /* Program the TDM interface once per stream. */

  ret = tc4x_i2s_tdm_config(priv, true);
  if (ret < 0)
    {
      return ret;
    }

  tc4x_putreg(priv, TC4X_TDM_TX_IOMUX_CTL,
              TDM_TX_IOMUX_SDOUT_DRV_EN |
              FIELD_PREP(TDM_TX_IOMUX_SDOUT_EN_SEL_MASK,
                         TDM_TX_IOMUX_SDOUT_EN_SEL_SHIFT, priv->tx_iomux) |
              FIELD_PREP(TDM_TX_IOMUX_SCK_MUX_SEL_MASK,
                         TDM_TX_IOMUX_SCK_MUX_SEL_SHIFT, priv->tx_iomux) |
              FIELD_PREP(TDM_TX_IOMUX_FSYNC_MUX_SEL_MASK,
                         TDM_TX_IOMUX_FSYNC_MUX_SEL_SHIFT, priv->tx_iomux));

  xport->apb    = NULL;
  xport->offset = 0;

#ifdef TC4X_I2S_HAVE_DMA
  if (xport->dma)
    {
      xport->wait_start = true;

      tc4x_putreg(priv, TC4X_TDM_INTR_TX, TDM_INTR_TX_ALL);
      tc4x_putreg(priv, TC4X_TDM_INTR_TX_MASK,
                  TDM_INTR_TX_FIFO_UNDERFLOW | TDM_INTR_TX_FIFO_OVERFLOW);
      up_enable_irq(priv->tx_irq);

      ret = tc4x_i2s_tx_dma_arm(priv);
      if (ret < 0)
        {
          up_disable_irq(priv->tx_irq);
          xport->wait_start = false;
          return ret;
        }

      xport->running = true;
      return OK;
    }
#endif

  /* Prime the FIFO from the CPU, then activate the FIFO and set the START run
   * bit AFTER ACTIVE.
   */

  tc4x_i2s_tx_fill(priv);

  tc4x_setbits(priv, TC4X_TDM_TX_FIFO_CTL, TDM_FIFO_CTL_ACTIVE);
  tc4x_setbits(priv, TC4X_TDM_TX_CTL, TDM_CTL_START);

  tc4x_putreg(priv, TC4X_TDM_INTR_TX, TDM_INTR_TX_ALL);
  tc4x_putreg(priv, TC4X_TDM_INTR_TX_MASK,
              TDM_INTR_TX_FIFO_TRIGGER | TDM_INTR_TX_FIFO_UNDERFLOW |
              TDM_INTR_TX_FIFO_OVERFLOW);
  up_enable_irq(priv->tx_irq);

  xport->running = true;
  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_rx_start
 ****************************************************************************/

static int tc4x_i2s_rx_start(struct tc4x_i2s_s *priv)
{
  struct tc4x_i2s_xport_s *xport = &priv->rx;
  int ret;

  if (xport->running || sq_peek(&xport->pend) == NULL)
    {
      return OK;
    }

  ret = tc4x_i2s_tdm_config(priv, false);
  if (ret < 0)
    {
      return ret;
    }

  tc4x_putreg(priv, TC4X_TDM_RX_IOMUX_CTL,
              FIELD_PREP(TDM_RX_IOMUX_SD_MUX_SEL_MASK,
                         TDM_RX_IOMUX_SD_MUX_SEL_SHIFT, priv->rx_iomux) |
              FIELD_PREP(TDM_RX_IOMUX_SCK_MUX_SEL_MASK,
                         TDM_RX_IOMUX_SCK_MUX_SEL_SHIFT, priv->rx_iomux) |
              FIELD_PREP(TDM_RX_IOMUX_FSYNC_MUX_SEL_MASK,
                         TDM_RX_IOMUX_FSYNC_MUX_SEL_SHIFT, priv->rx_iomux) |
              FIELD_PREP(TDM_RX_IOMUX_MCK_MUX_SEL_MASK,
                         TDM_RX_IOMUX_MCK_MUX_SEL_SHIFT, priv->rx_iomux));

  xport->apb    = NULL;
  xport->offset = 0;

#ifdef TC4X_I2S_HAVE_DMA
  xport->wait_start = xport->dma;
#endif

  tc4x_putreg(priv, TC4X_TDM_INTR_RX, TDM_INTR_RX_ALL);
  tc4x_putreg(priv, TC4X_TDM_INTR_RX_MASK,
              TDM_INTR_RX_FIFO_TRIGGER | TDM_INTR_RX_FIFO_OVERFLOW |
              TDM_INTR_RX_FIFO_UNDERFLOW | TDM_INTR_RX_IF_OVERFLOW);
  up_enable_irq(priv->rx_irq);

  /* Serializer must run to produce data: ACTIVE then START. */

  tc4x_setbits(priv, TC4X_TDM_RX_FIFO_CTL, TDM_FIFO_CTL_ACTIVE);
  tc4x_setbits(priv, TC4X_TDM_RX_CTL, TDM_CTL_START);

  xport->running = true;
  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_tx_stop / tc4x_i2s_rx_stop
 ****************************************************************************/

static void tc4x_i2s_stop(struct tc4x_i2s_s *priv,
                          struct tc4x_i2s_xport_s *xport, int irq,
                          uintptr_t fifo_ctl, uintptr_t ctl, worker_t worker)
{
  FAR struct tc4x_i2s_buffer_s *bfc;
  irqstate_t flags;

  up_disable_irq(irq);
#ifdef TC4X_I2S_HAVE_DMA
  if (xport->dma)
    {
      DMA_STOP(xport->dmach);
    }
#endif
  tc4x_clrbits(priv, fifo_ctl, TDM_FIFO_CTL_ACTIVE);
  tc4x_clrbits(priv, ctl, TDM_CTL_ENABLED);

  /* Drop every still-queued buffer (active + pending) so a restart starts
   * clean; deferred to the worker because apb_free() takes a mutex and stop
   * may run from an ISR.
   */

  flags = spin_lock_irqsave(&priv->spin);
  xport->running = false;
  xport->apb     = NULL;

  while ((bfc = (FAR struct tc4x_i2s_buffer_s *)
                sq_remfirst(&xport->act)) != NULL)
    {
      bfc->result = -ECANCELED;
      sq_addlast((sq_entry_t *)bfc, &xport->done);
    }

  while ((bfc = (FAR struct tc4x_i2s_buffer_s *)
                sq_remfirst(&xport->pend)) != NULL)
    {
      bfc->result = -ECANCELED;
      sq_addlast((sq_entry_t *)bfc, &xport->done);
    }

  spin_unlock_irqrestore(&priv->spin, flags);

  if (work_available(&xport->work))
    {
      work_queue(HPWORK, &xport->work, worker, priv, 0);
    }
}

static void tc4x_i2s_tx_stop(struct tc4x_i2s_s *priv)
{
  tc4x_i2s_stop(priv, &priv->tx, priv->tx_irq,
                TC4X_TDM_TX_FIFO_CTL, TC4X_TDM_TX_CTL, tc4x_i2s_tx_worker);
}

static void tc4x_i2s_rx_stop(struct tc4x_i2s_s *priv)
{
  tc4x_i2s_stop(priv, &priv->rx, priv->rx_irq,
                TC4X_TDM_RX_FIFO_CTL, TC4X_TDM_RX_CTL, tc4x_i2s_rx_worker);

  /* Empty residual RX FIFO so a restart reads no stale data. */

  while ((tc4x_getreg(priv, TC4X_TDM_RX_FIFO_STATUS) &
          TDM_FIFO_STATUS_USED_MASK) > 0)
    {
      (void)tc4x_getreg(priv, TC4X_TDM_RX_FIFO_RD);
    }
}

/****************************************************************************
 * Name: tc4x_i2s_schedule (+ tx/rx wrappers)
 *
 * Description:
 *   Move the head of a transport's active queue to its done queue with the
 *   given result and schedule that transport's completion worker.  Called
 *   from the ISR (or the fill loop) with the queue protected by the spinlock.
 *
 ****************************************************************************/

static void tc4x_i2s_schedule(struct tc4x_i2s_s *priv,
                              struct tc4x_i2s_xport_s *xport,
                              worker_t worker, int result)
{
  FAR struct tc4x_i2s_buffer_s *bfc;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->spin);
  bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->act);
  if (bfc != NULL)
    {
      bfc->result = result;
      sq_addlast((sq_entry_t *)bfc, &xport->done);
    }

  spin_unlock_irqrestore(&priv->spin, flags);

  if (work_available(&xport->work))
    {
      work_queue(HPWORK, &xport->work, worker, priv, 0);
    }
}

static void tc4x_i2s_tx_schedule(struct tc4x_i2s_s *priv, int result)
{
  tc4x_i2s_schedule(priv, &priv->tx, tc4x_i2s_tx_worker, result);
}

static void tc4x_i2s_rx_schedule(struct tc4x_i2s_s *priv, int result)
{
  tc4x_i2s_schedule(priv, &priv->rx, tc4x_i2s_rx_worker, result);
}

/****************************************************************************
 * Name: tc4x_i2s_complete (+ tx/rx workers)
 *
 * Description:
 *   Run the user completion callbacks for a transport's finished buffers on
 *   the HPWORK thread, then release each audio buffer and its container.
 *
 ****************************************************************************/

static void tc4x_i2s_complete(struct tc4x_i2s_s *priv,
                              struct tc4x_i2s_xport_s *xport)
{
  FAR struct tc4x_i2s_buffer_s *bfc;
  irqstate_t flags;

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->spin);
      bfc = (FAR struct tc4x_i2s_buffer_s *)sq_remfirst(&xport->done);
      spin_unlock_irqrestore(&priv->spin, flags);

      if (bfc == NULL)
        {
          break;
        }

      DEBUGASSERT(bfc->callback != NULL);
      bfc->callback(&priv->dev, bfc->apb, bfc->arg, bfc->result);
      apb_free(bfc->apb);
      tc4x_i2s_buf_free(priv, bfc);
    }
}

static void tc4x_i2s_tx_worker(FAR void *arg)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;

  tc4x_i2s_complete(priv, &priv->tx);
}

static void tc4x_i2s_rx_worker(FAR void *arg)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;

  tc4x_i2s_complete(priv, &priv->rx);
}

/****************************************************************************
 * Name: tc4x_i2s_tx_isr / tc4x_i2s_rx_isr
 *
 * Description:
 *   The TXERR/RXERR service-request lines carry the CPU-side TDM interrupts
 *   (FIFO trigger and the error/underflow/overflow causes).  On a FIFO trigger
 *   the CPU refills/drains the FIFO; on an underflow the current transfer is
 *   completed.
 *
 ****************************************************************************/

static int tc4x_i2s_tx_isr(int irq, FAR void *context, FAR void *arg)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;
  uint32_t status;

  UNUSED(irq);
  UNUSED(context);

  status = tc4x_getreg(priv, TC4X_TDM_INTR_TX_MASKED);

  if ((status & TDM_INTR_TX_FIFO_TRIGGER) != 0)
    {
      /* FIFO_TRIGGER is unmasked only on the CPU-fill path (masked off for the
       * SDMA path), so reaching here implies a CPU-fill instance.
       */

      tc4x_i2s_tx_fill(priv);
    }

  if ((status & TDM_INTR_TX_FIFO_UNDERFLOW) != 0)
    {
      /* FIFO fully drained: if there is still data queued this is a real
       * underflow (report error on the active buffer); otherwise it is the
       * expected end-of-stream.
       */

      if (priv->tx.apb == NULL && sq_peek(&priv->tx.pend) == NULL)
        {
          tc4x_i2s_tx_stop(priv);
        }
      else
        {
          tc4x_i2s_tx_schedule(priv, -EIO);
        }
    }

  tc4x_putreg(priv, TC4X_TDM_INTR_TX, status);
  return OK;
}

static int tc4x_i2s_rx_isr(int irq, FAR void *context, FAR void *arg)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)arg;
  uint32_t status;

  UNUSED(irq);
  UNUSED(context);

  status = tc4x_getreg(priv, TC4X_TDM_INTR_RX_MASKED);

  if ((status & TDM_INTR_RX_FIFO_TRIGGER) != 0)
    {
#ifdef TC4X_I2S_HAVE_DMA
      if (priv->rx.dma)
        {
          /* Defer-bootstrap: the FIFO has filled to the trigger level, so the
           * first (software-kicked) SDMA drain reads valid data.  Then mask
           * FIFO_TRIGGER off - steady-state drains are paced by the FIFO's own
           * hardware request and re-armed in tc4x_i2s_rx_dma_cb.
           */

          if (priv->rx.wait_start)
            {
              priv->rx.wait_start = false;
              tc4x_putreg(priv, TC4X_TDM_INTR_RX_MASK,
                          TDM_INTR_RX_FIFO_OVERFLOW |
                          TDM_INTR_RX_FIFO_UNDERFLOW |
                          TDM_INTR_RX_IF_OVERFLOW);
              tc4x_i2s_rx_dma_arm(priv);
            }
        }
      else
#endif
        {
          tc4x_i2s_rx_drain(priv);
        }
    }

  if ((status & (TDM_INTR_RX_FIFO_OVERFLOW | TDM_INTR_RX_IF_OVERFLOW)) != 0)
    {
      /* FIFO/interface overflow: the RX clock outran the drain.  Report it on
       * the active container and stop.
       */

      tc4x_i2s_rx_schedule(priv, -EIO);
    }

  tc4x_putreg(priv, TC4X_TDM_INTR_RX, status);
  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_samplerate / tc4x_i2s_datawidth / tc4x_i2s_channels
 *
 * Description:
 *   Direction-agnostic get/set helpers acting on one transport.  A zero
 *   argument queries the current value (returns Hz / bits / channels); a
 *   non-zero value sets it.  samplerate returns the resulting bitrate
 *   (rate * width) per the i2s.h contract; datawidth returns the set width.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: tc4x_i2s_rate_supported
 *
 * Description:
 *   True if rate is one of the sample frequencies the AUDIO TDM IP supports
 *   (4, 8, 11.025, 12, 16, 20, 22.05, 24, 30, 32, 40, 44.1, 48,
 *   60, 96, 192 kHz).
 *
 ****************************************************************************/

static bool tc4x_i2s_rate_supported(uint32_t rate)
{
  switch (rate)
    {
      case 4000:
      case 8000:
      case 11025:
      case 12000:
      case 16000:
      case 20000:
      case 22050:
      case 24000:
      case 30000:
      case 32000:
      case 40000:
      case 44100:
      case 48000:
      case 60000:
      case 96000:
      case 192000:
        return true;

      default:
        return false;
    }
}

static int32_t  tc4x_i2s_samplerate(struct tc4x_i2s_xport_s *xport,
                                    uint32_t rate)
{
  DEBUGASSERT(xport && rate > 0);

  if (!tc4x_i2s_rate_supported(rate))
    {
      i2serr("ERROR: unsupported sample rate %lu Hz\n", (unsigned long)rate);
      return -EINVAL;
    }

  xport->samplerate = rate;
  return rate;
}

static int32_t  tc4x_i2s_datawidth(struct tc4x_i2s_xport_s *xport, int bits)
{
  DEBUGASSERT(xport && bits > 1);

  if (tc4x_tdm_word_size_code(bits) < 0)
    {
      i2serr("ERROR: unsupported data width %d bits\n", bits);
      return -EINVAL;
    }

#ifdef TC4X_I2S_HAVE_DMA
  /* SDMA-offloaded direction is 32-bit only (FIFO port is 32-bit access). */

  if (xport->dma && bits != 32)
    {
      i2serr("ERROR: DMA path requires 32-bit width (got %d)\n", bits);
      return -EINVAL;
    }
#endif

  xport->datawidth = (uint8_t)bits;
  return bits;
}

static int tc4x_i2s_channels(struct tc4x_i2s_xport_s *xport, uint8_t channels)
{
  /* 2 = stereo I2S framing; 3..32 = TDM/PCM slots. */

  if (channels < 2 || channels > 32)
    {
      i2serr("ERROR: unsupported channel count %u\n", channels);
      return -EINVAL;
    }

  xport->channels = channels;
  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_transfer
 *
 * Description:
 *   Reference and queue one audio buffer on the given transport, then kick
 *   its serializer via start().  Shared by tc4x_i2s_send (TX) and
 *   tc4x_i2s_receive (RX), which differ only in direction: the enable flag,
 *   the transport (priv->tx / priv->rx) and the start function.
 *
 ****************************************************************************/

static int tc4x_i2s_transfer(struct tc4x_i2s_s *priv,
                             struct tc4x_i2s_xport_s *xport, bool enab,
                             int (*start)(struct tc4x_i2s_s *priv),
                             FAR struct ap_buffer_s *apb,
                             i2s_callback_t callback, FAR void *arg)
{
  FAR struct tc4x_i2s_buffer_s *bfc;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(priv != NULL && apb != NULL);

  if (!enab)
    {
      return -ENOSYS;
    }

  bfc = tc4x_i2s_buf_allocate(priv);
  if (bfc == NULL)
    {
      return -ENOMEM;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      tc4x_i2s_buf_free(priv, bfc);
      return ret;
    }

  apb_reference(apb);
  bfc->callback = callback;
  bfc->arg      = arg;
  bfc->apb      = apb;
  bfc->result   = -EBUSY;

  flags = spin_lock_irqsave(&priv->spin);
  sq_addlast((sq_entry_t *)bfc, &xport->pend);
  spin_unlock_irqrestore(&priv->spin, flags);

  ret = start(priv);
  if (ret < 0)
    {
      i2serr("ERROR: I2S/TDM%d start failed: %d\n", priv->tdm_index, ret);

      /* Roll back what this call queued: dequeue the container, drop the
       * buffer reference taken above, and return the container to the pool.
       */

      flags = spin_lock_irqsave(&priv->spin);
      sq_rem((sq_entry_t *)bfc, &xport->pend);
      spin_unlock_irqrestore(&priv->spin, flags);

      apb_free(apb);
      tc4x_i2s_buf_free(priv, bfc);
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: tc4x_i2s_ccu_wait_unlocked
 *
 * Description:
 *   Wait for the CCU to accept a new clock configuration (CCUSTAT.LCK back to
 *   0), matching the bare-spin convention of tc4x_clockconfig/xspi/leth.
 *
 ****************************************************************************/

static void tc4x_i2s_ccu_wait_unlocked(void)
{
  while (TC4X_CLOCK_CCUSTAT & TC4X_CCUSTAT_LCK)
    {
    }
}

/****************************************************************************
 * Name: tc4x_i2s_apll_set_rate
 *
 * Description:
 *   Configure the Audio PLL from scratch (P=1, K=10, NDIV from fOSC:
 *   fAUDIO = fOSC * N / (P * K)) via the powered-down sequence, trusting
 *   nothing from boot so the clock is deterministic.  APLLCON0/1 must be
 *   written by CPU0 (satisfied on this single-core board).
 *
 ****************************************************************************/

static int tc4x_i2s_apll_set_rate(uint32_t fapll_hz)
{
  uint32_t ndivreg;
  uint32_t val;
  int us;

  if (fapll_hz == 0)
    {
      return -EINVAL;
    }

  /* fDCO = fAUDIO * K must stay inside the 600-1000 MHz DCO window; K and P
   * below are constants, never read from a possibly-unconfigured boot state.
   */

  if ((uint64_t)fapll_hz * TC4X_APLL_K < TC4X_APLL_FDCO_MIN ||
      (uint64_t)fapll_hz * TC4X_APLL_K > TC4X_APLL_FDCO_MAX)
    {
      i2serr("ERROR: Audio PLL fDCO out of range\n");
      return -ERANGE;
    }

  ndivreg = (uint32_t)(((uint64_t)fapll_hz * TC4X_APLL_K * TC4X_APLL_P *
                        4096u) / TC4X_FOSC_HZ) - 4096u;
  ndivreg &= TC4X_APLLCON2_NDIV_MASK;

  /* Skip only when the full target state (P, K, N, powered, locked) is already
   * live, so a mismatch never trusts an unknown boot configuration.
   */

  if (((TC4X_CLOCK_APLLCON0 & TC4X_APLLCON0_PDIV_MASK) >>
        TC4X_APLLCON0_PDIV_SHIFT) == TC4X_APLL_PDIV &&
      (TC4X_CLOCK_APLLCON0 & TC4X_APLLCON0_PLLPWR) != 0 &&
      ((TC4X_CLOCK_APLLCON1 & TC4X_APLLCON1_KDIV_MASK) >>
        TC4X_APLLCON1_KDIV_SHIFT) == TC4X_APLL_KDIV_FINAL &&
      (TC4X_CLOCK_APLLCON2 & TC4X_APLLCON2_NDIV_MASK) == ndivreg &&
      (TC4X_CLOCK_APLLSTAT & TC4X_APLLSTAT_PLLLOCK) != 0)
    {
      return OK;
    }

  /* Powered-down sequence: every APLLCONx write is gated on CCUSTAT.LCK,
   * including APLLCON2 (its APLLSTAT.LCK is only the live single-step gate,
   * not used here).
   */

  TC4X_CLOCK_APLLCON0 = TC4X_CLOCK_APLLCON0 & ~TC4X_APLLCON0_PLLPWR;
  tc4x_i2s_ccu_wait_unlocked();

  /* Set the safe intermediate K (K=2) first; K moves from reset (1) to 10, and
   * the two-step avoids the dead-clock hazard of changing K in one write.
   */

  val  = TC4X_CLOCK_APLLCON1 & ~TC4X_APLLCON1_KDIV_MASK;
  val |= TC4X_APLL_KDIV_SAFE << TC4X_APLLCON1_KDIV_SHIFT;
  TC4X_CLOCK_APLLCON1 = val;
  tc4x_i2s_ccu_wait_unlocked();

  val  = TC4X_CLOCK_APLLCON0 & ~TC4X_APLLCON0_PDIV_MASK;
  val |= TC4X_APLL_PDIV << TC4X_APLLCON0_PDIV_SHIFT;
  TC4X_CLOCK_APLLCON0 = val;
  tc4x_i2s_ccu_wait_unlocked();

  val  = TC4X_CLOCK_APLLCON2 & ~TC4X_APLLCON2_NDIV_MASK;
  val |= ndivreg;
  TC4X_CLOCK_APLLCON2 = val;
  tc4x_i2s_ccu_wait_unlocked();

  TC4X_CLOCK_APLLCON0 = TC4X_CLOCK_APLLCON0 |
                        TC4X_APLLCON0_PLLPWR | TC4X_APLLCON0_RESLD;
  tc4x_i2s_ccu_wait_unlocked();

  for (us = 0; us < TC4X_APLL_LOCK_TIMEOUT_US; us++)
    {
      if ((TC4X_CLOCK_APLLSTAT & TC4X_APLLSTAT_PWRSTAT) != 0)
        {
          break;
        }

      up_udelay(1);
    }

  if (us >= TC4X_APLL_LOCK_TIMEOUT_US)
    {
      i2serr("ERROR: Audio PLL power up timed out\n");
      return -ETIMEDOUT;
    }

  up_mdelay(1);

  for (us = 0; us < TC4X_APLL_LOCK_TIMEOUT_US; us++)
    {
      if ((TC4X_CLOCK_APLLSTAT & TC4X_APLLSTAT_PLLLOCK) != 0)
        {
          break;
        }

      up_udelay(1);
    }

  if (us >= TC4X_APLL_LOCK_TIMEOUT_US)
    {
      i2serr("ERROR: Audio PLL lock timed out\n");
      return -ETIMEDOUT;
    }

  up_udelay(10);
  tc4x_i2s_ccu_wait_unlocked();

  val  = TC4X_CLOCK_APLLCON1 & ~TC4X_APLLCON1_KDIV_MASK;
  val |= TC4X_APLL_KDIV_FINAL << TC4X_APLLCON1_KDIV_SHIFT;
  TC4X_CLOCK_APLLCON1 = val;
  tc4x_i2s_ccu_wait_unlocked();

  return OK;
}

/****************************************************************************
 * Name: tc4x_i2s_apll_for_rate
 *
 * Description:
 *   Return the Audio-PLL frequency whose integer bit-clock divider lands
 *   exactly on the requested rate: 90.3168 MHz for the 44.1 kHz family,
 *   81.92/92.16 MHz for the x5 rates, else 98.304 MHz (48 kHz family).
 *
 ****************************************************************************/

static uint32_t tc4x_i2s_apll_for_rate(uint32_t fs)
{
  if (fs != 0 && (fs % 11025) == 0)
    {
      return 90316800u;         /* 11.025 / 22.05 / 44.1 / 88.2 kHz */
    }

  if (fs == 20000 || fs == 40000)
    {
      return 81920000u;
    }

  if (fs == 30000 || fs == 60000)
    {
      return 92160000u;
    }

  return TC4X_I2S_APLL_FREQUENCY;    /* 98.304 MHz (48 kHz family) */
}

/****************************************************************************
 * Name: tc4x_i2s_set_apll
 *
 * Description:
 *   Switch the Audio PLL to the family for the requested rate; cache clk_if.
 *
 ****************************************************************************/

static int tc4x_i2s_set_apll(struct tc4x_i2s_s *priv, uint32_t rate)
{
  uint32_t fapll = tc4x_i2s_apll_for_rate(rate);
  int ret = tc4x_i2s_apll_set_rate(fapll);

  if (ret == OK)
    {
      priv->clk_if = fapll;
    }

  return ret;
}

/****************************************************************************
 * Name: tc4x_i2s_getmclkfrequency
 *
 * Description:
 *   Report MCKO = clk_if / MCK_DIV for the codec.
 *
 ****************************************************************************/

static int32_t  tc4x_i2s_getmclkfrequency(struct i2s_dev_s *dev)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)dev;

  return priv->clk_if >> priv->mck_div;
}

/****************************************************************************
 * Name: I2S TX/RX ops - thin direction wrappers over the shared helpers
 ****************************************************************************/

static int32_t  tc4x_i2s_txsamplerate(struct i2s_dev_s *dev, uint32_t rate)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)dev;
  int32_t sample_rate = tc4x_i2s_samplerate(&priv->tx, rate);
  int ret;

  /* Program the APLL only when the rate was accepted (>0); a rejected rate
   * returns a negated errno and leaves the current clock untouched.  A failed
   * APLL reprogram is propagated as the error.
   */

  if (sample_rate > 0)
    {
      ret = tc4x_i2s_set_apll(priv, rate);
      if (ret < 0)
        {
          return ret;
        }
    }

  return sample_rate;
}

static int32_t  tc4x_i2s_txdatawidth(struct i2s_dev_s *dev, int bits)
{
  return tc4x_i2s_datawidth(&((struct tc4x_i2s_s *)dev)->tx, bits);
}

static int tc4x_i2s_txchannels(struct i2s_dev_s *dev, uint8_t channels)
{
  return tc4x_i2s_channels(&((struct tc4x_i2s_s *)dev)->tx, channels);
}

static int tc4x_i2s_send(struct i2s_dev_s *dev, FAR struct ap_buffer_s *apb,
                         i2s_callback_t callback, FAR void *arg,
                         uint32_t timeout)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)dev;

  UNUSED(timeout);
  return tc4x_i2s_transfer(priv, &priv->tx, priv->txenab,
                           tc4x_i2s_tx_start, apb, callback, arg);
}

static int32_t  tc4x_i2s_rxsamplerate(struct i2s_dev_s *dev, uint32_t rate)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)dev;
  int32_t sample_rate = tc4x_i2s_samplerate(&priv->rx, rate);
  int ret;

  if (sample_rate > 0)
    {
      ret = tc4x_i2s_set_apll(priv, rate);
      if (ret < 0)
        {
          return ret;
        }
    }

  return sample_rate;
}

static int32_t  tc4x_i2s_rxdatawidth(struct i2s_dev_s *dev, int bits)
{
  return tc4x_i2s_datawidth(&((struct tc4x_i2s_s *)dev)->rx, bits);
}

static int tc4x_i2s_rxchannels(struct i2s_dev_s *dev, uint8_t channels)
{
  return tc4x_i2s_channels(&((struct tc4x_i2s_s *)dev)->rx, channels);
}

static int tc4x_i2s_receive(struct i2s_dev_s *dev,
                            FAR struct ap_buffer_s *apb,
                            i2s_callback_t callback, FAR void *arg,
                            uint32_t timeout)
{
  struct tc4x_i2s_s *priv = (struct tc4x_i2s_s *)dev;

  UNUSED(timeout);
  return tc4x_i2s_transfer(priv, &priv->rx, priv->rxenab,
                           tc4x_i2s_rx_start, apb, callback, arg);
}

/****************************************************************************
 * Name: tc4x_i2s_ioctl
 *
 * Description:
 *   Handle the audio-buffer ioctls forwarded through the I2S lower half: report
 *   the preferred buffering and allocate/free audio pipeline buffers.
 *
 ****************************************************************************/

static int tc4x_i2s_ioctl(struct i2s_dev_s *dev, int cmd, unsigned long arg)
{
  FAR struct tc4x_i2s_s *priv = (FAR struct tc4x_i2s_s *)dev;
  FAR struct audio_buf_desc_s *bufdesc;
  int ret = -ENOTTY;

  switch (cmd)
    {
      /* AUDIOIOC_START/STOP arrive via the audio_i2s adapter with arg =
       * playback (1 = TX, 0 = RX).  The stream is actually kicked off when the
       * first buffer is queued (I2S_SEND/RECEIVE -> tx_start/rx_start), so
       * START is a no-op; STOP halts the requested direction.
       */

      case AUDIOIOC_START:
        ret = OK;
        break;

      case AUDIOIOC_STOP:
        {
          if (arg != 0)
            {
              tc4x_i2s_tx_stop(priv);
            }
          else
            {
              tc4x_i2s_rx_stop(priv);
            }

          ret = OK;
        }
        break;

      case AUDIOIOC_PAUSE:
      case AUDIOIOC_RESUME:
        ret = OK;
        break;

      case AUDIOIOC_ALLOCBUFFER:
        {
          bufdesc = (FAR struct audio_buf_desc_s *)arg;
          ret = apb_alloc(bufdesc);
        }
        break;

      case AUDIOIOC_FREEBUFFER:
        {
          bufdesc = (FAR struct audio_buf_desc_s *)arg;
          DEBUGASSERT(bufdesc->u.buffer != NULL);
          apb_free(bufdesc->u.buffer);
          ret = sizeof(struct audio_buf_desc_s);
        }
        break;

      default:
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: tc4x_i2s_getconfig
 *
 * Description:
 *   Resolve one TDM instance's configuration from its Kconfig options
 *   (CONFIG_TC4X_I2Sn_*), indexed by port.  Returns -ENODEV for a port that
 *   is out of range or whose instance is not enabled.  This keeps the board
 *   free of driver config: it passes only the instance number.
 *
 ****************************************************************************/

static int tc4x_i2s_getconfig(int port, struct tc4x_i2s_config_s *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->port    = (uint8_t)port;

  switch (port)
    {
#ifdef CONFIG_TC4X_I2S0
      case 0:
        cfg->tx_iomux = CONFIG_TC4X_I2S0_TX_IOMUX_SEL;
        cfg->rx_iomux = CONFIG_TC4X_I2S0_RX_IOMUX_SEL;
        cfg->mck_div  = CONFIG_TC4X_I2S0_MCK_DIV;
#ifdef CONFIG_TC4X_I2S0_TX_DMA
        cfg->tx_dma      = true;
        cfg->tx_dma_chan = CONFIG_TC4X_I2S0_TX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S0_RX_DMA
        cfg->rx_dma      = true;
        cfg->rx_dma_chan = CONFIG_TC4X_I2S0_RX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S0_LOOPBACK
        cfg->loopback    = true;
#endif
#ifdef CONFIG_TC4X_I2S0_TX_SLAVE
        cfg->tx_slave    = true;
#endif
#ifdef CONFIG_TC4X_I2S0_RX_SLAVE
        cfg->rx_slave    = true;
#endif
        return OK;
#endif /* CONFIG_TC4X_I2S0 */

#ifdef CONFIG_TC4X_I2S1
      case 1:
        cfg->tx_iomux = CONFIG_TC4X_I2S1_TX_IOMUX_SEL;
        cfg->rx_iomux = CONFIG_TC4X_I2S1_RX_IOMUX_SEL;
        cfg->mck_div  = CONFIG_TC4X_I2S1_MCK_DIV;
#ifdef CONFIG_TC4X_I2S1_TX_DMA
        cfg->tx_dma      = true;
        cfg->tx_dma_chan = CONFIG_TC4X_I2S1_TX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S1_RX_DMA
        cfg->rx_dma      = true;
        cfg->rx_dma_chan = CONFIG_TC4X_I2S1_RX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S1_LOOPBACK
        cfg->loopback    = true;
#endif
#ifdef CONFIG_TC4X_I2S1_TX_SLAVE
        cfg->tx_slave    = true;
#endif
#ifdef CONFIG_TC4X_I2S1_RX_SLAVE
        cfg->rx_slave    = true;
#endif
        return OK;
#endif /* CONFIG_TC4X_I2S1 */

#ifdef CONFIG_TC4X_I2S2
      case 2:
        cfg->tx_iomux = CONFIG_TC4X_I2S2_TX_IOMUX_SEL;
        cfg->rx_iomux = CONFIG_TC4X_I2S2_RX_IOMUX_SEL;
        cfg->mck_div  = CONFIG_TC4X_I2S2_MCK_DIV;
#ifdef CONFIG_TC4X_I2S2_TX_DMA
        cfg->tx_dma      = true;
        cfg->tx_dma_chan = CONFIG_TC4X_I2S2_TX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S2_RX_DMA
        cfg->rx_dma      = true;
        cfg->rx_dma_chan = CONFIG_TC4X_I2S2_RX_DMA_CHAN;
#endif
#ifdef CONFIG_TC4X_I2S2_LOOPBACK
        cfg->loopback    = true;
#endif
#ifdef CONFIG_TC4X_I2S2_TX_SLAVE
        cfg->tx_slave    = true;
#endif
#ifdef CONFIG_TC4X_I2S2_RX_SLAVE
        cfg->rx_slave    = true;
#endif
        return OK;
#endif /* CONFIG_TC4X_I2S2 */

      default:
        return -ENODEV;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tc4x_i2s_initialize
 *
 * Description:
 *   Initialise one AUDIO/TDM instance and return its lower-half I2S device.
 *   The board bring-up code registers it (i2schar_register() or a codec via
 *   audio_register()).  The AUDIO subsystem exists only on TC48x devices.
 *
 * Input Parameters:
 *   port - TDM instance number (0 .. TC4X_AUDIO_NTDM-1).  Its settings are
 *          taken from the CONFIG_TC4X_I2Sn_* Kconfig options.
 *
 * Returned Value:
 *   A pointer to the I2S device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct i2s_dev_s *tc4x_i2s_initialize(int port)
{
  struct tc4x_i2s_s *priv;
  struct tc4x_i2s_config_s cfgbuf;
  FAR const struct tc4x_i2s_config_s *cfg = &cfgbuf;
  int ret;
#ifdef TC4X_I2S_HAVE_DMA
  FAR struct dma_dev_s *dmadev;
  struct dma_config_s dmacfg;
#endif

  if (tc4x_i2s_getconfig(port, &cfgbuf) < 0)
    {
      i2serr("ERROR: I2S/TDM%d not enabled or out of range\n", port);
      return NULL;
    }

  priv = kmm_zalloc(sizeof(struct tc4x_i2s_s));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->dev.ops   = &g_tc4x_i2s_ops;
  priv->tdm_index = port;
  priv->base      = TC4X_AUDIO_TDM_BASE(port);
  priv->mck_div   = cfg->mck_div;
  priv->tx_iomux  = cfg->tx_iomux;
  priv->rx_iomux  = cfg->rx_iomux;
  priv->loopback  = cfg->loopback;
  priv->tx_slave  = cfg->tx_slave;
  priv->rx_slave  = cfg->rx_slave;
  priv->tx_irq    = TC4X_IRQ_AUDIO_TDM_TXERR(port);
  priv->rx_irq    = TC4X_IRQ_AUDIO_TDM_RXERR(port);
  priv->txenab    = true;
  priv->rxenab    = true;

  /* rate/width/channels stay zero until the app sets them via the I2S ops. */

  nxmutex_init(&priv->lock);
  nxsem_init(&priv->bufsem, 0, CONFIG_TC4X_I2S_MAXINFLIGHT);
  spin_lock_init(&priv->spin);
  tc4x_i2s_buf_initialize(priv);

  sq_init(&priv->tx.pend);
  sq_init(&priv->tx.act);
  sq_init(&priv->tx.done);
  sq_init(&priv->rx.pend);
  sq_init(&priv->rx.act);
  sq_init(&priv->rx.done);

  /* Bring up the AUDIO module clock and Audio PLL before any TDM access. */

  ret = tc4x_audio_clk_enable(priv);
  if (ret < 0)
    {
      goto errout;
    }

  /* Attach the CPU-side TX/RX handlers (disabled until a transfer starts). */

  ret = irq_attach(priv->tx_irq, tc4x_i2s_tx_isr, priv);
  if (ret < 0)
    {
      goto errout;
    }

  ret = irq_attach(priv->rx_irq, tc4x_i2s_rx_isr, priv);
  if (ret < 0)
    {
      goto errout;
    }

#ifdef TC4X_I2S_HAVE_DMA
  dmadev = tricore_dma_initialize();
  if (dmadev == NULL)
    {
      ret = -ENODEV;
      goto errout;
    }

  /* SDMA-fed TX FIFO (MEM->PERIPH, 32-bit, FIFO_PACED): grant SDMA0 write
   * access and route the TXDMA FIFO request to the channel.
   */

  if (cfg->tx_dma)
    {
      priv->tx.dma     = true;
      priv->tx.dmachan = cfg->tx_dma_chan;

      priv->tx.dmach = DMA_GET_CHAN(dmadev, priv->tx.dmachan);
      if (priv->tx.dmach == NULL)
        {
          ret = -EBUSY;
          goto errout;
        }

      memset(&dmacfg, 0, sizeof(dmacfg));
      dmacfg.direction = DMA_MEM_TO_DEV;
      dmacfg.src_width = sizeof(uint32_t);
      dmacfg.dst_width = sizeof(uint32_t);
      dmacfg.option    = TRICORE_DMA_BURST(1) | TRICORE_DMA_FIFO_PACED;

      ret = DMA_CONFIG(priv->tx.dmach, &dmacfg);
      if (ret < 0)
        {
          goto errout;
        }

      tc4x_i2s_apu_grant(priv, true, TC4X_I2S_DMA0_TAGID);
      tricore_route_irq_to_dma(TC4X_IRQ_AUDIO_TDM_TXDMA(port),
                               priv->tx.dmachan);
    }

  if (cfg->rx_dma)
    {
      priv->rx.dma     = true;
      priv->rx.dmachan = cfg->rx_dma_chan;

      priv->rx.dmach = DMA_GET_CHAN(dmadev, priv->rx.dmachan);
      if (priv->rx.dmach == NULL)
        {
          ret = -EBUSY;
          goto errout;
        }

      memset(&dmacfg, 0, sizeof(dmacfg));
      dmacfg.direction = DMA_DEV_TO_MEM;
      dmacfg.src_width = sizeof(uint32_t);
      dmacfg.dst_width = sizeof(uint32_t);
      dmacfg.option    = TRICORE_DMA_BURST(1) | TRICORE_DMA_FIFO_PACED;

      ret = DMA_CONFIG(priv->rx.dmach, &dmacfg);
      if (ret < 0)
        {
          goto errout;
        }

      tc4x_i2s_apu_grant(priv, false, TC4X_I2S_DMA0_TAGID);
      tricore_route_irq_to_dma(TC4X_IRQ_AUDIO_TDM_RXDMA(port),
                               priv->rx.dmachan);
    }
#endif /* TC4X_I2S_HAVE_DMA */

  i2sinfo("AUDIO/TDM%d I2S initialised at 0x%08lx\n",
          port, (unsigned long)priv->base);
  return &priv->dev;

errout:
  nxmutex_destroy(&priv->lock);
  nxsem_destroy(&priv->bufsem);
  kmm_free(priv);
  return NULL;
}

#endif /* CONFIG_TC4X_I2S */
