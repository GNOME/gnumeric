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
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "gnumeric.h"
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "file.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

void dif_file_open (GnumFileOpener const *fo, IOContext *io_context,
                    WorkbookView *wb_view, char const *filename);
void dif_file_save (GnumFileSaver const *fs, IOContext *io_context,
                    WorkbookView *wb_view, const gchar *file_name);

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
			if (comma) {
				comma++;
				cell = sheet_cell_fetch (src->sheet, col, row);
				cell_set_text (cell, comma);
				col++;
			}
			break;

		case 1:
			if (col > SHEET_MAX_COLS) {
				g_warning("DIF : Invalid DIF file has more than the maximum number of columns %d",
					SHEET_MAX_COLS);
				return FALSE;
			}
			cell = sheet_cell_fetch (src->sheet, col, row);
			chln = strlen(line2);
			if (*line2 == '"' && *(line2 + chln - 1) == '"') {
				ch = g_strndup(line2 + 1, chln - 2);
				cell_set_text (cell, ch);
				g_free(ch);
			} else
				cell_set_text (cell, line2);
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

void
dif_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wb_view, char const *filename)
{
	int len;
	struct stat sbuf;
	char const *data;
	int fd;
	ErrorInfo *error;

	fd = gnumeric_open_error_info (filename, O_RDONLY, &error);
	if (fd < 0) {
		gnumeric_io_error_info_set (io_context, error);
		return;
	}

	if (fstat (fd, &sbuf) < 0) {
		gnumeric_io_error_info_set (io_context, error_info_new_from_errno ());
		close (fd);
		return;
	}

	len = sbuf.st_size;
	if ((caddr_t)MAP_FAILED != (data = (caddr_t) (mmap(0, len, PROT_READ,
							   MAP_PRIVATE, fd, 0)))) {
		Workbook *wb = wb_view_workbook (wb_view);
		FileSource_t src;
		src.data  = data;
		src.cur   = data;
		src.len   = len;
		src.sheet = workbook_sheet_add (wb, NULL, FALSE);

		if (!dif_parse_sheet (&src)) {
			gnumeric_io_error_info_set (io_context,
			                            error_info_new_str (_("DIF : Failed to load sheet")));
		}
		munmap((char *)data, len);
	} else {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str (_("Unable to mmap the file")));
	}
	close (fd);
}

static int
dif_write_cell (FILE *f, Cell const *cell)
{
	if (!cell_is_blank (cell)) {
		char * text = cell_get_rendered_text (cell);

		/* FIXME : I have no idea the original code was trying to
		 * do but it was definitely wrong.  This is only marginally
		 * better.  It will dump the rendered string as a single line.
		 * with the magic prefix and quotes from the original.  At
		 * least it will not completely disregard negatives.
		 */
		fputs("1,0\n\"", f);
		fputs(text, f);
		fputs("\"\n", f);
		g_free (text);
	}

	if (ferror (f))
		return -1;

	return 0;
}

/*
 * write every sheet of the workbook to a DIF format file
 */
void
dif_file_save (GnumFileSaver const *fs, IOContext *io_context,
               WorkbookView *wb_view, const gchar *file_name)
{
	Workbook *wb = wb_view_workbook (wb_view);
	GList *sheet_list;
	Cell *cell;
	int row, col, rc=0;
	char *workstring;
	FILE *f;
	ErrorInfo *error;

	f = gnumeric_fopen_error_info (file_name, "w", &error);
	if (f == NULL) {
		gnumeric_io_error_info_set (io_context, error);
		return;
	}

	/*
	 * Since DIF files know nothing about paged spreadsheets,
	 * we're only going to export the first page.
	 */
	sheet_list = workbook_sheets (wb);

	/* FIXME : Why not a loop ? */
	if (sheet_list) {
		Sheet *sheet = sheet_list->data;
		Range r = sheet_get_extent (sheet);

		/*
		 * Write out the standard headers
		 */
		fputs ("TABLE\n0,1\n\"GNUMERIC\"\nVECTORS\n0,", f);
		workstring = g_strdup_printf("%d", r.end.row);
		fputs (workstring, f);
		g_free(workstring);
		fputs ("\n\"\"\nTUPLES\n0,", f);
		workstring = g_strdup_printf("%d", r.end.col);
		fputs (workstring, f);
		g_free(workstring);
		fputs ("\n\"\"\nDATA\n0,0\n\"\"\n", f);

		/*
		 * Process all cells
		 */
		for (row = r.start.row; row <= r.end.row; row++) {

			fputs ("-1,0\nBOT\n", f);

			for (col = r.start.col; col <= r.end.col; col++) {
				cell = sheet_cell_get (sheet, col, row);
				rc = dif_write_cell (f, cell);
				if (rc)
					goto out;
			}

		}
	}
	g_list_free (sheet_list);

	fputs ("-1,0\nEOD\n", f);

out:
	if (f)
		fclose (f);
	if (rc < 0) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str (_("Error while saving dif file.")));
	}
}
