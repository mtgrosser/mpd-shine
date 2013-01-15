/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
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

#include "config.h"
#include "encoder_api.h"
#include "encoder_plugin.h"
#include "fifo_buffer.h"
#include "growing_fifo.h"

#include <assert.h>
#include <string.h>
#include <shine.h>

struct shine_encoder {
	struct encoder encoder;

	struct fifo_buffer *buffer;
};

extern const struct encoder_plugin shine_encoder_plugin;

static inline GQuark
shine_encoder_quark(void)
{
	return g_quark_from_static_string("shine_encoder");
}

static struct encoder *
shine_encoder_init(G_GNUC_UNUSED const struct config_param *param,
		  G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder;

	encoder = g_new(struct shine_encoder, 1);
	encoder_struct_init(&encoder->encoder, &shine_encoder_plugin);

	return &encoder->encoder;
}

static void
shine_encoder_finish(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	g_free(encoder);
}

static void
shine_encoder_close(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	fifo_buffer_free(encoder->buffer);
}


static bool
shine_encoder_open(struct encoder *_encoder,
		  G_GNUC_UNUSED struct audio_format *audio_format,
		  G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	encoder->buffer = growing_fifo_new();
	return true;
}

static bool
shine_encoder_write(struct encoder *_encoder,
		   const void *data, size_t length,
		   G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	growing_fifo_append(&encoder->buffer, data, length);
	return length;
}

static size_t
shine_encoder_read(struct encoder *_encoder, void *dest, size_t length)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	size_t max_length;
	const void *src = fifo_buffer_read(encoder->buffer, &max_length);
	if (src == NULL)
		return 0;

	if (length > max_length)
		length = max_length;

	memcpy(dest, src, length);
	fifo_buffer_consume(encoder->buffer, length);
	return length;
}

const struct encoder_plugin shine_encoder_plugin = {
	.name = "shine",
	.init = shine_encoder_init,
	.finish = shine_encoder_finish,
	.open = shine_encoder_open,
	.close = shine_encoder_close,
	.write = shine_encoder_write,
	.read = shine_encoder_read,
};
