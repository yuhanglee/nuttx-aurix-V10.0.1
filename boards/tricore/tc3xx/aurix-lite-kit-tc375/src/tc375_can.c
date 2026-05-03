/****************************************************************************
 * boards/tricore/tc3xx/aurix-lite-kit-tc375/src/tc375_can.c
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

#define MCMCAN_BASE_ADDR             0xF0200000
#define MCMCAN_NODE0_BASE_ADDR       0xF0208200
#define MCMCAN_NODE0_MRAM_ADDR       0xF0200000
#define MCMCAN_NODE0_MRAM_SIZE       0x8000

#define MCMCAN_NODE0_IRQ_LINE0       364
#define MCMCAN_NODE0_IRQ_LINE1       365

#ifndef CONFIG_MCMCAN_NODE0_BITRATE
#  define CONFIG_MCMCAN_NODE0_BITRATE       CONFIG_CAN_MCAN_BITRATE
#endif
#ifndef CONFIG_MCMCAN_NODE0_SAMPLE_POINT
#  define CONFIG_MCMCAN_NODE0_SAMPLE_POINT  875
#endif
#define MCMCAN_NODE0_STD_FILTERS     32
#define MCMCAN_NODE0_EXT_FILTERS     32
#define MCMCAN_NODE0_RX_FIFO0        32
#define MCMCAN_NODE0_RX_FIFO1        32
#define MCMCAN_NODE0_TX_EVENTS       16
#define MCMCAN_NODE0_TX_SLOTS        16
#define MCMCAN_NODE0_NPCR            0x1

static struct can_dev_s g_node0_candev;

CAN_MCAN_CALLBACKS_DEFINE(g_node0_cbs,
                          MCMCAN_NODE0_TX_SLOTS,
                          MCMCAN_NODE0_STD_FILTERS,
                          MCMCAN_NODE0_EXT_FILTERS);

static const struct mcmcan_config g_mcmcan_module =
{
  .base       = MCMCAN_BASE_ADDR,
  .node_pin   =
    {
      [0] = { .present = true,  .npcr_val = MCMCAN_NODE0_NPCR },
      [1] = { .present = false, .npcr_val = 0 },
      [2] = { .present = false, .npcr_val = 0 },
      [3] = { .present = false, .npcr_val = 0 },
    },
};

static struct mcmcan_node_priv_s g_node0_priv =
{
  .node_base     = MCMCAN_NODE0_BASE_ADDR,
  .mram_base     = MCMCAN_NODE0_MRAM_ADDR,
  .irq_line0     = MCMCAN_NODE0_IRQ_LINE0,
  .irq_line1     = MCMCAN_NODE0_IRQ_LINE1,
  .module_config = &g_mcmcan_module,
  .initialized   = false,

  .mcan_config =
    {
      .callbacks    = &g_node0_cbs,
      .bitrate      = CONFIG_MCMCAN_NODE0_BITRATE,
      .sample_point = CONFIG_MCMCAN_NODE0_SAMPLE_POINT,
      .module_clock_hz = 80000000u,
#ifdef CONFIG_CAN_FD_MODE
      .bitrate_data      = CONFIG_MCMCAN_NODE0_BITRATE_DATA,
      .sample_point_data = CONFIG_MCMCAN_NODE0_SAMPLE_POINT_DATA,
#endif
      .phy_present  = false,
      .mram_elements =
        {
          0,
          MCMCAN_NODE0_STD_FILTERS,
          MCMCAN_NODE0_EXT_FILTERS,
          MCMCAN_NODE0_RX_FIFO0,
          MCMCAN_NODE0_RX_FIFO1,
          0,
          MCMCAN_NODE0_TX_EVENTS,
          MCMCAN_NODE0_TX_SLOTS,
        },
    },
};

#ifdef CONFIG_BOARD_CAN_TLE9251V_STB_PIN
static void tc375_tle9251v_normal_mode(void)
{
  gpio_pinset_t stb = AURIX_GPIO(20, 6, GPIO_OUTPUT, GPIO_ALT0);

  aurix_config_gpio(stb);
  aurix_gpio_write(stb, false);
}
#endif

static void tc375_config_can_pins(void)
{
  gpio_pinset_t pins[] =
    {
      AURIX_GPIO_FULL(GPIO_PORT20, GPIO_PIN8, GPIO_PERIPH, GPIO_ALT5,
        0, 0, 0, 0, GPIO_PERIPH_OWN_PAD),
      AURIX_GPIO_FULL(GPIO_PORT20, GPIO_PIN7, GPIO_INPUT, GPIO_PULL_NONE,
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

#ifdef CONFIG_BOARD_CAN_TLE9251V_STB_PIN
  tc375_tle9251v_normal_mode();
#endif

  tc375_config_can_pins();
  err = mcmcan_module_init(&g_mcmcan_module);
  if (err != 0)
    {
      return err;
    }

  mcmcan_node_initialize(&g_node0_priv, &g_node0_candev);

  return can_register("/dev/can0", &g_node0_candev);
}
