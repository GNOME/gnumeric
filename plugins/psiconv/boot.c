/* vim: set sw=8:
 * $Id$
 */

/*
 * boot.c : Installation and bootstraping routines to
 *          register the psiconv plugin.
 *
 * Copyright (C) 2000 Frodo Looijaard (frodol@hdds.nl)
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
#include "psiconv-plugin.h"
#include "workbook-view.h"
#include "workbook.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean psiconv_file_probe (GnumFileOpener const *fo, const gchar *file_name,
                            FileProbeLevel pl);
void     psiconv_file_open (GnumFileOpener const *fo, IOContext *io_context,
                           WorkbookView *wb_view, const char *filename);


gboolean
psiconv_file_probe (GnumFileOpener const *fo, const gchar *file_name, FileProbeLevel pl)
{
	FILE *file;
	gboolean res;

	file = fopen (file_name, "rb");
	if (file == NULL)
		return FALSE;
	res = psiconv_read_header (file);
	fclose (file);

	return res;
}

void
psiconv_file_open (GnumFileOpener const *fo, IOContext *io_context,
                  WorkbookView *wb_view, const char *filename)
{
	FILE *file;
	ErrorInfo *error;
	Workbook *wb;

	file = gnumeric_fopen_error_info (filename, "r", &error);
	if (file == NULL) {
		gnumeric_io_error_info_set (io_context, error);
	} else {
		wb = wb_view_workbook(wb_view);
		psiconv_read (io_context, wb, file);
		fclose (file);
	}
}
