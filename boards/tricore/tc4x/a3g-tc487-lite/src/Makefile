############################################################################
# boards/tricore/tc4x/a3g-tc487-lite/src/Makefile
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
# board_aurix_setup_serial_pin() weak override used at boot to configure
# the per-ASCLIN console pins.
CSRCS = tc487_appinit.c

ifeq ($(CONFIG_TC4X_I2S),y)
CSRCS += tc487_i2s.c
endif

ifeq ($(CONFIG_ARCH_LEDS),y)
CSRCS += tc487_autoleds.c
endif

ifeq ($(CONFIG_USERLED),y)
CSRCS += tc487_userleds.c
endif

ifeq ($(CONFIG_TC4X_QSPI),y)
CSRCS += tc487_qspi.c
endif

include $(TOPDIR)/boards/Board.mk
