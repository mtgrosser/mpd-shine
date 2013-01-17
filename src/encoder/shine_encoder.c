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

	return &encoder->encoder;
}

static void
shine_encoder_finish(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	g_free(encoder);
}


static bool
shine_encoder_setup(struct shine_encoder *encoder, GError **error)
{
	encoder->shine_config.wave.channels = 2; /* encoder.audio_format->channels; */
	encoder->shine_config.wave.samplerate = 44100; /* (long)encoder.audio_format->samplerate:*/
	encoder->shine_config.mpeg.mode = JOINT_STEREO;
	encoder->shine_config.mpeg.bitrate = encoder.bitrate;

	/* TODO: return errors */
	return true;
}

static bool
shine_encoder_open(struct encoder *_encoder, struct audio_format *audio_format,
		GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	audio_format->format = SAMPLE_FORMAT_S16;
	audio_format->channels = 2;
	audio_format->samplerate = 44100;
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

	return true;
}

static void
shine_encoder_close(struct encoder *_encoder)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	L3_close(encoder->shine);
}

static bool
shine_pcm_buffer_push(struct shine_encoder *encoder,
			const void *data, size_t length,
			G_GNUC_UNUSED GError **error)
{
	if (encoder->pcm_buffer_length + length <= sizeof(encoder->pcm_buffer)) {
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
shine_pcm_buffer_shift(struct shine_encoder *encoder)
{
        const int16_t *src = &encoder->pcm_buffer;
	size_t mpeg_frame_size, pcm_length;
	unsigned int k, pcm_frames;

	assert(encoder->working_buffer_length == 0);

	/* PCM frame size: 4 for stereo 16 bit */
	mpeg_frame_size = SAMPLES_PER_FRAME * audio_format_frame_size(&encoder->audio_format);

	if (encoder->pcm_buffer_length < audio_format_frame_size(&encoder->audio_format)) {
		/* discard incomplete PCM frame at the end of the buffer */
		return 0;
	} else {
		/* put one frame into working buffer and shift buffer */

		if (encoder->pcm_buffer_length >= mpeg_frame_size) {
			/* full MPEG frame */
			pcm_frames = (unsigned int)(SAMPLES_PER_FRAME * encoder.audio_format->channels);
		} else {
			/* incomplete MPEG frame, requires padding */
			pcm_frames = (unsigned int)(encoder->pcm_buffer_length / audio_format_frame_size(&encoder->audio_format));
		}

		/* de-interleave and copy PCM data into working buffer */
		for(k = 0; k < pcm_frames; k++) {
			if (encoder.audio_format->channels == 1) {
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

		/* shift PCM buffer */
		pcm_length = pcm_frames * audio_format_frame_size(&encoder->audio_format);
		encoder->pcm_buffer_length -= pcm_length;
		memmove(encoder->pcm_buffer, encoder->pcm_buffer + mpeg_frame_size, sizeof(encoder->pcm_buffer) - mpeg_frame_size);
		return(encoder->pcm_buffer_length);
	}
}

static bool
shine_encoder_write(struct encoder *_encoder,
			const void *data, size_t length,
			G_GNUC_UNUSED GError **error)
{
	struct shine_encoder *encoder = (struct shine_encoder *)_encoder;

	const int16_t *src = (const int16_t*)data;
	// int bytes_out;
	long encoded_length;
	unsigned num_pcm_frames;
	unsigned int i, k;
	unsigned char *encoded_data;

	assert(encoder->working_buffer_length == 0);

	num_pcm_frames =
		length / audio_format_frame_size(&encoder->audio_format); /* 4 for stereo 16 bit */

	// push new data to pcm buffer
	// work off pcm buffer framewise
	// push encoded frames to mpeg buffer

	if (!shine_pcm_buffer_push(encoder, data, length, error)) {
		L3_close(encoder->shine);
		return(false);
	}

	while (shine_pcm_buffer_shift(encoder) > 0) {
    data = L3_encode_frame(s,buffer,&written);
    write_mp3(written, data, &config);
  }




	if (num_samples + encoder->pcm_ 

	for (i = 0; i < num_frames; i++) {

		/* Copy single PCM frame to working buffer */
		for(k = 0; k < samp_per_frame; k++) {
			encoder->pcm_buffer[0][k] = *src++;
			encoder->pcm_buffer[1][k] = *src++;
		}

		/* Encode the frame */
	        encoded_data = L3_encode_frame(encoder->shine, encoder->pcm_buffer, &encoded_length);

		if (encoded_data == NULL) {
			g_set_error(error, shine_encoder_quark(), 0, "Shine encoder failed");
			return false;
	        }

		/* Copy MPEG data to buffer */
		memcpy(&encoder->mpeg_buffer + encoder->mpeg_buffer_length, encoder->pcm_buffer, encoded_length);
		encoder->mpeg_buffer_length += encoded_length;



while (wave_get(buffer, infile, &config)) {
    data = L3_encode_frame(s,buffer,&written);
    write_mp3(written, data, &config);  
  }

  /* Flush and write remaining data. */
  data = L3_flush(s,&written);



twolame_encode_buffer_interleaved(encoder->options,
						      src, num_frames,
						      encoder->buffer,
						      sizeof(encoder->buffer));
	if (bytes_out < 0) {
		g_set_error(error, twolame_encoder_quark(), 0,
			    "twolame encoder failed");
		return false;
	}

	encoder->buffer_length = (size_t)bytes_out;
	return true;


Encode audio data. Source data must have `samp_per_frames` audio samples per
 * channels. Mono encoder only expect one channel. 
 *
 * Returns a pointer to freshly encoded data while `written` contains the size of
 * available data. This pointer's memory is handled by the library and is only valid 
 * until the next call to `L3_encode_frame` or `L3_close` and may be NULL if no data
 * was written. */
unsigned char *L3_encode_frame(shine_t s, int16_t data[2][samp_per_frame], long *written);

/* Flush all data currently in the encoding buffer. Should be used before closing
 * the encoder, to make all encoded data has been written. */
unsigned char *L3_flush(shine_t s, long *written);





	data = L3_encode_frame(s,buffer,&written);
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
