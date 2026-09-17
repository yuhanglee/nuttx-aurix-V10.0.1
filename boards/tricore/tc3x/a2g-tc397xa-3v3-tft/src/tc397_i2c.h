/****************************************************************************
 * boards/tricore/tc3x/a2g-tc397xa-3v3-tft/src/tc397_i2c.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.
 *
 ****************************************************************************/

#ifndef __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_SRC_TC397_I2C_H
#define __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_SRC_TC397_I2C_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_TC3X_I2C

/****************************************************************************
 * Name: tc3x_i2cbus_initialize
 *
 * Description:
 *   Initialize one I2C bus.
 *
 * Input Parameters:
 *   port - I2C bus number (0 or 1)
 *
 * Returned Value:
 *   Valid i2c_master_s pointer on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct i2c_master_s *tc3x_i2cbus_initialize(int bus);

/****************************************************************************
 * Name: tc397_i2c_bringup
 *
 * Description:
 *   Board-level I2C initialization: init controllers, register /dev/i2cN.
 *   Call from board_app_initialize() or board_late_initialize().
 *
 ****************************************************************************/

int tc397_i2c_bringup(void);

#endif /* CONFIG_TC3X_I2C */
#endif /* __BOARDS_TRICORE_TC3X_A2G_TC397XA_3V3_TFT_SRC_TC397_I2C_H */
