# ##############################################################################
# boards/tricore/tc4x/a3g-tc4d7-lite/src/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_TRICORE_TOOLCHAIN_LLVM)
  set_property(
    GLOBAL PROPERTY LD_SCRIPT
                    "${NUTTX_BOARD_DIR}/scripts/tricore-llvm.ld")
else()
  set_property(
    GLOBAL PROPERTY LD_SCRIPT "${NUTTX_BOARD_DIR}/scripts/tricore.ld")
endif()

target_sources(board PRIVATE tc4d7_appinit.c tc4d7_amp_secondary.c)

if(CONFIG_ARCH_LEDS)
  target_sources(board PRIVATE tc4d7_autoleds.c)
endif()

if(CONFIG_USERLED)
  target_sources(board PRIVATE tc4d7_userleds.c)
endif()

if(CONFIG_TC4X_QSPI)
  target_sources(board PRIVATE tc4d7_qspi.c)
endif()

if(CONFIG_TC4X_I2C)
  target_sources(board PRIVATE tc4d7_i2c.c)
endif()

if(CONFIG_CAN_MCAN)
  target_sources(board PRIVATE tc4d7_can.c)
endif()

if(CONFIG_ARCH_BUTTONS)
  target_sources(board PRIVATE tc4d7_buttons.c)
endif()

if(CONFIG_TC4X_GETH)
  list(APPEND SRCS tc4d7_ethernet.c)
endif()
