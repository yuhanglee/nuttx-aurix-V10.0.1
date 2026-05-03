/****************************************************************************
 * arch/tricore/src/aurix/hardware/tricore_dmu.h
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

/* Infineon AURIX TC3x/TC4x Data Management Unit (DMU) register definitions.
 *
 * The TC3x and TC4x families both use a DMU command-sequence interpreter
 * to drive HOST PFLASH/DFLASH erase and program operations, but they use
 * different register layouts, different error-bit positions, and different
 * command-window base addresses.  This header exposes the family-specific
 * addresses and bit layouts as raw constants, plus a small set of abstract
 * macros (DMU_*_REG, DMU_CMD_BASE, DMU_BUSY_DF_MASK, DMU_ERR_FATAL_MASK,
 * DMU_CLRE_ALL) that the driver in tricore_flash.c uses to stay portable
 * across both families.
 *
 * References:
 *   - AURIX TC3xx UM Vol 2 chapter "Data Management Unit"
 *   - Infineon iLLD TC3-v1.20.0  (Src/BaseSw/Infra/Sfr/TC39xB/IfxDmu_*.h
 *                                 + iLLD/TC3xx/Tricore/Flash/Std/IfxFlash.h)
 *   - AURIX TC4Dx UM v1.1 chapter 6.4 / 6.8.4
 *   - Infineon iLLD TC4-v2.5.1  (src/Libraries/Infra/Sfr/TC4Dx/IfxDmu_*.h
 *                                 + iLLD/TC4xx/Tricore/Flash/Std/IfxFlash.h)
 */

#ifndef __ARCH_TRICORE_SRC_AURIX_HARDWARE_TRICORE_DMU_H
#define __ARCH_TRICORE_SRC_AURIX_HARDWARE_TRICORE_DMU_H

#include <nuttx/config.h>
#include <nuttx/bits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Common: DMU module base address (same on TC3x and TC4x) -----------------*/

#define DMU_BASE                       0xF8040000u

/* Logical sector / page sizes.
 *
 * DFLASH logical sector size is family-specific:
 *   - TC3xx (TC39xB): 0x1000 = 4 KB per IfxFlash_cfg_TC39xB.h
 *     (NUM_LOG_SECTORS=256, total 1 MB).  Erase commands targeting an
 *     address that is not 4 KB aligned trigger SQER.
 *   - TC4xx (TC4Dx):  0x800 = 2 KB per IfxFlash_cfg_TC4Dx.h
 *     (NUM_LOG_SECTORS=512, total 1 MB).
 * PFLASH logical sector size is 16 KB on both.
 */

#define TRICORE_PFLASH_SECTOR_SIZE     16384u   /* 16 KB                 */
#define TRICORE_PFLASH_PAGE_SIZE       32u      /* 32 B (256 bits)       */

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)
#  define TRICORE_DFLASH_SECTOR_SIZE   4096u    /* 4 KB on TC3xx         */
#else
#  define TRICORE_DFLASH_SECTOR_SIZE   2048u    /* 2 KB on TC4xx         */
#endif

#define TRICORE_DFLASH_PAGE_SIZE       8u       /* 8 B (both families)   */

/* AURIX DFLASH cells read back 0x00 after erase; programming flips
 * bits 0 -> 1, so the write-once / OR semantic is the inverse of
 * conventional NOR.  Reported via MTDIOC_ERASESTATE so any FTL /
 * filesystem layered on top knows the right erase value.
 */

#define TRICORE_FLASH_ERASED_BYTE      0x00u

/* Common command-cycle byte patterns / opcodes (same on TC3x and TC4x).
 *
 * Each command sequence is a sequence of word writes whose target offsets
 * inside the command window match these patterns.  See the AURIX UM
 * "Command sequences supported by the DMU" table.
 */

#define DMU_CSI_RST                    0x5554u  /* Reset to read / clear */
#define DMU_CSI_LOADP_64               0x55F0u  /* Load Page (64-bit)    */
#define DMU_CSI_LOADP_32               0x55F4u  /* Load Page (32-bit)    */
#define DMU_CSI_DPROT                  0x553Cu  /* Disable Protection    */
#define DMU_CSI_C1                     0xAA50u  /* Cycle 1: address      */
#define DMU_CSI_C2                     0xAA58u  /* Cycle 2: count/zero   */
#define DMU_CSI_C3                     0xAAA8u  /* Cycle 3 / 4 selector  */

#define DMU_CMD_RESET_TO_READ          0x00F0u  /* @5554, cycle 1        */
#define DMU_CMD_ENTER_PAGE_MODE_PF     0x0050u
#define DMU_CMD_ENTER_PAGE_MODE_DF     0x005Du

#define DMU_CMD_WRITE_PAGE             0x00AAu  /* Write Page            */
#define DMU_CMD_WRITE_PAGE_ONCE        0x00A8u
#define DMU_CMD_WRITE_BURST            0x00A6u
#define DMU_CMD_WRITE_BURST_ONCE       0x00A4u

#define DMU_CMD_ERASE_LSECTOR          0x0050u  /* Erase Logical Sec Rng */
#define DMU_CMD_VERIFY_ERASED_LSECTOR  0x005Fu

/****************************************************************************
 * Family-specific blocks
 ****************************************************************************/

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3X)

/* TC3xx (TC397, TC38EVOx, ...).  Per iLLD IfxDmu_reg.h + IfxFlash.h:
 *   - HOST PFLASH/DFLASH command writes go through the DFLASH virtual
 *     address itself (0xAF000000 | cycle_offset), NOT through a separate
 *     CSI window.
 *   - Status register is HF_STATUS at 0xF8040010 with per-bank BUSY bits
 *     (D0BUSY at bit 0).
 *   - Error register is HF_ERRSR at 0xF8040034 with a different bit
 *     layout from TC4x.
 *   - Clear-error register is HF_CLRE at 0xF8040038.
 */

#  define DMU_CMD_BASE                 0xAF000000u

#  define DMU_HF_STATUS_OFFSET         0x0010u
#  define DMU_HF_CONTROL_OFFSET        0x0014u
#  define DMU_HF_OPERATION_OFFSET      0x0018u
#  define DMU_HF_PROTECT_OFFSET        0x001Cu
#  define DMU_HF_ERRSR_OFFSET          0x0034u
#  define DMU_HF_CLRE_OFFSET           0x0038u

#  define DMU_HF_STATUS                (DMU_BASE + DMU_HF_STATUS_OFFSET)
#  define DMU_HF_ERRSR                 (DMU_BASE + DMU_HF_ERRSR_OFFSET)
#  define DMU_HF_CLRE                  (DMU_BASE + DMU_HF_CLRE_OFFSET)

/* HF_STATUS busy bits.  D0BUSY is bit 0 (HOST DFLASH(0)). */

#  define DMU_HF_STATUS_D0BUSY         BIT(0)
#  define DMU_HF_STATUS_D1BUSY         BIT(1)

/* HF_ERRSR bit fields (TC3xx). */

#  define DMU_HF_ERRSR_OPER            BIT(0)
#  define DMU_HF_ERRSR_SQER            BIT(1)
#  define DMU_HF_ERRSR_PROER           BIT(2)
#  define DMU_HF_ERRSR_PVER            BIT(3)
#  define DMU_HF_ERRSR_EVER            BIT(4)
#  define DMU_HF_ERRSR_ADER            BIT(5)
#  define DMU_HF_ERRSR_ORIER           BIT(6)

/* HF_CLRE clear-flag bits (TC3xx). */

#  define DMU_HF_CLRE_CSQER            BIT(1)
#  define DMU_HF_CLRE_CPROER           BIT(2)
#  define DMU_HF_CLRE_CPVER            BIT(3)
#  define DMU_HF_CLRE_CEVER            BIT(4)
#  define DMU_HF_CLRE_CADER            BIT(5)

/* Abstract names used by the driver. */

#  define DMU_STATUS_REG               DMU_HF_STATUS
#  define DMU_ERR_REG                  DMU_HF_ERRSR
#  define DMU_CLRE_REG                 DMU_HF_CLRE

#  define DMU_BUSY_DF_MASK             DMU_HF_STATUS_D0BUSY

#  define DMU_ERR_FATAL_MASK           (DMU_HF_ERRSR_OPER  | \
                                        DMU_HF_ERRSR_SQER  | \
                                        DMU_HF_ERRSR_PROER | \
                                        DMU_HF_ERRSR_PVER  | \
                                        DMU_HF_ERRSR_EVER  | \
                                        DMU_HF_ERRSR_ADER  | \
                                        DMU_HF_ERRSR_ORIER)

#  define DMU_CLRE_ALL                 (DMU_HF_CLRE_CSQER  | \
                                        DMU_HF_CLRE_CPROER | \
                                        DMU_HF_CLRE_CPVER  | \
                                        DMU_HF_CLRE_CEVER  | \
                                        DMU_HF_CLRE_CADER)

/* Default DFLASH(0) non-cached alias on TC3x. */

#  define DMU_DFLASH_DEFAULT_BASE      0xAF000000u

#elif defined(CONFIG_ARCH_CHIP_FAMILY_TC4X)

/* TC4xx (TC4Dx, TC49xN, ...).  Per iLLD IfxDmu_reg.h + IfxFlash.h:
 *   - HOST command writes go through the dedicated 64 KB CSI window at
 *     0xF8080000.
 *   - Status register is HCI_STATUS at 0xF8040004 with per-bank BUSY bits
 *     (BUSYHOSTDF at bit 16) plus REQACK/REQDONE in the high bits.
 *   - Error register is HCI_ERR at 0xF8040010 with a different bit
 *     layout from TC3x.
 *   - Clear-error register is HCI_CLRERR at 0xF8040014.
 *   - Default DFLASH non-cached alias is 0xAE000000 (NOT 0xAF000000 — that
 *     is unmapped on TC4Dx).
 */

#  define DMU_CMD_BASE                 0xF8080000u
#  define DMU_HCI_CSI_SIZE             0x10000u

#  define DMU_HCI_STATUS_OFFSET        0x0004u
#  define DMU_HCI_BCONTROL_OFFSET      0x0008u
#  define DMU_HCI_OCONTROL_OFFSET      0x000Cu
#  define DMU_HCI_ERR_OFFSET           0x0010u
#  define DMU_HCI_CLRERR_OFFSET        0x0014u
#  define DMU_HCI_INTEN_OFFSET         0x0018u
#  define DMU_HCI_SLEEP_OFFSET         0x001Cu

#  define DMU_HCI_STATUS               (DMU_BASE + DMU_HCI_STATUS_OFFSET)
#  define DMU_HCI_OCONTROL             (DMU_BASE + DMU_HCI_OCONTROL_OFFSET)
#  define DMU_HCI_ERR                  (DMU_BASE + DMU_HCI_ERR_OFFSET)
#  define DMU_HCI_CLRERR               (DMU_BASE + DMU_HCI_CLRERR_OFFSET)

/* HCI_STATUS busy bits. */

#  define DMU_HCI_STATUS_BUSYHOSTDF    BIT(16)
#  define DMU_HCI_STATUS_BUSYCSDF      BIT(17)
#  define DMU_HCI_STATUS_REQDONE       BIT(31)

/* HCI_ERR bit fields (TC4xx). */

#  define DMU_HCI_ERR_ADER             BIT(0)
#  define DMU_HCI_ERR_SQER             BIT(1)
#  define DMU_HCI_ERR_PROER            BIT(2)
#  define DMU_HCI_ERR_ABER             BIT(4)
#  define DMU_HCI_ERR_CLER             BIT(5)
#  define DMU_HCI_ERR_PVER             BIT(6)
#  define DMU_HCI_ERR_EVER             BIT(7)
#  define DMU_HCI_ERR_OPER             BIT(16)
#  define DMU_HCI_ERR_ORIER            BIT(17)

/* HCI_CLRERR clear-flag bits (TC4xx). */

#  define DMU_HCI_CLRERR_CADER         BIT(0)
#  define DMU_HCI_CLRERR_CSQER         BIT(1)
#  define DMU_HCI_CLRERR_CPROER        BIT(2)
#  define DMU_HCI_CLRERR_CABER         BIT(4)
#  define DMU_HCI_CLRERR_CCLER         BIT(5)
#  define DMU_HCI_CLRERR_CPVER         BIT(6)
#  define DMU_HCI_CLRERR_CEVER         BIT(7)

/* Abstract names used by the driver. */

#  define DMU_STATUS_REG               DMU_HCI_STATUS
#  define DMU_ERR_REG                  DMU_HCI_ERR
#  define DMU_CLRE_REG                 DMU_HCI_CLRERR

#  define DMU_BUSY_DF_MASK             DMU_HCI_STATUS_BUSYHOSTDF

#  define DMU_ERR_FATAL_MASK           (DMU_HCI_ERR_ADER  | \
                                        DMU_HCI_ERR_SQER  | \
                                        DMU_HCI_ERR_PROER | \
                                        DMU_HCI_ERR_ABER  | \
                                        DMU_HCI_ERR_CLER  | \
                                        DMU_HCI_ERR_PVER  | \
                                        DMU_HCI_ERR_EVER  | \
                                        DMU_HCI_ERR_OPER  | \
                                        DMU_HCI_ERR_ORIER)

#  define DMU_CLRE_ALL                 (DMU_HCI_CLRERR_CADER  | \
                                        DMU_HCI_CLRERR_CSQER  | \
                                        DMU_HCI_CLRERR_CPROER | \
                                        DMU_HCI_CLRERR_CABER  | \
                                        DMU_HCI_CLRERR_CCLER  | \
                                        DMU_HCI_CLRERR_CPVER  | \
                                        DMU_HCI_CLRERR_CEVER)

/* Default DFLASH(0) non-cached alias on TC4Dx. */

#  define DMU_DFLASH_DEFAULT_BASE      0xAE000000u

#endif /* family */

#endif /* __ARCH_TRICORE_SRC_AURIX_HARDWARE_TRICORE_DMU_H */
