/*
 * csv-io.c: save/read Sheets using a CSV encoding.
 * (some code taken from xml-io.c by Daniel Veillard <Daniel.Veillard@w3.org>)
 *
 * Vincent Renardias <vincent@ldsol.com>
 *
 * $Id$
 */

/*
 * TODO:
 * handle quoted CSV
 */

#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include "csv-io.h"
#include "file.h"
#include "gnumeric-util.h"


static void
insert_csv_cell (Sheet* sheet, const char *string, int start, int end, int col, int row)
{
	char *p;
	Cell *cell;
	int len;

	if(sheet == NULL)
		return;

	len = end - start + 1;
	if (len < 0)
		return;
	p = g_new (char, len + 1);
	memcpy (p, string + start, len);
	p[len] = 0;

#if 0
	fprintf(stderr,"'%s' at col:%d, row:%d.\n", p, col, row);
#endif

	if ((cell = sheet_cell_get (sheet, row, col)) == NULL){
		if ((cell = sheet_cell_new (sheet, row, col)) == 0){
			g_free (p);
			return;
		}
	}
	cell_set_text_simple (cell, p);
	g_free (p);
}

static int
csv_parse_file (const char *filename,Sheet *sheet)
{
	int		fd;
	struct stat	buf;
	int		flen;	/* file length */
	char		*file;	/* data pointer */
	int		idx, lindex;
	int		crow=0,ccol=0,mcol=0; /* current/max col/row */
	gboolean        file_mmaped, data;

	struct {
		int non_printables;
		int lines;
		int commas;
	} statistics;

	if ((fd = open (filename, O_RDONLY)) < 0){
		char *msg;
		int  err = errno;

		msg = g_strdup_printf (_("While opening %s\n%s"),
					 filename, g_strerror (err));
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);

		return 0;
	}

	if (fstat (fd, &buf) == -1){
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				 "Cannot stat the file");
		close(fd);
		return 0;
	}

	/* FIXME: ARBITRARY VALUE */
	if (buf.st_size < 1 || buf.st_size > 1000000){
		close(fd);
		return 0;
	} else {
		flen = buf.st_size;
	}

	file = mmap (NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == (char*)-1) {
		/* mmap failed.  */
		file_mmaped = FALSE;

		file = g_new (char, flen);
		if (file) {
			if (read (fd, file, flen) != flen) {
				g_free (file);
				file = 0;
			}
		}
	} else {
		file_mmaped = TRUE;
	}
	close (fd);

	if (!file) {
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				 _("Failed to read csv file"));
		return 0;
	}

	statistics.non_printables = 0;
	statistics.lines = 0;
	statistics.commas = 0;

	idx = 0;
	lindex = 0;
	data = FALSE;

	while (idx < flen) {
	switch (file [idx]) {
		case '\r':
			if (idx + 1 == flen || file [idx+1] != '\n')
				statistics.non_printables++;
			idx++;
			break;
		case '\n':
			if (data){  /* Non empty line */
				insert_csv_cell (sheet, file, lindex, idx-1, crow, ccol);
			}
			data = FALSE;
			lindex = idx+1;
			if (ccol > mcol){
				mcol=ccol;
			}
			ccol = 0;
			crow++;
			idx++;
			statistics.lines++;
			break;
		case ',':
			if(data){  /* Non empty cell */
				insert_csv_cell (sheet, file, lindex, idx-1, crow, ccol);
			}
			data = FALSE;
			lindex = idx+1;
			ccol++;
			idx++;
			statistics.commas++;
			break;

		default:
			if (!isspace ((unsigned char)file[idx]) &&
			    !isprint ((unsigned char)file[idx]))
				statistics.non_printables++;
			idx++;
			data = TRUE;
			break;
		}
	}

	if (sheet) {
		sheet->max_col_used=mcol;
		sheet->max_row_used=crow;
	}

	if (file_mmaped)
		munmap (file, flen);
	else
		g_free (file);

	/* Heuristics ahead!  */
	if (statistics.non_printables > flen / 200 ||
	    statistics.commas < statistics.lines / 2) {
		return 0;
	}

	return 1;
}


static Workbook *
csv_read_workbook (const char* filename)
{
	Workbook	*book;
	Sheet		*sheet;

	book = workbook_new ();
	if (!book) return NULL;

	sheet = sheet_new (book, _("NoName"));
	workbook_attach_sheet (book, sheet);

	/*if (sheet != NULL){
		book->sheet = sheet;
	}*/

	if ((csv_parse_file (filename, sheet)) == 0) {
		workbook_destroy (book);
		return NULL;
	}

	return book;
}


static gboolean
csv_probe (const char *filename)
{
	if(csv_parse_file (filename, NULL) == 1){
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
csv_init (void)
{
	const char *desc = _("CSV (comma separated values)");

	file_format_register_open (1, desc, csv_probe, csv_read_workbook);
	/* file_format_register_save (".csv", desc, gnumericWriteCSVWorkbook);*/
}

static void
csv_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (csv_probe, csv_read_workbook);
	/* file_format_unregister_save (csv_read_workbook); */
}

static int
csv_can_unload (PluginData *pd)
{
	return TRUE;
}

int
init_plugin (PluginData *pd)
{
	csv_init ();

	pd->can_unload = csv_can_unload;
	pd->cleanup_plugin = csv_cleanup_plugin;
	pd->title = g_strdup (_("CSV (comma separated value file import/export plugin)"));

	return 0;
}
