############################################################################
# boards/tricore/tc4xx/a3g-tc4d7-lite/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################
include $(TOPDIR)/Make.defs

# Always compile the board init file: it provides the
# board_aurix_setup_serial_pin() weak override that the AMP secondary
# CPUs (which don't enable CONFIG_BOARDCTL) still need at boot to
# configure their per-ASCLIN pins.
CSRCS = tc4d7_appinit.c

# AMP secondary CPU init thread (used when INIT_ENTRYPOINT=amp_secondary_main)
CSRCS += tc4d7_amp_secondary.c

ifeq ($(CONFIG_TC4XX_I2C),y)
CSRCS += tc4xx_i2c_bringup.c
endif

ifeq ($(CONFIG_TC4XX_GETH),y)
CSRCS += tc4d7_ethernet.c
endif

ifeq ($(CONFIG_CAN),y)
CSRCS += tc4d7_can.c
endif

ifeq ($(CONFIG_ARCH_LEDS),y)
CSRCS += tc4d7_autoleds.c
endif

ifeq ($(CONFIG_USERLED),y)
CSRCS += tc4d7_userleds.c
endif

include $(TOPDIR)/boards/Board.mk
