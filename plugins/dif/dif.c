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
#include <glib/gi18n.h>
#include <gnumeric.h>

#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <io-context.h>
#include <workbook-view.h>
#include <workbook.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>

#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#include <stdlib.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#define N_INPUT_LINES_BETWEEN_UPDATES   50

void dif_file_open (GnmFileOpener const *fo, IOContext *io_context,
                    GODoc *doc, GsfInput *input);
void dif_file_save (GnmFileSaver const *fs, IOContext *io_context,
                    WorkbookView const *wbv, GsfOutput *output);

typedef struct {
	IOContext *io_context;

	GsfInputTextline *input;
	gint   line_no;
	gchar *line;

	Sheet *sheet;

	GIConv converter;
} DifInputContext;


static DifInputContext *
dif_input_context_new (IOContext *io_context, Workbook *wb, GsfInput *input)
{
	DifInputContext *ctxt = NULL;

	ctxt = g_new (DifInputContext, 1);
	ctxt->io_context     = io_context;

	ctxt->input	     = (GsfInputTextline *) gsf_input_textline_new (input);

	ctxt->line_no        = 1;
	ctxt->line           = NULL;
	ctxt->sheet          = workbook_sheet_add (wb, NULL, FALSE);
	ctxt->converter      = g_iconv_open ("UTF-8", "ISO-8859-1");

	io_progress_message (io_context, _("Reading file..."));

	return ctxt;
}

static void
dif_input_context_destroy (DifInputContext *ctxt)
{
	io_progress_unset (ctxt->io_context);
	g_object_unref (G_OBJECT (ctxt->input)); ctxt->input = NULL;
	gsf_iconv_close (ctxt->converter);
	g_free (ctxt->line);
	g_free (ctxt);
}

static gboolean
dif_get_line (DifInputContext *ctxt)
{
	char *raw;

	raw = gsf_input_textline_ascii_gets (ctxt->input);
	if (!raw)
		return FALSE;

	g_free (ctxt->line);
	ctxt->line =
		g_convert_with_iconv (raw, -1, ctxt->converter, NULL, NULL, NULL);

	return ctxt->line != NULL;
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_header (DifInputContext *ctxt)
{
	while (1) {
		gchar *topic, *num_line, *str_line;
		size_t str_line_len;

		if (!dif_get_line (ctxt))
			return FALSE;
		topic = g_alloca (strlen (ctxt->line) + 1);
		strcpy (topic, ctxt->line);

		if (!dif_get_line (ctxt))
			return FALSE;
		num_line = g_alloca (strlen (ctxt->line) + 1);
		strcpy (num_line, ctxt->line);

		if (!dif_get_line (ctxt))
			return FALSE;
		str_line_len = strlen (ctxt->line);
		str_line = g_alloca (str_line_len + 1);
		strcpy (str_line, ctxt->line);

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
dif_parse_data (DifInputContext *ctxt)
{
	gboolean too_many_rows = FALSE, too_many_columns = FALSE;
	gint row = -1, col = 0;

	while (1) {
		gint val_type;
		GnmCell *cell;
		gchar *msg;

		if (!dif_get_line (ctxt))
			return FALSE;

		val_type = atoi (ctxt->line);
		if (val_type == 0) {
			gchar *comma;

			(void) dif_get_line (ctxt);
			if (col > SHEET_MAX_COLS) {
				too_many_columns = TRUE;
				continue;
			}
			comma = strchr (ctxt->line, ',');
			if (comma != NULL) {
				cell = sheet_cell_fetch (ctxt->sheet, col, row);
				cell_set_text (cell, comma + 1);
				col++;
			} else {
				msg = g_strdup_printf (_("Syntax error at line %d. Ignoring."),
				                       ctxt->line_no);
				g_warning (msg);
				g_free (msg);
			}
		} else if (val_type == 1) {
			if (!dif_get_line (ctxt)) {
				return FALSE;
			}
			if (col > SHEET_MAX_COLS) {
				too_many_columns = TRUE;
				continue;
			}
			cell_set_text (sheet_cell_fetch (ctxt->sheet, col, row),
				ctxt->line);
			col++;
		} else if (val_type == -1) {
			if (!dif_get_line (ctxt)) {
				return FALSE;
			}
			if (strcmp (ctxt->line, "BOT") == 0) {
				col = 0;
				row++;
				if (row > SHEET_MAX_ROWS) {
					too_many_rows = TRUE;
					break;
				}
			} else if (strcmp (ctxt->line, "EOD") == 0) {
				break;
			} else {
				msg = g_strdup_printf (
				      _("Unknown data value \"%s\" at line %d. Ignoring."),
				      ctxt->line, ctxt->line_no);
				g_warning (msg);
				g_free (msg);
			}
		} else {
			msg = g_strdup_printf (
			      _("Unknown value type %d at line %d. Ignoring."),
			      val_type, ctxt->line_no);
			g_warning (msg);
			g_free (msg);
			(void) dif_get_line (ctxt);
		}
	}

	if (too_many_rows) {
		g_warning (_("DIF file has more than the maximum number of rows %d. "
		             "Ignoring remaining rows."), SHEET_MAX_ROWS);
	}
	if (too_many_columns) {
		g_warning (_("DIF file has more than the maximum number of columns %d. "
		             "Ignoring remaining columns."), SHEET_MAX_COLS);
	}

	return TRUE;
}

static void
dif_parse_sheet (DifInputContext *ctxt)
{
	if (!dif_parse_header (ctxt)) {
		gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
		_("Unexpected end of file at line %d while reading header."),
		ctxt->line_no));
	} else if (!dif_parse_data(ctxt)) {
		gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
		_("Unexpected end of file at line %d while reading data."),
		ctxt->line_no));
	}
}

void
dif_file_open (GnmFileOpener const *fo, IOContext *io_context,
               GODoc *doc, GsfInput *input)
{
	Workbook *wb = WORKBOOK (doc);
	DifInputContext *ctxt = dif_input_context_new (io_context, wb, input);

	workbook_set_saveinfo (wb, FILE_FL_MANUAL_REMEMBER,
		gnm_file_saver_for_id ("Gnumeric_dif:dif"));
	if (ctxt != NULL) {
		dif_parse_sheet (ctxt);
		if (gnumeric_io_error_occurred (io_context))
			gnumeric_io_error_push (io_context,
				error_info_new_str (_("Error while reading DIF file.")));
		dif_input_context_destroy (ctxt);
	} else if (!gnumeric_io_error_occurred (io_context))
		gnumeric_io_error_unknown (io_context);
}

/*
 * Write _current_ sheet of the workbook to a DIF format file
 */
void
dif_file_save (GnmFileSaver const *fs, IOContext *io_context,
               WorkbookView const *wbv, GsfOutput *output)
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

	/* Write out the standard headers */
	res  = gsf_output_puts (output, "TABLE\n" "0,1\n" "\"GNUMERIC\"\n");
	if (res) res = gsf_output_printf (output,
					  "VECTORS\n" "0,%d\n" "\"\"\n",
					  r.end.row);
	if (res) res = gsf_output_printf (output,
					  "TUPLES\n" "0,%d\n" "\"\"\n",
					  r.end.col);
	if (res) res= gsf_output_puts (output, "DATA\n0,0\n" "\"\"\n");

	/* Process all cells */
	for (row = r.start.row; res && row <= r.end.row; row++) {
		gsf_output_puts (output, "-1,0\n" "BOT\n");
		for (col = r.start.col; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
			if (cell_is_empty (cell)) {
				gsf_output_puts(output, "1,0\n" "\"\"\n");
			} else {
				gchar *str;

				str = cell_get_rendered_text (cell);
				res = gsf_output_printf (output,
							 "1.0\n" "\"%s\"\n",
							 str);
				g_free (str);
			}
		}
	}
	gsf_output_puts (output, "-1,0\n" "EOD\n");

	if (!res)
		gnumeric_io_error_string (io_context, _("Error while saving DIF file."));
}
