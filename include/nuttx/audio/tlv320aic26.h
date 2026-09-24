/****************************************************************************
 * include/nuttx/audio/tlv320aic26.h
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

#ifndef __INCLUDE_NUTTX_AUDIO_TLV320AIC26_H
#define __INCLUDE_NUTTX_AUDIO_TLV320AIC26_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/spi/spi.h>
#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>

#if defined(CONFIG_AUDIO) && defined(CONFIG_AUDIO_TLV320AIC26)

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: tlv320aic26_initialize
 *
 * Description:
 *   Initialise the TLV320AIC26 codec and return its audio lower-half
 *   interface.  The codec is controlled over SPI and streams audio over the
 *   supplied I2S device.
 *
 * Input Parameters:
 *   spi       - SPI bus the codec control port is attached to.
 *   devid     - SPI device id used for chip-select.
 *   i2s       - I2S device carrying the audio data.
 *   mclk_freq - Codec MCLK frequency in Hz.
 *
 * Returned Value:
 *   A new audio lower-half interface on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct audio_lowerhalf_s *
tlv320aic26_initialize(FAR struct spi_dev_s *spi, uint32_t devid,
                       FAR struct i2s_dev_s *i2s, uint32_t mclk_freq);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* CONFIG_AUDIO && CONFIG_AUDIO_TLV320AIC26 */
#endif /* __INCLUDE_NUTTX_AUDIO_TLV320AIC26_H */
