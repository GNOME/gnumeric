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
#include <stdlib.h>
#include <gnome.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "../../src/gnumeric.h"
#include "../../src/gnumeric-util.h"
#include "../../src/plugin.h"
#include "csv-io.h"
#include "file.h"


static char
insert_csv_cell (Sheet* sheet, char *string, int start, int end, int col, int row)
{
	char	*p;
	int	i;
	int	ii = 0;
	Cell	*cell;

	if(sheet == NULL)
		return 0;
	
	if (start > 1 && end > 1){
		p = g_malloc (end - start + 1);

		g_assert (p != NULL);
		
		for (i = start; i <= end; i++){
			p [ii++] = string [i];
		}
		p [ii] = '\0';
	} else {
		p = (char*) g_malloc (2);
		p [0] = string [0];
		p [1] = '\0';
	}
	/*fprintf(stderr,"'%s' at col:%d, row:%d.\n", p, col, row);*/

	if ((cell = sheet_cell_get (sheet, row, col)) == NULL){
		if ((cell = sheet_cell_new (sheet, row, col)) == 0){
			return 0;
		}
	}
	cell_set_text_simple (cell, p);
	free (p);

	return 0;
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
	int		data=0;
	int		non_printable=0;

	idx = 0;
	lindex = -1;

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
				 "Can not stat the file");
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
	
	if ((file = mmap (NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0)) == (char*)-1){
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR, "Can not mmap the file");
		close (fd);
		return 0;
	}

	while (idx <= flen){
	switch (file [idx]){
		case '\r':
			if (file [idx+1] == '\n')
				idx++;	
			break;
		case '\n':
			if (data){  /* Non empty line */
				insert_csv_cell (sheet, file, lindex, idx-1, crow, ccol);
			}
			data = 0;
			lindex = idx+1;
			if (ccol > mcol){
				mcol=ccol;
			}
			ccol = 0;
			crow++;
			idx++;
			break;
		case ',':
			if(data){  /* Non empty cell */
				insert_csv_cell (sheet, file, lindex, idx-1, crow, ccol);
			}
			data = 0;
			lindex = idx+1;
			ccol++;
			idx++;
			break;

		default:
			if (file [idx] < 21 || file [idx] > 126){
				non_printable++;
				if (non_printable > 10){ /* FIXME: ARBITRARY VALUE */   
					close (fd);
					return 0;
				}
			}
			idx++;
			data = 1;
			break;
		}
	}

	if (sheet){
		sheet->max_col_used=mcol;
		sheet->max_row_used=crow;
	}
	munmap (file, flen);	
	close (fd);
	return 1;
}


Workbook *
csv_read_workbook (const char* filename)
{
	Workbook	*book;
	Sheet		*sheet;

	book = workbook_new ();  /* FIXME: Can this return NULL? */
	sheet = sheet_new (book, _("NoName"));
	workbook_attach_sheet (book, sheet);

	/*if (sheet != NULL){
		book->sheet = sheet;
	}*/	

	if ((csv_parse_file (filename, sheet)) == 0){
		return NULL;
	}

	return book;
}


static gboolean
csv_probe (const char *filename)
{
	if(csv_parse_file (filename,0) == 1){
		return TRUE;
	} else {
		return FALSE;
	}
}

void
csv_init (void)
{
	char *desc = _("CSV (comma separated values)");
	
	file_format_register_open (0, desc, csv_probe, csv_read_workbook);
	/* file_format_register_save (".csv", desc, gnumericWriteCSVWorkbook);*/
}

static int
csv_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_save (csv_read_workbook);
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
	pd->title = g_strdup (_("CSV (comma separated value file import/export plugin"));
	
	return 0;
}
