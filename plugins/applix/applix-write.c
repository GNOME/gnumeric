/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * applix-write.c : Routines to write applix version 4 spreadsheets.
 *
 * Copyright (C) 2000-2004 Jody Goldberg (jody@gnome.org)
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

/*
 * I do not have much in the way of useful docs.
 * This is a guess based on some sample sheets with a few pointers from
 * 	http://www.vistasource.com/products/axware/fileformats/wptchc01.html
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "applix.h"
#include "application.h"
#include "expr.h"
#include "value.h"
#include "sheet.h"
#include "number-match.h"
#include "cell.h"
#include "parse-util.h"
#include "sheet-style.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "selection.h"
#include "position.h"
#include "ranges.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "error-info.h"

#include <goffice/utils/go-file.h>
#include <goffice/app/go-plugin-impl.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output.h>
#include <string.h>

typedef struct {
	GOExporter base;

	Workbook const *wb;
} GnmApplixOut;

/* #define NO_DEBUG_APPLIX */
#ifndef NO_DEBUG_APPLIX
#define d(level, code)	do { if (debug_applix_write > level) { code } } while (0)
static int debug_applix_write = 0;
#else
#define d(level, code)
#endif

static void
applix_write_header (GnmApplixOut const *state)
{
	gsf_output_printf (state->base.output,
			   "*BEGIN SPREADSHEETS VERSION=442/430 "
			   "ENCODING=7BIT\n");
	gsf_output_printf (state->base.output, "Num ExtLinks: 0\n");
	gsf_output_printf (state->base.output,
			   "Spreadsheet Dump Rev 4.42 Line Length 80\n");
#warning "FIXME: filename is fs encoded; that's not right, but neither is UTF-8."
	gsf_output_printf (state->base.output,
			   "Current Doc Real Name: %s",
			   workbook_get_uri (state->wb));
}

static void
applix_write_colormap (GnmApplixOut *state)
{
}

static void
gnm_applix_out_export (GOExporter *exporter)
{
	GnmApplixOut *state = (GnmApplixOut *)exporter;
	state->wb = (Workbook *) exporter->doc;

	d (1, fprintf (stderr, "------------Start writing"););
	applix_write_header (state);
	applix_write_colormap (state);
	d (1, fprintf (stderr, "------------Finish writing"););
#if 0
	if (state.parse_error != NULL)
		gnumeric_io_error_info_set (io_context, state.parse_error);
#endif
}

static void
gnm_applix_out_class_init (GOExporterClass *export_class)
{
	export_class->Prepare	= NULL;
	export_class->Export	= gnm_applix_out_export;
}
typedef GOExporterClass GnmApplixOutClass;
static GType gnm_applix_out_type;
void
gnm_applix_exporter_register (GOPlugin *plugin)
{
	GSF_DYNAMIC_CLASS (GnmApplixOut, gnm_applix_out,
		gnm_applix_out_class_init, NULL, GO_EXPORTER_TYPE,
		G_TYPE_MODULE (plugin), gnm_applix_out_type);
}
