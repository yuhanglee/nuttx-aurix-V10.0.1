/****************************************************************************
 * drivers/net/dwxgmac.c
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

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <syslog.h>
#include <errno.h>

#include <arpa/inet.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/net/net.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/netdev.h>
#include <nuttx/queue.h>
#include <nuttx/random.h>
#include <nuttx/wqueue.h>

#include <arch/barriers.h>
#if defined(CONFIG_DWXGMAC_QOS)
#include "qos_priv.h"
#else
#include "xgmac_priv.h"
#endif



#define ETHWORK             HPWORK


#ifdef CONFIG_NET_DWXGMAC_DMA_SECTION
#  define DWXGMAC_DMA_SECTION \
   __attribute__((section(CONFIG_NET_DWXGMAC_DMA_SECTION)))
#else
#  define DWXGMAC_DMA_SECTION
#endif

#define ETH_IF_COUNT CONFIG_DWXGMAC_NETHERNET
#define RX_DMA_DESC_CNT  CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS
#define TX_DMA_DESC_CNT  CONFIG_DWXGMAC_NUM_TX_DMA_DESCRIPTORS
#define RX_CH_COUNT  CONFIG_DWXGMAC_NUM_RX_CHANNELS
#define TX_CH_COUNT  CONFIG_DWXGMAC_NUM_TX_CHANNELS
#define PKTBUF_SZ  ALIGN_UP(CONFIG_NET_ETH_PKTSIZE, 8)

/* receive() takes a replacement buffer before releasing the one it hands
 * up, so the RX quota must exceed the ring or every refill fails and the
 * frame is dropped.
 */

#define RX_SPARE_BUFS  8
#define RX_QUOTA  (RX_DMA_DESC_CNT * RX_CH_COUNT + RX_SPARE_BUFS)
#define TX_QUOTA  (TX_DMA_DESC_CNT * TX_CH_COUNT)

#if CONFIG_IOB_NBUFFERS < RX_QUOTA + TX_QUOTA
#  error CONFIG_IOB_NBUFFERS is too small for the configured DMA rings
#endif

#if CONFIG_IOB_BUFSIZE < CONFIG_NET_LL_GUARDSIZE + DWXGMAC_RX_BUFSZ
#  error CONFIG_IOB_BUFSIZE must hold the L2 guard plus one RX DMA buffer
#endif


static struct eth_dwmac_dma_desc
  g_rx_descs[ETH_IF_COUNT][RX_CH_COUNT][RX_DMA_DESC_CNT]
  aligned_data(64) DWXGMAC_DMA_SECTION;
static struct eth_dwmac_dma_desc
  g_tx_descs[ETH_IF_COUNT][TX_CH_COUNT][TX_DMA_DESC_CNT]
  aligned_data(64) DWXGMAC_DMA_SECTION;

static struct eth_dwmac_dma_ch_config g_dma_rx_cfg[ETH_IF_COUNT][RX_CH_COUNT];
static struct eth_dwmac_dma_ch_config g_dma_tx_cfg[ETH_IF_COUNT][TX_CH_COUNT];
static struct eth_dwmac_rx_queue     g_mtl_rx_cfg[ETH_IF_COUNT][RX_CH_COUNT];
static struct eth_dwmac_tx_queue     g_mtl_tx_cfg[ETH_IF_COUNT][TX_CH_COUNT];
static struct eth_dwmac_config       g_dwmac_cfg[ETH_IF_COUNT];
static struct eth_xgmac_config       g_xgmac_cfg[ETH_IF_COUNT];
static struct eth_dwmac_dma_rx_ch_data g_dma_rx_data[ETH_IF_COUNT][RX_CH_COUNT];
static struct eth_dwmac_dma_tx_ch_data g_dma_tx_data[ETH_IF_COUNT][TX_CH_COUNT];
static struct eth_xgmac_data           g_xgmac_data[ETH_IF_COUNT];

static FAR struct xgmac_driver_s *g_ch_owner[8];

static struct xgmac_driver_s g_xgmac[CONFIG_DWXGMAC_NETHERNET];

static inline int xgmac_intf(FAR struct xgmac_driver_s *priv)
{
  return (int)(priv - g_xgmac);
}

/* 802.1Q PCP p is steered to RX queue p % RX_CH_COUNT.  TX uses the same
 * rule in xgmac_txch_select(), so both directions of one priority share a
 * channel number.
 */

static uint8_t xgmac_rxq_pcp_mask(uint8_t queue)
{
  uint8_t mask = 0;
  uint8_t pcp;

  for (pcp = 0; pcp < 8; pcp++)
    {
      if (pcp % RX_CH_COUNT == queue)
        {
          mask |= 1u << pcp;
        }
    }

  return mask;
}

static void xgmac_cfg_init(int intf)
{
  uint8_t ch;

  for (ch = 0; ch < RX_CH_COUNT; ch++)
    {
      g_dma_rx_cfg[intf][ch].descs       = g_rx_descs[intf][ch];
      g_dma_rx_cfg[intf][ch].descs_count = RX_DMA_DESC_CNT;
      g_dma_rx_cfg[intf][ch].nr          = ch;

      g_mtl_rx_cfg[intf][ch].nr          = ch;
      g_mtl_rx_cfg[intf][ch].priority    = xgmac_rxq_pcp_mask(ch);
      g_mtl_rx_cfg[intf][ch].threshold   = 64;
      g_mtl_rx_cfg[intf][ch].sf          = true;
      g_mtl_rx_cfg[intf][ch].dma_channel = ch;

      /* Only one queue may claim multicast and broadcast, otherwise the
       * MAC duplicates those frames onto every channel.
       */

      g_mtl_rx_cfg[intf][ch].route_multi_broad = (ch == 0);
    }

  for (ch = 0; ch < TX_CH_COUNT; ch++)
    {
      g_dma_tx_cfg[intf][ch].descs       = g_tx_descs[intf][ch];
      g_dma_tx_cfg[intf][ch].descs_count = TX_DMA_DESC_CNT;
      g_dma_tx_cfg[intf][ch].nr          = ch;

      g_mtl_tx_cfg[intf][ch].nr        = ch;
      g_mtl_tx_cfg[intf][ch].threshold = 64;
      g_mtl_tx_cfg[intf][ch].sf        = true;
    }

  g_dwmac_cfg[intf].dma_rx_channel = RX_CH_COUNT;
  g_dwmac_cfg[intf].dma_tx_channel = TX_CH_COUNT;
  g_dwmac_cfg[intf].dma_rx         = g_dma_rx_cfg[intf];
  g_dwmac_cfg[intf].dma_tx         = g_dma_tx_cfg[intf];
  g_dwmac_cfg[intf].mtl_rx_queues  = RX_CH_COUNT;
  g_dwmac_cfg[intf].mtl_tx_queues  = TX_CH_COUNT;
  g_dwmac_cfg[intf].mtl_rx         = g_mtl_rx_cfg[intf];
  g_dwmac_cfg[intf].mtl_tx         = g_mtl_tx_cfg[intf];
  g_dwmac_cfg[intf].is_xgmac       = true;

  g_xgmac_cfg[intf].dwmac = g_dwmac_cfg[intf];
  g_xgmac_cfg[intf].ubl   = true;
  g_xgmac_cfg[intf].blen  = 0x4;   /* BLEN16 */

  g_xgmac_data[intf].dwmac.dma_rx = g_dma_rx_data[intf];
  g_xgmac_data[intf].dwmac.dma_tx = g_dma_tx_data[intf];
}

static void xgmac_dma_rx_process(FAR struct xgmac_driver_s *priv, uint8_t ch);


static void dma_tx_data_init(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  struct eth_dwmac_dma_tx_ch_data *d = &priv->data->dwmac.dma_tx[ch];
  int cnt = priv->config->dwmac.dma_tx[ch].descs_count;

  d->head = 0;
  d->tail = 0;
  nxmutex_init(&d->lock);
  memset(d->pkt, 0, sizeof(d->pkt));
  memset(priv->config->dwmac.dma_tx[ch].descs, 0,
         cnt * sizeof(struct eth_dwmac_dma_desc));
}

static void dma_rx_data_init(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];
  int cnt = priv->config->dwmac.dma_rx[ch].descs_count;

  d->head = 0;
  d->tail = 0;
  memset(d->pkt, 0, sizeof(d->pkt));
  memset(priv->config->dwmac.dma_rx[ch].descs, 0,
         cnt * sizeof(struct eth_dwmac_dma_desc));
}

static int dma_rx_fill_desc(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  const struct eth_dwmac_dma_ch_config *dcfg = &priv->config->dwmac.dma_rx[ch];
  struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];
  int i;

  for (i = 0; i < (int)dcfg->descs_count; i++)
    {
      if (d->pkt[i] == NULL)
        {
          d->pkt[i] = netpkt_alloc(&priv->dev, NETPKT_RX);
          if (d->pkt[i] == NULL)
            {
              nerr("RX packet alloc failed, ch%u desc %d\n", ch, i);
              return -ENOMEM;
            }
        }

      eth_dwmac_rdes_rd_set(&dcfg->descs[i],
                            (uintptr_t)netpkt_getdata(&priv->dev, d->pkt[i]));
    }

  d->head = 0;
  d->tail = dcfg->descs_count;

  /* Kick DMA by writing tail pointer past last descriptor */

  eth_dwmac_dma_rx_set_tail(priv, ch);
  return OK;
}

static void mac_init(FAR struct xgmac_driver_s *priv)
{
  eth_dwmac_mac_config(priv);
}

/* Compare the requested channel and queue counts against what the MAC
 * was synthesized with, and report the MTL FIFO split.  A queue smaller
 * than one frame cannot hold a full packet, which store-and-forward and
 * checksum insertion both require.
 */

static void xgmac_check_caps(FAR struct xgmac_driver_s *priv)
{
  const struct eth_dwmac_config *cfg = &priv->config->dwmac;
  uint32_t f1 = xgmac_read(priv->base + MAC_HW_FEATURE1);
  uint32_t f2 = xgmac_read(priv->base + MAC_HW_FEATURE2);
  uint32_t rxch = FIELD_GET(MAC_HW_FEATURE2_RXCHCNT, f2) + 1;
  uint32_t txch = FIELD_GET(MAC_HW_FEATURE2_TXCHCNT, f2) + 1;
  uint32_t rxq = FIELD_GET(MAC_HW_FEATURE2_RXQCNT, f2) + 1;
  uint32_t txq = FIELD_GET(MAC_HW_FEATURE2_TXQCNT, f2) + 1;
  uint32_t rxfifo = 128u << FIELD_GET(MAC_HW_FEATURE1_RXFIFOSIZE, f1);
  uint32_t txfifo = 128u << FIELD_GET(MAC_HW_FEATURE1_TXFIFOSIZE, f1);
  uint32_t q;

  syslog(LOG_INFO,
        "xgmac caps: rxch=%" PRIu32 " txch=%" PRIu32 " rxq=%" PRIu32
        " txq=%" PRIu32 " rxfifo=%" PRIu32 " txfifo=%" PRIu32 "\n",
        rxch, txch, rxq, txq, rxfifo, txfifo);

  if (cfg->dma_rx_channel > rxch || cfg->mtl_rx_queues > rxq)
    nerr("RX channels/queues %u/%u exceed hardware %" PRIu32 "/%" PRIu32 "\n",
         cfg->dma_rx_channel, cfg->mtl_rx_queues, rxch, rxq);

  if (cfg->dma_tx_channel > txch || cfg->mtl_tx_queues > txq)
    nerr("TX channels/queues %u/%u exceed hardware %" PRIu32 "/%" PRIu32 "\n",
         cfg->dma_tx_channel, cfg->mtl_tx_queues, txch, txq);

#if CONFIG_DWXGMAC_TX_FIFO_SIZE > 0
  /* The reported size is wrong on some parts, so the board can say what the
   * block really has.
   */

  txfifo = CONFIG_DWXGMAC_TX_FIFO_SIZE;
#endif

  /* Share the synthesized FIFO out over the active queues rather than
   * guessing a budget at build time.
   */

  rxfifo /= cfg->mtl_rx_queues;
  txfifo /= cfg->mtl_tx_queues;

  for (q = 0; q < cfg->mtl_rx_queues; q++)
    priv->config->dwmac.mtl_rx[q].size = rxfifo;

  for (q = 0; q < cfg->mtl_tx_queues; q++)
    priv->config->dwmac.mtl_tx[q].size = txfifo;

  if (rxfifo < CONFIG_NET_ETH_PKTSIZE || txfifo < CONFIG_NET_ETH_PKTSIZE)
    nerr("MTL queue smaller than one frame: rx=%" PRIu32 " tx=%" PRIu32 "\n",
         rxfifo, txfifo);

  /* The checksum engine has to hold the whole frame in the transmit queue
   * until the sum is known, and OSF lets the DMA push a second frame in
   * behind it.  With less room than that the MAC sends the frame on with
   * the checksum field untouched instead of reporting anything, which the
   * peer then discards: TCP looks connected but never carries data.  Fall
   * back to the checksums computed by the network stack, which only exist
   * if they were left enabled.
   */

  if (priv->txcoe && txfifo < 2 * CONFIG_NET_ETH_PKTSIZE)
    {
      priv->txcoe = false;
      priv->data->dwmac.txcoe_available = false;

#ifdef CONFIG_NET_TCP_CHECKSUMS
      syslog(LOG_INFO, "xgmac: TX FIFO %" PRIu32 " B per queue, checksum "
             "insertion disabled\n", txfifo);
#else
      nerr("TX FIFO %" PRIu32 " B per queue cannot insert checksums and "
           "CONFIG_NET_TCP_CHECKSUMS is off: TCP will not pass data\n",
           txfifo);
#endif
    }
}

static void mtl_init(FAR struct xgmac_driver_s *priv)
{
  const struct eth_dwmac_config *cfg = &priv->config->dwmac;
  uint32_t q;

  for (q = 0; q < cfg->mtl_rx_queues; q++)
    eth_dwmac_mtl_rxq_init(priv, q);

  for (q = 0; q < cfg->mtl_tx_queues; q++)
    eth_dwmac_mtl_txq_init(priv, q);

  eth_dwmac_mtl_set_rxq_ctrl(priv);
  eth_dwmac_mtl_set_mode(priv, 0);
}

static void dma_init(FAR struct xgmac_driver_s *priv)
{
  const struct eth_dwmac_config *cfg = &priv->config->dwmac;
  uint8_t ch;

  if (eth_dwmac_dma_reset(priv) < 0)
    nerr("dma_init: DMA SWR did not clear (continuing anyway)\n");

  eth_dwmac_dma_set_sysbus(priv);

  for (ch = 0; ch < cfg->dma_tx_channel; ch++)
    {
      eth_dwmac_dma_tx_init(priv, ch);
      eth_dwmac_dma_tx_irq_enable(priv, ch);
    }

  for (ch = 0; ch < cfg->dma_rx_channel; ch++)
    {
      eth_dwmac_dma_rx_init(priv, ch);
      eth_dwmac_dma_rx_irq_enable(priv, ch);
    }
}

static void set_link_state(FAR struct xgmac_driver_s *priv, bool up)
{
  const struct eth_dwmac_config *cfg = &priv->config->dwmac;
  irqstate_t flags;
  uint8_t ch;

  flags = enter_critical_section();

  if (up)
    {
#if defined(CONFIG_DWXGMAC_QOS)
      /* EQOS MAC_CONFIGURATION speed field is {PS, FES}:
       * 1000Mbps: PS=0 FES=0; 100Mbps: PS=1 FES=1; 10Mbps: PS=1 FES=0
       */
#  if CONFIG_DWXGMAC_LINK_SPEED == 1000
      eth_dwmac_mac_set_link(priv, 0, true);
#  elif CONFIG_DWXGMAC_LINK_SPEED == 100
      eth_dwmac_mac_set_link(priv, 3, true);
#  else
      eth_dwmac_mac_set_link(priv, 2, true);
#  endif
#else
      /* XGMAC MAC_TX_CONFIGURATION.SS: 3=1G, 4=100M, 7=10M. */
#  if CONFIG_DWXGMAC_LINK_SPEED == 1000
      eth_dwmac_mac_set_link(priv, 3, true);
#  elif CONFIG_DWXGMAC_LINK_SPEED == 100
      eth_dwmac_mac_set_link(priv, 4, true);
#  else
      eth_dwmac_mac_set_link(priv, 7, true);
#  endif
#endif

      for (ch = 0; ch < cfg->dma_tx_channel; ch++)
        eth_dwmac_dma_tx_start(priv, ch);
    }
  else
    {
      for (ch = 0; ch < cfg->dma_tx_channel; ch++)
        eth_dwmac_dma_tx_stop(priv, ch);
      for (ch = 0; ch < cfg->dma_tx_channel; ch++)
        eth_dwmac_dma_tx_queue_flush(priv, ch);
    }

  eth_dwmac_mac_rx_set_state(priv, up);
  eth_dwmac_mac_tx_set_state(priv, up);

  ninfo("set_link_state: up=%d\n", up);

  leave_critical_section(flags);
}

/* Pick the TX DMA channel for one frame.  Each channel drains the MTL
 * TX queue of the same number.  The VLAN device inserts the 802.1Q tag
 * before calling transmit, so tagged frames go to channel PCP % channels
 * and untagged frames to channel 0.
 */

static uint8_t xgmac_txch_select(FAR struct xgmac_driver_s *priv,
                                 FAR netpkt_t *pkt, unsigned int len)
{
  uint8_t nch = priv->config->dwmac.dma_tx_channel;
  FAR const uint8_t *frame;

  if (nch <= 1 || len < 16)
    {
      return 0;
    }

  frame = netpkt_getdata(&priv->dev, pkt);
  if (frame[12] != 0x81 || frame[13] != 0x00)
    {
      return 0;
    }

  return (frame[14] >> 5) % nch;
}

static int dwxgmac_transmit(FAR struct netdev_lowerhalf_s *dev,
                            FAR netpkt_t *pkt)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev;
  struct eth_dwmac_config *cfg = &priv->config->dwmac;
  struct eth_dwmac_dma_tx_ch_data *td;
  struct eth_dwmac_dma_ch_config *dcfg;
  struct eth_xgmac_tdes_rd *tdes;
  unsigned int len;
  uint16_t next;
  uint8_t ch;

  len = netpkt_getdatalen(dev, pkt);
  if (len == 0 || len > PKTBUF_SZ)
    return -EINVAL;

  ch   = xgmac_txch_select(priv, pkt, len);
  td   = &priv->data->dwmac.dma_tx[ch];
  dcfg = &cfg->dma_tx[ch];

  nxmutex_lock(&td->lock);

  /* One slot is left unused so a full ring is distinguishable from an
   * empty one.
   */

  next = (td->tail + 1) % dcfg->descs_count;
  if (next == td->head)
    {
      nxmutex_unlock(&td->lock);
      return -ENOSPC;
    }

  tdes = (struct eth_xgmac_tdes_rd *)&dcfg->descs[td->tail];

  eth_dwmac_tdes_rd_set_buffer(tdes, netpkt_getdata(dev, pkt), len);
  UP_DSB();
  eth_dwmac_tdes_rd_set(tdes, true, true, len, priv->txcoe, false);
  UP_DSB();

  td->pkt[td->tail] = pkt;
  td->tail = next;

  eth_dwmac_dma_tx_set_tail(priv, ch);

  ninfo("xgmac_tx: ch=%u len=%u\n", ch, len);

  nxmutex_unlock(&td->lock);
  return OK;
}

static FAR netpkt_t *dwxgmac_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev;
  uint8_t nch = priv->config->dwmac.dma_rx_channel;
  uint8_t ch;
  uint8_t i;

  /* Each call hands up one frame, so always starting the search at channel
   * zero lets a busy channel keep the others waiting: with one VLAN per
   * channel the lowest priority one takes almost the whole link.  Carry on
   * from where the last frame came from instead.
   */

  for (i = 0; i < nch; i++)
    {
      const struct eth_dwmac_dma_ch_config *dcfg;
      struct eth_dwmac_dma_rx_ch_data *d;

      ch   = (priv->rxch + i) % nch;
      dcfg = &priv->config->dwmac.dma_rx[ch];
      d    = &priv->data->dwmac.dma_rx[ch];

      for (; ; )
        {
          FAR void *desc = &dcfg->descs[d->head];
          FAR netpkt_t *pkt;
          FAR netpkt_t *fresh;
          uint16_t pktlen;
          bool good;

          if (eth_dwmac_rdes_own(desc))
            break;

          good = eth_dwmac_rdes_last(desc) &&
                 !eth_dwmac_rdes_desc_error(desc) &&
                 !eth_dwmac_rdes_context(desc) &&
                 !eth_dwmac_rdes_pkt_error(desc) &&
                 !eth_dwmac_rdes_csum_error(desc);

          pktlen = eth_dwmac_rdes_packet_length(desc);
          pkt = d->pkt[d->head];

          /* The slot needs a buffer before the DMA can use it again, so
           * only hand this packet up if a replacement is available.
           */

          fresh = netpkt_alloc(dev, NETPKT_RX);
          if (fresh != NULL)
            {
              d->pkt[d->head] = fresh;
            }
          else
            {
              NETDEV_RXDROPPED(&dev->netdev);
              pkt = NULL;
            }

          eth_dwmac_rdes_rd_set(desc,
            (uintptr_t)netpkt_getdata(dev, d->pkt[d->head]));

          d->head = (d->head + 1) % dcfg->descs_count;
          d->tail = dcfg->descs_count;
          eth_dwmac_dma_rx_set_tail(priv, ch);

          if (pkt == NULL)
            {
              continue;
            }

          if (!good || pktlen == 0 || pktlen > DWXGMAC_RX_BUFSZ)
            {
              NETDEV_RXERRORS(&dev->netdev);
              netpkt_free(dev, pkt, NETPKT_RX);
              continue;
            }

          netpkt_setdatalen(dev, pkt, pktlen);
          priv->rxch = (ch + 1) % nch;
          return pkt;
        }
    }

  return NULL;
}

static void xgmac_dma_rx_process(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  eth_dwmac_dma_rx_clear_irq(priv, ch);
  netdev_lower_rxready(&priv->dev);
}

/* netdev_lower_txdone() walks the VLAN devices under net_lock(), which may
 * block, so it must not run in the interrupt handler.
 */

static void xgmac_txdone_work(FAR void *arg)
{
  FAR struct xgmac_driver_s *priv = arg;

  netdev_lower_txdone(&priv->dev);
}

static void xgmac_dma_tx_process(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  const struct eth_dwmac_dma_ch_config *dcfg = &priv->config->dwmac.dma_tx[ch];
  struct eth_dwmac_dma_tx_ch_data *td = &priv->data->dwmac.dma_tx[ch];
  bool freed = false;

  eth_dwmac_dma_tx_clear_irq(priv, ch);

  while (td->head != td->tail)
    {
      FAR void *desc = &dcfg->descs[td->head];

      if (eth_dwmac_tdes_own(desc))
        break;

      if (eth_dwmac_tdes_last(desc) && eth_dwmac_tdes_error(desc))
        {
          nerr("TX DMA error ch%d\n", ch);
          NETDEV_TXERRORS(&priv->dev.netdev);
        }

      if (td->pkt[td->head] != NULL)
        {
          netpkt_free(&priv->dev, td->pkt[td->head], NETPKT_TX);
          td->pkt[td->head] = NULL;
        }

      td->head = (td->head + 1) % dcfg->descs_count;
      freed = true;
    }

  if (freed && work_available(&priv->txdone_work))
    {
      work_queue(ETHWORK, &priv->txdone_work, xgmac_txdone_work, priv, 0);
    }
}

int dwxgmac_mac_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct xgmac_driver_s *priv = &g_xgmac[(int)arg];
  uint32_t dma_status;

  /* Drain the DMA channel-status line until it reads zero before
   * returning.  The GETH interrupt is delivered through an
   * edge-triggered AURIX SRC, so the IRQ line must be deasserted
   * before the ISR exits or the next completion will not produce a
   * new rising edge and the SRC will not re-arm.
   */

  while ((dma_status =
          xgmac_read(priv->dmabase + DMA_INTERRUPT_STATUS) & 0xFFFF) != 0)
    {
      while (dma_status)
        {
          uint8_t dma_ch = __builtin_ctz(dma_status);
          uint32_t ch_sts =
            xgmac_read(priv->dmabase + DMA_CHi_STATUS(dma_ch));
          FAR struct xgmac_driver_s *chpriv = g_ch_owner[dma_ch];

          /* The channel belongs to whichever interface registered it, not
           * necessarily the one this interrupt was attached for.
           */

          if (chpriv == NULL)
            {
              chpriv = priv;
            }

          if (ch_sts & (DMA_CHi_STATUS_RI | DMA_CHi_STATUS_RBU))
            xgmac_dma_rx_process(chpriv, dma_ch);

          if (ch_sts & DMA_CHi_STATUS_TI)
            xgmac_dma_tx_process(chpriv, dma_ch);

          /* Clear all status bits for this channel */
          xgmac_write(ch_sts, priv->dmabase + DMA_CHi_STATUS(dma_ch));

          dma_status &= ~(1u << dma_ch);
        }
    }

#ifdef CONFIG_DWXGMAC_QOS
  if (xgmac_read(priv->base + MAC_INTERRUPT_STATUS) &
      (MAC_INTERRUPT_STATUS_MMCIS | MAC_INTERRUPT_STATUS_MMCRXIS |
       MAC_INTERRUPT_STATUS_MMCTXIS | MAC_INTERRUPT_STATUS_MMCRXIPIS))
    {
      xgmac_read(priv->base + MMC_RX_INTERRUPT);
      xgmac_read(priv->base + MMC_TX_INTERRUPT);
      xgmac_read(priv->base + MMC_IPC_RX_INTERRUPT);
    }
#endif

  return OK;
}

static int dwxgmac_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev;
  uint8_t ch;
  int ret;

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    {
      ret = dma_rx_fill_desc(priv, ch);
      if (ret < 0)
        return ret;

      eth_dwmac_dma_rx_start(priv, ch);
    }

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    eth_dwmac_dma_tx_start(priv, ch);

  if (priv->irq)
    up_enable_irq(priv->irq);

  priv->ifup = true;

  set_link_state(priv, priv->link_up);

  if (priv->link_up)
    {
      netdev_lower_carrier_on(dev);
    }
  else
    {
      netdev_lower_carrier_off(dev);
    }

  return OK;
}

static int dwxgmac_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev;
  uint8_t ch;
  int i;

  if (priv->irq)
    up_disable_irq(priv->irq);

  work_cancel(ETHWORK, &priv->txdone_work);

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    {
      eth_dwmac_dma_rx_stop(priv, ch);
      eth_dwmac_dma_rx_disable_irq(priv, ch);
    }

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    {
      eth_dwmac_dma_tx_stop(priv, ch);
      eth_dwmac_dma_tx_disable_irq(priv, ch);
    }

  /* Give the buffers held by both rings back to the pool. */

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    {
      struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];

      for (i = 0; i < priv->config->dwmac.dma_rx[ch].descs_count; i++)
        {
          if (d->pkt[i] != NULL)
            {
              netpkt_free(dev, d->pkt[i], NETPKT_RX);
              d->pkt[i] = NULL;
            }
        }
    }

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    {
      struct eth_dwmac_dma_tx_ch_data *td = &priv->data->dwmac.dma_tx[ch];

      for (i = 0; i < priv->config->dwmac.dma_tx[ch].descs_count; i++)
        {
          if (td->pkt[i] != NULL)
            {
              netpkt_free(dev, td->pkt[i], NETPKT_TX);
              td->pkt[i] = NULL;
            }
        }

      td->head = 0;
      td->tail = 0;
    }

  priv->ifup = false;
  set_link_state(priv, false);
  netdev_lower_carrier_off(dev);

  return OK;
}

static void xgmac_generate_mac(FAR uint8_t *mac)
{
  uint32_t rnd = 0;

  arc4random_buf(&rnd, sizeof(rnd));

  mac[0] = 0x02;  /* locally administered, unicast */
  mac[1] = 0x00;
  mac[2] = 0x00;
  mac[3] = (rnd >> 0) & 0xFF;
  mac[4] = (rnd >> 8) & 0xFF;
  mac[5] = (rnd >> 16) & 0xFF;
}

/****************************************************************************
 * Name: dwxgmac_carrier
 *
 * Description:
 *   Report a PHY link change.  Boards whose link takes time to converge
 *   call this when the PHY (and SerDes, where present) reports link, and
 *   again if it is lost.
 *
 ****************************************************************************/

void dwxgmac_carrier(int intf, bool up)
{
  FAR struct xgmac_driver_s *priv = &g_xgmac[intf];

  priv->link_up = up;

  if (!priv->ifup)
    {
      return;
    }

  set_link_state(priv, up);

  if (up)
    {
      netdev_lower_carrier_on(&priv->dev);
    }
  else
    {
      netdev_lower_carrier_off(&priv->dev);
    }
}

static const struct netdev_ops_s g_dwxgmac_ops =
{
  .ifup     = dwxgmac_ifup,
  .ifdown   = dwxgmac_ifdown,
  .transmit = dwxgmac_transmit,
  .receive  = dwxgmac_receive,
};

int xgmac_initialize(int intf)
{
  FAR struct xgmac_driver_s *priv;
  uint8_t ch;
  int ret;
  uint32_t hwf0;

  DEBUGASSERT(intf < CONFIG_DWXGMAC_NETHERNET);
  priv = &g_xgmac[intf];
  memset(priv, 0, sizeof(*priv));

  xgmac_cfg_init(intf);

  priv->base    = CONFIG_DWXGMAC_BASE + intf * CONFIG_DWXGMAC_MAC_STRIDE;
  priv->dmabase = CONFIG_DWXGMAC_BASE + CONFIG_DWXGMAC_DMA_OFFSET;
  priv->irq     = CONFIG_DWXGMAC_DMA_IRQ;
  priv->config  = &g_xgmac_cfg[intf];
  priv->data    = &g_xgmac_data[intf];

  /* FIXME: boards without link reporting never
   * call dwxgmac_carrier(), and must not be left with carrier down.
   */

  priv->link_up = true;

  priv->dev.ops = &g_dwxgmac_ops;

  /* Cap how many pool buffers the rings may hold at once. */

  priv->dev.quota[NETPKT_TX] = TX_QUOTA;
  priv->dev.quota[NETPKT_RX] = RX_QUOTA;

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    dma_tx_data_init(priv, ch);

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    dma_rx_data_init(priv, ch);

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    g_ch_owner[priv->config->dwmac.dma_rx[ch].nr] = priv;

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    g_ch_owner[priv->config->dwmac.dma_tx[ch].nr] = priv;

  dma_init(priv);

  xgmac_write(0, priv->base + MAC_INTERRUPT_ENABLE);
  xgmac_write(0xFFFFFFFF, priv->base + MMC_FPE_RX_INTERRUPT_MASK);
  xgmac_write(0xFFFFFFFF, priv->base + MMC_FPE_TX_INTERRUPT_MASK);
  xgmac_write(0xFFFFFFFF, priv->base + MMC_IPC_RX_INTERRUPT_MASK);
#ifdef CONFIG_DWXGMAC_QOS
  xgmac_write(0xFFFFFFFF, priv->base + MMC_RX_INTERRUPT_MASK);
  xgmac_write(0xFFFFFFFF, priv->base + MMC_TX_INTERRUPT_MASK);
  xgmac_write(MMC_CONTROL_MCF | MMC_CONTROL_CNTRST,
              priv->base + MMC_CONTROL);
#endif

  hwf0 = xgmac_read(priv->base + MAC_HW_FEATURE0);

  priv->txcoe = (hwf0 & MAC_HW_FEATURE0_TXCOESEL) != 0;
  priv->data->dwmac.txcoe_available = priv->txcoe;
  priv->data->dwmac.rxcoe_available =
             (hwf0 & MAC_HW_FEATURE0_RXCOESEL) != 0;

  xgmac_check_caps(priv);
  mtl_init(priv);
  mac_init(priv);

  xgmac_generate_mac(priv->dev.netdev.d_mac.ether.ether_addr_octet);
  eth_dwmac_set_mac_addr(priv, 0,
                         priv->dev.netdev.d_mac.ether.ether_addr_octet);

  if (priv->irq)
    {
      ret = irq_attach(priv->irq, dwxgmac_mac_isr, (void *)intf);
      if (ret < 0)
        return ret;
    }

  ret = netdev_lower_register(&priv->dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      if (priv->irq)
        irq_detach(priv->irq);

      return ret;
    }

  printf("XGMAC%d initialized, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", intf,
        priv->dev.netdev.d_mac.ether.ether_addr_octet[0],
        priv->dev.netdev.d_mac.ether.ether_addr_octet[1],
        priv->dev.netdev.d_mac.ether.ether_addr_octet[2],
        priv->dev.netdev.d_mac.ether.ether_addr_octet[3],
        priv->dev.netdev.d_mac.ether.ether_addr_octet[4],
        priv->dev.netdev.d_mac.ether.ether_addr_octet[5]);

  return OK;
}
