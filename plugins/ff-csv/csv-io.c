/*
 * csv-io.c: read sheets using a CSV encoding.
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Jody Goldberg   <jgoldberg@home.com>
 */

#include <config.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <gnome.h>
#include "plugin.h"
#include "gnumeric.h"
#include "workbook.h"
#include "cell.h"
#include "file.h"
#include "command-context.h"

static int
csv_write_workbook (CommandContext *context, Workbook *wb,
		    const char *filename);

typedef struct {
	char const *data, *cur;
	int         len;

	int line;
	Sheet *sheet;
} FileSource_t;


static int
csv_parse_field (CommandContext *context, FileSource_t *src, Cell *cell)
{
	GString *res = NULL;
	char *field;
	char const *cur = src->cur;
	char const delim =
		(*cur != '"' && *cur != '\'')
		? ',' : *cur;
	char const * const start = (delim == ',') ? cur : ++cur;
	char *template = _("Invalid CSV file - \n"
			   "unexpected end of line at line %d");
	char *message;

	while (*cur && *cur != delim && *cur != '\n' && *cur != '\r') {
		if (*cur == '\\') {
			/* If this is the first escape character get setup */
			if (start != cur) {
				char *tmp = g_strndup (start, cur-start);
				res = g_string_new (tmp);
				g_free (tmp);
			} else
				res = g_string_new ("");

			if (!cur[1]) {
				message = g_strdup_printf (template,
							   src->line);
				gnumeric_error_read (context, message);
				g_free (message);

				return -1;
			}

			/* \r\n is a single embedded newline, ignore the \r */
			if (cur [1] == '\r' && cur [2] == '\n')
				++cur;

			g_string_append_c (res, cur[1]);
			cur += 2;
		} else {
			if (res != NULL)
				g_string_append_c (res, *cur);
			++cur;
		}
	}

	/* Skip close delimiter */
	if (*cur) {
		src->cur = cur;
		if (delim != ',')
			++src->cur;
		if (*src->cur == ',')
			++src->cur;
	}

	if (res != NULL) {
		field = res->str;
		g_string_free (res, FALSE);
	} else
		field = g_strndup (start, cur-start);

	cell_set_text_simple (cell, field);
	g_free (field);

	return 0;
}

static int
csv_parse_sheet (CommandContext *context, FileSource_t *src)
{
	int row, col;
	char *template = _("Invalid CSV file has more than the maximum\n"
			   "number of %s %d");
	char *message;
	int result;

	for (row = 0 ; *src->cur ; ++row, ++src->line, ++(src->cur)) {
		if (row >= SHEET_MAX_ROWS) {
			message = g_strdup_printf (template, _("rows"),
						   SHEET_MAX_ROWS);
			gnumeric_error_read (context, message);
			g_free (message);

			return -1;
		}

		for (col = 0 ;
		     *src->cur && *src->cur != '\n' && *src->cur != '\r' ;
		     ++col) {
			Cell *cell;
			if (col >= SHEET_MAX_COLS) {
				message = g_strdup_printf (template,
							   _("columns"),
							   SHEET_MAX_ROWS);
				gnumeric_error_read (context, message);
				g_free (message);
				
				return -1;
			}

			cell = sheet_cell_new (src->sheet, col, row);
			result = csv_parse_field (context, src, cell);
			if (result != 0)
				return result;
		}

		/* \r\n is a single end of line, ignore the \r */
		if (src->cur [0] == '\r' && src->cur [1] == '\n')
			src->cur++;
	}
	return 0;
}

#ifndef MAP_FAILED
#   define MAP_FAILED -1
#endif

static int
csv_read_workbook (CommandContext *context, Workbook *book,
		   char const *filename)
{
	int result = 0;
	int len;
	struct stat sbuf;
	char const *data;
	int const   fd = open(filename, O_RDONLY);

	if (fd < 0) {
		gnumeric_error_read (context, g_strerror (errno));
		return -1;
	}

	if (fstat(fd, &sbuf) < 0) {
		close (fd);
		gnumeric_error_read (context, g_strerror (errno));
		return -1;
	}

	len = sbuf.st_size;
	if ((caddr_t)MAP_FAILED != (data = (caddr_t) (mmap(0, len, PROT_READ,
							   MAP_PRIVATE, fd, 0)))) {
		FileSource_t src;
		char *name = g_strdup_printf (_("Imported %s"), g_basename (filename));

		src.data  = data;
		src.cur   = data;
		src.len   = len;
		src.sheet = sheet_new (book, name);

		workbook_attach_sheet (book, src.sheet);
		g_free (name);

		result = csv_parse_sheet (context, &src);

		if (result != 0)
			workbook_detach_sheet (book, src.sheet, TRUE);
		else
			workbook_set_saveinfo (book, filename, FILE_FL_MANUAL,
					       csv_write_workbook);

		munmap((char *)data, len);
	} else {
		gnumeric_error_read (context, _("Unable to mmap the file"));
		result = -1;
	}

	close(fd);

	return result;
}

static int
csv_write_cell (FILE *f, Cell *cell, int col, int row)
{
	if (col > 0)
		fputc (',', f);
	if (cell) {
		gboolean quoting = FALSE;
		const char *s;

		if (strchr (cell->text->str, ',') ||
		    strchr (cell->text->str, '"') ||
		    strchr (cell->text->str, ' ') ||
		    strchr (cell->text->str, '\t')) {
			quoting = TRUE;
			fputc ('"', f);
		}

		s = cell->text->str;
		while (*s) {
			if (*s == '"')
				fputs ("\"\"", f);
			else
				fputc (*s, f);

			s++;
		}

		if (quoting)
			fputc ('"', f);
	}

	if (ferror (f))
		return -1;

	return 0;
}


#ifndef PAGE_SIZE
#define PAGE_SIZE (BUFSIZ*8)
#endif

/*
 * write every sheet of the workbook to a comma-separated-values file
 */
static int
csv_write_workbook (CommandContext *context, Workbook *wb,
		    const char *filename)
{
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	int row, col, rc=0;
	FILE *f = fopen (filename, "w");

	if (!f) {
		gnumeric_error_save (context, g_strerror (errno));
		return -1;
	}

	setvbuf (f, NULL, _IOFBF, PAGE_SIZE);

	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;

		for (row = 0; row <= sheet->rows.max_used; row++) {
			for (col = 0; col <= sheet->cols.max_used; col++) {
				cell = sheet_cell_get (sheet, col, row);
				rc = csv_write_cell (f, cell, col, row);
				if (rc)
					goto out;
			}

			/* TODO TODO TODO : Add a flag to optionally
			 * produce \r\n pairs.
			 */
			fputc ('\n', f);
		}

		sheet_list = sheet_list->next;
	}

out:
	if (f)
		fclose (f);
	if (rc < 0)
		gnumeric_error_save (context, "");

	return rc;	/* Q: what do we have to return here?? */
}


static int
csv_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}


static void
csv_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, csv_read_workbook);
	file_format_unregister_save (csv_write_workbook);
}

#define CSV_TITLE _("Comma Separated Value (CSV) module")
#define CSV_DESCR _("This plugin reads and writes comma separated value formatted data (*.csv)")

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	char *desc;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	desc = _("Comma Separated Value (CSV) import");
	file_format_register_open (1, desc, NULL, csv_read_workbook);

	desc = _("Comma Separated Value format (*.csv)");
	file_format_register_save (".csv", desc, FILE_FL_MANUAL,
				   csv_write_workbook);

	desc = _("Comma Separated Value (CSV) module");

	if (plugin_data_init (pd, csv_can_unload, csv_cleanup_plugin,
			      CSV_TITLE, CSV_DESCR))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;
}
