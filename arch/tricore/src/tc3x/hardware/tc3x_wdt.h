/****************************************************************************
 * arch/tricore/src/tc3x/hardware/tc3x_wdt.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_WDT_H
#define __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_WDT_H

#include <nuttx/config.h>
#include <nuttx/bits.h>

#define TC3X_SCU_BASE              0xF0036000u

#define TC3X_WDTCPU0_BASE          (TC3X_SCU_BASE + 0x024Cu)
#define TC3X_WDTCPU_STRIDE         0x0Cu
#define TC3X_WDTCPU_CON0_OFFSET    0x00u
#define TC3X_WDTCPU_CON1_OFFSET    0x04u
#define TC3X_WDTCPU_SR_OFFSET      0x08u

#define TC3X_WDTS_CON0             (TC3X_SCU_BASE + 0x02A8u)
#define TC3X_WDTS_CON1             (TC3X_SCU_BASE + 0x02ACu)
#define TC3X_WDTS_SR               (TC3X_SCU_BASE + 0x02B0u)

#define WDT_CON0_ENDINIT           BIT(0)
#define WDT_CON0_LCK               BIT(1)
#define WDT_CON0_PW_SHIFT          2
#define WDT_CON0_PW_MASK           GENMASK(15, 2)
#define WDT_CON0_REL_SHIFT         16
#define WDT_CON0_REL_MASK          GENMASK(31, 16)

#define WDT_CON1_CLRIRF            BIT(0)

#define WDT_CON1_IR0               BIT(2)
#define WDT_CON1_DR                BIT(3)
#define WDT_CON1_IR1               BIT(5)
#define WDT_CON1_UR                BIT(6)
#define WDT_CON1_PAR               BIT(7)
#define WDT_CON1_TCR               BIT(8)

#define WDT_SR_AE                  BIT(0)
#define WDT_SR_OE                  BIT(1)
#define WDT_SR_IS0                 BIT(2)
#define WDT_SR_DS                  BIT(3)
#define WDT_SR_TO                  BIT(4)
#define WDT_SR_IS1                 BIT(5)
#define WDT_SR_TIM_SHIFT           16
#define WDT_SR_TIM_MASK            GENMASK(31, 16)

#define WDT_TIMER_MAX              0xFFFFu

#define WDT_DIVIDER_16384          16384u
#define WDT_DIVIDER_256            256u
#define WDT_DIVIDER_64             64u
#define WDT_DIVIDER_16             16u

#define WDT_IFS_DIV16384           0u
#define WDT_IFS_DIV256             1u
#define WDT_IFS_DIV64              2u
#define WDT_IFS_DIV16              3u

#endif /* __ARCH_TRICORE_SRC_TC3X_HARDWARE_TC3X_WDT_H */
