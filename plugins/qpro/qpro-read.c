/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * qpro-read.c: Read Quatro Pro files
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "qpro.h"

#include <plugin-util.h>
#include <file.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <sheet-style.h>
#include <style-color.h>
#include <parse-util.h>
#include <module-plugin-defs.h>

#include <gsf/gsf-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static gboolean
qpro_check_signature (GsfInput *input)
{
	guint8 const *header;
	guint16 version;

	if (gsf_input_seek (input, 0, GSF_SEEK_SET) ||
	    NULL == (header = gsf_input_read (input, 2+2+2, NULL)) ||
	    GSF_LE_GET_GUINT16 (header + 0) != 0 ||
	    GSF_LE_GET_GUINT16 (header + 2) != 2)
		return FALSE;
	version = GSF_LE_GET_GUINT16 (header + 4);
	return (version == 0x1001 || /* 'WB1' format, documented */
		version == 0x1002 || /* 'WB2' format, documented */
		version == 0x1006 ||  /* qpro 6.0 ?? */
		version == 0x1007);  /* qpro 7.0 ?? */
}

gboolean
qpro_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	GsfInfile *ole;
	GsfInput *stream;
	gboolean res = FALSE;

	/* check for >= QPro 6.0 which is OLE based */
	ole = gsf_infile_msole_new (input, NULL);
	if (ole != NULL) {
		stream = gsf_infile_child_by_name (ole, "PerfectOffice_MAIN");
		if (stream != NULL) {
			res = qpro_check_signature (stream);
			g_object_unref (G_OBJECT (stream));
		}
		g_object_unref (G_OBJECT (ole));
	} else
		res = qpro_check_signature (input);

	return res;
}

typedef struct {
	GsfInput	*input;
	IOContext	*io_context;
	WorkbookView	*wbv;
	Workbook	*wb;
	Sheet		*cur_sheet;
} QProReadState;

static guint8 const *
qpro_get_record (QProReadState *state, guint16 *id, guint16 *len)
{
	guint8 const *data;

	if (NULL == (data = gsf_input_read (state->input, 4, NULL))) {
		g_warning ("read failure");
		return NULL;
	}
	*id  = GSF_LE_GET_GUINT16 (data + 0);
	*len = GSF_LE_GET_GUINT16 (data + 2);

	printf ("%hd with %hd\n", *id, *len);

	if (*len == 0)
		return "";
	/* some sanity checking */
	g_return_val_if_fail (*len < 0x2000, NULL);
	data = gsf_input_read (state->input, *len, NULL);
	if (data == NULL)
		g_warning ("huh? failue reading %hd for type %hd", *len, *id);
	return data;
}

#define state(f)	case f : printf ("%s = %hd\n", #f, len); break
#define validate(f,expected) qpro_validate_len (state, #f, len, expected)

static gboolean
qpro_validate_len (QProReadState *state, char const *id, guint16 len, int expected_len)
{
	if (expected_len >= 0  && len != expected_len) {
		char *msg = g_strdup_printf ("Invalid '%s' record of length %hd instead of %d",
			id, len, expected_len);
		gnm_io_warning (state->io_context, msg);
		g_free (msg);
		return FALSE;
	}

	return TRUE;
}

static void
qpro_parse_formula (QProReadState *state, int col, int row,
		    guint8 const *data)
{
	double val = gnumeric_get_le_double (data);
	int ex;

	frexp (val, &ex);
	puts (cell_coord_name (col, row));
	printf ("Exp = 0x%x, %g %d\n", (GSF_LE_GET_GUINT16 (data + 6) >> 1) & 0x7ff,
		val, ex);
	gsf_mem_dump (data, 8);
}

static MStyle *
qpro_get_style (QProReadState *state, guint8 const *data)
{
	unsigned attr_id = GSF_LE_GET_GUINT16 (data) >> 3;
	printf ("Get Attr %u\n", attr_id);
	return sheet_style_default (state->cur_sheet);
}

static void
qpro_read_sheet (QProReadState *state)
{
	guint16 id, len;
	guint8 const *data;

	/* We can use col_name as a quick proxy for the defaul q-pro sheet names */
	char const *def_name = col_name (workbook_sheet_count (state->wb));
	Sheet *sheet = sheet_new (state->wb, def_name);

	state->cur_sheet = sheet;
	workbook_sheet_attach (state->wb, sheet, NULL);
	printf ("----------> start %s\n", def_name);
	while (NULL != (data = qpro_get_record (state, &id, &len))) {
		switch (id) {
		case QPRO_BLANK_CELL:
			if (validate (QPRO_BLANK_CELL, 6))
				sheet_style_set_pos (sheet,
					data [0], GSF_LE_GET_GUINT16 (data + 2),
					qpro_get_style (state, data + 4));
			break;

		case QPRO_INTEGER_CELL:
			if (validate (QPRO_INTEGER_CELL, 8)) {
				int col = data [0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));
				cell_assign_value (sheet_cell_fetch (sheet, col, row),
					value_new_int (GSF_LE_GET_GUINT16 (data + 6)));
			}
			break;

		case QPRO_FLOATING_POINT_CELL:
			if (validate (QPRO_FLOATING_POINT_CELL, 14)) {
				int col = data [0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));
				cell_assign_value (sheet_cell_fetch (sheet, col, row),
					value_new_float (gnumeric_get_le_double (data + 6)));
			}
			break;

		case QPRO_LABEL_CELL:
			if (validate (QPRO_LABEL_CELL, -1)) {
				int col = data [0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));
				cell_assign_value (sheet_cell_fetch (sheet, col, row),
					value_new_string (data + 7));
			}
			break;

		case QPRO_FORMULA_CELL:
			if (validate (QPRO_FORMULA_CELL, -1)) {
				int col = data [0];
				int row = GSF_LE_GET_GUINT16 (data + 2);
				sheet_style_set_pos (sheet, col, row,
					qpro_get_style (state, data + 4));

				qpro_parse_formula (state, col, row, data + 6);
			}
			break;

		case QPRO_END_OF_PAGE:
			break;

		case QPRO_COLUMN_SIZE:
			/* ignore this, we auto generate this info */
			break;

		case QPRO_PROTECTION:
			if (validate (QPRO_PROTECTION, 1))
				sheet->is_protected = (data[0] == 0xff);
			break;

		case QPRO_PAGE_NAME:
			if (validate (QPRO_PAGE_NAME, -1)) {
#warning this is wrong, but the workbook interface is confused and needs a control.
				sheet_rename (sheet, data);
			}
			break;

		case QPRO_PAGE_ATTRIBUTE:
			if (validate (QPRO_PAGE_ATTRIBUTE, 30)) {
#warning TODO, mostly simple
			}
			break;

		case QPRO_DEFAULT_ROW_HEIGHT_PANE1:
		case QPRO_DEFAULT_ROW_HEIGHT_PANE2:
			if (validate (QPRO_DEFAULT_ROW_HEIGHT, 2)) {
			}
			break;

		case QPRO_DEFAULT_COL_WIDTH_PANE1:
		case QPRO_DEFAULT_COL_WIDTH_PANE2:
			if (validate (QPRO_DEFAULT_COL_WIDTH, 2)) {
			}
			break;

		case QPRO_MAX_FONT_PANE1:
		case QPRO_MAX_FONT_PANE2 :
			/* just ignore for now */
			break;

		case QPRO_PAGE_TAB_COLOR :
			if (validate (QPRO_PAGE_TAB_COLOR, 4))
				sheet_set_tab_color (sheet, style_color_new_i8 (
					data [0], data [1], data [2]), NULL);
			break;

		case QPRO_PAGE_ZOOM_FACTOR :
			if (validate (QPRO_PAGE_ZOOM_FACTOR, 4)) {
				guint16 low  = GSF_LE_GET_GUINT16 (data);
				guint16 high = GSF_LE_GET_GUINT16 (data + 2);

				if (low == 100) {
					if (high < 10 || high > 400) {
						char *msg = g_strdup_printf ("Invalid zoom %hd %%",
							high);
						gnm_io_warning (state->io_context, msg);
						g_free (msg);
					} else
						sheet_set_zoom_factor (sheet, ((double)high) / 100.,
							FALSE, FALSE);
				}
			}
			break;
		}

		if (id == QPRO_END_OF_PAGE)
			break;
	}
	printf ("----------< end\n");
	state->cur_sheet = NULL;
}

static void
qpro_read_workbook (QProReadState *state, GsfInput *input)
{
	guint16 id, len;
	guint8 const *data;

	state->input = input;
	gsf_input_seek (input, 0, GSF_SEEK_SET);

	while (NULL != (data = qpro_get_record (state, &id, &len))) {
		switch (id) {
		case QPRO_BEGINNING_OF_FILE:
			if (validate (QPRO_BEGINNING_OF_FILE, 2)) {
				guint16 version;
				version = GSF_LE_GET_GUINT16 (data);
			}
			break;
		case QPRO_BEGINNING_OF_PAGE:
			qpro_read_sheet (state);
			break;

		default :
			if (id > QPRO_LAST_SANE_ID) {
				char *msg = g_strdup_printf ("Invalid record %d of length %hd",
					id, len);
				gnm_io_warning (state->io_context, msg);
				g_free (msg);
			}
		};
		if (id == QPRO_END_OF_FILE)
			break;
	}
}

void
qpro_file_open (GnumFileOpener const *fo, IOContext *context,
		WorkbookView *new_wb_view, GsfInput *input)
{
	QProReadState state;
	GsfInput *stream = NULL;
	GsfInfile *ole = gsf_infile_msole_new (input, NULL);

	state.io_context = context;
	state.wbv = new_wb_view;
	state.wb = wb_view_workbook (new_wb_view);
	state.cur_sheet = NULL;

	/* check for >= QPro 6.0 which is OLE based */
	ole = gsf_infile_msole_new (input, NULL);
	if (ole != NULL) {
		stream = gsf_infile_child_by_name (ole, "PerfectOffice_MAIN");
		if (stream != NULL) {
			qpro_read_workbook (&state, stream);
			g_object_unref (G_OBJECT (stream));
		}
		g_object_unref (G_OBJECT (ole));
	} else
		qpro_read_workbook (&state, input);
}
