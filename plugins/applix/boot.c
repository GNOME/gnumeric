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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "applix.h"

#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>

#include <gsf/gsf-input.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean applix_file_probe (GOFileOpener const *fo, GsfInput *input,
                            GOFileProbeLevel pl);
void     applix_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                           WorkbookView *wb_view, GsfInput *input);
void     applix_file_save (GOFileSaver const *fs, GOIOContext *io_context,
			   WorkbookView const *wb_view, GsfOutput *output);

gboolean
applix_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	static guint8 const signature[] = "*BEGIN SPREADSHEETS VERSION";
	guint8 const *header;

	return !gsf_input_seek (input, 0, G_SEEK_SET) &&
		NULL != (header = gsf_input_read (input, sizeof (signature)-1, NULL)) &&
		0 == memcmp (header, signature, sizeof (signature)-1);
}

void
applix_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                  WorkbookView *wb_view, GsfInput *input)
{
	applix_read (io_context, wb_view, input);
}

void
applix_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		  WorkbookView const *wb_view, GsfOutput *output)
{
	applix_write (io_context, wb_view, output);
}
