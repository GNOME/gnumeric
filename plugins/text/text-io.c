/*
 * text-io.c: save/read gnumeric Sheets using an simple text encoding.
 * (most of the code taken from xml-io.c and xml-io.c)
 *
 * Takashi Matsuda <matsu@arch.comp.kyutech.ac.jp>
 *
 * $Id$
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <gnome.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include "text-io.h"
#include "file.h"

typedef struct {
	gint     status;
	gint     col;
	gint     row;
	gint     sht;
	FILE    *file;
	Sheet   *sheet;
} TextData;

/* these seems not necessary
static Sheet    *text_read_sheet     (const char *filename);
static int       text_write_sheet    (Sheet *sheet, const char *filename);
*/

static gboolean  text_read_workbook  (Workbook *wb, const char *filename);
static int       text_write_workbook (Workbook *wb, const char *filename);

static void      writeTextSheet      (TextData *data, Sheet *sheet);
static void      writeTextWorkbook   (TextData *data, Workbook *wb);

static void
get_cell_pos (gpointer key, gpointer value, gpointer d)
{
	gint col = -1, row = -1;
	gint *data = (gint *) d;
	Cell *cell = (Cell *) value;

	if (cell && cell->col)
		col = cell->col->pos;
	if (cell && cell->row)
		row = cell->row->pos;

	if (col < 0 && row < 0)
		return;

	if (!cell->text || !cell->text->str || cell->text->str[0] == '\0')
		return;

	if (data[0] == -1 || col < data[0])
		data[0] = col;
	if (data[1] == -1 || col > data[1])
		data[1] = col;

	if (data[2] == -1 || row < data[2])
		data[2] = row;
	if (data[3] == -1 || row > data[3])
		data[3] = row;
}

static int
_sheet_get_actual_area (Sheet *sheet,
			gint *min_col, gint *min_row,
			gint *max_col, gint *max_row)
{
	gint dat[4] = {-1, -1, -1, -1};

	g_return_val_if_fail (sheet != NULL, -1);

	g_hash_table_foreach (sheet->cell_hash, get_cell_pos, dat);

	if (min_col)
		*min_col = dat[0];
	if (max_col)
		*max_col = dat[1];

	if (min_row)
		*min_row = dat[2];
	if (max_row)
		*max_row = dat[3];

	return (dat[0]>=0 && dat[1]>=0 && dat[2]>=0 && dat[3]>=0)? 0:-1;
}

static void
writeTextSheet (TextData *data, Sheet *sheet)
{
	gint min_col, max_col, min_row, max_row;
	gint res;
	gint i, j;
	Cell *cell;

	/*
	* General informations about the Sheet.
	*/

	res = _sheet_get_actual_area (sheet,
				      &min_col, &min_row, &max_col, &max_row);

	if (res == 0){
		if (data->sht > 0)
			fputc ('\f', data->file);
		data->sht ++;

		if (sheet->name)
			fputs (sheet->name, data->file);
		fputc ('\n', data->file);

		/*
		 * Cells informations
		 */

		for (i=min_row; i<=max_row; i++){
			for (j=min_col; j<=max_col; j++){
				if (j != min_col)
					fputc ('\t', data->file);

				cell = sheet_cell_get (sheet, j, i);
				if (cell && cell->text && cell->text->str)
					fputs (cell->text->str, data->file);
			}
			fputc ('\n', data->file);
		}
	}
}

static void
writeTextSheetTo (gpointer key, gpointer value, gpointer d)
{
	TextData *data  = (TextData *) d;
	Sheet    *sheet = (Sheet *) value;

	writeTextSheet (data, sheet);
}

static void
writeTextWorkbook (TextData *data, Workbook *wb)
{
	g_hash_table_foreach (wb->sheets, writeTextSheetTo, data);
}

#if 0
static gint
change_sheet_name (Sheet* sheet, char *string, int start, int end)
{
	gchar  *p;

	if(sheet == NULL)
		return  -1;

	if (start < 0 || end <= start)
		return -1;

	p = g_malloc (end - start + 1);
	g_assert (p != NULL);
	g_memmove (p, string + start, end - start);
	p[end - start] = '\0';

	sheet_rename (sheet, p);
	free (p);

	return 0;
}
#endif


static gint
insert_cell (Sheet* sheet, char *string, int start, int end, int col, int row)
{
	gchar  *p;
	Cell   *cell;

	if(sheet == NULL)
		return  -1;

	if (start < 0 || end <= start)
		return -1;

	p = g_malloc (end - start + 1);
	g_assert (p != NULL);
	g_memmove (p, string + start, end - start);
	p[end - start] = '\0';

	/*fprintf(stderr,"'%s' at col:%d, row:%d.\n", p, col, row);*/

	if ((cell = sheet_cell_get (sheet, row, col)) == NULL){
		if ((cell = sheet_cell_new (sheet, row, col)) == 0){
			return -1;
		}
	}
	cell_set_text_simple (cell, p);
	free (p);

	return 0;
}

static gint
text_parse_file (gchar *file, gint flen, gint start,
		 Sheet *sheet)
{
	gint     idx, lindex;
	gint     crow = 0, ccol = 0, mcol = 0; /* current/max col/row */
	gint     data = 0;
	gboolean sheet_end = FALSE;     /* '\f' separates the sheets */
	gboolean sheet_name = TRUE;     /* first line is sheet name */

	idx = start;
	lindex = start;

	while (!sheet_end && idx < flen){
		gboolean done = FALSE;
		gint clen = mblen (file+idx, flen-idx);

		/*
		 * if charactor is something wrong, treat it as 1byte character.
		 */

		if (clen <= 0)
			clen = 1;

		if (clen == 1){
			switch (file[idx]){
			case '\r':
				idx ++;
				done = TRUE;
				break;

			case '\n':
			case '\f':
				if (data)
					insert_cell (sheet, file,
						     lindex, idx,
						     crow, ccol);
				if (ccol > mcol)
					mcol = ccol;
				ccol = 0;
				crow ++;
				if (file[idx] == '\f')
					sheet_end = TRUE;
				data = 0;
				idx ++;
				lindex = idx;
				done = TRUE;
				break;

			case '\t':
				if(data){  /* Non empty cell */
					insert_cell (sheet, file,
						     lindex, idx, crow, ccol);
				}
				data = 0;
				ccol ++;
				idx ++;
				lindex = idx;
				sheet_name = FALSE;
				done = TRUE;
				break;
			}
		}
		if (!done){
			idx += clen;
			data = 1;
		}
	}

	if (data && idx >= 0){
		insert_cell (sheet, file, lindex, idx, crow, ccol);
		crow ++;
	}

	sheet->cols.max_used = mcol;
	sheet->rows.max_used = crow;

	return idx;
}


static gboolean
readTextWorkbook (Workbook *book, const char* filename, gboolean probe)
{
	Sheet      *sheet = NULL;
	int	    fd;
	struct stat buf;
	gint	    flen;	/* file length */
	gchar      *file;	/* data pointer */
	gint	    idx;

	if ((fd = open (filename, O_RDONLY)) < 0){
		char *msg;
		int   err = errno;

		msg = g_strdup_printf (_("While opening %s\n%s"),
				       filename, g_strerror (err));
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);

		return FALSE;
	}

	if (fstat (fd, &buf) == -1){
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				 "Can not stat the file");
		close(fd);
		return FALSE;
	}

	/* FIXME: ARBITRARY VALUE */
	if (buf.st_size < 1 || buf.st_size > 1000000){
		close(fd);
		return FALSE;
	} else
		flen = buf.st_size;

	file = mmap (NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == (char*)-1){
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				 "Can not mmap the file");
		close (fd);
		return FALSE;
	}

	idx = 0;
	while (idx >= 0 && idx < flen){
		sheet = sheet_new (book, _("NoName"));
		if (sheet == NULL)
			break;

		idx = text_parse_file (file, flen, idx, sheet);

		if (idx >= 0){
			sheet->modified = FALSE;
			workbook_attach_sheet (book, sheet);
		}
	}

	munmap (file, flen);
	close (fd);

	return TRUE;
}

static gboolean
text_read_workbook (Workbook *wb, const char* filename)
{
	gboolean ret;

	ret = readTextWorkbook (wb, filename, FALSE);
	if (ret){
		workbook_set_filename (wb, filename);
		workbook_set_title (wb, filename);
		workbook_recalc_all (wb);
	}

	return TRUE;
}

#if 0
int
text_write_sheet (Sheet * sheet, const char *filename)
{
	TextData data = { 0, 0, 0, 0, NULL };

	g_return_val_if_fail (sheet != NULL, -1);
	g_return_val_if_fail (IS_SHEET (sheet), -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	* Open output file
	*/
	data.file = fopen (filename, "w");
	if (data.file == NULL)
		return -1;

	writeTextSheet (&data, sheet);

	fclose (data.file);

	if (data.status == 0)
		sheet->modified = FALSE;

	return data.status;
}
#endif

/*
* Save a Workbook in a simple text formatted file
* returns 0 in case of success, -1 otherwise.
*/

int
text_write_workbook (Workbook *wb, const char *filename)
{
	TextData data = { 0, 0, 0, 0, NULL };

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	* Open Output File
	*/
	data.file = fopen (filename, "w");
	if (data.file == NULL)
		return -1;

	writeTextWorkbook (&data, wb);

	fclose (data.file);

	return data.status;
}

static void
text_init (void)
{
	char *desc = _("Simple Text Format");

	file_format_register_open (0, desc,
				   NULL,
				   text_read_workbook);

	file_format_register_save (".txt", desc,
				   text_write_workbook);
}

static void
text_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, text_read_workbook);
	file_format_unregister_save (text_write_workbook);
	g_free (pd->title);
}

static int
text_can_unload (PluginData *pd)
{
	return TRUE;
}

int
init_plugin (CmdContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return -2;

	text_init ();

	pd->can_unload = text_can_unload;
	pd->cleanup_plugin = text_cleanup_plugin;
	pd->title = g_strdup (_("TXT (simple text import/export plugin)"));

	return 0;
}
