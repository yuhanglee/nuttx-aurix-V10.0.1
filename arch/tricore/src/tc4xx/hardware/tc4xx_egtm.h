/****************************************************************************
 * arch/tricore/src/tc4xx/hardware/tc4xx_egtm.h
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
 ****************************************************************************/

#ifndef __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_EGTM_H
#define __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_EGTM_H

/* TC4xx eGTM register map (UM Table 221/223). */

#include <nuttx/bits.h>

#define EGTM_BASE         0xF9080000u

/* CLC (offset 0x60000, endinit-protected) */

#define EGTM_CLC          (EGTM_BASE + 0x60000u)
#define EGTM_CLC_DISR     BIT(0)
#define EGTM_CLC_DISS     BIT(1)

/* CFGCMU0 protection. CMU NUM/DEN are guarded. */

#define EGTM_CFGCMU0_PROTSE  (EGTM_BASE + 0x6001Cu)
#define EGTM_CFGCMU0_PROTE   (EGTM_BASE + 0x601D0u)

#define PROT_SWEN         BIT(3)
#define PROT_STATE_RUN    0x4u
#define PROT_UNLOCK       PROT_SWEN
#define PROT_LOCK_RUN     (PROT_SWEN | PROT_STATE_RUN)

/* CMU: GCLK = feGTM * GCLK_NUM / GCLK_DEN */

#define EGTM_CMU_CLK_EN   (EGTM_BASE + 0x00080u)
#define EGTM_CMU_GCLK_NUM (EGTM_BASE + 0x00084u)
#define EGTM_CMU_GCLK_DEN (EGTM_BASE + 0x00088u)

#define CMU_CLK_EN_CFGCMU (0x2u << 0u)
#define CMU_CLK_EN_FXCLK  (0x2u << 22u)
#define CMU_CLK_EN_CFGCMU_FXCLK (CMU_CLK_EN_CFGCMU | CMU_CLK_EN_FXCLK)

/* TOM channels 0-7 (base 0x01000, stride 0x40) */

#define EGTM_TOM_CH_BASE(ch)       (EGTM_BASE + 0x01000u + (ch) * 0x40u)
#define EGTM_TOM_CH_CTRL(ch)       (EGTM_TOM_CH_BASE(ch) + 0x00u)
#define EGTM_TOM_CH_SR0(ch)        (EGTM_TOM_CH_BASE(ch) + 0x04u)
#define EGTM_TOM_CH_SR1(ch)        (EGTM_TOM_CH_BASE(ch) + 0x08u)
#define EGTM_TOM_CH_CM0(ch)        (EGTM_TOM_CH_BASE(ch) + 0x0Cu)
#define EGTM_TOM_CH_CM1(ch)        (EGTM_TOM_CH_BASE(ch) + 0x10u)
#define EGTM_TOM_CH_CN0(ch)        (EGTM_TOM_CH_BASE(ch) + 0x14u)
#define EGTM_TOM_CH_IRQ_NOTIFY(ch) (EGTM_TOM_CH_BASE(ch) + 0x1Cu)
#define EGTM_TOM_CH_IRQ_EN(ch)     (EGTM_TOM_CH_BASE(ch) + 0x20u)
#define EGTM_TOM_CH_IRQ_MODE(ch)   (EGTM_TOM_CH_BASE(ch) + 0x28u)

/* CTRL.CLK_SRC [15:12]: FXCLK_RES tap index (0..4). */

#define TOM_CTRL_CLK_SRC_SHIFT  12u

#define TOM_IRQ_EN_CCU0TC       BIT(0)
#define TOM_IRQ_EN_CCU1TC       BIT(1)

#define TOM_IRQ_MODE_LEVEL      0x0u
#define TOM_IRQ_MODE_PULSE      0x1u

/* TGC0 (channels 0-7) */

#define EGTM_TOM_TGC0_GLB_CTRL   (EGTM_BASE + 0x01430u)
#define EGTM_TOM_TGC0_FUPD_CTRL  (EGTM_BASE + 0x01438u)
#define EGTM_TOM_TGC0_ENDIS_CTRL (EGTM_BASE + 0x01470u)
#define EGTM_TOM_TGC0_ENDIS_STAT (EGTM_BASE + 0x01474u)
#define EGTM_TOM_TGC0_OUTEN_CTRL (EGTM_BASE + 0x01478u)
#define EGTM_TOM_TGC0_OUTEN_STAT (EGTM_BASE + 0x0147Cu)

/* 2-bit per channel: 0b10 = enable, 0b01 = disable */

#define TGC_CH_ENABLE(ch)        (0x2u << ((ch) * 2u))
#define TGC_CH_DISABLE(ch)       (0x1u << ((ch) * 2u))
#define TGC_GLB_CTRL_HOST_TRIG   BIT(0)

/* ICM */

#define EGTM_ICM_IRQG_TOM0_CI    (EGTM_BASE + 0x005A0u)
#define EGTM_ICM_IRQG_TOM1_CI    (EGTM_BASE + 0x005A4u)

/* TOUTSEL[n] (base 0x61100, stride 4): 6 x 5-bit SEL fields per reg,
 * reserved bits at 15 and 31. SEL field x of TOUTSEL[n] drives pad
 * TOUT(n*6 + x).
 */

#define EGTM_TOUTSEL(n)     (EGTM_BASE + 0x61100u + (n) * 4u)
#define TOUTSEL_SHIFT(x)    (((x) < 3u) ? ((x) * 5u) : ((x) * 5u + 1u))
#define TOUTSEL_MASK(x)     (0x1Fu << TOUTSEL_SHIFT(x))

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_EGTM_H */
