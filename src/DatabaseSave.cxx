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
#include "DatabaseSave.hxx"
#include "DatabaseLock.hxx"
#include "Directory.hxx"
#include "DirectorySave.hxx"
#include "song.h"
#include "TextFile.hxx"
#include "TagInternal.hxx"
#include "tag.h"

extern "C" {
#include "path.h"
}

#include <glib.h>

#include <assert.h>
#include <stdlib.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "database"

#define DIRECTORY_INFO_BEGIN "info_begin"
#define DIRECTORY_INFO_END "info_end"
#define DB_FORMAT_PREFIX "format: "
#define DIRECTORY_MPD_VERSION "mpd_version: "
#define DIRECTORY_FS_CHARSET "fs_charset: "
#define DB_TAG_PREFIX "tag: "

enum {
	DB_FORMAT = 1,
};

G_GNUC_CONST
static GQuark
db_quark(void)
{
	return g_quark_from_static_string("database");
}

void
db_save_internal(FILE *fp, const Directory *music_root)
{
	assert(music_root != NULL);

	fprintf(fp, "%s\n", DIRECTORY_INFO_BEGIN);
	fprintf(fp, DB_FORMAT_PREFIX "%u\n", DB_FORMAT);
	fprintf(fp, "%s%s\n", DIRECTORY_MPD_VERSION, VERSION);
	fprintf(fp, "%s%s\n", DIRECTORY_FS_CHARSET, path_get_fs_charset());

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i)
		if (!ignore_tag_items[i])
			fprintf(fp, DB_TAG_PREFIX "%s\n", tag_item_names[i]);

	fprintf(fp, "%s\n", DIRECTORY_INFO_END);

	directory_save(fp, music_root);
}

bool
db_load_internal(TextFile &file, Directory *music_root, GError **error)
{
	char *line;
	int format = 0;
	bool found_charset = false, found_version = false;
	bool success;
	bool tags[TAG_NUM_OF_ITEM_TYPES];

	assert(music_root != NULL);

	/* get initial info */
	line = file.ReadLine();
	if (line == NULL || strcmp(DIRECTORY_INFO_BEGIN, line) != 0) {
		g_set_error(error, db_quark(), 0, "Database corrupted");
		return false;
	}

	memset(tags, false, sizeof(tags));

	while ((line = file.ReadLine()) != NULL &&
	       strcmp(line, DIRECTORY_INFO_END) != 0) {
		if (g_str_has_prefix(line, DB_FORMAT_PREFIX)) {
			format = atoi(line + sizeof(DB_FORMAT_PREFIX) - 1);
		} else if (g_str_has_prefix(line, DIRECTORY_MPD_VERSION)) {
			if (found_version) {
				g_set_error(error, db_quark(), 0,
					    "Duplicate version line");
				return false;
			}

			found_version = true;
		} else if (g_str_has_prefix(line, DIRECTORY_FS_CHARSET)) {
			const char *new_charset, *old_charset;

			if (found_charset) {
				g_set_error(error, db_quark(), 0,
					    "Duplicate charset line");
				return false;
			}

			found_charset = true;

			new_charset = line + sizeof(DIRECTORY_FS_CHARSET) - 1;
			old_charset = path_get_fs_charset();
			if (old_charset != NULL
			    && strcmp(new_charset, old_charset)) {
				g_set_error(error, db_quark(), 0,
					    "Existing database has charset "
					    "\"%s\" instead of \"%s\"; "
					    "discarding database file",
					    new_charset, old_charset);
				return false;
			}
		} else if (g_str_has_prefix(line, DB_TAG_PREFIX)) {
			const char *name = line + sizeof(DB_TAG_PREFIX) - 1;
			enum tag_type tag = tag_name_parse(name);
			if (tag == TAG_NUM_OF_ITEM_TYPES) {
				g_set_error(error, db_quark(), 0,
					    "Unrecognized tag '%s', "
					    "discarding database file",
					    name);
				return false;
			}

			tags[tag] = true;
		} else {
			g_set_error(error, db_quark(), 0,
				    "Malformed line: %s", line);
			return false;
		}
	}

	if (format != DB_FORMAT) {
		g_set_error(error, db_quark(), 0,
			    "Database format mismatch, "
			    "discarding database file");
		return false;
	}

	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i) {
		if (!ignore_tag_items[i] && !tags[i]) {
			g_set_error(error, db_quark(), 0,
				    "Tag list mismatch, "
				    "discarding database file");
			return false;
		}
	}

	g_debug("reading DB");

	db_lock();
	success = directory_load(file, music_root, error);
	db_unlock();

	return success;
}
