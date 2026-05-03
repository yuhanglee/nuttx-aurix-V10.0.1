/****************************************************************************
 * drivers/xspi/xspi_dw_ssi.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/spi/xspi.h>
#include <nuttx/spi/xspi_dw_ssi.h>
#include <nuttx/mtd/hyperflash.h>

#include "xspi_dw_ssi_regs.h"


static int      dw_ssi_lock(FAR struct xspi_dev_s *dev, bool lock);
static int      dw_ssi_setopmode(FAR struct xspi_dev_s *dev, uint8_t opmode);
static int      dw_ssi_setmode(FAR struct xspi_dev_s *dev, uint8_t cpp_mode);
static uint32_t dw_ssi_setfrequency(FAR struct xspi_dev_s *dev, uint32_t hz);
static int      dw_ssi_transfer(FAR struct xspi_dev_s *dev,
                                FAR const struct xspi_op_s *op);
static FAR void *dw_ssi_alloc(FAR struct xspi_dev_s *dev, size_t len);
static void     dw_ssi_free(FAR struct xspi_dev_s *dev, FAR void *buf);


static const struct xspi_ops_s g_dw_ssi_ops =
{
  .lock         = dw_ssi_lock,
  .setopmode    = dw_ssi_setopmode,
  .setmode      = dw_ssi_setmode,
  .setfrequency = dw_ssi_setfrequency,
  .transfer     = dw_ssi_transfer,
  .alloc        = dw_ssi_alloc,
  .free         = dw_ssi_free,
};


static inline uint32_t dw_ssi_read(FAR struct xspi_dw_ssi_s *priv,
                                   unsigned int offset)
{
  return xspi_read(priv->cfg->base + offset);
}

static inline void dw_ssi_write(FAR struct xspi_dw_ssi_s *priv,
                                unsigned int offset, uint32_t val)
{
  xspi_write(val, priv->cfg->base + offset);
}

static inline void dw_ssi_modify(FAR struct xspi_dw_ssi_s *priv,
                                 unsigned int offset,
                                 uint32_t clrbits, uint32_t setbits)
{
  xspi_modreg(setbits, clrbits | setbits, priv->cfg->base + offset);
}

static int dw_ssi_lock(FAR struct xspi_dev_s *dev, bool lock)
{
  FAR struct xspi_dw_ssi_s *priv = (FAR struct xspi_dw_ssi_s *)dev;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static int dw_ssi_setopmode(FAR struct xspi_dev_s *dev, uint8_t opmode)
{
  UNUSED(dev);

  if (opmode != XSPI_OP_MODE_CONTROLLER)
    {
      return -ENOTSUP;
    }

  return OK;
}

static int dw_ssi_setmode(FAR struct xspi_dev_s *dev, uint8_t cpp_mode)
{
  FAR struct xspi_dw_ssi_s *priv = (FAR struct xspi_dw_ssi_s *)dev;

  if (cpp_mode > XSPI_CPP_MODE_3)
    {
      return -EINVAL;
    }

  priv->cpp_mode = cpp_mode;
  return OK;
}


static uint32_t dw_ssi_calc_sckdv(uint32_t ssi_clk_hz, uint32_t hz)
{
  uint32_t sckdv;

  if (hz == 0)
    {
      return 0;
    }

  sckdv = (ssi_clk_hz + hz - 1) / hz;
  sckdv = (sckdv + 1) & ~1u;

  if (sckdv < 2)
    {
      sckdv = 2;
    }
  else if (sckdv > 65534)
    {
      sckdv = 65534;
    }

  return sckdv;
}

static uint32_t dw_ssi_setfrequency(FAR struct xspi_dev_s *dev, uint32_t hz)
{
  FAR struct xspi_dw_ssi_s *priv = (FAR struct xspi_dw_ssi_s *)dev;
  uint32_t sckdv;
  uint32_t actual;

  sckdv = dw_ssi_calc_sckdv(priv->cfg->ssi_clk_hz, hz);

  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_BAUDR_OFFSET, sckdv);

  actual = (sckdv == 0) ? 0 : priv->cfg->ssi_clk_hz / sckdv;
  priv->frequency = actual;
  return actual;
}


static int dw_ssi_io_mode_map(uint8_t io_mode,
                              FAR uint32_t *spi_frf,
                              FAR uint32_t *trans_type)
{
  switch (io_mode)
    {
      case XSPI_IO_MODE_SINGLE:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_STD;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT0;
        return OK;

      case XSPI_IO_MODE_DUAL_1_1_2:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_DUAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT0;
        return OK;

      case XSPI_IO_MODE_DUAL_1_2_2:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_DUAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT1;
        return OK;

      case XSPI_IO_MODE_DUAL:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_DUAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT2;
        return OK;

      case XSPI_IO_MODE_QUAD_1_1_4:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_QUAD;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT0;
        return OK;

      case XSPI_IO_MODE_QUAD_1_4_4:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_QUAD;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT1;
        return OK;

      case XSPI_IO_MODE_QUAD:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_QUAD;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT2;
        return OK;

      case XSPI_IO_MODE_OCTAL_1_1_8:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_OCTAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT0;
        return OK;

      case XSPI_IO_MODE_OCTAL_1_8_8:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_OCTAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT1;
        return OK;

      case XSPI_IO_MODE_OCTAL:
        *spi_frf    = XSPI_DW_SSI_CTRLR0_SPI_FRF_OCTAL;
        *trans_type = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT2;
        return OK;

      default:
        return -ENOTSUP;
    }
}

/* Map cmd_nbits to SPI_CTRLR0.INST_L encoding */

static int dw_ssi_inst_l_encode(uint8_t nbits, FAR uint32_t *encoded)
{
  switch (nbits)
    {
      case 0:  *encoded = XSPI_DW_SSI_SPI_CTRLR0_INST_L_0;  return OK;
      case 4:  *encoded = XSPI_DW_SSI_SPI_CTRLR0_INST_L_4;  return OK;
      case 8:  *encoded = XSPI_DW_SSI_SPI_CTRLR0_INST_L_8;  return OK;
      case 16: *encoded = XSPI_DW_SSI_SPI_CTRLR0_INST_L_16; return OK;
      default: return -EINVAL;
    }
}

/* Validate op for v1: SDR only, no HyperBus, CS0 only, no DQS, no HEX mode. */

static int dw_ssi_validate_op(FAR const struct xspi_op_s *op)
{
  if (op == NULL)
    {
      return -EINVAL;
    }

  if (op->proto == XSPI_PROTO_HYPERBUS)
    {
      if (op->io_mode != XSPI_IO_MODE_OCTAL ||
          op->data_rate != XSPI_DATA_RATE_D_D_D ||
          op->cs != 0 ||
          op->cmd_nbits != 0 ||
          op->addr_nbits != 48 ||
          (op->len & 0x1u) != 0 ||
          op->dir == XSPI_DIR_NONE)
        {
          return -EINVAL;
        }

      return OK;
    }

  if (op->proto != XSPI_PROTO_STD ||
      op->data_rate != XSPI_DATA_RATE_S_S_S)
    {
      return -ENOTSUP;
    }

  if (op->cs != 0 || op->dqs != 0)
    {
      return -ENOTSUP;
    }

  if (op->io_mode == XSPI_IO_MODE_HEX ||
      op->io_mode == XSPI_IO_MODE_HEX_8_8_16 ||
      op->io_mode == XSPI_IO_MODE_HEX_8_16_16)
    {
      return -ENOTSUP;
    }

  if (op->cmd_nbits != 0 && op->cmd_nbits != 4 &&
      op->cmd_nbits != 8 && op->cmd_nbits != 16)
    {
      return -EINVAL;
    }

  if (op->addr_nbits > 60)
    {
      return -EINVAL;
    }

  if (op->dummy > 31)
    {
      return -EINVAL;
    }

  if (op->len > 65536)
    {
      return -E2BIG;
    }

  if ((op->dir == XSPI_DIR_READ || op->dir == XSPI_DIR_WRITE) &&
      (op->buf == NULL || op->len == 0))
    {
      return -EINVAL;
    }

  if (op->dir == XSPI_DIR_NONE && (op->buf != NULL || op->len != 0))
    {
      return -EINVAL;
    }

  return OK;
}


static int dw_ssi_poll(FAR struct xspi_dw_ssi_s *priv, unsigned int offset,
                       uint32_t mask, uint32_t want, clock_t deadline)
{
  unsigned int spin = 0;

  while ((dw_ssi_read(priv, offset) & mask) != want)
    {
      if ((sclock_t)(clock_systime_ticks() - deadline) >= 0)
        {
          return -ETIMEDOUT;
        }

      if (++spin >= 256)
        {
          spin = 0;
          sched_yield();
        }
    }

  return OK;
}


static int dw_ssi_tx_drain(FAR struct xspi_dw_ssi_s *priv,
                           FAR const uint8_t *buf, size_t len,
                           clock_t deadline)
{
  size_t pushed = 0;
  int ret;

  while (pushed < len)
    {
      ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                        XSPI_DW_SSI_SR_TFNF, XSPI_DW_SSI_SR_TFNF, deadline);
      if (ret < 0)
        {
          return ret;
        }

      dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), buf[pushed]);
      pushed++;
    }

  return (int)pushed;
}


static int dw_ssi_rx_drain(FAR struct xspi_dw_ssi_s *priv,
                           FAR uint8_t *buf, size_t len,
                           clock_t deadline)
{
  size_t got = 0;
  int ret;

  while (got < len)
    {
      ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                        XSPI_DW_SSI_SR_RFNE, XSPI_DW_SSI_SR_RFNE, deadline);
      if (ret < 0)
        {
          return ret;
        }

      buf[got] = (uint8_t)dw_ssi_read(priv, XSPI_DW_SSI_DR_OFFSET(0));
      got++;
    }

  return (int)got;
}

static int dw_ssi_transfer_hb(FAR struct xspi_dw_ssi_s *priv,
                              FAR const struct xspi_op_s *op)
{
  uint32_t  waddr  = op->addrval;
  uint8_t   rw     = (op->dir == XSPI_DIR_READ) ? HF_RW_READ : HF_RW_WRITE;
  uint16_t  ca_hi  = HF_CA47_32(rw, waddr);
  uint16_t  ca_md  = HF_CA31_16(waddr);
  uint16_t  ca_lo  = HF_CA15_0(waddr);
  uint32_t  dr1    = ((uint32_t)ca_hi << 16) | (uint32_t)ca_md;
  uint32_t  dr2    = (uint32_t)ca_lo;
  size_t    nwords = op->len / 2u;
  uint32_t  timeout_ms;
  clock_t   deadline;
  uint32_t  ctrlr0;
  uint32_t  ctrlr1;
  uint32_t  spi_ctrlr0;
  int       ret;
  size_t    i;

  if (nwords == 0)
    {
      return -EINVAL;
    }

  timeout_ms = op->timeout_ms ?
               op->timeout_ms : CONFIG_XSPI_DEFAULT_TIMEOUT_MS;
  deadline = clock_systime_ticks() + MSEC2TICK(timeout_ms);

  ctrlr0 = XSPI_DW_SSI_CTRLR0_FRF_SPI |
           XSPI_DW_SSI_CTRLR0_SPI_FRF_OCTAL |
           XSPI_DW_SSI_CTRLR0_SPI_HYPERBUS_EN |
           XSPI_DW_SSI_CTRLR0_DFS(16) |
           ((op->dir == XSPI_DIR_READ) ?
            XSPI_DW_SSI_CTRLR0_TMOD_RX : XSPI_DW_SSI_CTRLR0_TMOD_TX);

  if (priv->cpp_mode & 0x1)
    {
      ctrlr0 |= XSPI_DW_SSI_CTRLR0_SCPH;
    }

  if (priv->cpp_mode & 0x2)
    {
      ctrlr0 |= XSPI_DW_SSI_CTRLR0_SCPOL;
    }

  ctrlr1 = (op->dir == XSPI_DIR_READ) ? (uint32_t)(nwords - 1) : 0u;

  spi_ctrlr0 = XSPI_DW_SSI_SPI_CTRLR0_TRANS_TYPE_TT2 |
               XSPI_DW_SSI_SPI_CTRLR0_ADDR_L(48) |
               XSPI_DW_SSI_SPI_CTRLR0_INST_L_0 |
               (((uint32_t)op->dummy & 0x1fu) <<
                XSPI_DW_SSI_SPI_CTRLR0_WAIT_CYCLES_SHIFT) |
               XSPI_DW_SSI_SPI_CTRLR0_SPI_DDR_EN |
               XSPI_DW_SSI_SPI_CTRLR0_SPI_RXDS_EN |
               XSPI_DW_SSI_SPI_CTRLR0_CLK_STRETCH_EN;

  if (op->dir == XSPI_DIR_WRITE)
    {
      spi_ctrlr0 |= XSPI_DW_SSI_SPI_CTRLR0_SPI_RXDS_SIG_EN;
    }

  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);

  while (dw_ssi_read(priv, XSPI_DW_SSI_SR_OFFSET) & XSPI_DW_SSI_SR_RFNE)
    {
      (void)dw_ssi_read(priv, XSPI_DW_SSI_DR_OFFSET(0));
    }

  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR0_OFFSET, ctrlr0);
  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR1_OFFSET, ctrlr1);
  dw_ssi_write(priv, XSPI_DW_SSI_SPI_CTRLR0_OFFSET, spi_ctrlr0);
  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, XSPI_DW_SSI_SSIENR_SSIC_EN);

  dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), dr1);
  dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), dr2);

  if (op->dir == XSPI_DIR_WRITE)
    {
      FAR const uint16_t *src = (FAR const uint16_t *)op->buf;
      for (i = 0; i < nwords; i++)
        {
          ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                            XSPI_DW_SSI_SR_TFNF, XSPI_DW_SSI_SR_TFNF,
                            deadline);
          if (ret < 0)
            {
              goto out;
            }

          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), src[i]);
        }
    }

  dw_ssi_write(priv, XSPI_DW_SSI_SER_OFFSET, XSPI_DW_SSI_SER_CS0);

  if (op->dir == XSPI_DIR_READ)
    {
      FAR uint16_t *dst = (FAR uint16_t *)op->buf;
      for (i = 0; i < nwords; i++)
        {
          ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                            XSPI_DW_SSI_SR_RFNE, XSPI_DW_SSI_SR_RFNE,
                            deadline);
          if (ret < 0)
            {
              goto out;
            }

          dst[i] = (uint16_t)dw_ssi_read(priv, XSPI_DW_SSI_DR_OFFSET(0));
        }
    }

  ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                    XSPI_DW_SSI_SR_BUSY, 0, deadline);

out:
  dw_ssi_write(priv, XSPI_DW_SSI_SER_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);
  return (ret < 0) ? ret : OK;
}

static int dw_ssi_transfer(FAR struct xspi_dev_s *dev,
                           FAR const struct xspi_op_s *op)
{
  FAR struct xspi_dw_ssi_s *priv = (FAR struct xspi_dw_ssi_s *)dev;
  uint32_t ctrlr0;
  uint32_t ctrlr1;
  uint32_t spi_ctrlr0;
  uint32_t spi_frf;
  uint32_t trans_type;
  uint32_t inst_l;
  uint32_t tmod;
  uint32_t dfs_bits;
  uint32_t timeout_ms;
  clock_t deadline;
  int ret;

  ret = dw_ssi_validate_op(op);
  if (ret < 0)
    {
      return ret;
    }

  if (op->proto == XSPI_PROTO_HYPERBUS)
    {
      return dw_ssi_transfer_hb(priv, op);
    }

  ret = dw_ssi_io_mode_map(op->io_mode, &spi_frf, &trans_type);
  if (ret < 0)
    {
      return ret;
    }

  ret = dw_ssi_inst_l_encode(op->cmd_nbits, &inst_l);
  if (ret < 0)
    {
      return ret;
    }


  if (op->dir == XSPI_DIR_NONE && op->addr_nbits == 0)
    {
      tmod     = XSPI_DW_SSI_CTRLR0_TMOD_TX;
      dfs_bits = (op->cmd_nbits == 0) ? 8 : op->cmd_nbits;
      ctrlr1   = 0;
    }
  else if (op->dir == XSPI_DIR_READ)
    {
      /* STD SPI uses TMOD=EEPROM: TX FIFO drives cmd/addr, then the
       * controller auto-receives CTRLR1.NDF+1 bytes -- canonical IP
       * pattern. Enhanced SPI (Dual/Quad/Octal) uses TMOD=RX, the
       * cmd/addr/dummy come from SPI_CTRLR0.
       */

      tmod     = (op->io_mode == XSPI_IO_MODE_SINGLE) ?
                 XSPI_DW_SSI_CTRLR0_TMOD_EEPROM :
                 XSPI_DW_SSI_CTRLR0_TMOD_RX;
      dfs_bits = 8;
      ctrlr1   = ((op->len - 1) & XSPI_DW_SSI_CTRLR1_NDF_MASK);
    }
  else
    {
      tmod     = XSPI_DW_SSI_CTRLR0_TMOD_TX;
      dfs_bits = 8;
      ctrlr1   = 0;
    }

  timeout_ms = op->timeout_ms ?
               op->timeout_ms : CONFIG_XSPI_DEFAULT_TIMEOUT_MS;
  deadline = clock_systime_ticks() + MSEC2TICK(timeout_ms);

  /* Assemble CTRLR0 */

  ctrlr0 = XSPI_DW_SSI_CTRLR0_FRF_SPI |
           tmod |
           spi_frf |
           XSPI_DW_SSI_CTRLR0_DFS(dfs_bits);

  if (priv->cpp_mode & 0x1)
    {
      ctrlr0 |= XSPI_DW_SSI_CTRLR0_SCPH;
    }

  if (priv->cpp_mode & 0x2)
    {
      ctrlr0 |= XSPI_DW_SSI_CTRLR0_SCPOL;
    }


  spi_ctrlr0 = trans_type |
               ((uint32_t)op->dummy <<
                XSPI_DW_SSI_SPI_CTRLR0_WAIT_CYCLES_SHIFT) |
               XSPI_DW_SSI_SPI_CTRLR0_CLK_STRETCH_EN;

  if (op->io_mode != XSPI_IO_MODE_SINGLE)
    {
      /* Enhanced SPI: cmd/addr come from SPI_CTRLR0 phases. */

      spi_ctrlr0 |= XSPI_DW_SSI_SPI_CTRLR0_ADDR_L(op->addr_nbits) |
                    inst_l;
    }
  else
    {
      /* Standard SPI PIO: cmd/addr/dummy travel through TX FIFO.
       * Some DWC_SSI variants still process INST_L/ADDR_L in std mode
       * and inject extra bytes -- mask both off.
       */

      UNUSED(inst_l);
    }

  /* Program the controller */

  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR0_OFFSET, ctrlr0);
  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR1_OFFSET, ctrlr1);
  dw_ssi_write(priv, XSPI_DW_SSI_SPI_CTRLR0_OFFSET, spi_ctrlr0);
  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, XSPI_DW_SSI_SSIENR_SSIC_EN);
  dw_ssi_write(priv, XSPI_DW_SSI_SER_OFFSET, XSPI_DW_SSI_SER_CS0);

  if (op->addr_nbits > 32)
    {
      ret = -ENOTSUP;
      goto out;
    }

  /* Data phase */

  if (op->dir == XSPI_DIR_READ && op->io_mode == XSPI_IO_MODE_SINGLE)
    {
      /* TMOD=EEPROM: push cmd+addr into TX FIFO; controller drains
       * TX, then auto-receives NDF+1 bytes per CTRLR1.
       */

      size_t cmd_bytes  = (op->cmd_nbits == 8) ? 1 :
                          (op->cmd_nbits == 16) ? 2 : 0;
      size_t addr_bytes = (op->addr_nbits + 7) / 8;
      size_t i;

      for (i = 0; i < cmd_bytes; i++)
        {
          uint32_t tx = (op->cmdval >> ((cmd_bytes - 1 - i) * 8)) & 0xff;
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), tx);
        }

      for (i = 0; i < addr_bytes; i++)
        {
          uint32_t tx = (op->addrval >>
                         ((addr_bytes - 1 - i) * 8)) & 0xff;
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), tx);
        }

      ret = dw_ssi_rx_drain(priv, (FAR uint8_t *)op->buf,
                            op->len, deadline);
      if (ret < 0)
        {
          goto out;
        }
    }
  else if (op->dir == XSPI_DIR_WRITE)
    {
      if (op->cmd_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->cmdval);
        }

      if (op->addr_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->addrval);
        }

      ret = dw_ssi_tx_drain(priv, (FAR const uint8_t *)op->buf,
                            op->len, deadline);
      if (ret < 0)
        {
          goto out;
        }

      ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                        XSPI_DW_SSI_SR_TFE, XSPI_DW_SSI_SR_TFE, deadline);
      if (ret < 0)
        {
          goto out;
        }
    }
  else if (op->dir == XSPI_DIR_READ)
    {
      /* Enhanced SPI (Dual/Quad/Octal): cmd/addr/dummy come from
       * SPI_CTRLR0; the controller is RX-only for the data phase.
       */

      if (op->cmd_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->cmdval);
        }

      if (op->addr_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->addrval);
        }

      ret = dw_ssi_rx_drain(priv, (FAR uint8_t *)op->buf,
                            op->len, deadline);
      if (ret < 0)
        {
          goto out;
        }
    }
  else
    {
      /* Cmd / addr only: push them and wait for TX FIFO drain. */

      if (op->cmd_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->cmdval);
        }

      if (op->addr_nbits != 0)
        {
          dw_ssi_write(priv, XSPI_DW_SSI_DR_OFFSET(0), op->addrval);
        }

      ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                        XSPI_DW_SSI_SR_TFE, XSPI_DW_SSI_SR_TFE, deadline);
      if (ret < 0)
        {
          goto out;
        }
    }

  /* Wait for the controller to finish shifting */

  ret = dw_ssi_poll(priv, XSPI_DW_SSI_SR_OFFSET,
                    XSPI_DW_SSI_SR_BUSY, 0, deadline);

out:
  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_SER_OFFSET, 0);
  return (ret < 0) ? ret : OK;
}

static FAR void *dw_ssi_alloc(FAR struct xspi_dev_s *dev, size_t len)
{
  UNUSED(dev);
  return kmm_malloc(len);
}

static void dw_ssi_free(FAR struct xspi_dev_s *dev, FAR void *buf)
{
  UNUSED(dev);
  kmm_free(buf);
}


static void dw_ssi_hw_reset(FAR struct xspi_dw_ssi_s *priv)
{
  dw_ssi_write(priv, XSPI_DW_SSI_SSIENR_OFFSET, 0);

  dw_ssi_write(priv, XSPI_DW_SSI_IMR_OFFSET, 0);

  (void)dw_ssi_read(priv, XSPI_DW_SSI_ICR_OFFSET);
  (void)dw_ssi_read(priv, XSPI_DW_SSI_TXEICR_OFFSET);
  (void)dw_ssi_read(priv, XSPI_DW_SSI_RXOICR_OFFSET);
  (void)dw_ssi_read(priv, XSPI_DW_SSI_RXUICR_OFFSET);
  (void)dw_ssi_read(priv, XSPI_DW_SSI_MSTICR_OFFSET);

  dw_ssi_write(priv, XSPI_DW_SSI_SER_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR0_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR1_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_SPI_CTRLR0_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_BAUDR_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_TXFTLR_OFFSET, 0);
  dw_ssi_write(priv, XSPI_DW_SSI_RXFTLR_OFFSET, 0);

  dw_ssi_write(priv, XSPI_DW_SSI_CTRLR2_OFFSET,
               priv->cfg->use_gpio_pads ?
               XSPI_DW_SSI_CTRLR2_PORTSEL_GPIO :
               XSPI_DW_SSI_CTRLR2_PORTSEL_P16);
}


FAR struct xspi_dev_s *
xspi_dw_ssi_initialize(FAR struct xspi_dw_ssi_s *priv,
                       FAR const struct xspi_dw_ssi_config_s *cfg)
{
  if (priv == NULL || cfg == NULL || cfg->base == 0 ||
      cfg->ssi_clk_hz == 0)
    {
      return NULL;
    }

  if (priv->initialized)
    {
      return &priv->dev;
    }

  priv->dev.ops     = &g_dw_ssi_ops;
  priv->cfg         = cfg;
  priv->frequency   = 0;
  priv->cpp_mode    = XSPI_CPP_MODE_0;

  nxmutex_init(&priv->lock);

  dw_ssi_hw_reset(priv);

  priv->initialized = true;

  return &priv->dev;
}
