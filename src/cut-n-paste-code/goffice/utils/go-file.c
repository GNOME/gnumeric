/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-file.c :
 *
 * Copyright (C) 2004 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <goffice/goffice-config.h>
#include "go-file.h"
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-input-stdio.h>
#ifdef WITH_GNOME
#include <libgnomevfs/gnome-vfs-utils.h>
#include <gsf-gnome/gsf-input-gnomevfs.h>
#endif

/* ------------------------------------------------------------------------- */

char *
go_filename_from_uri (const char *uri)
{
#ifdef WITH_GNOME
	return gnome_vfs_get_local_path_from_uri (uri);
#else
	return g_filename_from_uri (uri, NULL, NULL);
#endif
}


char *
go_filename_to_uri (const char *filename)
{
	if (g_path_is_absolute (filename)) {
		/* FIXME: Resolve ".." and "." parts.  */
#ifdef WITH_GNOME
		return gnome_vfs_get_uri_from_local_path (filename);
#else
		return g_filename_to_uri (uri, NULL, NULL);
#endif
	} else {
		char *result;
		char *current_dir = g_get_current_dir ();
		gsize clen = strlen (current_dir);
		char *abs_filename =
			g_strconcat (current_dir,
				     (clen && current_dir[clen - 1] == G_DIR_SEPARATOR
				      ? ""
				      : G_DIR_SEPARATOR_S),
				     filename,
				     NULL);
		g_return_val_if_fail (g_path_is_absolute (abs_filename), NULL);
		result = go_filename_to_uri (abs_filename);
		g_free (current_dir);
		g_free (abs_filename);
		return result;
	}
}


char *
go_shell_arg_to_uri (const char *arg)
{
#ifdef WITH_GNOME
	return gnome_vfs_make_uri_from_shell_arg (arg);
#else
	if (g_path_is_absolute (arg))
		return go_filename_to_uri (arg);
	else {
		/* See if it's a file: uri.  */
		char *tmp = go_filename_from_uri (arg);
		if (tmp) {
			g_free (tmp);
			return g_strdup (arg);
		}
	}

	/* Just assume it's a filename.  */
	return go_filename_to_uri (arg);
#endif
}

/**
 * go_basename_from_uri:
 * @uri :
 *
 * Decode the final path component.  Returns as UTF-8 encoded.
 **/
char *
go_basename_from_uri (const char *uri)
{
	char *uri_basename = g_path_get_basename (uri);
	char *fake_uri = g_strconcat ("file:///", uri_basename, NULL);
	char *filename = go_filename_from_uri (fake_uri);
	char *basename = filename ? g_path_get_basename (filename) : NULL;
	char *basename_utf8 = basename ? g_filename_to_utf8 (basename, -1, NULL, NULL, NULL) : NULL;
	g_free (uri_basename);
	g_free (fake_uri);
	g_free (filename);
	g_free (basename);

	return basename_utf8;
}

/* ------------------------------------------------------------------------- */

static GsfInput *
open_plain_file (const char *path, GError **err)
{
	GsfInputMemory *in_mem;
	GsfInputStdio  *in_stdio;

	in_mem = gsf_input_mmap_new (path, NULL);
	if (in_mem != NULL)
		return GSF_INPUT (in_mem);

	/* Only report error if stdio fails too */
	in_stdio = gsf_input_stdio_new (path, err);
	return in_stdio ? GSF_INPUT (in_stdio) : NULL;
}


/**
 * go_file_open :
 * @uri :
 * @err : #GError
 *
 * Try all available methods to open a file or return an error
 **/
GsfInput *
go_file_open (char const *uri, GError **err)
{
	char *filename;

	if (err != NULL)
		*err = NULL;
	g_return_val_if_fail (uri != NULL, NULL);

	if (uri[0] == G_DIR_SEPARATOR) {
		g_warning ("Got plain filename %s in go_file_open.", uri);
		return open_plain_file (uri, err);
	}

	filename = go_filename_from_uri (uri);
	if (filename) {
		GsfInput *result = open_plain_file (filename, err);
		g_free (filename);
		return result;
	}

#ifdef WITH_GNOME
	return (GsfInput *)gsf_input_gnomevfs_new (uri, err);
#else
	g_set_error (err, gsf_input_error (), 0,
		     "Invalid or non-supported URI");
	return NULL; 
#endif
}

/* ------------------------------------------------------------------------- */
