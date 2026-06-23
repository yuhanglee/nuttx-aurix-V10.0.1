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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
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

static struct eth_dwmac_dma_desc
  g_rx0_descs[CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS]
  aligned_data(64) DWXGMAC_DMA_SECTION;
static struct eth_dwmac_dma_desc
  g_tx0_descs[CONFIG_DWXGMAC_NUM_TX_DMA_DESCRIPTORS]
  aligned_data(64) DWXGMAC_DMA_SECTION;

static uint8_t
  g_rxbuf_pool[CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS]
              [ALIGN_UP(CONFIG_NET_ETH_PKTSIZE, 8)]
  aligned_data(64) DWXGMAC_DMA_SECTION;

static uint8_t
  g_txbuf_pool[CONFIG_DWXGMAC_NUM_TX_DMA_DESCRIPTORS]
              [ALIGN_UP(CONFIG_NET_ETH_PKTSIZE, 8)]
  aligned_data(64) DWXGMAC_DMA_SECTION;

static uint8_t *g_txslot_buf[CONFIG_DWXGMAC_NUM_TX_DMA_DESCRIPTORS];

static uint8_t
  g_pollbuf[ALIGN_UP(CONFIG_NET_ETH_PKTSIZE + CONFIG_NET_GUARDSIZE, 8)]
  aligned_data(64) DWXGMAC_DMA_SECTION;

static struct eth_dwmac_dma_ch_config g_dma_rx_cfg[1] =
{
  { .descs = g_rx0_descs,
    .descs_count = CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS,
    .nr = 0
  },
};

static struct eth_dwmac_dma_ch_config g_dma_tx_cfg[1] =
{
  { .descs = g_tx0_descs,
    .descs_count = CONFIG_DWXGMAC_NUM_TX_DMA_DESCRIPTORS,
    .nr = 0
  },
};

static struct eth_dwmac_rx_queue g_mtl_rx_cfg[1] =
{
  { .nr = 0,
    .size = 8192,
    .priority = 0xFF,
    .threshold = 64,
    .sf = true,
    .dma_channel = 0,
    .route_multi_broad = true,
  },
};

static struct eth_dwmac_tx_queue g_mtl_tx_cfg[1] =
{
  { .nr = 0,
    .size = 4096,
    .threshold = 64,
    .sf = true
  },
};

static const struct eth_dwmac_config g_dwmac_cfg0 =
{
  .dma_rx_channel = 1,
  .dma_tx_channel = 1,
  .dma_rx = g_dma_rx_cfg,
  .dma_tx = g_dma_tx_cfg,
  .mtl_rx_queues = 1,
  .mtl_tx_queues = 1,
  .mtl_rx = g_mtl_rx_cfg,
  .mtl_tx = g_mtl_tx_cfg,
  .is_xgmac = true,
};

static struct eth_xgmac_config g_xgmac_cfg[CONFIG_DWXGMAC_NETHERNET] =
{
  {
    .dwmac = g_dwmac_cfg0,
    .ubl = true,
    .blen = 0x4,  /* BLEN16 (1 << 3 in SYSBUS_MODE, written as blen << 1) */
  }
};

static struct eth_dwmac_dma_rx_ch_data g_dma_rx_data[1];
static struct eth_dwmac_dma_tx_ch_data g_dma_tx_data[1];

static struct eth_xgmac_data g_xgmac_data[CONFIG_DWXGMAC_NETHERNET] =
{
  { .dwmac = { .dma_rx = g_dma_rx_data, .dma_tx = g_dma_tx_data } },
};

static struct xgmac_driver_s g_xgmac[CONFIG_DWXGMAC_NETHERNET];

static void xgmac_dma_rx_process(FAR struct xgmac_driver_s *priv, uint8_t ch);
void xgmac_txavail_work(FAR void *arg);


static void dma_tx_data_init(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  struct eth_dwmac_dma_tx_ch_data *d = &priv->data->dwmac.dma_tx[ch];
  int cnt = priv->config->dwmac.dma_tx[ch].descs_count;

  int i;

  d->head = 0;
  d->tail = 0;
  nxmutex_init(&d->lock);
  nxsem_init(&d->desc_used, 0, cnt - 1);
  memset(priv->config->dwmac.dma_tx[ch].descs, 0,
         cnt * sizeof(struct eth_dwmac_dma_desc));

  for (i = 0; i < cnt; i++)
    {
      g_txslot_buf[i] = g_txbuf_pool[i];
    }
}

static void dma_rx_data_init(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];
  int cnt = priv->config->dwmac.dma_rx[ch].descs_count;
  int i;

  d->head = 0;
  d->tail = 0;
  nxsem_init(&d->desc_used, 0, cnt - 1);

  for (i = 0; i < cnt; i++)
    {
      d->data[i] = g_rxbuf_pool[i];
    }

  memset(priv->config->dwmac.dma_rx[ch].descs, 0,
         cnt * sizeof(struct eth_dwmac_dma_desc));
}

static void dma_rx_fill_desc(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  const struct eth_dwmac_dma_ch_config *dcfg = &priv->config->dwmac.dma_rx[ch];
  struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];
  int i;

  for (i = 0; i < (int)dcfg->descs_count; i++)
    {
      eth_dwmac_rdes_rd_set(&dcfg->descs[i], (uintptr_t)d->data[i]);
    }

  d->head = 0;
  d->tail = dcfg->descs_count;

  /* Kick DMA by writing tail pointer past last descriptor */
  eth_dwmac_dma_rx_set_tail(priv, ch);
}

static void mac_init(FAR struct xgmac_driver_s *priv)
{
  eth_dwmac_mac_config(priv);
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
      /* 100Mbps full duplex: SS=4 for XGMAC 100M */
      eth_dwmac_mac_set_link(priv, 4, true);
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

static int xgmac_send(FAR struct xgmac_driver_s *priv,
                      FAR uint8_t *buf, uint16_t len, bool swap)
{
  struct eth_dwmac_config *cfg = &priv->config->dwmac;
  struct eth_dwmac_dma_tx_ch_data *td = &priv->data->dwmac.dma_tx[0];
  struct eth_dwmac_dma_ch_config *dcfg = &cfg->dma_tx[0];
  struct eth_xgmac_tdes_rd *tdes;
  FAR uint8_t *txbuf;

  if (!buf || len == 0 || len > sizeof(g_txbuf_pool[0]))
    return -EINVAL;

  nxmutex_lock(&td->lock);

  if (nxsem_wait_uninterruptible(&td->desc_used) < 0)
    {
      nxmutex_unlock(&td->lock);
      return -ETIMEDOUT;
    }

  if (swap)
    {
      /* Zero-copy: hand the caller's buffer (d_buf) to the DMA and
       * give the stack the slot's previous buffer, which the
       * descriptor-grant guarantees is no longer in flight.
       */

      txbuf = buf;
      priv->dev.d_buf = g_txslot_buf[td->tail];
      g_txslot_buf[td->tail] = buf;
    }
  else
    {
      /* Copy into the slot-owned buffer so the caller's buffer (an
       * RX ring buffer carrying an in-place reply) is free for reuse
       * immediately.
       */

      txbuf = g_txslot_buf[td->tail];
      memcpy(txbuf, buf, len);
    }

  tdes = (struct eth_xgmac_tdes_rd *)&dcfg->descs[td->tail];

  eth_dwmac_tdes_rd_set_buffer(tdes, txbuf, len);
  UP_DSB();
  eth_dwmac_tdes_rd_set(tdes, true, true, len, priv->txcoe, false);
  UP_DSB();

  td->tail = (td->tail + 1) % dcfg->descs_count;

  eth_dwmac_dma_tx_set_tail(priv, 0);

  ninfo("xgmac_tx: len=%u\n", len);

  nxmutex_unlock(&td->lock);
  return OK;
}

static int xgmac_txpoll(FAR struct net_driver_s *dev)
{
  if (dev->d_len > 0)
    {
      xgmac_send(dev->d_private, dev->d_buf, dev->d_len, true);
      dev->d_len = 0;
    }

   return  0;
}

static void xgmac_rx_input(FAR struct xgmac_driver_s *priv)
{
  struct net_driver_s *dev = &priv->dev;
  struct eth_hdr_s *eth = (struct eth_hdr_s *)dev->d_buf;

#ifdef CONFIG_NET_IPv4
  if (eth->type == HTONS(ETHTYPE_IP))
    {
      NETDEV_RXIPV4(dev);
      ipv4_input(dev);
      if (dev->d_len > 0)
        {
          xgmac_send(priv, dev->d_buf, dev->d_len, false);
          dev->d_len = 0;
        }
    }
  else
#endif
#ifdef CONFIG_NET_IPv6
  if (eth->type == HTONS(ETHTYPE_IP6))
    {
      NETDEV_RXIPV6(dev);
      ipv6_input(dev);
      if (dev->d_len > 0)
        {
          xgmac_send(priv, dev->d_buf, dev->d_len, false);
          dev->d_len = 0;
        }
    }
  else
#endif
#ifdef CONFIG_NET_ARP
  if (eth->type == HTONS(ETHTYPE_ARP))
    {
      NETDEV_RXARP(dev);
      arp_input(dev);
      if (dev->d_len > 0)
        {
          xgmac_send(priv, dev->d_buf, dev->d_len, false);
          dev->d_len = 0;
        }
    }
  else
#endif
    {
      NETDEV_RXDROPPED(dev);
    }

  dev->d_len = 0;
}

static void xgmac_rx_work(FAR void *arg)
{
  FAR struct xgmac_driver_s *priv = arg;
  struct net_driver_s *dev = &priv->dev;
  uint8_t ch;

  net_lock();

  /* d_buf temporarily points at RX ring buffers below; remember the
   * stack's current TX compose buffer.  An ACK sent from the input
   * path uses the copy path, but a zero-copy swap can still occur if
   * the poll path runs from this work item, so reload via priv.
   */

  priv->pollbuf = dev->d_buf;

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    {
      const struct eth_dwmac_dma_ch_config *dcfg =
        &priv->config->dwmac.dma_rx[ch];
      struct eth_dwmac_dma_rx_ch_data *d = &priv->data->dwmac.dma_rx[ch];
      bool processed = false;

      for (; ; )
        {
          void *desc = &dcfg->descs[d->head];

          if (eth_dwmac_rdes_own(desc))
            break;

          if (eth_dwmac_rdes_desc_error(desc))
            {
              nerr("RX desc error at %d\n", d->head);
              NETDEV_RXERRORS(dev);
            }
          else if (eth_dwmac_rdes_context(desc))
            {
              /* ignore context descriptors */
            }
          else if (eth_dwmac_rdes_pkt_error(desc))
            {
              NETDEV_RXERRORS(dev);
            }
          else if (eth_dwmac_rdes_csum_error(desc))
            {
              NETDEV_RXERRORS(dev);
            }
          else if (eth_dwmac_rdes_last(desc))
            {
              uint16_t pktlen = eth_dwmac_rdes_packet_length(desc);

              if (pktlen > 0 && pktlen <= CONFIG_NET_ETH_PKTSIZE)
                {
                  dev->d_buf = d->data[d->head];
                  dev->d_len = pktlen;
                  NETDEV_RXPACKETS(dev);
                  xgmac_rx_input(priv);
                }
            }

          eth_dwmac_rdes_rd_set(desc, (uintptr_t)d->data[d->head]);
          d->head = (d->head + 1) % dcfg->descs_count;
          processed = true;
        }

      if (processed)
        {
          d->tail = dcfg->descs_count;
          eth_dwmac_dma_rx_set_tail(priv, ch);
        }
    }

  dev->d_buf = priv->pollbuf;
  dev->d_len = 0;
  net_unlock();
}

static void xgmac_dma_rx_process(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  eth_dwmac_dma_rx_clear_irq(priv, ch);
  work_queue(ETHWORK, &priv->rxwork, xgmac_rx_work, priv, 0);
}

static void xgmac_dma_tx_process(FAR struct xgmac_driver_s *priv, uint8_t ch)
{
  const struct eth_dwmac_dma_ch_config *dcfg = &priv->config->dwmac.dma_tx[ch];
  struct eth_dwmac_dma_tx_ch_data *td = &priv->data->dwmac.dma_tx[ch];
  bool freed = false;

  eth_dwmac_dma_tx_clear_irq(priv, ch);

  while (td->head != td->tail)
    {
      void *desc = &dcfg->descs[td->head];

      if (eth_dwmac_tdes_own(desc))
        break;

      if (eth_dwmac_tdes_last(desc) && eth_dwmac_tdes_error(desc))
        {
          nerr("TX DMA error ch%d\n", ch);
          NETDEV_TXERRORS(&priv->dev);
        }

      td->head = (td->head + 1) % dcfg->descs_count;
      nxsem_post(&td->desc_used);
      freed = true;
    }

  if (freed && priv->ifup && work_available(&priv->pollwork))
    work_queue(ETHWORK, &priv->pollwork, xgmac_txavail_work, priv, 0);
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
          xgmac_read(priv->base + DMA_INTERRUPT_STATUS) & 0xFFFF) != 0)
    {
      while (dma_status)
        {
          uint8_t dma_ch = __builtin_ctz(dma_status);
          uint32_t ch_sts =
            xgmac_read(priv->base + DMA_CHi_STATUS(dma_ch));

          if (ch_sts & (DMA_CHi_STATUS_RI | DMA_CHi_STATUS_RBU))
            xgmac_dma_rx_process(priv, dma_ch);

          if (ch_sts & DMA_CHi_STATUS_TI)
            xgmac_dma_tx_process(priv, dma_ch);

          /* Clear all status bits for this channel */
          xgmac_write(ch_sts, priv->base + DMA_CHi_STATUS(dma_ch));

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

static int dwxgmac_ifup(FAR struct net_driver_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev->d_private;
  uint8_t ch;

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    {
      dma_rx_fill_desc(priv, ch);
      eth_dwmac_dma_rx_start(priv, ch);
    }

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    eth_dwmac_dma_tx_start(priv, ch);

  if (priv->irq)
    up_enable_irq(priv->irq);

  priv->ifup = true;
  set_link_state(priv, true);
  netdev_carrier_on(dev);

  return OK;
}

static int dwxgmac_ifdown(FAR struct net_driver_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev->d_private;
  uint8_t ch;

  if (priv->irq)
    up_disable_irq(priv->irq);

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

  priv->ifup = false;
  set_link_state(priv, false);
  netdev_carrier_off(dev);

  return OK;
}

void xgmac_txavail_work(FAR void *arg)
{
  FAR struct xgmac_driver_s *priv = arg;

  net_lock();
  if (priv->ifup)
    devif_poll(&priv->dev, xgmac_txpoll);
  net_unlock();
}

int dwxgmac_txavail(FAR struct net_driver_s *dev)
{
  FAR struct xgmac_driver_s *priv = (FAR struct xgmac_driver_s *)dev->d_private;

  if (work_available(&priv->pollwork))
    work_queue(ETHWORK, &priv->pollwork, xgmac_txavail_work, priv, 0);

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

int xgmac_initialize(int intf)
{
  FAR struct xgmac_driver_s *priv;
  uint8_t ch;
  int ret;

  DEBUGASSERT(intf < CONFIG_DWXGMAC_NETHERNET);
  priv = &g_xgmac[intf];
  memset(priv, 0, sizeof(*priv));

  priv->base   = CONFIG_DWXGMAC_BASE;
  priv->irq    = CONFIG_DWXGMAC_DMA_IRQ;
  priv->config = &g_xgmac_cfg[intf];
  priv->data   = &g_xgmac_data[intf];

  priv->dev.d_ifup    = dwxgmac_ifup;
  priv->dev.d_ifdown  = dwxgmac_ifdown;
  priv->dev.d_txavail = dwxgmac_txavail;
  priv->dev.d_private = priv;
  priv->dev.d_buf     = g_pollbuf;

  for (ch = 0; ch < priv->config->dwmac.dma_tx_channel; ch++)
    dma_tx_data_init(priv, ch);

  for (ch = 0; ch < priv->config->dwmac.dma_rx_channel; ch++)
    dma_rx_data_init(priv, ch);

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

  {
    uint32_t hwf0 = xgmac_read(priv->base + MAC_HW_FEATURE0);

    priv->txcoe = (hwf0 & MAC_HW_FEATURE0_TXCOESEL) != 0;
    priv->data->dwmac.txcoe_available = priv->txcoe;
    priv->data->dwmac.rxcoe_available =
      (hwf0 & MAC_HW_FEATURE0_RXCOESEL) != 0;
  }
#endif

  mtl_init(priv);
  mac_init(priv);

  xgmac_generate_mac(priv->dev.d_mac.ether.ether_addr_octet);
  eth_dwmac_set_mac_addr(priv, 0, priv->dev.d_mac.ether.ether_addr_octet);

  if (priv->irq)
    {
      ret = irq_attach(priv->irq, dwxgmac_mac_isr, (void *)intf);
      if (ret < 0)
        return ret;
    }

  ret = netdev_register(&priv->dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      if (priv->irq)
        irq_detach(priv->irq);

      return ret;
    }

  printf("XGMAC%d initialized, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", intf,
        priv->dev.d_mac.ether.ether_addr_octet[0],
        priv->dev.d_mac.ether.ether_addr_octet[1],
        priv->dev.d_mac.ether.ether_addr_octet[2],
        priv->dev.d_mac.ether.ether_addr_octet[3],
        priv->dev.d_mac.ether.ether_addr_octet[4],
        priv->dev.d_mac.ether.ether_addr_octet[5]);

  return OK;
}
