/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dif.c: read/write sheets using a CSV encoding.
 *
 * Authors:
 *   Kevin Handy <kth@srv.net>
 *   Zbigniew Chyla <cyba@gnome.pl>
 *
 *	Based on ff-csv code.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>

#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>

#include <goffice/app/go-plugin-impl.h>
#include <goffice/utils/go-file.h>
#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>

#define N_INPUT_LINES_BETWEEN_UPDATES   50

typedef struct {
	GOImporter	  base;

	GsfInputTextline *textline;
	gint   line_no;
	gchar *line;

	Sheet *sheet;

	GIConv converter;
} GnmDifIn;

static gboolean
dif_get_line (GnmDifIn *state)
{
	char *raw = gsf_input_textline_ascii_gets (state->textline);
	if (NULL == raw)
		return FALSE;
	g_free (state->line);
	state->line = g_convert_with_iconv (raw, -1, state->converter,
					   NULL, NULL, NULL);
	return state->line != NULL;
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_header (GnmDifIn *state)
{
	while (1) {
		gchar *topic, *num_line, *str_line;
		size_t str_line_len;

		if (!dif_get_line (state))
			return FALSE;
		topic = g_alloca (strlen (state->line) + 1);
		strcpy (topic, state->line);

		if (!dif_get_line (state))
			return FALSE;
		num_line = g_alloca (strlen (state->line) + 1);
		strcpy (num_line, state->line);

		if (!dif_get_line (state))
			return FALSE;
		str_line_len = strlen (state->line);
		str_line = g_alloca (str_line_len + 1);
		strcpy (str_line, state->line);

		if (strcmp (topic, "TABLE") == 0) {
			gchar *name;

			if (str_line_len > 2 && str_line[0] == '"' && str_line[str_line_len - 1] == '"') {
				str_line[str_line_len - 1] = '\0';
				name = str_line + 1;
			}  else {
				name = str_line;
			}
			if (name[0] != '\0') {
				/* FIXME - rename the sheet */
			}
		} else if (strcmp (topic, "DATA") == 0) {
			break;
		}

		/*
		 * Other "standard" header entry items are:
		 * SIZE, LABEL, UNITS, TUPLES, VECTORS, COMMENT, MINORSTART,
		 * TRUELENGTH, PERIODICITY, DISPLAYUNITS
		 */
	}

	return TRUE;
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_data (GnmDifIn *state)
{
	gboolean too_many_rows = FALSE, too_many_columns = FALSE;
	gint row = -1, col = 0;

	while (1) {
		gint val_type;
		GnmCell *cell;
		gchar *msg;

		if (!dif_get_line (state))
			return FALSE;

		val_type = atoi (state->line);
		if (val_type == 0) {
			gchar *comma;

			(void) dif_get_line (state);
			if (col > SHEET_MAX_COLS) {
				too_many_columns = TRUE;
				continue;
			}
			comma = strchr (state->line, ',');
			if (comma != NULL) {
				cell = sheet_cell_fetch (state->sheet, col, row);
				cell_set_text (cell, comma + 1);
				col++;
			} else {
				msg = g_strdup_printf (_("Syntax error at line %d. Ignoring."),
				                       state->line_no);
				g_warning (msg);
				g_free (msg);
			}
		} else if (val_type == 1) {
			if (!dif_get_line (state)) {
				return FALSE;
			}
			if (col > SHEET_MAX_COLS) {
				too_many_columns = TRUE;
				continue;
			}
			cell_set_text (sheet_cell_fetch (state->sheet, col, row),
				state->line);
			col++;
		} else if (val_type == -1) {
			if (!dif_get_line (state)) {
				return FALSE;
			}
			if (strcmp (state->line, "BOT") == 0) {
				col = 0;
				row++;
				if (row > SHEET_MAX_ROWS) {
					too_many_rows = TRUE;
					break;
				}
			} else if (strcmp (state->line, "EOD") == 0) {
				break;
			} else {
				msg = g_strdup_printf (
				      _("Unknown data value \"%s\" at line %d. Ignoring."),
				      state->line, state->line_no);
				g_warning (msg);
				g_free (msg);
			}
		} else {
			msg = g_strdup_printf (
			      _("Unknown value type %d at line %d. Ignoring."),
			      val_type, state->line_no);
			g_warning (msg);
			g_free (msg);
			(void) dif_get_line (state);
		}
	}

	if (too_many_rows)
		go_importer_warn (&state->base,
			_("DIF file has more than the maximum number of rows %d. Ignoring remaining rows."),
			SHEET_MAX_ROWS);
	if (too_many_columns)
		go_importer_warn (&state->base,
			_("DIF file has more than the maximum number of columns %d. Ignoring remaining columns."),
			SHEET_MAX_COLS);

	return TRUE;
}

static void
gnm_dif_in_import (GOImporter *imp, GODoc *doc)
{
	Workbook *wb = WORKBOOK (doc);
	GnmDifIn *state = (GnmDifIn *)imp;

	state->textline	     = (GsfInputTextline *) gsf_input_textline_new (imp->input);
	state->line_no        = 1;
	state->line           = NULL;
	state->sheet          = workbook_sheet_add (wb, NULL, FALSE);
	state->converter      = g_iconv_open ("UTF-8", "ISO-8859-1");

	if (!dif_parse_header (state))
		go_importer_fail (imp, 
			_("Unexpected end of file at line %d while reading header."),
			state->line_no);
	else if (!dif_parse_data(state))
		go_importer_fail (imp, 
			_("Unexpected end of file at line %d while reading data."),
			state->line_no);
	else
		workbook_set_saveinfo (wb, FILE_FL_MANUAL_REMEMBER,
			gnm_file_saver_for_id ("Gnumeric_dif:dif"));

	g_object_unref (state->textline);
	gsf_iconv_close (state->converter);
	g_free (state->line);
}

static void
gnm_dif_in_class_init (GOImporterClass *import_class)
{
	import_class->ProbeContent	= NULL;
	import_class->Import		= gnm_dif_in_import;
}

/*****************************************************************************/

static void
gnm_dif_out_export (GOExporter *exporter)
{
	Sheet *sheet;
	GnmRange r;
	gint row, col;
	gboolean res;

	sheet = wb_view_cur_sheet (wbv);
	if (sheet == NULL) {
		gnumeric_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	r = sheet_get_extent (sheet, FALSE);

	/* standard header */
	res = gsf_output_puts (exporter->output,   "TABLE\n" "0,1\n" "\"GNUMERIC\"\n") ||
	      gsf_output_printf (exporter->output, "VECTORS\n" "0,%d\n" "\"\"\n", r.end.row) ||
	      gsf_output_printf (exporter->output, "TUPLES\n" "0,%d\n" "\"\"\n", r.end.col) ||
	      gsf_output_puts (exporter->output,   "DATA\n0,0\n" "\"\"\n");

	/* Process all cells */
	for (row = r.start.row; res && row <= r.end.row; row++) {
		gsf_output_puts (exporter->output, "-1,0\n" "BOT\n");
		for (col = r.start.col; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
			if (!cell_is_empty (cell)) {
				gchar *str;

				str = cell_get_rendered_text (cell);
				res = gsf_output_printf (exporter->output, "1.0\n" "\"%s\"\n", str);
				g_free (str);
			} else
				gsf_output_puts (exporter->output, "1,0\n" "\"\"\n");
		}
	}
	gsf_output_puts (exporter->output, "-1,0\n" "EOD\n");

	if (!res)
		gnumeric_io_error_string (io_context, _("Error while saving DIF file."));
}

static void
gnm_dif_out_class_init (GOExporterClass *export_class)
{
	export_class->Prepare	= NULL;
	export_class->Export	= gnm_dif_out_export;
}

typedef GOImporterClass GnmDifInClass;
typedef GOExporterClass GnmDifOutClass;
typedef GOExporter	GnmDifOut;
static GType gnm_dif_in_type, gnm_dif_out_type;
G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin)
{
	GSF_DYNAMIC_CLASS (GnmDifIn, gnm_dif_in,
		gnm_dif_in_class_init, NULL, GO_IMPORTER_TYPE,
		G_TYPE_MODULE (plugin), gnm_dif_in_type);
	GSF_DYNAMIC_CLASS (GnmDifOut, gnm_dif_in,
		gnm_dif_out_class_init, NULL, GO_EXPORTER_TYPE,
		G_TYPE_MODULE (plugin), gnm_dif_out_type);
}
