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

#include "config.h"
#include "MusicBuffer.hxx"
#include "MusicChunk.hxx"

#include <glib.h>

#include <assert.h>

struct music_buffer {
	struct music_chunk *chunks;
	unsigned num_chunks;

	struct music_chunk *available;

	/** a mutex which protects #available */
	GMutex *mutex;

#ifndef NDEBUG
	unsigned num_allocated;
#endif

	music_buffer(unsigned _num_chunks)
		:chunks(g_new(struct music_chunk, _num_chunks)),
		 num_chunks(_num_chunks),
		 available(chunks),
		 mutex(g_mutex_new())
#ifndef NDEBUG
		, num_allocated(0)
#endif
	{
		assert(num_chunks > 0);

		struct music_chunk *chunk;
		chunk = available = chunks;

		for (unsigned i = 1; i < num_chunks; ++i) {
			chunk->next = &chunks[i];
			chunk = chunk->next;
		}

		chunk->next = nullptr;
	}

	~music_buffer() {
		assert(chunks != nullptr);
		assert(num_chunks > 0);
		assert(num_allocated == 0);

		g_mutex_free(mutex);
		g_free(chunks);
	}
};

struct music_buffer *
music_buffer_new(unsigned num_chunks)
{
	return new music_buffer(num_chunks);
}

void
music_buffer_free(struct music_buffer *buffer)
{
	delete buffer;
}

unsigned
music_buffer_size(const struct music_buffer *buffer)
{
	return buffer->num_chunks;
}

struct music_chunk *
music_buffer_allocate(struct music_buffer *buffer)
{
	struct music_chunk *chunk;

	g_mutex_lock(buffer->mutex);

	chunk = buffer->available;
	if (chunk != NULL) {
		buffer->available = chunk->next;
		music_chunk_init(chunk);

#ifndef NDEBUG
		++buffer->num_allocated;
#endif
	}

	g_mutex_unlock(buffer->mutex);
	return chunk;
}

void
music_buffer_return(struct music_buffer *buffer, struct music_chunk *chunk)
{
	assert(buffer != NULL);
	assert(chunk != NULL);

	if (chunk->other != NULL)
		music_buffer_return(buffer, chunk->other);

	g_mutex_lock(buffer->mutex);

	music_chunk_free(chunk);

	chunk->next = buffer->available;
	buffer->available = chunk;

#ifndef NDEBUG
	--buffer->num_allocated;
#endif

	g_mutex_unlock(buffer->mutex);
}