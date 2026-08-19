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

/* TC4xx eGTM register map. Per-cluster macros take the cluster index
 * `cl` (0..EGTM_NCLUSTERS-1) as their first argument.
 */

#include <nuttx/bits.h>

#define EGTM_BASE               0xF9080000u
#define EGTM_CLS_STRIDE         0x20000u
#define EGTM_CLS_BASE(cl)       (EGTM_BASE + (cl) * EGTM_CLS_STRIDE)

/* CLC (module-level, endinit-protected) */

#define EGTM_CLC          (EGTM_BASE + 0x60000u)
#define EGTM_CLC_DISR     BIT(0)
#define EGTM_CLC_DISS     BIT(1)

/* CFGCMU0 protection (module-level) */

#define EGTM_CFGCMU0_PROTSE  (EGTM_BASE + 0x6001Cu)
#define EGTM_CFGCMU0_PROTE   (EGTM_BASE + 0x601D0u)

#define PROT_SWEN         BIT(3)
#define PROT_STATE_RUN    0x4u
#define PROT_UNLOCK       PROT_SWEN
#define PROT_LOCK_RUN     (PROT_SWEN | PROT_STATE_RUN)

/* CMU (cluster 0 only): GCLK = feGTM * GCLK_NUM / GCLK_DEN */

#define EGTM_CMU_CLK_EN   (EGTM_BASE + 0x00080u)
#define EGTM_CMU_GCLK_NUM (EGTM_BASE + 0x00084u)
#define EGTM_CMU_GCLK_DEN (EGTM_BASE + 0x00088u)

#define EGTM_CMU_CLKX_CTRL(x)   (EGTM_BASE + 0x0008Cu + (x) * 4u)

#define CMU_CLK_EN_CFGCMU (0x2u << 0u)
#define CMU_CLK_EN_FXCLK  (0x2u << 22u)
#define CMU_CLK_EN_CFGCMU_FXCLK (CMU_CLK_EN_CFGCMU | CMU_CLK_EN_FXCLK)
#define CMU_CLK_EN_CLK(x)       (0x2u << (2u + (x) * 2u))
#define CMU_CLK_DIS_CLK(x)      (0x1u << (2u + (x) * 2u))

/* TOM channels 0-15 per cluster */

#define EGTM_TOM_CH_BASE(cl, ch)   (EGTM_CLS_BASE(cl) + 0x01000u + (ch) * 0x40u)
#define EGTM_TOM_CH_CTRL(cl, ch)       (EGTM_TOM_CH_BASE(cl, ch) + 0x00u)
#define EGTM_TOM_CH_SR0(cl, ch)        (EGTM_TOM_CH_BASE(cl, ch) + 0x04u)
#define EGTM_TOM_CH_SR1(cl, ch)        (EGTM_TOM_CH_BASE(cl, ch) + 0x08u)
#define EGTM_TOM_CH_CM0(cl, ch)        (EGTM_TOM_CH_BASE(cl, ch) + 0x0Cu)
#define EGTM_TOM_CH_CM1(cl, ch)        (EGTM_TOM_CH_BASE(cl, ch) + 0x10u)
#define EGTM_TOM_CH_CN0(cl, ch)        (EGTM_TOM_CH_BASE(cl, ch) + 0x14u)
#define EGTM_TOM_CH_IRQ_NOTIFY(cl, ch) (EGTM_TOM_CH_BASE(cl, ch) + 0x1Cu)
#define EGTM_TOM_CH_IRQ_EN(cl, ch)     (EGTM_TOM_CH_BASE(cl, ch) + 0x20u)
#define EGTM_TOM_CH_IRQ_MODE(cl, ch)   (EGTM_TOM_CH_BASE(cl, ch) + 0x28u)

/* CTRL.CLK_SRC: FXCLK_RES tap index (0..4) */

#define TOM_CTRL_CLK_SRC_SHIFT  12u

#define TOM_IRQ_EN_CCU0TC       BIT(0)
#define TOM_IRQ_EN_CCU1TC       BIT(1)

#define TOM_IRQ_MODE_LEVEL      0x0u
#define TOM_IRQ_MODE_PULSE      0x1u

/* TGC groups per cluster: g=0 covers CH0-7, g=1 covers CH8-15 */

#define EGTM_TOM_TGC_GLB_CTRL(cl, g)   (EGTM_CLS_BASE(cl) + 0x01430u + (g) * 0x80u)
#define EGTM_TOM_TGC_FUPD_CTRL(cl, g)  (EGTM_CLS_BASE(cl) + 0x01438u + (g) * 0x80u)
#define EGTM_TOM_TGC_ENDIS_CTRL(cl, g) (EGTM_CLS_BASE(cl) + 0x01470u + (g) * 0x80u)
#define EGTM_TOM_TGC_ENDIS_STAT(cl, g) (EGTM_CLS_BASE(cl) + 0x01474u + (g) * 0x80u)
#define EGTM_TOM_TGC_OUTEN_CTRL(cl, g) (EGTM_CLS_BASE(cl) + 0x01478u + (g) * 0x80u)
#define EGTM_TOM_TGC_OUTEN_STAT(cl, g) (EGTM_CLS_BASE(cl) + 0x0147Cu + (g) * 0x80u)

#define TGC_GROUP(ch)            ((ch) >> 3u)

/* 2-bit per channel: 0b10 = enable, 0b01 = disable */

#define TGC_CH_ENABLE(ch)        (0x2u << (((ch) & 0x7u) * 2u))
#define TGC_CH_DISABLE(ch)       (0x1u << (((ch) & 0x7u) * 2u))
#define TGC_GLB_CTRL_HOST_TRIG   BIT(0)

#define EGTM_ATOM_CH_BASE(cl, ch)      (EGTM_CLS_BASE(cl) + 0x01800u + (ch) * 0x80u)
#define EGTM_ATOM_CH_CTRL(cl, ch)       (EGTM_ATOM_CH_BASE(cl, ch) + 0x04u)
#define EGTM_ATOM_CH_SR0(cl, ch)        (EGTM_ATOM_CH_BASE(cl, ch) + 0x08u)
#define EGTM_ATOM_CH_SR1(cl, ch)        (EGTM_ATOM_CH_BASE(cl, ch) + 0x0Cu)
#define EGTM_ATOM_CH_CM0(cl, ch)        (EGTM_ATOM_CH_BASE(cl, ch) + 0x10u)
#define EGTM_ATOM_CH_CN0(cl, ch)        (EGTM_ATOM_CH_BASE(cl, ch) + 0x18u)
#define EGTM_ATOM_CH_IRQ_NOTIFY(cl, ch) (EGTM_ATOM_CH_BASE(cl, ch) + 0x20u)
#define EGTM_ATOM_CH_IRQ_EN(cl, ch)     (EGTM_ATOM_CH_BASE(cl, ch) + 0x24u)
#define EGTM_ATOM_CH_IRQ_MODE(cl, ch)   (EGTM_ATOM_CH_BASE(cl, ch) + 0x2Cu)
#define EGTM_ATOM_CH_CTRL_SR(cl, ch)    (EGTM_ATOM_CH_BASE(cl, ch) + 0x34u)

#define ATOM_CTRL_MODE_SOMP      0x2u
#define ATOM_CTRL_CLK_SRC_SHIFT  12u

#define ATOM_IRQ_EN_CCU0TC       BIT(0)

#define ATOM_IRQ_MODE_PULSE      0x1u

#define EGTM_ATOM_AGC_GLB_CTRL(cl)     (EGTM_CLS_BASE(cl) + 0x01C40u)
#define EGTM_ATOM_AGC_ENDIS_CTRL(cl)   (EGTM_CLS_BASE(cl) + 0x01C44u)
#define EGTM_ATOM_AGC_OUTEN_CTRL(cl)   (EGTM_CLS_BASE(cl) + 0x01C50u)
#define EGTM_ATOM_AGC_FUPD_CTRL(cl)    (EGTM_CLS_BASE(cl) + 0x01C58u)

#define AGC_CH_ENABLE(ch)              (0x2u << ((ch) * 2u))
#define AGC_CH_DISABLE(ch)             (0x1u << ((ch) * 2u))
#define AGC_GLB_CTRL_HOST_TRIG         BIT(0)

#define AGC_GLB_CTRL_UPEN_EN(ch)       (0x2u << (16u + (ch) * 2u))

/* Per-cluster ICM channel-interrupt group registers */

#define EGTM_ICM_IRQG_TOM_CI(cl, g)  (EGTM_CLS_BASE(cl) + 0x005A0u + (g) * 4u)
#define EGTM_ICM_IRQG_ATOM_CI(cl)     (EGTM_CLS_BASE(cl) + 0x005B0u)

#define EGTM_NCLUSTERS      3u
#define EGTM_TOM_NCHANNELS  16u
#define EGTM_ATOM_NCHANNELS 8u

/* TOUTSEL: 6 x 5-bit SEL fields per register. SEL field x of
 * TOUTSEL[n] drives pad TOUT(n*6 + x).
 */

#define EGTM_TOUTSEL(n)     (EGTM_BASE + 0x61100u + (n) * 4u)
#define TOUTSEL_SHIFT(x)    (((x) < 3u) ? ((x) * 5u) : ((x) * 5u + 1u))
#define TOUTSEL_MASK(x)     (0x1Fu << TOUTSEL_SHIFT(x))

#endif /* __ARCH_TRICORE_SRC_TC4XX_HARDWARE_TC4XX_EGTM_H */
