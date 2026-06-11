/****************************************************************************
 * drivers/net/dwxgmac.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 *
 ****************************************************************************/

#ifndef ETHERNET_DWXGMAC_H
#define ETHERNET_DWXGMAC_H

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/net/netdev.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/bits.h>

struct eth_dwmac_dma_desc
{
  uint32_t des0;
  uint32_t des1;
  uint32_t des2;
  uint32_t des3;
};

/****************************************************************************
 * TX descriptor (TDES) helpers shared by the XGMAC and GMAC-QoS variants.
 *
 * The read/ring TDES layout is identical for both IPs.
 ****************************************************************************/

struct eth_xgmac_tdes_rd
{
  struct
  { 
    uint32_t buf1ap;
  } tdes0;
  struct
  { 
    uint32_t buf2ap;
  } tdes1;
  struct
  {
    uint32_t b1l  : 14;
    uint32_t vtir : 2;
    uint32_t b2l  : 14;
    uint32_t ttse : 1;
    uint32_t ioc  : 1;
  } tdes2;
  struct
  {
    uint32_t fl      : 15;
    uint32_t tpl     : 1;
    uint32_t cic     : 2;
    uint32_t tse     : 1;
    uint32_t slotnum : 4;
    uint32_t saic    : 3;
    uint32_t cpc     : 2;
    uint32_t ld      : 1;
    uint32_t fd      : 1;
    uint32_t ctxt    : 1;
    uint32_t own     : 1;
  } tdes3;
};

#ifdef CONFIG_DWXGMAC_QOS
#  define DWMAC_TDES3_DERR  BIT(23)
#else
#  define DWMAC_TDES3_DERR  BIT(27)
#endif

static inline void eth_dwmac_tdes_rd_set_buffer(struct eth_xgmac_tdes_rd *t,
                                                uint8_t *buf, uint16_t len)
{
  t->tdes0.buf1ap = (uint32_t)(uintptr_t)buf;
#if UINTPTR_MAX == UINT64_MAX
  t->tdes1.buf2ap = (uint32_t)((uintptr_t)buf >> 32u);
#else
  t->tdes1.buf2ap = 0;
#endif
  t->tdes2.b1l = len;
  t->tdes2.b2l = 0;
}

static inline void eth_dwmac_tdes_rd_set(struct eth_xgmac_tdes_rd *t,
                                         bool fd, bool ld, uint16_t len,
                                         bool txcoe, bool ts)
{
  t->tdes2.ioc  = ld ? 1 : 0;
  t->tdes2.ttse = ts ? 1 : 0;

  uint32_t d3 = 0;
  d3 |= (len & 0x7fffu) | BIT(31);
  if (txcoe) d3 |= (3u << 16);
  if (ld)    d3 |= BIT(28);
  if (fd)    d3 |= BIT(29);

  *(volatile uint32_t *)&t->tdes3 = d3;
}

static inline bool eth_dwmac_tdes_own(void *d)
{
  return ((struct eth_xgmac_tdes_rd *)d)->tdes3.own;
}

static inline bool eth_dwmac_tdes_last(void *d)
{
  return ((struct eth_xgmac_tdes_rd *)d)->tdes3.ld;
}

static inline bool eth_dwmac_tdes_error(void *d)
{
  return (*((volatile uint32_t *)d + 3) & DWMAC_TDES3_DERR) != 0;
}

struct eth_dwmac_dma_tx_ch_data
{
  sem_t desc_used;
  uint16_t head;
  uint16_t tail;
  mutex_t lock;
};

struct eth_dwmac_dma_rx_ch_data
{
  sem_t desc_used;
  uint16_t head;
  uint16_t tail;
  uint8_t *data[CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS];
};

struct eth_dwmac_dma_ch_config
{
  struct eth_dwmac_dma_desc *descs;
  uint16_t descs_count;
  uint8_t nr;
  bool osf;
};

struct eth_dwmac_rx_queue
{
  uint16_t size;
  uint8_t nr;
  uint8_t priority;
  uint8_t dma_channel;
  uint8_t threshold;
  bool sf;
  bool dynamic_dma_channel;
  bool av_queue;
  bool route_multi_broad;
};

struct eth_dwmac_tx_queue
{
  uint16_t size;
  uint8_t nr;
  uint8_t priority;
  uint8_t threshold;
  bool sf;
};

struct eth_dwmac_config
{
  bool is_xgmac;
  bool clocks_stop;
  bool slave_mode;
  bool da_duplication;
  bool loopback;
  bool drop_tx_status;

  uint8_t dma_rx_channel;
  uint8_t dma_tx_channel;
  uint8_t mtl_rx_queues;
  uint8_t mtl_tx_queues;

  struct eth_dwmac_dma_ch_config *dma_rx;
  struct eth_dwmac_dma_ch_config *dma_tx;
  struct eth_dwmac_rx_queue *mtl_rx;
  struct eth_dwmac_tx_queue *mtl_tx;
};

struct eth_dwmac_data
{
  struct eth_dwmac_dma_rx_ch_data *dma_rx;
  struct eth_dwmac_dma_tx_ch_data *dma_tx;

  uint8_t mac_addr[6];
  bool started;
  bool txcoe_available;
  bool rxcoe_available;
};

struct eth_xgmac_config
{
  const struct eth_dwmac_config dwmac;
  uint8_t blen;
  bool aal;
  bool ubl;
};

struct eth_xgmac_data
{
  struct eth_dwmac_data dwmac;
};

struct xgmac_driver_s
{
  struct net_driver_s dev;
  uintptr_t base;
  int irq;

  struct eth_xgmac_config *config;
  struct eth_xgmac_data *data;

  struct work_s rxwork;
  struct work_s pollwork;

  bool ifup;
  bool txcoe;
};

#endif /* ETHERNET_DWXGMAC_H */
