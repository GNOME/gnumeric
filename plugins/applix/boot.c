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
#include "io-context.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "applix.h"
#include "workbook-view.h"
#include "workbook.h"

#include <stdio.h>
#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean applix_file_probe (GnumFileOpener const *fo, char const *file_name,
                            FileProbeLevel pl);
void     applix_file_open (GnumFileOpener const *fo, IOContext *io_context,
                           WorkbookView *wb_view, char const *filename);

gboolean
applix_file_probe (GnumFileOpener const *fo, char const *file_name, FileProbeLevel pl)
{
	gboolean res = FALSE;
	FILE *file = fopen (file_name, "r");
	if (file == NULL) {
		res = applix_read_header (file);
		fclose (file);
	}
	return res;
}

void
applix_file_open (GnumFileOpener const *fo, IOContext *io_context,
                  WorkbookView *wb_view, char const *filename)
{
	ErrorInfo *error;
	FILE *file = gnumeric_fopen_error_info (filename, "r", &error);
	if (file != NULL) {
		applix_read (io_context, wb_view, file);
		fclose (file);
	} else
		gnumeric_io_error_info_set (io_context, error);
}

void
applix_file_save (GnumFileSaver const *fs, IOContext *io_context,
		  WorkbookView *wb_view, char const *filename)
{
	ErrorInfo *error;
	FILE *file = gnumeric_fopen_error_info (filename, "w", &error);
	if (file != NULL) {
		applix_write (io_context, wb_view, file);
		fclose (file);
	} else
		gnumeric_io_error_info_set (io_context, error);
}
