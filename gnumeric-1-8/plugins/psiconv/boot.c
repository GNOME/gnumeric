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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>
#include "psiconv-plugin.h"
#include "workbook-view.h"
#include "workbook.h"

#include <stdio.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean psiconv_file_probe (GOFileOpener const *fo, GsfInput *input,
                            FileProbeLevel pl);
void     psiconv_file_open (GOFileOpener const *fo, IOContext *io_context,
			    WorkbookView *wb_view, GsfInput *input);


gboolean
psiconv_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	return psiconv_read_header (input);
}

void
psiconv_file_open (GOFileOpener const *fo, IOContext *io_context,
                  WorkbookView *wb_view, GsfInput *input)
{
	psiconv_read (io_context, wb_view_get_workbook(wb_view), input);
}
