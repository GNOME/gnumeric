/*
 * dif.c: read/write sheets using a DIF encoding.
 *
 * Authors:
 *   Kevin Handy <kth@srv.net>
 *   Zbigniew Chyla <cyba@gnome.pl>
 *
 *	Based on ff-csv code.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>

#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <numbers.h>
#include <gutils.h>
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>
#include <gnm-plugin.h>

#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;

#define N_INPUT_LINES_BETWEEN_UPDATES   50

void dif_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                    WorkbookView *wbv, GsfInput *input);
void dif_file_save (GOFileSaver const *fs, GOIOContext *io_context,
                    WorkbookView const *wbv, GsfOutput *out);

typedef struct {
	GOIOContext *io_context;

	GsfInputTextline *input;
	gint   line_no;
	gsize  line_len;
	gchar *line;

	Sheet *sheet;

	GIConv converter;
} DifInputContext;


static DifInputContext *
dif_input_context_new (GOIOContext *io_context, Workbook *wb, GsfInput *input)
{
	DifInputContext *ctxt = NULL;

	ctxt = g_new (DifInputContext, 1);
	ctxt->io_context     = io_context;

	ctxt->input	     = (GsfInputTextline *) gsf_input_textline_new (input);

	ctxt->line_no        = 1;
	ctxt->line           = NULL;
	ctxt->sheet          = workbook_sheet_add (wb, -1, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	ctxt->converter      = g_iconv_open ("UTF-8", "ISO-8859-1");

	go_io_progress_message (io_context, _("Reading file..."));

	return ctxt;
}

static void
dif_input_context_destroy (DifInputContext *ctxt)
{
	go_io_progress_unset (ctxt->io_context);
	g_object_unref (ctxt->input); ctxt->input = NULL;
	gsf_iconv_close (ctxt->converter);
	g_free (ctxt->line);
	g_free (ctxt);
}

static gboolean
dif_get_line (DifInputContext *ctxt)
{
	char *raw;

	if (NULL == (raw = gsf_input_textline_ascii_gets (ctxt->input)))
		return FALSE;

	g_free (ctxt->line);
	ctxt->line = g_convert_with_iconv (raw, -1, ctxt->converter,
					   NULL, &ctxt->line_len, NULL);

	ctxt->line_no++;
	return ctxt->line != NULL;
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_header (DifInputContext *ctxt)
{
	while (1) {
		gchar *topic = NULL, *num_line = NULL, *str_line = NULL;
		size_t str_line_len;
		int res = -1;

		if (!dif_get_line (ctxt)) {
			res = FALSE;
			goto out;
		}
		topic = g_strdup (ctxt->line);

		if (!dif_get_line (ctxt)) {
			res = FALSE;
			goto out;
		}
		num_line = g_strdup (ctxt->line);

		if (!dif_get_line (ctxt)) {
			res = FALSE;
			goto out;
		}
		str_line = g_strdup (ctxt->line);
		str_line_len = strlen (str_line);

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
			res = TRUE;
		}

		/*
		 * Other "standard" header entry items are:
		 * SIZE, LABEL, UNITS, TUPLES, VECTORS, COMMENT, MINORSTART,
		 * TRUELENGTH, PERIODICITY, DISPLAYUNITS
		 */

	out:
		g_free (topic);
		g_free (num_line);
		g_free (str_line);
		if (res >= 0)
			return res;
	}
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_data (DifInputContext *ctxt)
{
	gboolean too_many_rows = FALSE, too_many_columns = FALSE;
	gint row = -1, col = 0;
	gint val_type;
	GnmCell *cell;
	gchar *msg;

	while (1) {
		if (!dif_get_line (ctxt))
			return FALSE;

		val_type = atoi (ctxt->line);
		if (val_type == 0) {
			gchar const *comma = strchr (ctxt->line, ',');
			if (comma == NULL)
				go_io_warning (ctxt->io_context,
						_("Syntax error at line %d. Ignoring."),
						ctxt->line_no);
			else if (col > gnm_sheet_get_max_cols (ctxt->sheet)) {
				too_many_columns = TRUE;
				break;
			} else {
				gnm_float num = gnm_strto (comma+1, NULL);
				GnmValue *v = NULL;

				if (!dif_get_line (ctxt))
					return FALSE;

				if (0 == strcmp (ctxt->line, "V")) {		/* V       value */
					v = value_new_float (num);
				} else if (0 == strcmp (ctxt->line, "NA")) {	/* NA      not available res must be O */
					v = value_new_error_NA (NULL);
				} else if (0 == strcmp (ctxt->line, "TRUE")) {	/* TRUE    bool T	 res must be 1 */
					v = value_new_bool (TRUE);
				} else if (0 == strcmp (ctxt->line, "FALSE")) {	/* FALSE   bool F	 res must be O */
					v = value_new_bool (TRUE);
				} else if (0 == strcmp (ctxt->line, "ERROR")) {	/* ERROR   err		 res must be O */
					go_io_warning (ctxt->io_context,
							_("Unknown value type '%s' at line %d. Ignoring."),
							ctxt->line, ctxt->line_no);
				}

				if (NULL != v) {
					cell = sheet_cell_fetch (ctxt->sheet, col, row);
					gnm_cell_set_value (cell, v);
				}
				col++;
			}
		} else if (val_type == 1) {
			if (!dif_get_line (ctxt))
				return FALSE;
			if (col > gnm_sheet_get_max_cols (ctxt->sheet)) {
				too_many_columns = TRUE;
				continue;
			}
			cell = sheet_cell_fetch (ctxt->sheet, col, row);
			if (ctxt->line_len >= 2 &&
			    ctxt->line[0] == '"' && ctxt->line[ctxt->line_len - 1] == '"') {
				ctxt->line[ctxt->line_len - 1] = '\0';
				gnm_cell_set_text (cell, ctxt->line + 1);
			} else
				gnm_cell_set_text (cell, ctxt->line);
			col++;
		} else if (val_type == -1) {
			if (!dif_get_line (ctxt))
				return FALSE;
			if (strcmp (ctxt->line, "BOT") == 0) {
				col = 0;
				row++;
				if (row > gnm_sheet_get_max_rows (ctxt->sheet)) {
					too_many_rows = TRUE;
					break;
				}
			} else if (strcmp (ctxt->line, "EOD") == 0) {
				break;
			} else {
				msg = g_strdup_printf (
				      _("Unknown data value \"%s\" at line %d. Ignoring."),
				      ctxt->line, ctxt->line_no);
				g_warning ("%s", msg);
				g_free (msg);
			}
		} else {
			msg = g_strdup_printf (
			      _("Unknown value type %d at line %d. Ignoring."),
			      val_type, ctxt->line_no);
			g_warning ("%s", msg);
			g_free (msg);
			(void) dif_get_line (ctxt);
		}
	}

	if (too_many_rows) {
		g_warning (_("DIF file has more than the maximum number of rows %d. "
		             "Ignoring remaining rows."), gnm_sheet_get_max_rows (ctxt->sheet));
	}
	if (too_many_columns) {
		g_warning (_("DIF file has more than the maximum number of columns %d. "
		             "Ignoring remaining columns."), gnm_sheet_get_max_cols (ctxt->sheet));
	}

	return TRUE;
}

static void
dif_parse_sheet (DifInputContext *ctxt)
{
	GnmLocale *locale = gnm_push_C_locale ();

	if (!dif_parse_header (ctxt)) {
		go_io_error_info_set (ctxt->io_context, go_error_info_new_printf (
		_("Unexpected end of file at line %d while reading header."),
		ctxt->line_no));
	} else if (!dif_parse_data(ctxt)) {
		go_io_error_info_set (ctxt->io_context, go_error_info_new_printf (
		_("Unexpected end of file at line %d while reading data."),
		ctxt->line_no));
	}

	gnm_pop_C_locale (locale);
}

void
dif_file_open (GOFileOpener const *fo, GOIOContext *io_context,
               WorkbookView *wbv, GsfInput *input)
{
	Workbook *wb = wb_view_get_workbook (wbv);
	DifInputContext *ctxt = dif_input_context_new (io_context, wb, input);

	workbook_set_saveinfo (wb, GO_FILE_FL_MANUAL_REMEMBER,
		go_file_saver_for_id ("Gnumeric_dif:dif"));
	if (ctxt != NULL) {
		dif_parse_sheet (ctxt);
		if (go_io_error_occurred (io_context))
			go_io_error_push (io_context,
				go_error_info_new_str (_("Error while reading DIF file.")));
		dif_input_context_destroy (ctxt);
	} else if (!go_io_error_occurred (io_context))
		go_io_error_unknown (io_context);
}

/*
 * Write _current_ sheet of the workbook to a DIF format file
 */
void
dif_file_save (GOFileSaver const *fs, GOIOContext *io_context,
               WorkbookView const *wbv, GsfOutput *out)
{
	GnmLocale *locale;
	Sheet *sheet;
	GnmRange r;
	gint row, col;
	gboolean ok = TRUE;

	sheet = wb_view_cur_sheet (wbv);
	if (sheet == NULL) {
		go_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	r = sheet_get_extent (sheet, FALSE, TRUE);

	/* Write out the standard headers */
	gsf_output_puts   (out, "TABLE\n"   "0,1\n" "\"GNUMERIC\"\n");
	gsf_output_printf (out, "VECTORS\n" "0,%d\n" "\"\"\n", r.end.col+1);
	gsf_output_printf (out, "TUPLES\n"  "0,%d\n" "\"\"\n", r.end.row+1);
	gsf_output_puts   (out, "DATA\n"    "0,0\n"  "\"\"\n");

	locale = gnm_push_C_locale ();

	/* Process all cells */
	for (row = r.start.row; ok && row <= r.end.row; row++) {
		gsf_output_puts (out, "-1,0\n" "BOT\n");
		for (col = r.start.col; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
			if (gnm_cell_is_empty (cell)) {
				gsf_output_puts(out, "1,0\n" "\"\"\n");
			} else if (VALUE_IS_BOOLEAN (cell->value)) {
				if (value_get_as_checked_bool (cell->value))
					gsf_output_puts(out, "0,1\n" "TRUE\n");
				else
					gsf_output_puts(out, "0,0\n" "FALSE\n");
			} else if (VALUE_IS_ERROR (cell->value)) {
				if (value_error_classify (cell->value) == GNM_ERROR_NA)
					gsf_output_puts(out, "0,0\n" "NA\n");
				else
					gsf_output_puts(out, "0,0\n" "ERROR\n");
			} else if (VALUE_IS_FLOAT (cell->value))
				gsf_output_printf (out, "0,%" GNM_FORMAT_g "\n" "V\n",
					value_get_as_float (cell->value));
			else {
				gchar *str = gnm_cell_get_rendered_text (cell);
				ok = gsf_output_printf (out,
							 "1,0\n" "\"%s\"\n",
							 str);
				g_free (str);
			}
		}
	}

	gsf_output_puts (out, "-1,0\n" "EOD\n");

	gnm_pop_C_locale (locale);

	if (!ok)
		go_io_error_string (io_context, _("Error while saving DIF file."));
}
