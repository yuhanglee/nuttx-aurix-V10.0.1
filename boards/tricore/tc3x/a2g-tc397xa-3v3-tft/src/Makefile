############################################################################
# boards/tricore/tc3x/a2g-tc397xa-3v3-tft/src/Makefile
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

CSRCS = tc397_appinit.c

# AMP secondary CPU init thread (used when INIT_ENTRYPOINT=amp_secondary_main)
CSRCS += tc397_amp_secondary.c

ifeq ($(CONFIG_ARCH_LEDS),y)
CSRCS += tc397_autoleds.c
endif

ifeq ($(CONFIG_USERLED),y)
CSRCS += tc397_userleds.c
endif

ifeq ($(CONFIG_TC3X_I2C),y)
CSRCS += tc397_i2c.c
endif

ifeq ($(CONFIG_CAN_MCAN),y)
CSRCS += tc397_can.c
endif

ifeq ($(CONFIG_TC3X_QSPI),y)
CSRCS += tc397_qspi.c
endif

include $(TOPDIR)/boards/Board.mk
