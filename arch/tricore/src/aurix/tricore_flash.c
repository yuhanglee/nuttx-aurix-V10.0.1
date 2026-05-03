/****************************************************************************
 * arch/tricore/src/aurix/tricore_flash.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/mtd/mtd.h>

#include <arch/barriers.h>

#include "tricore_internal.h"
#include "hardware/tricore_dmu.h"

#ifdef CONFIG_TRICORE_FLASH_MTD

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3XX)
extern void aurix_safety_endinit_enable(bool enable);
#  define DMU_ENDINIT_UNLOCK()  aurix_safety_endinit_enable(false)
#  define DMU_ENDINIT_LOCK()    aurix_safety_endinit_enable(true)
#else
#  define DMU_ENDINIT_UNLOCK()  do { } while (0)
#  define DMU_ENDINIT_LOCK()    do { } while (0)
#endif


#ifndef CONFIG_TRICORE_DFLASH_BASE
#  define CONFIG_TRICORE_DFLASH_BASE   DMU_DFLASH_DEFAULT_BASE
#endif

#ifndef CONFIG_TRICORE_DFLASH_SIZE
   /* Smallest DFLASH(0) variant.  Override per board if larger. */
#  define CONFIG_TRICORE_DFLASH_SIZE   (128u * 1024u)
#endif

#define TRICORE_DFLASH_BASE             ((uintptr_t)CONFIG_TRICORE_DFLASH_BASE)
#define TRICORE_DFLASH_SIZE             ((size_t)CONFIG_TRICORE_DFLASH_SIZE)

#define TRICORE_DFLASH_NSECTORS \
  (TRICORE_DFLASH_SIZE / TRICORE_DFLASH_SECTOR_SIZE)

#define TRICORE_DFLASH_PAGES_PER_SECTOR \
  (TRICORE_DFLASH_SECTOR_SIZE / TRICORE_DFLASH_PAGE_SIZE)

#define TRICORE_DFLASH_NPAGES \
  (TRICORE_DFLASH_SIZE / TRICORE_DFLASH_PAGE_SIZE)


#define TRICORE_FLASH_POLL_LIMIT        (10u * 1000u * 1000u)


struct tricore_flash_dev_s
{
  struct mtd_dev_s mtd;       /* MTD interface (must be first) */
  mutex_t          lock;      /* Serialises DMU command sequences */
};


static uint32_t g_last_dmu_err    = 0u;
static uint32_t g_last_dmu_status = 0u;
static uint32_t g_last_dmu_phase  = 0u;


static int  tricore_flash_erase(struct mtd_dev_s *dev, off_t startblock,
                                size_t nblocks);
static ssize_t tricore_flash_bread(struct mtd_dev_s *dev, off_t startblock,
                                   size_t nblocks, uint8_t *buf);
static ssize_t tricore_flash_bwrite(struct mtd_dev_s *dev, off_t startblock,
                                    size_t nblocks, const uint8_t *buf);
static ssize_t tricore_flash_read(struct mtd_dev_s *dev, off_t offset,
                                  size_t nbytes, uint8_t *buf);
static int  tricore_flash_ioctl(struct mtd_dev_s *dev, int cmd,
                                unsigned long arg);


static struct tricore_flash_dev_s g_tricore_flash =
{
  .mtd =
  {
    .erase  = tricore_flash_erase,
    .bread  = tricore_flash_bread,
    .bwrite = tricore_flash_bwrite,
    .read   = tricore_flash_read,
    .ioctl  = tricore_flash_ioctl,
    .name   = "tc-dflash",
  },
  .lock     = NXMUTEX_INITIALIZER,
};



static inline void csi_write(uint16_t cycle_addr, uint32_t data)
{
  putreg32(data, DMU_CMD_BASE + cycle_addr);
}


static inline void csi_reset_to_read(void)
{
  csi_write(DMU_CSI_RST, DMU_CMD_RESET_TO_READ);
}


static inline void dmu_clear_errors(void)
{
  putreg32(DMU_CLRE_ALL, DMU_CLRE_REG);
}


static int dmu_wait_done(void)
{
  uint32_t status;
  uint32_t err;
  unsigned int retries = TRICORE_FLASH_POLL_LIMIT;

  do
    {
      status = getreg32(DMU_STATUS_REG);

      if ((status & DMU_BUSY_DF_MASK) == 0u)
        {

          err = getreg32(DMU_ERR_REG);
          if ((err & DMU_ERR_FATAL_MASK) != 0u)
            {
              g_last_dmu_err = err;
              g_last_dmu_status = status;
              g_last_dmu_phase = 1u;  /* error found after busy clear */
              dmu_clear_errors();
              csi_reset_to_read();
              return -EIO;
            }

          return OK;
        }

      err = getreg32(DMU_ERR_REG);
      if ((err & DMU_ERR_FATAL_MASK) != 0u)
        {
          g_last_dmu_err = err;
          g_last_dmu_status = status;
          g_last_dmu_phase = 2u;  /* error found mid-busy */
          dmu_clear_errors();
          csi_reset_to_read();
          return -EIO;
        }
    }
  while (--retries != 0u);

  g_last_dmu_err = getreg32(DMU_ERR_REG);
  g_last_dmu_status = status;
  g_last_dmu_phase = 3u;  /* timeout */
  csi_reset_to_read();
  return -ETIMEDOUT;
}


void tricore_flash_get_last_dmu_state(uint32_t *err, uint32_t *status,
                                      uint32_t *phase)
{
  if (err    != NULL) *err    = g_last_dmu_err;
  if (status != NULL) *status = g_last_dmu_status;
  if (phase  != NULL) *phase  = g_last_dmu_phase;
}


static inline uint32_t tricore_dflash_uncached_addr(uint32_t byte_offset)
{
  return (uint32_t)TRICORE_DFLASH_BASE + byte_offset;
}


static int tricore_dflash_erase_sectors(uint32_t sector_index,
                                        uint32_t count)
{
  uint32_t sa;
  int      ret;

  if (count == 0u)
    {
      return OK;
    }

  if (sector_index + count > TRICORE_DFLASH_NSECTORS)
    {
      return -EINVAL;
    }

  sa = tricore_dflash_uncached_addr(sector_index *
                                    TRICORE_DFLASH_SECTOR_SIZE);

  DMU_ENDINIT_UNLOCK();

  {
    irqstate_t        flags    = enter_critical_section();
    volatile uint32_t *cmd5554 = (volatile uint32_t *)(DMU_CMD_BASE | 0x5554u);
    volatile uint32_t *addr1   = (volatile uint32_t *)(DMU_CMD_BASE | 0xAA50u);
    volatile uint32_t *addr2   = (volatile uint32_t *)(DMU_CMD_BASE | 0xAA58u);
    volatile uint32_t *addr3   = (volatile uint32_t *)(DMU_CMD_BASE | 0xAAA8u);
    volatile uint32_t *addr4   = (volatile uint32_t *)(DMU_CMD_BASE | 0xAAA8u);

    *cmd5554 = 0xFAu;            /* Clear Status            */
    *addr1   = sa;               /* Erase cycle 1: SA       */
    *addr2   = count & 0xFFu;    /* Erase cycle 2: count    */
    *addr3   = 0x80u;            /* Erase cycle 3: 0x80     */
    *addr4   = 0x50u;            /* Erase cycle 4: 0x50     */
    UP_DSB();

    leave_critical_section(flags);
  }

  DMU_ENDINIT_LOCK();

  ret = dmu_wait_done();
  return ret;
}


static int tricore_dflash_write_page(uint32_t page_offset,
                                     const uint8_t *page)
{
  uint32_t pa;
  uint32_t w0;
  uint32_t w1;
  int      ret;

  if ((page_offset & (TRICORE_DFLASH_PAGE_SIZE - 1u)) != 0u)
    {
      return -EINVAL;
    }

  if (page_offset >= TRICORE_DFLASH_SIZE)
    {
      return -EINVAL;
    }

  pa = tricore_dflash_uncached_addr(page_offset);


  w0 = (uint32_t)page[0]
     | ((uint32_t)page[1] << 8)
     | ((uint32_t)page[2] << 16)
     | ((uint32_t)page[3] << 24);
  w1 = (uint32_t)page[4]
     | ((uint32_t)page[5] << 8)
     | ((uint32_t)page[6] << 16)
     | ((uint32_t)page[7] << 24);

  DMU_ENDINIT_UNLOCK();

  {
    irqstate_t        flags  = enter_critical_section();
    volatile uint32_t *cmd5554 = (volatile uint32_t *)(DMU_CMD_BASE | 0x5554u);
    volatile uint64_t *cmdLP64 = (volatile uint64_t *)(DMU_CMD_BASE | 0x55F0u);
    volatile uint32_t *addr1  = (volatile uint32_t *)(DMU_CMD_BASE | 0xAA50u);
    volatile uint32_t *addr2  = (volatile uint32_t *)(DMU_CMD_BASE | 0xAA58u);
    volatile uint32_t *addr3  = (volatile uint32_t *)(DMU_CMD_BASE | 0xAAA8u);
    volatile uint32_t *addr4  = (volatile uint32_t *)(DMU_CMD_BASE | 0xAAA8u);
    uint64_t           page64 = ((uint64_t)w1 << 32) | (uint64_t)w0;

    *cmd5554 = 0xF0u;            /* Reset to Read   */
    *cmd5554 = 0xFAu;            /* Clear Status    */
    *cmd5554 = 0x5Du;            /* Enter Page Mode (DFLASH) */
    *cmdLP64 = page64;           /* Load Page 64-bit */
    *addr1   = pa;               /* Write Page cycle 1 */
    *addr2   = 0x00u;            /* Write Page cycle 2 */
    *addr3   = 0xA0u;            /* Write Page cycle 3 */
    *addr4   = 0xAAu;            /* Write Page cycle 4 */
    UP_DSB();

    leave_critical_section(flags);
  }

  DMU_ENDINIT_LOCK();

  ret = dmu_wait_done();
  return ret;
}


static int tricore_flash_erase(struct mtd_dev_s *dev, off_t startblock,
                               size_t nblocks)
{
  struct tricore_flash_dev_s *priv = (struct tricore_flash_dev_s *)dev;
  int ret;

  if (startblock < 0 || nblocks == 0u)
    {
      return (nblocks == 0u) ? 0 : -EINVAL;
    }

  if ((uint32_t)startblock + nblocks > TRICORE_DFLASH_NSECTORS)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = tricore_dflash_erase_sectors((uint32_t)startblock,
                                     (uint32_t)nblocks);

  if (ret < 0)
    {
      printf("tc-dflash: erase startblock=%lu nblocks=%lu ret=%d "
             "DMU err=0x%08lx status=0x%08lx phase=%lu\n",
             (unsigned long)startblock, (unsigned long)nblocks, ret,
             (unsigned long)g_last_dmu_err,
             (unsigned long)g_last_dmu_status,
             (unsigned long)g_last_dmu_phase);
    }

  nxmutex_unlock(&priv->lock);
  return (ret < 0) ? ret : (int)nblocks;
}


static ssize_t tricore_flash_bread(struct mtd_dev_s *dev, off_t startblock,
                                   size_t nblocks, uint8_t *buf)
{
  size_t nbytes;

  if (buf == NULL)
    {
      return -EINVAL;
    }

  if (startblock < 0 ||
      (uint32_t)startblock + nblocks > TRICORE_DFLASH_NPAGES)
    {
      return -EINVAL;
    }

  nbytes = nblocks * TRICORE_DFLASH_PAGE_SIZE;
  memcpy(buf,
         (const void *)(TRICORE_DFLASH_BASE +
                        (uint32_t)startblock * TRICORE_DFLASH_PAGE_SIZE),
         nbytes);

  return (ssize_t)nblocks;
}


static ssize_t tricore_flash_bwrite(struct mtd_dev_s *dev, off_t startblock,
                                    size_t nblocks, const uint8_t *buf)
{
  struct tricore_flash_dev_s *priv = (struct tricore_flash_dev_s *)dev;
  uint32_t   off;
  size_t     remaining;
  int        ret;

  if (buf == NULL || nblocks == 0u)
    {
      return (nblocks == 0u) ? 0 : -EINVAL;
    }

  if (startblock < 0 ||
      (uint32_t)startblock + nblocks > TRICORE_DFLASH_NPAGES)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  off       = (uint32_t)startblock * TRICORE_DFLASH_PAGE_SIZE;
  remaining = nblocks * TRICORE_DFLASH_PAGE_SIZE;

  while (remaining != 0u)
    {
      ret = tricore_dflash_write_page(off, buf);
      if (ret < 0)
        {
          printf("tc-dflash: write off=0x%lx ret=%d "
                 "DMU err=0x%08lx status=0x%08lx phase=%lu\n",
                 (unsigned long)off, ret,
                 (unsigned long)g_last_dmu_err,
                 (unsigned long)g_last_dmu_status,
                 (unsigned long)g_last_dmu_phase);
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      off       += TRICORE_DFLASH_PAGE_SIZE;
      buf       += TRICORE_DFLASH_PAGE_SIZE;
      remaining -= TRICORE_DFLASH_PAGE_SIZE;
    }

  nxmutex_unlock(&priv->lock);
  return (ssize_t)nblocks;
}


static ssize_t tricore_flash_read(struct mtd_dev_s *dev, off_t offset,
                                  size_t nbytes, uint8_t *buf)
{
  if (buf == NULL)
    {
      return -EINVAL;
    }

  if (offset < 0 || (size_t)offset + nbytes > TRICORE_DFLASH_SIZE)
    {
      return -EINVAL;
    }

  memcpy(buf, (const void *)(TRICORE_DFLASH_BASE + (uint32_t)offset),
         nbytes);

  return (ssize_t)nbytes;
}


static int tricore_flash_ioctl(struct mtd_dev_s *dev, int cmd,
                               unsigned long arg)
{
  int ret = -ENOTTY;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          struct mtd_geometry_s *geo = (struct mtd_geometry_s *)
                                       ((uintptr_t)arg);
          if (geo == NULL)
            {
              return -EINVAL;
            }

          memset(geo, 0, sizeof(*geo));
          geo->blocksize    = TRICORE_DFLASH_PAGE_SIZE;
          geo->erasesize    = TRICORE_DFLASH_SECTOR_SIZE;
          geo->neraseblocks = TRICORE_DFLASH_NSECTORS;
          strlcpy(geo->model, "tc-dflash", sizeof(geo->model));
          ret = OK;
        }
        break;

      case MTDIOC_ERASESTATE:
        {
          uint8_t *value = (uint8_t *)((uintptr_t)arg);
          if (value == NULL)
            {
              return -EINVAL;
            }

          *value = TRICORE_FLASH_ERASED_BYTE;
          ret = OK;
        }
        break;

      case MTDIOC_BULKERASE:
        {
          struct tricore_flash_dev_s *priv =
              (struct tricore_flash_dev_s *)dev;

          ret = nxmutex_lock(&priv->lock);
          if (ret < 0)
            {
              break;
            }

          ret = tricore_dflash_erase_sectors(0u,
                                             TRICORE_DFLASH_NSECTORS);
          nxmutex_unlock(&priv->lock);
        }
        break;

      default:
        break;
    }

  return ret;
}



struct mtd_dev_s *tricore_flash_initialize(void)
{
  return &g_tricore_flash.mtd;
}

#endif /* CONFIG_TRICORE_FLASH_MTD */
