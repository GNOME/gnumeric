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
#include "file.h"

typedef struct {
	char const *data, *cur;
	int         len;

	int line;
	Sheet *sheet;
} FileSource_t;


static char *
csv_parse_field (FileSource_t *src)
{
	GString *res = NULL;
	char const *cur = src->cur;
	char const delim =
		(*cur != '"' && *cur != '\'')
		? ',' : *cur;
	char const * const start = (delim == ',') ? cur : ++cur;

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
				g_warning ("CSV : Unexpected end of line at line %d", src->line);
				return NULL;
			}

			g_string_append_c (res, cur[1]);
			cur += 2;
		} else {
			if (res != NULL)
				g_string_append_c (res, *cur);
			++cur;
		}
	}

	/* Skip close delemiter */
	if (*cur) {
		src->cur = cur;
		if (delim != ',')
			++src->cur;
		if (*src->cur == ',')
			++src->cur;
	}

	if (res != NULL) {
		char *tmp = res->str;
		g_string_free (res, FALSE);
		return tmp;
	}

	return g_strndup (start, cur-start);
}

static gboolean
csv_parse_sheet (FileSource_t *src)
{
	int row, col;
	char *field;

	for (row = 0 ; *src->cur ; ++row, ++src->line, ++(src->cur)) {
		if (row >= SHEET_MAX_ROWS) {
			g_warning ("CSV : Invalid CSV file has more than the maximum number of rows %d",
				   SHEET_MAX_ROWS);
			return FALSE;
		}

		for (col = 0 ; *src->cur && *src->cur != '\n' && *src->cur != '\r' ; ++col) {
			if (col >= SHEET_MAX_COLS) {
				g_warning ("CSV : Invalid CSV file has more than the maximum number of columns %d",
					   SHEET_MAX_COLS);
				return FALSE;
			}

			if (NULL != (field = csv_parse_field (src))) {
				Cell *cell = sheet_cell_new (src->sheet, col, row);
				cell_set_text_simple (cell, field);
				g_free (field);
			} else
				return FALSE;

			if (src->cur [0] == '\r' && src->cur [1] == '\n')
				src->cur++;
		}
	}
	return TRUE;
}

#ifndef MAP_FAILED
#   define MAP_FAILED -1
#endif

static gboolean
csv_read_workbook (Workbook *book, char const *filename)
{
	/* TODO : When there is a reasonable error reporting
	 * mechanism use it and put all the error code back
	 */
	gboolean result = FALSE;
	int len;
	struct stat sbuf;
	char const *data;
	int const fd = open(filename, O_RDONLY);
	if (fd < 0)
		return FALSE;

	if (fstat(fd, &sbuf) < 0) {
		close (fd);
		return FALSE;
	}

	len = sbuf.st_size;
	if (MAP_FAILED != (data = (char const *) (mmap(0, len, PROT_READ,
						       MAP_PRIVATE, fd, 0)))) {
		FileSource_t src;
		char * name = g_strdup_printf (_("Imported %s"), g_basename (filename));

		src.data  = data;
		src.cur   = data;
		src.len   = len;
		src.sheet = sheet_new (book, name);

		workbook_attach_sheet (book, src.sheet);
		g_free (name);

		result = csv_parse_sheet (&src);

		if (!result)
			workbook_detach_sheet (book, src.sheet, TRUE);

		munmap((char *)data, len);
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
csv_write_workbook (Workbook *wb, const char *filename)
{
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	int row, col, rc=0;
	FILE *f = fopen (filename, "w");

	if (!f)
		return -1;
	
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
			
			fputc ('\n', f);
		}

		sheet_list = sheet_list->next;
	}

out:
	if (f)
		fclose (f);
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
	g_free (pd->title);
}


int
init_plugin (PluginData * pd)
{
	char *desc;
	
	desc = _("Comma Separated Value (CSV) import");
	file_format_register_open (1, desc, NULL, csv_read_workbook);

	desc = _("Comma Separated Value format (*.csv)");
	file_format_register_save (".csv", desc, csv_write_workbook);

	desc = _("Comma Separated Value (CSV) module");
	pd->title = g_strdup (desc);
	pd->can_unload = csv_can_unload;
	pd->cleanup_plugin = csv_cleanup_plugin;

	return 0;
}
