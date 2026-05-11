/****************************************************************************
 * boards/tricore/tc3xx/a2g-tc397-5v-trb/src/tc397_can.c
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#include "tc3xx_gpio.h"
#include "tricore_mcmcan.h"

//TODO all in Kconfig
#define CONFIG_MCMCAN0_BASE_ADDR            0xF0200000
#define CONFIG_MCMCAN0_NODE0_BASE_ADDR      0xF0208200
#define CONFIG_MCMCAN0_NODE0_MRAM_ADDR      0xF0200000
#define CONFIG_MCMCAN0_NODE0_MRAM_SIZE      0x8000

#define CONFIG_MCMCAN0_NODE0_IRQ_LINE0      364
#define CONFIG_MCMCAN0_NODE0_IRQ_LINE1      365

#ifndef CONFIG_MCMCAN0_NODE0_BITRATE
#  define CONFIG_MCMCAN0_NODE0_BITRATE      CONFIG_CAN_MCAN_BITRATE
#endif
#ifndef CONFIG_MCMCAN0_NODE0_SAMPLE_POINT
#  define CONFIG_MCMCAN0_NODE0_SAMPLE_POINT 875
#endif

#define CONFIG_MCMCAN0_NODE0_STD_FILTERS    32
#define CONFIG_MCMCAN0_NODE0_EXT_FILTERS    32
#define CONFIG_MCMCAN0_NODE0_RX_FIFO0       32
#define CONFIG_MCMCAN0_NODE0_RX_FIFO1       32
#define CONFIG_MCMCAN0_NODE0_TX_EVENTS      16
#define CONFIG_MCMCAN0_NODE0_TX_SLOTS       16
#define CONFIG_MCMCAN0_NODE0_NPCR           0x1

#define CONFIG_MCMCAN1_BASE_ADDR            0xF0210000
#define CONFIG_MCMCAN1_NODE0_BASE_ADDR      0xF0218200
#define CONFIG_MCMCAN1_NODE0_MRAM_ADDR      0xF0210000
#define CONFIG_MCMCAN1_NODE0_MRAM_SIZE      0x4000

#define CONFIG_MCMCAN1_NODE0_IRQ_LINE0      380
#define CONFIG_MCMCAN1_NODE0_IRQ_LINE1      381

#ifndef CONFIG_MCMCAN1_NODE0_BITRATE
#  define CONFIG_MCMCAN1_NODE0_BITRATE      CONFIG_CAN_MCAN_BITRATE
#endif
#ifndef CONFIG_MCMCAN1_NODE0_SAMPLE_POINT
#  define CONFIG_MCMCAN1_NODE0_SAMPLE_POINT 875
#endif

#define CONFIG_MCMCAN1_NODE0_STD_FILTERS    32
#define CONFIG_MCMCAN1_NODE0_EXT_FILTERS    32
#define CONFIG_MCMCAN1_NODE0_RX_FIFO0       32
#define CONFIG_MCMCAN1_NODE0_RX_FIFO1       32
#define CONFIG_MCMCAN1_NODE0_TX_EVENTS      16
#define CONFIG_MCMCAN1_NODE0_TX_SLOTS       16
#define CONFIG_MCMCAN1_NODE0_NPCR           0x2

/* MCMCAN0 node 0 — data-phase fallbacks */
#ifdef CONFIG_CAN_FD_MODE
#  ifndef CONFIG_MCMCAN0_NODE0_BITRATE_DATA
#    define CONFIG_MCMCAN0_NODE0_BITRATE_DATA      CONFIG_CAN_MCAN_BITRATE_DATA
#  endif
#  ifndef CONFIG_MCMCAN0_NODE0_SAMPLE_POINT_DATA
#    define CONFIG_MCMCAN0_NODE0_SAMPLE_POINT_DATA 875
#  endif
#endif

/* MCMCAN1 node 0 — data-phase fallbacks */
#ifdef CONFIG_CAN_FD_MODE
#  ifndef CONFIG_MCMCAN1_NODE0_BITRATE_DATA
#    define CONFIG_MCMCAN1_NODE0_BITRATE_DATA      CONFIG_CAN_MCAN_BITRATE_DATA
#  endif
#  ifndef CONFIG_MCMCAN1_NODE0_SAMPLE_POINT_DATA
#    define CONFIG_MCMCAN1_NODE0_SAMPLE_POINT_DATA 875
#  endif
#endif

static struct can_dev_s g_can0_dev;
static struct can_dev_s g_can1_dev;

CAN_MCAN_CALLBACKS_DEFINE(g_can0_cbs,
                          CONFIG_MCMCAN0_NODE0_TX_SLOTS,
                          CONFIG_MCMCAN0_NODE0_STD_FILTERS,
                          CONFIG_MCMCAN0_NODE0_EXT_FILTERS);

CAN_MCAN_CALLBACKS_DEFINE(g_can1_cbs,
                          CONFIG_MCMCAN1_NODE0_TX_SLOTS,
                          CONFIG_MCMCAN1_NODE0_STD_FILTERS,
                          CONFIG_MCMCAN1_NODE0_EXT_FILTERS);

static const struct mcmcan_config g_mcmcan0_module =
{
  .base       = CONFIG_MCMCAN0_BASE_ADDR,
  .node_pin   =
    {
      [0] = { .present = true,  .npcr_val = CONFIG_MCMCAN0_NODE0_NPCR },
      [1] = { .present = false, .npcr_val = 0 },
      [2] = { .present = false, .npcr_val = 0 },
      [3] = { .present = false, .npcr_val = 0 },
    },
};

static const struct mcmcan_config g_mcmcan1_module =
{
  .base       = CONFIG_MCMCAN1_BASE_ADDR,
  .node_pin   =
    {
      [0] = { .present = true,  .npcr_val = CONFIG_MCMCAN1_NODE0_NPCR },
      [1] = { .present = false, .npcr_val = 0 },
      [2] = { .present = false, .npcr_val = 0 },
      [3] = { .present = false, .npcr_val = 0 },
    },
};

static struct mcmcan_node_priv_s g_can0_priv =
{
  .node_base     = CONFIG_MCMCAN0_NODE0_BASE_ADDR,
  .mram_base     = CONFIG_MCMCAN0_NODE0_MRAM_ADDR,
  .irq_line0     = CONFIG_MCMCAN0_NODE0_IRQ_LINE0,
  .irq_line1     = CONFIG_MCMCAN0_NODE0_IRQ_LINE1,
  .module_config = &g_mcmcan0_module,
  .initialized   = false,

  .mcan_config =
    {
      .callbacks    = &g_can0_cbs,
      .bitrate      = CONFIG_MCMCAN0_NODE0_BITRATE,
      .sample_point = CONFIG_MCMCAN0_NODE0_SAMPLE_POINT,
      .module_clock_hz = 80000000u,
#ifdef CONFIG_CAN_FD_MODE
      .bitrate_data      = CONFIG_MCMCAN0_NODE0_BITRATE_DATA,
      .sample_point_data = CONFIG_MCMCAN0_NODE0_SAMPLE_POINT_DATA,
#endif
      .phy_present  = false,
      .mram_elements =
        {
          0,
          CONFIG_MCMCAN0_NODE0_STD_FILTERS,
          CONFIG_MCMCAN0_NODE0_EXT_FILTERS,
          CONFIG_MCMCAN0_NODE0_RX_FIFO0,
          CONFIG_MCMCAN0_NODE0_RX_FIFO1,
          0,
          CONFIG_MCMCAN0_NODE0_TX_EVENTS,
          CONFIG_MCMCAN0_NODE0_TX_SLOTS,
        },
    },
};

static struct mcmcan_node_priv_s g_can1_priv =
{
  .node_base     = CONFIG_MCMCAN1_NODE0_BASE_ADDR,
  .mram_base     = CONFIG_MCMCAN1_NODE0_MRAM_ADDR,
  .irq_line0     = CONFIG_MCMCAN1_NODE0_IRQ_LINE0,
  .irq_line1     = CONFIG_MCMCAN1_NODE0_IRQ_LINE1,
  .module_config = &g_mcmcan1_module,
  .initialized   = false,

  .mcan_config =
    {
      .callbacks    = &g_can1_cbs,
      .bitrate      = CONFIG_MCMCAN1_NODE0_BITRATE,
      .sample_point = CONFIG_MCMCAN1_NODE0_SAMPLE_POINT,
      .module_clock_hz = 80000000u,
#ifdef CONFIG_CAN_FD_MODE
      .bitrate_data      = CONFIG_MCMCAN1_NODE0_BITRATE_DATA,
      .sample_point_data = CONFIG_MCMCAN1_NODE0_SAMPLE_POINT_DATA,
#endif
      .phy_present  = false,
      .mram_elements =
        {
          0,
          CONFIG_MCMCAN1_NODE0_STD_FILTERS,
          CONFIG_MCMCAN1_NODE0_EXT_FILTERS,
          CONFIG_MCMCAN1_NODE0_RX_FIFO0,
          CONFIG_MCMCAN1_NODE0_RX_FIFO1,
          0,
          CONFIG_MCMCAN1_NODE0_TX_EVENTS,
          CONFIG_MCMCAN1_NODE0_TX_SLOTS,
        },
    },
};


static void tc397_config_can_pins(void)
{
  gpio_pinset_t pins[] =
    {
      /* CAN0 TX outputs */
      AURIX_GPIO_FULL(GPIO_PORT20, GPIO_PIN8, GPIO_PERIPH, GPIO_ALT5,
      0, 0, 0, 0, GPIO_PERIPH_OWN_PAD),
      /* CAN0 RX outputs */
      AURIX_GPIO_FULL(GPIO_PORT20, GPIO_PIN7, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 0, 0, GPIO_PERIPH_OWN_PAD),

      /* CAN1 TX outputs */
      AURIX_GPIO_FULL(GPIO_PORT23, GPIO_PIN1, GPIO_PERIPH, GPIO_ALT5,
      0, 0, 0, 0, GPIO_PERIPH_OWN_PAD),
      /* CAN1 RX outputs */
      AURIX_GPIO_FULL(GPIO_PORT23, GPIO_PIN0, GPIO_INPUT, GPIO_PULL_NONE,
      0, 0, 0, 0, GPIO_PERIPH_OWN_PAD),
    };

  for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++)
    {
      aurix_config_gpio(pins[i]);
    }
}


int mcmcan_board_initialize(void)
{
  int err;

  tc397_config_can_pins();

  /* CAN0 module 0 node 0 */
  err = mcmcan_module_init(&g_mcmcan0_module);
  if (err != 0)
    {
      _err("MCMCAN module 0 init failed: %d\n", err);
      return err;
    }

  mcmcan_node_initialize(&g_can0_priv, &g_can0_dev);
  err = can_register("/dev/can0", &g_can0_dev);
  if (err != 0)
    {
      _err("can_register /dev/can0 failed: %d\n", err);
      return err;
    }

  /* CAN1 module 1 node 0 */
  err = mcmcan_module_init(&g_mcmcan1_module);
  if (err != 0)
    {
      _err("MCMCAN module 1 init failed: %d\n", err);
      return OK;  /* CAN0 still functional */
    }

  mcmcan_node_initialize(&g_can1_priv, &g_can1_dev);
  err = can_register("/dev/can1", &g_can1_dev);
  if (err != 0)
    {
      _err("can_register /dev/can1 failed: %d\n", err);
    }

  return OK;
}
