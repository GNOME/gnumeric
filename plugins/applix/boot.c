/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * boot.c : Installation and bootstraping routines to
 *          register the applix plugin.
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "applix.h"

#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <workbook-view.h>
#include <workbook.h>
#include <io-context.h>

#include <gsf/gsf-input.h>
#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean applix_file_probe (GnmFileOpener const *fo, GsfInput *input,
                            FileProbeLevel pl);
void     applix_file_open (GnmFileOpener const *fo, IOContext *io_context,
                           WorkbookView *wb_view, GsfInput *input);
void     applix_file_save (GnmFileSaver const *fs, IOContext *io_context,
			   WorkbookView const *wb_view, GsfOutput *output);

gboolean
applix_file_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	static guint8 const signature[] = "*BEGIN SPREADSHEETS VERSION";
	guint8 const *header;

	return !gsf_input_seek (input, 0, G_SEEK_SET) &&
		NULL != (header = gsf_input_read (input, sizeof (signature)-1, NULL)) &&
		0 == memcmp (header, signature, sizeof (signature)-1);
}

void
applix_file_open (GnmFileOpener const *fo, IOContext *io_context,
                  WorkbookView *wb_view, GsfInput *input)
{
	applix_read (io_context, wb_view, input);
}

void
applix_file_save (GnmFileSaver const *fs, IOContext *io_context,
		  WorkbookView const *wb_view, GsfOutput *output)
{
	applix_write (io_context, wb_view, output);
}
