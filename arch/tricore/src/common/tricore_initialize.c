/****************************************************************************
 * arch/tricore/src/common/tricore_initialize.c
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


#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/bits.h>

#include "tricore_internal.h"


volatile uintptr_t *g_current_regs[CONFIG_SMP_NCPUS];



#if defined(CONFIG_STACK_COLORATION) && CONFIG_ARCH_INTERRUPTSTACK > 15
static inline void up_color_intstack(void)
{
  uint32_t *ptr = (uint32_t *)g_intstackalloc;
  ssize_t size;

  for (size = (CONFIG_ARCH_INTERRUPTSTACK & ~15);
       size > 0;
       size -= sizeof(uint32_t))
    {
      *ptr++ = INTSTACK_COLOR;
    }
}
#else
#  define up_color_intstack()
#endif



void up_initialize(void)
{
  /* Colorize the interrupt stack */

  up_color_intstack();


  aurix_earlyserialinit();
  aurix_serialinit();
}

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC4XX)
#  define IFX_CPUn_REG(n) (0xF8800000 + 0x40000 * (n))
#elif defined(CONFIG_ARCH_CHIP_FAMILY_TC3XX)
#  define IFX_CPUn_REG(n) (0xF8800000 + 0x20000 * (n))
#else
#  error "Unknown AURIX chip family for IFX_CPUn_REG"
#endif

#define IFX_CPU_HALT BIT(0)
#define IFX_CPU_BOOTCON(n) (IFX_CPUn_REG(n) + 0x1FE60)
#define IFX_CPU_PC(n) (IFX_CPUn_REG(n) + 0x1FE08)

#define IFX_CPU_SYSCON(n) (IFX_CPUn_REG(n) + 0x1FE14)
#define IFX_CPU_SYSCON_BHALT BIT(24)

#if defined(CONFIG_ARCH_CHIP_FAMILY_TC3XX)
static const uintptr_t g_tc3xx_cpu_base[6] =
{
  0xF8800000U,
  0xF8820000U,
  0xF8840000U,
  0xF8860000U,
  0xF8880000U,
  0xF88C0000U,
};
#endif

#define IFX_CPU_PROTSPRSE(n) (IFX_CPUn_REG(n) + 0xE008)
#define IFX_CPU_ACCENSPRCFG_WRA(n) (IFX_CPUn_REG(n) + 0xE020)
#define IFX_CPU_ACCENSPRCFG_WRB(n) (IFX_CPUn_REG(n) + 0xE024)

#define IFX_CPU_PROTDLMUSE(n) (IFX_CPUn_REG(n) + 0xE048)
#define IFX_CPU_ACCENDLMUCFG_WRA(n) (IFX_CPUn_REG(n) + 0xE060)
#define IFX_CPU_ACCENDLMUCFG_WRB(n) (IFX_CPUn_REG(n) + 0xE064)

#define IFX_CPU_PROTSFRSE(n) (IFX_CPUn_REG(n) + 0xE088)
#define IFX_CPU_ACCENSFRCFG_WRA(n) (IFX_CPUn_REG(n) + 0xE0A0)

#define IFX_CPU_PROTSTMSE(n) (IFX_CPUn_REG(n) + 0xE0D8)
#define IFX_CPU_ACCENSTMCFG_WRA(n) (IFX_CPUn_REG(n) + 0xE0E0)
#define IFX_CPU_PROT_RANGE(c)	(c << 8)

extern const unsigned int _flash_banks[];

static void tricore_enable_sfr_access(int n)
{
	uintptr_t reg;

	reg = IFX_CPU_PROTSFRSE(n);
	putreg32(0, reg);
	reg = IFX_CPU_ACCENSFRCFG_WRA(n);
	putreg32(0xFFFFFFFFU, reg);

	/* allow all range 0-7 range */
	reg = IFX_CPU_PROTSTMSE(n);
	putreg32(IFX_CPU_PROT_RANGE(0x7), reg);
	reg = IFX_CPU_ACCENSTMCFG_WRA(n);
	putreg32(0xFFFFFFFFU, reg);

	/* allow all range 0-15 range */
	reg = IFX_CPU_PROTDLMUSE(n);
	putreg32(IFX_CPU_PROT_RANGE(0xF), reg);
	reg = IFX_CPU_ACCENDLMUCFG_WRA(n);
	putreg32(0xFFFFFFFFU, reg);
	reg = IFX_CPU_ACCENDLMUCFG_WRB(n);
	putreg32(0xFFFFFFFFU, reg);

	/* allow all range 0-15 range */
	reg = IFX_CPU_PROTSPRSE(n);
	putreg32(IFX_CPU_PROT_RANGE(0xF), reg);
	reg = IFX_CPU_ACCENSPRCFG_WRA(n);
	putreg32(0xFFFFFFFFU, reg);
	reg = IFX_CPU_ACCENSPRCFG_WRB(n);
	putreg32(0xFFFFFFFFU, reg);
}

void tricore_start(int current_cpu)
{
	uintptr_t hreg;
	uintptr_t pcreg;


#if defined(CPU) && (CPU == 0) && defined(CONFIG_ARCH_CHIP_FAMILY_TC4XX)
	if (current_cpu == 0) {
		for (int i = 1; i < CONFIG_TRICORE_AMP_NUM_CPUS; i++) {
			hreg = IFX_CPU_BOOTCON(i);
			if (getreg32(hreg)) {
				pcreg = IFX_CPU_PC(i);
				putreg32(_flash_banks[i], pcreg);
				modreg32(0, IFX_CPU_HALT, hreg);
			}
		}
	}
#elif defined(CPU) && (CPU == 0) && defined(CONFIG_ARCH_CHIP_FAMILY_TC3XX)
	(void)hreg;
	if (current_cpu == 0) {
		for (int i = 1; i < CONFIG_TRICORE_AMP_NUM_CPUS; i++) {
			uintptr_t base = g_tc3xx_cpu_base[i];
			uint32_t syscon;

			pcreg = base + 0x1FE08;
			putreg32(_flash_banks[i], pcreg);

			syscon = getreg32(base + 0x1FE14);
			if (syscon & IFX_CPU_SYSCON_BHALT) {
				putreg32(syscon & ~IFX_CPU_SYSCON_BHALT,
					 base + 0x1FE14);
			}
		}
	}
#else
	(void)current_cpu;
	(void)hreg;
	(void)pcreg;
#endif

#if defined(CPU) && (CPU != 0)
	{
		extern int amp_secondary_main(int argc, char *argv[]);
		amp_secondary_main(0, NULL);
		for (;;) ;
	}
#else
	nx_start();
#endif
}
