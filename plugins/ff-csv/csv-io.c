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

struct FileSource
{
	char const *data, *cur;
	int         len;

	int line;
	Sheet *sheet;
};

static char *
csv_parse_field (struct FileSource *src)
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
csv_parse_sheet (struct FileSource *src)
{
	int row, col;
	char *field;

	for (row = 0 ; *src->cur ; ++row, ++src->line, ++(src->cur)) {
		if (row >= SHEET_MAX_ROWS) {
			g_warning ("CSV : Invalid CSV file has more than the maximum number of rows %d",
				   SHEET_MAX_ROWS);
			return NULL;
		}

		for (col = 0 ; *src->cur && *src->cur != '\n' && *src->cur != '\r' ; ++col) {
			if (col >= SHEET_MAX_COLS) {
				g_warning ("CSV : Invalid CSV file has more than the maximum number of columns %d",
					   SHEET_MAX_COLS);
				return NULL;
			}

			if (NULL != (field = csv_parse_field (src))) {
				Cell *cell = sheet_cell_new (src->sheet, col, row);
				cell_set_text_simple (cell, field);
				g_free (field);
			} else
				return FALSE;
		}
	}
	return TRUE;
}

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
		struct FileSource src;
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
csv_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}

static void
csv_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, csv_read_workbook);
}

int
init_plugin (PluginData * pd)
{
	file_format_register_open (1, _("Comma Separated Value (CSV) import"), NULL, csv_read_workbook);
	pd->can_unload = csv_can_unload;
	pd->cleanup_plugin = csv_cleanup_plugin;
	pd->title = g_strdup (_("Comma Separated Value (CSV) module"));

	return 0;
}
