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
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "file.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#define N_INPUT_LINES_BETWEEN_UPDATES   50

void dif_file_open (GnumFileOpener const *fo, IOContext *io_context,
                    WorkbookView *wbv, char const *filename);
void dif_file_save (GnumFileSaver const *fs, IOContext *io_context,
                    WorkbookView *wbv, const gchar *file_name);

typedef struct {
	IOContext *io_context;

	gint   data_size;
	guchar *data, *cur;

	gint   line_no;
	gchar *line;
	gint   line_len, alloc_line_len;

	Sheet *sheet;
} DifInputContext;


static DifInputContext *
dif_input_context_new (IOContext *io_context, Workbook *wb, char const *file_name)
{
	DifInputContext *ctxt = NULL;
	gint size;
	guchar *data;
	ErrorInfo *mmap_error;

	data = gnumeric_mmap_error_info (file_name, &size, &mmap_error);
	if (mmap_error != NULL) {
		gnumeric_io_error_info_set (io_context, mmap_error);
		return NULL;
	}

	ctxt = g_new (DifInputContext, 1);
	ctxt->io_context     = io_context;
	ctxt->data_size      = size;
	ctxt->data           = data;
	ctxt->cur            = data;
	ctxt->line_no        = 1;
	ctxt->line           = g_malloc (1);
	ctxt->line_len       = 0;
	ctxt->alloc_line_len = 0;
	ctxt->sheet          = workbook_sheet_add (wb, NULL, FALSE);

	io_progress_message (io_context, _("Reading file..."));
	memory_io_progress_set (io_context, ctxt->data, ctxt->data_size);

	return ctxt;
}

static void
dif_input_context_destroy (DifInputContext *ctxt)
{
	io_progress_unset (ctxt->io_context);
	munmap (ctxt->data, ctxt->data_size);
	g_free (ctxt->line);
	g_free (ctxt);
}

static gboolean
dif_get_line (DifInputContext *ctxt)
{
	guchar *p, *p_limit;

	p_limit = ctxt->data + ctxt->data_size;
	if (ctxt->cur >= p_limit) {
		ctxt->line[0] = '\0';
		ctxt->line_len = 0;
		return FALSE;
	}

	for (p = ctxt->cur; p < p_limit && p[0] != '\n' && p[0] != '\r'; p++);

	ctxt->line_len = p - ctxt->cur;
	if (ctxt->line_len > ctxt->alloc_line_len) {
		g_free (ctxt->line);
		ctxt->alloc_line_len = MAX (ctxt->alloc_line_len * 2, ctxt->line_len);
		ctxt->line = g_malloc (ctxt->alloc_line_len + 1);
	}
	if (ctxt->line_len > 0) {
		memcpy (ctxt->line, ctxt->cur, ctxt->line_len);
	}
	ctxt->line[ctxt->line_len] = '\0';

	if (p == p_limit || (p == p_limit - 1 && (p[0] == '\n' || p[0] == '\r'))) {
		ctxt->cur = p_limit;
	} else if ((p[0] == '\n' && p[1] == '\r') || (p[0] == '\r' && p[1] == '\n')) {
		ctxt->cur = p + 2;
	} else {
		ctxt->cur = p + 1;
	}

	if ((++ctxt->line_no % N_INPUT_LINES_BETWEEN_UPDATES) == 0) {
		memory_io_progress_update (ctxt->io_context, ctxt->cur);
	}

	return TRUE;
}

static gboolean
dif_eat_line (DifInputContext *ctxt)
{
	guchar *p, *p_limit;

	p_limit = ctxt->data + ctxt->data_size;
	if (ctxt->cur >= p_limit) {
		return FALSE;
	}

	for (p = ctxt->cur; p < p_limit && p[0] != '\n' && p[0] != '\r'; p++);

	if (p == p_limit || (p == p_limit - 1 && (p[0] == '\n' || p[0] == '\r'))) {
		ctxt->cur = p;
	} else if ((p[0] == '\n' && p[1] == '\r') || (p[0] == '\r' && p[1] == '\n')) {
		ctxt->cur = p + 2;
	} else {
		ctxt->cur = p + 1;
	}

	if ((++ctxt->line_no % N_INPUT_LINES_BETWEEN_UPDATES) == 0) {
		memory_io_progress_update (ctxt->io_context, ctxt->cur);
	}

	return TRUE;
}

/*
 * Raturns FALSE on EOF.
 */
static gboolean
dif_parse_header (DifInputContext *ctxt)
{
	while (1) {
		gboolean lines_ok = TRUE;
		gchar *topic, *num_line, *str_line;
		gint str_line_len;

		lines_ok = lines_ok && dif_get_line (ctxt);
		topic = g_alloca (ctxt->line_len + 1);
		strcpy (topic, ctxt->line);

		lines_ok = lines_ok && dif_get_line (ctxt);
		num_line = g_alloca (ctxt->line_len + 1);
		strcpy (num_line, ctxt->line);

		lines_ok = lines_ok && dif_get_line (ctxt);
		str_line_len = ctxt->line_len;
		str_line = g_alloca (str_line_len + 1);
		strcpy (str_line, ctxt->line);

		if (!lines_ok) {
			return FALSE;
		}

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
		Cell *cell;
		gchar *msg;

		if (!dif_get_line (ctxt)) {
			return FALSE;
		}

		val_type = atoi (ctxt->line);
		if (val_type == 0) {
			gchar *comma;

			(void) dif_eat_line (ctxt);
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
			cell = sheet_cell_fetch (ctxt->sheet, col, row);
			if (ctxt->line_len >= 2 && ctxt->line[0] == '"' &&
			    ctxt->line[ctxt->line_len - 1] == '"') {
				ctxt->line[ctxt->line_len - 1] = '\0';
				cell_set_text (cell, ctxt->line + 1);
			} else {
				cell_set_text (cell, ctxt->line);
			}
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
			(void) dif_eat_line (ctxt);
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
dif_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wbv, char const *file_name)
{
	DifInputContext *ctxt;

	ctxt = dif_input_context_new (io_context, wb_view_workbook (wbv), file_name);
	if (ctxt != NULL) {
		dif_parse_sheet (ctxt);
		if (gnumeric_io_error_occurred (io_context)) {
			gnumeric_io_error_push (io_context, error_info_new_str (
			_("Error while reading DIF file.")));
		}
		dif_input_context_destroy (ctxt);
	} else {
		if (!gnumeric_io_error_occurred) {
			gnumeric_io_error_unknown (io_context);
		}
	}
}

/*
 * Write _current_ sheet of the workbook to a DIF format file
 */
void
dif_file_save (GnumFileSaver const *fs, IOContext *io_context,
               WorkbookView *wbv, const gchar *file_name)
{
	FILE *f;
	ErrorInfo *open_error;
	Sheet *sheet;
	Range r;
	gint row, col;

	f = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (f == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	sheet = wbv->current_sheet;
	if (sheet == NULL) {
		gnumeric_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	r = sheet_get_extent (sheet, FALSE);

	/* Write out the standard headers */
	fputs ("TABLE\n" "0,1\n" "\"GNUMERIC\"\n", f);
	fprintf (f, "VECTORS\n" "0,%d\n" "\"\"\n", r.end.row);
	fprintf (f, "TUPLES\n" "0,%d\n" "\"\"\n", r.end.col);
	fputs ("DATA\n0,0\n" "\"\"\n", f);

	/* Process all cells */
	for (row = r.start.row; row <= r.end.row; row++) {
		fputs ("-1,0\n" "BOT\n", f);
		for (col = r.start.col; col <= r.end.col; col++) {
			Cell *cell;

			cell = sheet_cell_get (sheet, col, row);
			if (cell_is_blank (cell)) {
				fputs("1,0\n" "\"\"\n", f);
			} else {
				gchar *str;

				str = cell_get_rendered_text (cell);
				fprintf (f, "1.0\n" "\"%s\"\n", str);
				g_free (str);
			}
		}
	}
	fputs ("-1,0\n" "EOD\n", f);

	if (ferror (f)) {
		gnumeric_io_error_string (io_context, _("Error while saving DIF file."));
	}

	fclose (f);
}
