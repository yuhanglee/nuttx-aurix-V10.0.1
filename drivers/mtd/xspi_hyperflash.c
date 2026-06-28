/****************************************************************************
 * drivers/mtd/xspi_hyperflash.c
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
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mtd/hyperflash.h>
#include <nuttx/mtd/xspi_hyperflash.h>
#include <nuttx/spi/xspi.h>

#define HF_WAIT_CYCLES_DEFAULT     17u
#define HF_WB_MAX_WORDS            256u
#define HF_STATUS_TIMEOUT_MS       5000u
#define HF_MTD_BLOCKSIZE           4096u
#define HF_READ_CHUNK_WORDS        8u

struct hf_chip_info_s
{
  uint16_t mfr_id;
  uint16_t iface;
  uint16_t density;
  uint32_t chip_size_bytes;
  uint32_t sector_size_bytes;
  uint32_t aso_overlay_word;
  const char *model;
};

struct hf_dev_s
{
  struct mtd_dev_s       mtd;
  FAR struct xspi_dev_s *xspi;
  struct hf_chip_info_s  info;
};

static int hf_xfer_read(FAR struct hf_dev_s *priv, uint32_t waddr,
                        FAR uint16_t *buf, size_t nwords);
static int hf_xfer_write_word(FAR struct hf_dev_s *priv, uint32_t waddr,
                              uint16_t data);

static int     hf_unlock_prefix(FAR struct hf_dev_s *priv);
static int     hf_soft_reset(FAR struct hf_dev_s *priv);
static int     hf_status_poll(FAR struct hf_dev_s *priv,
                              unsigned int timeout_ms,
                              FAR uint16_t *last);
static int     hf_sector_erase(FAR struct hf_dev_s *priv,
                               uint32_t sector_waddr);
static int     hf_program_buffer(FAR struct hf_dev_s *priv,
                                 uint32_t base_waddr,
                                 FAR const uint16_t *data,
                                 unsigned int nwords);
static int     hf_probe(FAR struct hf_dev_s *priv);

static int     hf_mtd_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks);
static ssize_t hf_mtd_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks, FAR uint8_t *buf);
static ssize_t hf_mtd_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                             size_t nblocks, FAR const uint8_t *buf);
static ssize_t hf_mtd_read(FAR struct mtd_dev_s *dev, off_t offset,
                           size_t nbytes, FAR uint8_t *buf);
static int     hf_mtd_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                            unsigned long arg);

static const struct hf_chip_info_s g_hf_chips[] =
{
  {
    .mfr_id            = HF_MFR_INFINEON,
    .iface             = 0x007Bu,
    .density           = 0x081Au,
    .chip_size_bytes   = 64u * 1024u * 1024u,
    .sector_size_bytes = 256u * 1024u,
    .aso_overlay_word  = 0x800u,
    .model             = "S26HS512",
  },
};

static int hf_xfer_read(FAR struct hf_dev_s *priv, uint32_t waddr,
                        FAR uint16_t *buf, size_t nwords)
{
  struct xspi_op_s op;
  size_t           done = 0;
  int              ret;

  while (done < nwords)
    {
      size_t chunk = nwords - done;

      if (chunk > HF_READ_CHUNK_WORDS)
        {
          chunk = HF_READ_CHUNK_WORDS;
        }

      memset(&op, 0, sizeof(op));
      op.proto      = XSPI_PROTO_HYPERBUS;
      op.io_mode    = XSPI_IO_MODE_OCTAL;
      op.data_rate  = XSPI_DATA_RATE_D_D_D;
      op.cs         = 0;
      op.dir        = XSPI_DIR_READ;
      op.addr_nbits = 48;
      op.addrval    = waddr + done;
      op.dummy      = HF_WAIT_CYCLES_DEFAULT;
      op.buf        = buf + done;
      op.len        = chunk * 2u;
      op.timeout_ms = 100;

      ret = XSPI_TRANSFER(priv->xspi, &op);
      if (ret < 0)
        {
          return ret;
        }

      done += chunk;
    }

  return 0;
}

static int hf_xfer_write_word(FAR struct hf_dev_s *priv, uint32_t waddr,
                              uint16_t data)
{
  struct xspi_op_s op;
  uint16_t         tx = data;

  memset(&op, 0, sizeof(op));
  op.proto      = XSPI_PROTO_HYPERBUS;
  op.io_mode    = XSPI_IO_MODE_OCTAL;
  op.data_rate  = XSPI_DATA_RATE_D_D_D;
  op.cs         = 0;
  op.dir        = XSPI_DIR_WRITE;
  op.addr_nbits = 48;
  op.addrval    = waddr;
  op.dummy      = 0;
  op.buf        = &tx;
  op.len        = 2;
  op.timeout_ms = 100;

  return XSPI_TRANSFER(priv->xspi, &op);
}

static int hf_unlock_prefix(FAR struct hf_dev_s *priv)
{
  int r1 = hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_UNLOCK_DATA1);
  int r2 = hf_xfer_write_word(priv, HF_UNLOCK_ADDR2, HF_UNLOCK_DATA2);
  return (r1 < 0) ? r1 : r2;
}

static int hf_soft_reset(FAR struct hf_dev_s *priv)
{
  return hf_xfer_write_word(priv, 0u, HF_CMD_RESET);
}

static int hf_status_poll(FAR struct hf_dev_s *priv,
                          unsigned int timeout_ms,
                          FAR uint16_t *last)
{
  uint16_t st = 0;
  unsigned int ms;
  int      ret;

  ret = hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_RDVSTR);
  if (ret < 0)
    {
      return ret;
    }

  for (ms = 0; ms < timeout_ms; ms++)
    {
      ret = hf_xfer_read(priv, 0u, &st, 1);
      if (ret == 0 && (st & HF_SR_DRB))
        {
          if (last)
            {
              *last = st;
            }

          (void)hf_soft_reset(priv);
          return 0;
        }

      up_mdelay(1);
    }

  if (last)
    {
      *last = st;
    }

  (void)hf_soft_reset(priv);
  return -ETIMEDOUT;
}

static void hf_prime(FAR struct hf_dev_s *priv)
{
  uint16_t dummy = 0;

  (void)hf_xfer_read(priv, HF_UNLOCK_ADDR1, &dummy, 1);
  (void)hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_UNLOCK_DATA1);
  (void)hf_xfer_read(priv, HF_UNLOCK_ADDR1, &dummy, 1);
  up_udelay(1000);
}

static int hf_sector_erase(FAR struct hf_dev_s *priv, uint32_t sector_waddr)
{
  int ret;

  hf_prime(priv);

  ret = hf_unlock_prefix(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_ERASE_SETUP);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_unlock_prefix(priv);
  if (ret < 0)
    {
      return ret;
    }

  return hf_xfer_write_word(priv, sector_waddr, HF_CMD_SECTOR_ERASE);
}

static int hf_program_buffer(FAR struct hf_dev_s *priv,
                             uint32_t base_waddr,
                             FAR const uint16_t *data,
                             unsigned int nwords)
{
  unsigned int i;
  int          ret;

  if (nwords == 0 || nwords > HF_WB_MAX_WORDS)
    {
      return -EINVAL;
    }

  ret = hf_unlock_prefix(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_xfer_write_word(priv, base_waddr, HF_CMD_WBPROG);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_xfer_write_word(priv, base_waddr, (uint16_t)(nwords - 1));
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < nwords; i++)
    {
      ret = hf_xfer_write_word(priv, base_waddr + i, data[i]);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = hf_xfer_write_word(priv, base_waddr, HF_CMD_WBCONFIRM);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(2);
  return OK;
}

static void hf_softreset(FAR struct hf_dev_s *priv)
{
  uint16_t dummy = 0;
  int i;

  for (i = 0; i < 16; i++)
    {
      (void)hf_xfer_write_word(priv, 0u,              HF_CMD_RESET);
      (void)hf_xfer_read(priv, HF_UNLOCK_ADDR1, &dummy, 1);
      (void)hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_RESET);
      (void)hf_xfer_read(priv, 0u,             &dummy, 1);
    }

  up_udelay(2000);
}

static int hf_probe(FAR struct hf_dev_s *priv)
{
  uint16_t mfr;
  uint16_t iface;
  uint16_t density;
  size_t   i;
  int      ret;

  ret = XSPI_LOCK(priv->xspi, true);
  if (ret < 0)
    {
      return ret;
    }

 /* the S26HS HyperFlash chip is NOT reset by the SoC
  * RESET pin pulse alone if it was left in a stale ASO/CFI/status-read
  * mode by a prior boot. Without this the very first multi-cycle command
  * after boot is silently absorbed and writes never persist.
  */

  hf_softreset(priv);

  for (i = 0; i < 10; i++)
    {
      hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_ASO_ENTRY);
      hf_xfer_read(priv, 0x800u, &mfr, 1);
    }

  hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_ASO_ENTRY);
  ret = hf_xfer_read(priv, 0x800u, &mfr, 1);
  if (ret < 0)
    {
      goto unlock;
    }

  hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_ASO_ENTRY);
  ret = hf_xfer_read(priv, 0x801u, &iface, 1);
  if (ret < 0)
    {
      goto unlock;
    }

  hf_xfer_write_word(priv, HF_UNLOCK_ADDR1, HF_CMD_ASO_ENTRY);
  ret = hf_xfer_read(priv, 0x802u, &density, 1);
  if (ret < 0)
    {
      goto unlock;
    }

  (void)hf_soft_reset(priv);

  for (i = 0; i < sizeof(g_hf_chips) / sizeof(g_hf_chips[0]); i++)
    {
      if (g_hf_chips[i].mfr_id == mfr)
        {
          priv->info = g_hf_chips[i];
          finfo("hyperflash: matched %s mfr=0x%04x iface=0x%04x dens=0x%04x\n",
                priv->info.model, mfr, iface, density);
          ret = OK;
          goto unlock;
        }
    }

  ferr("hyperflash: unknown mfr=0x%04x iface=0x%04x dens=0x%04x\n",
       mfr, iface, density);
  ret = -ENODEV;

unlock:
  XSPI_LOCK(priv->xspi, false);
  return ret;
}

static int hf_mtd_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                        size_t nblocks)
{
  FAR struct hf_dev_s *priv = (FAR struct hf_dev_s *)dev;
  size_t   i;
  uint32_t sector_words = priv->info.sector_size_bytes / 2u;
  int      ret;

  ret = XSPI_LOCK(priv->xspi, true);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < nblocks; i++)
    {
      uint32_t sector_waddr = (startblock + i) * sector_words;
      uint16_t st = 0;

      ret = hf_sector_erase(priv, sector_waddr);
      if (ret < 0)
        {
          break;
        }

      ret = hf_status_poll(priv, HF_STATUS_TIMEOUT_MS, &st);
      if (ret < 0)
        {
          ferr("hyperflash: erase block %ju failed st=0x%04x ret=%d\n",
               (uintmax_t)(startblock + i), st, ret);
          break;
        }
    }

  XSPI_LOCK(priv->xspi, false);
  return (ret < 0) ? ret : (int)nblocks;
}

static ssize_t hf_mtd_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks, FAR uint8_t *buf)
{
  FAR struct hf_dev_s *priv = (FAR struct hf_dev_s *)dev;
  uint32_t block_words  = HF_MTD_BLOCKSIZE / 2u;
  uint32_t base_waddr   = (uint32_t)startblock * block_words;
  size_t   total_words  = nblocks * block_words;
  int      ret;

  ret = XSPI_LOCK(priv->xspi, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_xfer_read(priv, base_waddr, (FAR uint16_t *)buf, total_words);

  XSPI_LOCK(priv->xspi, false);
  return (ret < 0) ? ret : (ssize_t)nblocks;
}

static ssize_t hf_mtd_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                             size_t nblocks, FAR const uint8_t *buf)
{
  FAR struct hf_dev_s *priv = (FAR struct hf_dev_s *)dev;
  uint32_t block_words  = HF_MTD_BLOCKSIZE / 2u;
  uint32_t base_waddr   = (uint32_t)startblock * block_words;
  size_t   total_words  = nblocks * block_words;
  size_t   off          = 0;
  int      ret;

  ret = XSPI_LOCK(priv->xspi, true);
  if (ret < 0)
    {
      return ret;
    }

  while (off < total_words)
    {
      size_t   chunk = total_words - off;
      uint16_t st    = 0;

      if (chunk > HF_WB_MAX_WORDS)
        {
          chunk = HF_WB_MAX_WORDS;
        }

      ret = hf_program_buffer(priv, base_waddr + off,
                              (FAR const uint16_t *)(buf + off * 2u),
                              chunk);
      if (ret < 0)
        {
          break;
        }

      up_mdelay(2);
      ret = hf_status_poll(priv, HF_STATUS_TIMEOUT_MS, &st);
      if (ret < 0)
        {
          ferr("hyperflash: program off=%zu failed st=0x%04x ret=%d\n",
               off, st, ret);
          break;
        }

      off += chunk;
    }

  XSPI_LOCK(priv->xspi, false);
  return (ret < 0) ? ret : (ssize_t)nblocks;
}

static ssize_t hf_mtd_read(FAR struct mtd_dev_s *dev, off_t offset,
                           size_t nbytes, FAR uint8_t *buf)
{
  FAR struct hf_dev_s *priv = (FAR struct hf_dev_s *)dev;
  uint32_t waddr;
  size_t   nwords;
  int      ret;

  if ((offset & 0x1u) != 0 || (nbytes & 0x1u) != 0)
    {
      return -EINVAL;
    }

  waddr  = (uint32_t)offset / 2u;
  nwords = nbytes / 2u;

  ret = XSPI_LOCK(priv->xspi, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = hf_xfer_read(priv, waddr, (FAR uint16_t *)buf, nwords);

  XSPI_LOCK(priv->xspi, false);
  return (ret < 0) ? ret : (ssize_t)nbytes;
}

static int hf_mtd_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                        unsigned long arg)
{
  FAR struct hf_dev_s *priv = (FAR struct hf_dev_s *)dev;
  int ret = -ENOTTY;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          FAR struct mtd_geometry_s *geo =
              (FAR struct mtd_geometry_s *)arg;
          if (geo == NULL)
            {
              ret = -EINVAL;
              break;
            }

          memset(geo, 0, sizeof(*geo));
          geo->blocksize    = HF_MTD_BLOCKSIZE;
          geo->erasesize    = priv->info.sector_size_bytes;
          geo->neraseblocks = priv->info.chip_size_bytes /
                              priv->info.sector_size_bytes;
          strncpy(geo->model, priv->info.model, NAME_MAX);
          ret = OK;
        }
        break;

      case MTDIOC_ERASESTATE:
        {
          FAR uint8_t *result = (FAR uint8_t *)arg;
          if (result == NULL)
            {
              ret = -EINVAL;
              break;
            }

          *result = 0xff;
          ret = OK;
        }
        break;

      default:
        break;
    }

  return ret;
}

FAR struct mtd_dev_s *
xspi_hyperflash_initialize(FAR struct xspi_dev_s *xspi)
{
  FAR struct hf_dev_s *priv;
  int ret;

  if (xspi == NULL)
    {
      return NULL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->xspi = xspi;

  priv->mtd.erase  = hf_mtd_erase;
  priv->mtd.bread  = hf_mtd_bread;
  priv->mtd.bwrite = hf_mtd_bwrite;
  priv->mtd.read   = hf_mtd_read;
  priv->mtd.ioctl  = hf_mtd_ioctl;
  priv->mtd.name   = "hyperflash";

  ret = hf_probe(priv);
  if (ret < 0)
    {
      kmm_free(priv);
      return NULL;
    }

  return &priv->mtd;
}
