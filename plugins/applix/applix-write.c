/*
 * applix-write.c : Routines to write applix version 4 spreadsheets.
 *
 * I have no docs or specs for this format that are useful.
 * This is a guess based on some sample sheets.
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
#include <application.h>
#include <expr.h>
#include <value.h>
#include <sheet.h>
#include <number-match.h>
#include <cell.h>
#include <parse-util.h>
#include <sheet-style.h>
#include <style.h>
#include <style-border.h>
#include <style-color.h>
#include <selection.h>
#include <position.h>
#include <ranges.h>
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>

#include <string.h>
#include <gsf/gsf-output.h>

typedef struct {
	GsfOutput     *sink;
	GOErrorInfo     *parse_error;
	WorkbookView const *wb_view;
	Workbook      *wb;
} ApplixWriteState;

/* #define NO_DEBUG_APPLIX */
#ifndef NO_DEBUG_APPLIX
#define d(level, code)	do { if (debug_applix_write > level) { code } } while (0)
static int debug_applix_write = 0;
#else
#define d(level, code)
#endif

static void
applix_write_header (ApplixWriteState const *state)
{
	gsf_output_printf (state->sink,
			   "*BEGIN SPREADSHEETS VERSION=442/430 "
			   "ENCODING=7BIT\n");
	gsf_output_printf (state->sink, "Num ExtLinks: 0\n");
	gsf_output_printf (state->sink,
			   "Spreadsheet Dump Rev 4.42 Line Length 80\n");
#warning "FIXME: filename is fs encoded; that's not right, but neither is UTF-8."
	gsf_output_printf (state->sink,
			   "Current Doc Real Name: %s",
			   go_doc_get_uri (GO_DOC (state->wb)));
}

static void
applix_write_colormap (ApplixWriteState *state)
{
}

void
applix_write (GOIOContext *io_context, WorkbookView const *wb_view, GsfOutput *sink)
{
	ApplixWriteState	state;

	/* Init the state variable */
	state.sink        = sink;
	state.parse_error = NULL;
	state.wb_view     = wb_view;
	state.wb          = wb_view_get_workbook (wb_view);

	d (1, fprintf (stderr, "------------Start writing"););
	applix_write_header (&state);
	applix_write_colormap (&state);
	d (1, fprintf (stderr, "------------Finish writing"););

	if (state.parse_error != NULL)
		go_io_error_info_set (io_context, state.parse_error);
}
