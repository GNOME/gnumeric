/* vim: set sw=8: */

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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnome/libgnome.h>
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

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
	FILE          *file;
	ErrorInfo     *parse_error;
	WorkbookView  *wb_view;
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
	fprintf (state->file, "*BEGIN SPREADSHEETS VERSION=442/430 ENCODING=7BIT\n");
	fprintf (state->file, "Num ExtLinks: 0\n");
	fprintf (state->file, "Spreadsheet Dump Rev 4.42 Line Length 80\n");
	fprintf (state->file, "Current Doc Real Name: %s", workbook_get_filename (state->wb));
}

static void
applix_write_colormap (ApplixWriteState *state)
{
}

void
applix_write (IOContext *io_context, WorkbookView *wb_view, FILE *file)
{
	ApplixWriteState	state;

	/* Init the state variable */
	state.file        = file;
	state.parse_error = NULL;
	state.wb_view     = wb_view;
	state.wb          = wb_view_workbook (wb_view);

	applix_write_header (&state);
	applix_write_colormap (&state);

	if (state.parse_error != NULL)
		gnumeric_io_error_info_set (io_context, state.parse_error);
}
