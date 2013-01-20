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

#include "shine_encoder.h"

#include <assert.h>
#include <string.h>

extern const struct encoder_plugin shine_encoder_plugin;

static inline GQuark
shine_encoder_quark(void)
{
	return g_quark_from_static_string("shine_encoder");
}

static bool
shine_encoder_configure(struct shine_encoder *encoder,
			const struct config_param *param, GError **error)
{
	const char *value;
	char *endptr;

	value = config_get_block_string(param, "bitrate", NULL);
	if (value != NULL) {
		/* a bit rate was configured */
		encoder->bitrate = g_ascii_strtoll(value, &endptr, 10);

		if (*endptr != '\0' || encoder->bitrate <= 0) {
			g_set_error(error, shine_encoder_quark(), 0,
				    "bitrate at line %i should be a positive integer",
				    param->line);
			return false;
		}

	} else {
		g_set_error(error, shine_encoder_quark(), 0,
				    "no bitrate defined "
				    "at line %i",
				    param->line);
			return false;
	}
	g_message("%s", "shine_encoder_configure");
	return true;
}

static struct encoder *
shine_encoder_init(const struct config_param *param, GError **error)
{
	struct shine_encoder *encoder;

	encoder = g_new(struct shine_encoder, 1);
	encoder_struct_init(&encoder->encoder, &shine_encoder_plugin);

	/* load configuration from "param" */
	if (!shine_encoder_configure(encoder, param, error)) {
		/* configuration has failed, roll back and return error */
		g_free(encoder);
		return NULL;
	}
g_message("%s", "shine_encoder_init");
	return &encoder->encoder;
}

static void
shine_encoder_finish(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;
g_message("%s", "shine_encoder_finish");
	g_free(encoder);
}


static bool
shine_encoder_setup(struct shine_encoder *encoder, GError **error)
{
	L3_set_config_mpeg_defaults(&encoder->shine_config.mpeg);

	encoder->shine_config.wave.channels = encoder->audio_format.channels;
	encoder->shine_config.wave.samplerate = (long)encoder->audio_format.sample_rate;
	if (encoder->shine_config.wave.channels == 2)
		encoder->shine_config.mpeg.mode = STEREO;
	else
		encoder->shine_config.mpeg.mode = MONO;

	encoder->shine_config.mpeg.bitr = encoder->bitrate;
	g_message("shine_encoder_setup, %u channels, samplerate %u Hz, bitrate %u kbit/s, mode %u",  encoder->shine_config.wave.channels, encoder->shine_config.wave.samplerate, encoder->shine_config.mpeg.bitr, encoder->shine_config.mpeg.mode);

	/* Check channels */
	if (encoder->audio_format.channels != 1 && encoder->audio_format.channels != 2) {
		g_set_error(error, shine_encoder_quark(), 0, "Stereo or mono stream required");
		return false;
	}

	/* Check samplerate */
	if (L3_find_samplerate_index(encoder->shine_config.wave.samplerate) < 0) {
		g_set_error(error, shine_encoder_quark(), 0, "Invalid samplerate");
		return false;
	}

	/* See if bitrate is valid */
	if (L3_find_bitrate_index(encoder->shine_config.mpeg.bitr) < 0) {
		g_set_error(error, shine_encoder_quark(), 0, "Invalid bitrate");
		return false;
	}

	return true;
}

static bool
shine_encoder_open(struct encoder *_encoder, struct audio_format *audio_format,
		GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	audio_format->format = SAMPLE_FORMAT_S16;
	audio_format->channels = 2;
	audio_format->sample_rate = 44100;
	encoder->audio_format = *audio_format;
	if (!shine_encoder_setup(encoder, error)) {
		return false;
	}

	encoder->shine = L3_initialise(&encoder->shine_config);

	if (encoder->shine == NULL) {
		g_set_error(error, shine_encoder_quark(), 0,
			    "L3_initialise() failed");
		return false;
	}
 g_message("encoder_open: %u", encoder->shine);

	return true;
}

static void
shine_encoder_close(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;
g_message("%s", "shine_encoder_close");
	L3_close(encoder->shine);
}

static bool
shine_encoder_flush(struct encoder *_encoder, G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	unsigned char *encoded_data;
	long encoded_length;

	encoded_data = L3_flush(encoder->shine, &encoded_length);

	if(encoded_data != NULL)
g_message("%s", "shine_encoder_flush");
		shine_mpeg_buffer_push(encoder, encoded_data, encoded_length, error);
	return true;
}

static bool
shine_pcm_buffer_push(struct shine_encoder *encoder,
			const void *data, size_t length,
			G_GNUC_UNUSED GError **error)
{
	if (encoder->pcm_buffer_length + length <= sizeof(encoder->pcm_buffer)) {
//g_message("shine_pcm_buffer_push(length: %u, pcm_buffer_length: %u)", length, encoder->pcm_buffer_length);
		/* append */
		memcpy(encoder->pcm_buffer + encoder->pcm_buffer_length, data, length);
		encoder->pcm_buffer_length += length;
		return true;
	} else {
		 g_set_error(error, shine_encoder_quark(), 0,
	                            "Shine PCM buffer overflow");
		return false;
	}
}

static size_t
shine_pcm_buffer_shift(struct shine_encoder *encoder, bool flush)
{
	const int16_t *src = encoder->pcm_buffer;
	size_t mpeg_frame_size, pcm_frame_size, pcm_length;
	unsigned int k, pcm_frames;

	/* raw PCM frame size: 4 bytes for stereo 16 bit */
	pcm_frame_size = audio_format_frame_size(&encoder->audio_format);
	/* size of the PCM data required for one MPEG frame */
	mpeg_frame_size = SAMPLES_PER_FRAME * pcm_frame_size;

	if (encoder->pcm_buffer_length < mpeg_frame_size) {
		/* insufficient amount of PCM data */

		if (!flush) return 0;
		/* flush requested, but incomplete MPEG frame - requires padding */
		pcm_frames = (unsigned int)(encoder->pcm_buffer_length / audio_format_frame_size(&encoder->audio_format));
	} else {
		/* full MPEG frame - put it into working buffer and shift PCM buffer */
		pcm_frames = SAMPLES_PER_FRAME; /* (unsigned int)(2 * SAMPLES_PER_FRAME * encoder->audio_format.channels); */
			// g_message("shift_buffer pcm_buffer_length: %u pcm_frames: %u (full frame)", encoder->pcm_buffer_length, pcm_frames);
	}

	assert(pcm_frames <= SAMPLES_PER_FRAME);

	/* de-interleave and copy PCM data into working buffer */
	for(k = 0; k < pcm_frames; k++) {
		if (encoder->audio_format.channels == 1) {
			/* mono */
			encoder->working_buffer[0][k] = *src++;
			encoder->working_buffer[1][k] = 0;
		} else {
			/* stereo */
			encoder->working_buffer[0][k] = *src++;
			encoder->working_buffer[1][k] = *src++;
                }
	}

	/* pad */
	for(k = pcm_frames; k < SAMPLES_PER_FRAME; k++) {
		encoder->working_buffer[0][k] = 0;
		encoder->working_buffer[1][k] = 0;
	}

	// g_message("deinterleaved %u", pcm_frames);

	/* shift PCM buffer */
	pcm_length = pcm_frames * pcm_frame_size;
	encoder->pcm_buffer_length -= pcm_length;
	memmove(encoder->pcm_buffer, encoder->pcm_buffer + mpeg_frame_size, mpeg_frame_size);

	return encoder->pcm_buffer_length;
}

static bool
shine_mpeg_buffer_push(struct shine_encoder *encoder,
                        const void *data, size_t length,
                        G_GNUC_UNUSED GError **error)
{
        if (encoder->mpeg_buffer_length + length <= sizeof(encoder->mpeg_buffer)) {
//g_message("shine_mpeg_buffer_push(length: %u)", length);

                /* append */
                memcpy(encoder->mpeg_buffer + encoder->mpeg_buffer_length, data, length);
                encoder->mpeg_buffer_length += length;
                return true;
        } else {
                 g_set_error(error, shine_encoder_quark(), 0,
                                    "Shine MPEG buffer overflow");
                return false;
        }
}

static bool
shine_encoder_write(struct encoder *_encoder,
			const void *data, size_t length,
			G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	long encoded_length;
	unsigned char *encoded_data;

	/* push new data to PCM buffer */
	if (!shine_pcm_buffer_push(encoder, data, length, error)) {
		L3_close(encoder->shine);
		return false;
	}
//g_message("pushed to pcm buffer, length now: %u", encoder->pcm_buffer_length);
	/* work off PCM buffer by MPEG frame-sized chunks */
	while (shine_pcm_buffer_shift(encoder, false) > 0) {
//g_message("after shift, pcm_buf_len %u, will encode now", encoder->pcm_buffer_length);
		/* Encode the MPEG frame */
		encoded_data = L3_encode_frame(encoder->shine, encoder->working_buffer, &encoded_length);
//g_message("encoded data length: %u  data: %u", encoded_length, encoded_data);
		if (encoded_data != NULL && encoded_length > 0) {
			/* Append MPEG data to buffer */
			if (!shine_mpeg_buffer_push(encoder, encoded_data, encoded_length, error)) {
				L3_close(encoder->shine);
				return false;
			}
		}
	}

	return true;
}

static size_t
shine_encoder_read(struct encoder *_encoder, void *dest, size_t length)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	if (length > encoder->mpeg_buffer_length)
		length = encoder->mpeg_buffer_length;

	if (length == 0)
		return length;

//g_message("shine_encoder_read(length: %u)", length);

	memcpy(dest, encoder->mpeg_buffer, length);
	encoder->mpeg_buffer_length -= length;

	memmove(encoder->mpeg_buffer, encoder->mpeg_buffer + length,
		encoder->mpeg_buffer_length);

	return length;
}

static const char *
shine_encoder_get_mime_type(G_GNUC_UNUSED struct encoder *_encoder)
{
	return "audio/mpeg";
}

const struct encoder_plugin shine_encoder_plugin = {
	.name = "shine",
	.init = shine_encoder_init,
	.finish = shine_encoder_finish,
	.open = shine_encoder_open,
	.close = shine_encoder_close,
	.end = shine_encoder_flush,
	.flush = shine_encoder_flush,
	.write = shine_encoder_write,
	.read = shine_encoder_read,
	.get_mime_type = shine_encoder_get_mime_type,
};
