/*
 * Copyright (C) 2003-2013 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPD_SHINE_ENCODER_API_H
#define MPD_SHINE_ENCODER_API_H

#include "config.h"
#include "encoder_api.h"
#include "encoder_plugin.h"
#include "audio_format.h"

#include <shine/layer3.h>

#define SAMPLES_PER_FRAME 1152

struct shine_encoder {
        struct encoder encoder;

        struct audio_format audio_format;
        int bitrate;

        shine_config_t shine_config;
        shine_t shine;

        int16_t pcm_buffer[32768];
        size_t pcm_buffer_length;

	int16_t working_buffer[2][samp_per_frame];

        unsigned char mpeg_buffer[32768];
        size_t mpeg_buffer_length;
};

static bool
shine_mpeg_buffer_push(struct shine_encoder *encoder,
                        const void *data, size_t length,
                        G_GNUC_UNUSED GError **error);


static size_t
shine_pcm_buffer_shift(struct shine_encoder *encoder, bool flush);


static void
shine_encoder_close(struct encoder *_encoder);

#endif
