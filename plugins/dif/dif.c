/*
 * dif.c: read sheets using a CSV encoding.
 *
 * Kevin Handy <kth@srv.net>
 *	Based on ff-csv code.
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


static gboolean
dif_parse_line(FileSource_t *src, char **line)
{
	const char* start = src->cur;

	if (*src->cur == '\0')
		return FALSE;

	while (*src->cur != '\0' && *src->cur != '\n' && *src->cur != '\r')
	{
		src->cur++;
	}
	*line = g_strndup(start, src->cur - start);

	if (*src->cur == '\n' && *(src->cur + 1) == '\r')
		src->cur++;
	else if (*src->cur == '\r' && *(src->cur + 1) == '\n')
		src->cur++;
	src->cur++;

	return TRUE;
}

static gboolean
dif_parse_header(FileSource_t *src)
{
	char *line1, *line2, *line3;

	while (1)
	{
		if (dif_parse_line(src, &line1) == FALSE)
			return FALSE;
		if (dif_parse_line(src, &line2) == FALSE)
			return FALSE;
		if (dif_parse_line(src, &line3) == FALSE)
			return FALSE;

		if (strcmp(line1, "TABLE") == 0)
		{
		}
		else if (strcmp(line1, "VECTORS") == 0)
		{
		}
		else if (strcmp(line1, "TUPLES") == 0)
		{
		}
		else if (strcmp(line1, "DATA") == 0)
		{
			return TRUE;
		}
		else if (strcmp(line1, "COMMENT") == 0)
		{
		}
		else if (strcmp(line1, "LABEL") == 0)
		{
		}
		else if (strcmp(line1, "SIZE") == 0)
		{
		}
		else if (strcmp(line1, "PERIODICITY") == 0)
		{
		}
		else if (strcmp(line1, "MINORSTART") == 0)
		{
		}
		else if (strcmp(line1, "TRUELENGTH") == 0)
		{
		}
		else if (strcmp(line1, "UNITS") == 0)
		{
		}
		else if (strcmp(line1, "DISPLAYUNITS") == 0)
		{
		}
		else
		{
			g_warning("DIF : Invalid header item '%s'", line1);
			g_free(line1);
			g_free(line2);
			g_free(line3);
			return FALSE;
		}
		g_free(line1);
		g_free(line2);
		g_free(line3);
	}
}

static gboolean
dif_parse_data(FileSource_t *src)
{
	char *line1, *line2;
	char *comma;
	int type;
	Cell *cell;
	int row = -1, col = 0;
	char *ch;
	int chln;

	while (1)
	{
		if (dif_parse_line(src, &line1) == FALSE)
			return FALSE;
		if (dif_parse_line(src, &line2) == FALSE)
			return FALSE;

		type = atoi(line1);	/* Supposed to stop at comma */

#if 0
g_warning("DIF cell %d : '%s', '%s'", type, line1, line2);
#endif
		switch(type)
		{
		case 0:
			if (col > SHEET_MAX_COLS) {
				g_warning("DIF : Invalid DIF file has more than the maximum number of columns %d",
					SHEET_MAX_COLS);
				return FALSE;
			}
			comma = strchr(line1, ',');
			if (comma)
			{
				comma++;
				cell = sheet_cell_new(src->sheet, col, row);
				cell_set_text_simple(cell, comma);
				col++;
			}
			break;

		case 1:
			if (col > SHEET_MAX_COLS) {
				g_warning("DIF : Invalid DIF file has more than the maximum number of columns %d",
					SHEET_MAX_COLS);
				return FALSE;
			}
			cell = sheet_cell_new(src->sheet, col, row);
			chln = strlen(line2);
			if (*line2 == '"' && *(line2 + chln - 1) == '"')
			{
				ch = g_strndup(line2 + 1, chln - 2);
				cell_set_text_simple(cell, ch);
				g_free(ch);
			}
			else
				cell_set_text_simple(cell, line2);
			col++;
			break;

		case -1:
			if (strcmp(line2, "BOT") == 0)
			{
#if 0
g_warning("DIF cell BOT");
#endif
				col = 0;
				row++;
				if (row > SHEET_MAX_ROWS) {
					g_warning("DIF : Invalid DIF file has more than the maximum number of rows %d",
						SHEET_MAX_ROWS);
					return FALSE;
				}
			}
			else if (strcmp(line2, "EOD") == 0)
			{
#if 0
g_warning("DIF cell EOD");
#endif
				g_free(line1);
				g_free(line2);
				return TRUE;
			}
			else
			{
				g_free(line1);
				g_free(line2);
				return FALSE;
			}
			break;

		default:
			return FALSE;

		}

		g_free(line1);
		g_free(line2);
	}
}

static gboolean
dif_parse_sheet (FileSource_t *src)
{
	if (dif_parse_header(src) == FALSE)
		return FALSE;
	if (dif_parse_data(src) == FALSE)
		return FALSE;

#if 0
g_warning("DIF SUCCESS");
#endif
	return TRUE;
}

#ifndef MAP_FAILED
#   define MAP_FAILED -1
#endif

static char *
dif_read_workbook (Workbook *book, char const *filename)
{
	char *result = NULL;
	int len;
	struct stat sbuf;
	char const *data;
	int const fd = open(filename, O_RDONLY);
	if (fd < 0)
		return g_strdup (g_strerror(errno));

	if (fstat(fd, &sbuf) < 0) {
		close (fd);
		return g_strdup (g_strerror(errno));
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


		if (!dif_parse_sheet (&src)) {
			workbook_detach_sheet (book, src.sheet, TRUE);
			result = g_strdup(_(("DIF : Failed to load sheet")));
		}

		munmap((char *)data, len);
	} else
		result = g_strdup (_("Unable to mmap the file"));
	close(fd);

	return result;
}


static int
dif_write_cell (FILE *f, Cell *cell, int col, int row)
{
	char* ch;
	int negative;

	if (cell) {

		switch (cell->value->type)
		{
		case VALUE_EMPTY:	/* Empty Cell */
			fputs("1,0\n\"\"\n", f);
			break;

		case VALUE_STRING:	/* Text Value */
			fputs("1,0\n\"", f);
			fputs(cell->text->str, f);
			fputs("\"\n", f);
			break;

		default:		/* Assumed Numeric */
			fputs("0,", f);
			negative = 0;
			for (ch = cell->text->str; *ch != '\0'; ch++)
			{
				/*
				 * We want to retain all the significant digits
				 * and as much formatting as is meaningful in a
				 * numeric constant, so I'm using the string
				 * format instead of monkeying around with the
				 * raw value (and I don't have to worry about
				 * what formulas do either).
				 */
				switch(*ch)
				{
				case '(':
				case '-':
					if (negative == 0)
						fputc('-', f);
					negative = -1;
					break;
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
				case '.':
					fputc(*ch, f);
					break;
				}
			}
			fputs("\n\"\"\n", f);
			break;

		}
	}

	if (ferror (f))
		return -1;

	return 0;
}


#ifndef PAGE_SIZE
#define PAGE_SIZE (BUFSIZ*8)
#endif

/*
 * write every sheet of the workbook to a DIF format file
 */
static int
dif_write_workbook (Workbook *wb, const char *filename)
{
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	int row, col, rc=0;
	FILE *f = fopen (filename, "w");
	char *workstring;

	if (!f)
		return -1;

	/*
	 * Since DIF files know nothing about paged spreadsheets,
	 * we're only going to export the first page.
	 */
	sheet_list = workbook_sheets (wb);

	if (sheet_list) {
		sheet = sheet_list->data;

		/*
		 * Write out the standard headers
		 */
		fputs ("TABLE\n0,1\n\"GNUMERIC\"\nVECTORS\n0,", f);
		setvbuf (f, NULL, _IOFBF, PAGE_SIZE);
		workstring = g_strdup_printf("%d", sheet->rows.max_used);
		fputs (workstring, f);
		g_free(workstring);
		fputs ("\n\"\"\nTUPLES\n0,", f);
		workstring = g_strdup_printf("%d", sheet->cols.max_used);
		fputs (workstring, f);
		g_free(workstring);
		fputs ("\n\"\"\nDATA\n0,0\n\"\"\n", f);

		/*
		 * Process all cells
		 */
		for (row = 0; row <= sheet->rows.max_used; row++) {

			fputs ("-1,0\nBOT\n", f);

			for (col = 0; col <= sheet->cols.max_used; col++) {
				cell = sheet_cell_get (sheet, col, row);
				rc = dif_write_cell (f, cell, col, row);
				if (rc)
					goto out;
			}

		}

		sheet_list = sheet_list->next;
	}

	fputs ("-1,0\nEOD\n", f);

out:
	if (f)
		fclose (f);
	return rc;	/* Q: what do we have to return here?? */
}


static int
dif_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}


static void
dif_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, dif_read_workbook);
	file_format_unregister_save (dif_write_workbook);
	g_free (pd->title);
}


PluginInitResult
init_plugin (CommandContext *context, PluginData * pd)
{
	char *desc;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	desc = _("Data Interchange Format (DIF) import");
	file_format_register_open (1, desc, NULL, dif_read_workbook);

	desc = _("Data Interchange Format (*.dif)");
	file_format_register_save (".dif", desc, dif_write_workbook);

	desc = _("Data Interchange Format (CSV) module");
	pd->title = g_strdup (desc);
	pd->can_unload = dif_can_unload;
	pd->cleanup_plugin = dif_cleanup_plugin;

	return PLUGIN_OK;
}
