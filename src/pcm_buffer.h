/*
 * Copyright (C) 2003-2009 The Music Player Daemon Project
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PCM_BUFFER_H
#define PCM_BUFFER_H

#include <glib.h>

/**
 * Manager for a temporary buffer which grows as needed.  We could
 * allocate a new buffer every time pcm_convert() is called, but that
 * would put too much stress on the allocator.
 */
struct pcm_buffer {
	char *buffer;

	size_t size;
};

/**
 * Initialize the buffer, but don't allocate anything yet.
 */
static inline void
pcm_buffer_init(struct pcm_buffer *buffer)
{
	buffer->buffer = NULL;
	buffer->size = 0;
}

/**
 * Free resources.  This function may be called more than once.
 */
static inline void
pcm_buffer_deinit(struct pcm_buffer *buffer)
{
	g_free(buffer->buffer);

	buffer->buffer = NULL;
}

/**
 * Get the buffer, and guarantee a minimum size.  This buffer becomes
 * invalid with the next pcm_buffer_get() call.
 */
static inline void *
pcm_buffer_get(struct pcm_buffer *buffer, size_t size)
{
	if (buffer->size < size) {
		/* free the old buffer */
		g_free(buffer->buffer);

		/* allocate a new buffer; align at 64kB boundaries */
		buffer->buffer = g_malloc((size | 0xffff) + 1);
	}

	return buffer->buffer;
}

#endif