/****************************************************************************
 * include/nuttx/mtd/hyperflash.h
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

#ifndef __INCLUDE_NUTTX_MTD_HYPERFLASH_H
#define __INCLUDE_NUTTX_MTD_HYPERFLASH_H

#include <nuttx/bits.h>
#include <stdint.h>

/* Unlock cycle (16-bit-word addresses, byte data) */

#define HF_UNLOCK_ADDR1         0x555
#define HF_UNLOCK_ADDR2         0x2AA
#define HF_UNLOCK_DATA1         0x0AA
#define HF_UNLOCK_DATA2         0x055

/* Command codes */

#define HF_CMD_RESET            0xF0
#define HF_CMD_RDVSTR           0x70
#define HF_CMD_PGWORD           0xA0
#define HF_CMD_WBPROG           0x25
#define HF_CMD_WBCONFIRM        0x29
#define HF_CMD_ERASE_SETUP      0x80
#define HF_CMD_SECTOR_ERASE     0x30
#define HF_CMD_CHIP_ERASE       0x10
#define HF_CMD_ERASE_SUSPEND    0xB0
#define HF_CMD_ERASE_RESUME     0x30
#define HF_CMD_ASO_ENTRY        0x98

/* 48-bit Command-Address layout, MSB first on the wire.
 *   CA[47]    R/W#     1=read, 0=write
 *   CA[46]    AS       0=memory, 1=register
 *   CA[45]    BT       1=linear, 0=wrapped
 *   CA[44:16] row + upper col
 *   CA[15:3]  reserved
 *   CA[2:0]   lower col
 */

#define HF_RW_READ              1u
#define HF_RW_WRITE             0u

#define HF_CA47_32(rw, waddr)                                              \
  ((uint16_t)(((rw) << 15) | (1u << 13) |                                  \
              (((waddr) >> 19) & 0x3Fu)))

#define HF_CA31_16(waddr)       ((uint16_t)(((waddr) >> 3) & 0xFFFFu))

#define HF_CA15_0(waddr)        ((uint16_t)((waddr) & 0x7u))

/* Volatile Status Register bits (Cypress/Infineon S26 layout). */

#define HF_SR_DRB               BIT(7)
#define HF_SR_ESSB              BIT(6)
#define HF_SR_ESB               BIT(5)
#define HF_SR_PSB               BIT(4)
#define HF_SR_WBASB             BIT(3)
#define HF_SR_PSSB              BIT(2)
#define HF_SR_SLSB              BIT(1)

/* JEDEC manufacturer IDs (HF subset). */

#define HF_MFR_INFINEON         0x0034
#define HF_MFR_MICRON           0x002C
#define HF_MFR_MACRONIX         0x00C2

#define HF_DEFAULT_INITIAL_LATENCY  16

#endif /* __INCLUDE_NUTTX_MTD_HYPERFLASH_H */
