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
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include "workbook.h"
#include "cell.h"
#include "command-context.h"
#include "file.h"
#include "plugin.h"
#include "plugin-util.h"
#include "rendered-value.h"

typedef struct {
	gint     status;
	gint     col;
	gint     row;
	gint     sht;
	FILE    *file;
	Sheet   *sheet;
} TextData;

static int       text_write_workbook (CommandContext *context, Workbook *wb,
				      const char *filename);

static void      writeTextSheet      (TextData *data, Sheet *sheet);
static void      writeTextWorkbook   (TextData *data, Workbook *wb);

/* FIXME : Delete this.  The core should provide appropriate utils */
static void
get_cell_pos (gpointer key, gpointer value, gpointer d)
{
	gint col = -1, row = -1;
	gint *data = (gint *) d;
	Cell *cell = (Cell *) value;

	if (cell_is_blank(cell))
		return;

	if (cell->col_info)
		col = cell->col_info->pos;
	if (cell->row_info)
		row = cell->row_info->pos;

	if (col < 0 && row < 0)
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

		if (sheet->name_unquoted)
			fputs (sheet->name_unquoted, data->file);
		fputc ('\n', data->file);

		/*
		 * Cells informations
		 */

		for (i=min_row; i<=max_row; i++){
			for (j=min_col; j<=max_col; j++){
				if (j != min_col)
					fputc ('\t', data->file);

				cell = sheet_cell_get (sheet, j, i);
				if (cell != NULL) {
					char * text = cell_get_rendered_text (cell);
					fputs (text, data->file);
					g_free (text);
				}
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
/*
 * FIXME:
 * Enable this. Should be called from text_parse_file on first line
 */
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

	if ((cell = sheet_cell_fetch (sheet, row, col)) == NULL)
		return -1;

	cell_set_text (cell, p);
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


static int
readTextWorkbook (CommandContext *context, Workbook *book,
		  const char* filename, gboolean probe)
{
	Sheet  *sheet = NULL;
	gint	flen;	/* file length */
	gchar  *file;	/* data pointer */
	gint	idx;
	int     fd;

	if ((file = (gchar *) gnumeric_mmap_open (context, filename, &fd, &flen)) != NULL) {

		/* FIXME: ARBITRARY VALUE */
		if (flen < 1) {
			gnumeric_mmap_close (context, file, fd, flen);
			gnumeric_error_read (context, _("Empty file"));
			return -1;
		} else if ( flen > 1000000){
			gnumeric_mmap_close (context, file, fd, flen);
			gnumeric_error_read (context, _("File is too large"));
			return -1;
		}

		idx = 0;
		while (idx >= 0 && idx < flen){
			char *sheetname = g_strdup_printf (_("Imported %s"), g_basename (filename));
			sheet = sheet_new (book, sheetname);
			g_free (sheetname);
			
			if (sheet == NULL)
				break;

			idx = text_parse_file (file, flen, idx, sheet);

			if (idx >= 0){
				Range range;
				
				sheet->modified = FALSE;
				workbook_attach_sheet (book, sheet);

				workbook_recalc (book);
				range = sheet_get_extent (sheet);
				sheet_range_calc_spans (sheet, range, TRUE);
			}
		}
	}

	gnumeric_mmap_close (context, file, fd, flen);

	return 0;
}

static int
text_read_workbook (CommandContext *context, Workbook *wb,
		    const char* filename)
{
	int ret;

	ret = readTextWorkbook (context, wb, filename, FALSE);
	if (ret == 0)
		workbook_set_saveinfo (wb, filename,
				       FILE_FL_MANUAL, text_write_workbook);

	return ret;
}

/*
* Save a Workbook in a simple text formatted file
* returns 0 in case of success, -1 otherwise.
*/

int
text_write_workbook (CommandContext *context, Workbook *wb,
		     const char *filename)
{
	TextData data = { 0, 0, 0, 0, NULL };

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	* Open Output File
	*/
	data.file = gnumeric_fopen (context, filename, "w");
	if (!data.file) {
		return -1;
	}

	writeTextWorkbook (&data, wb);

	fclose (data.file);

	return data.status;
}

static void
text_init (void)
{
	char const * const desc = _("Simple Text");

	file_format_register_open (0, desc, NULL,
				   text_read_workbook);

	file_format_register_save (".txt", desc, FILE_FL_MANUAL,
				   text_write_workbook);
}

static void
text_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, text_read_workbook);
	file_format_unregister_save (text_write_workbook);
}

static int
text_can_unload (PluginData *pd)
{
	return TRUE;
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	text_init ();

	if (plugin_data_init (pd, text_can_unload, text_cleanup_plugin,
			      _("Basic Text I/O"),
			      _("Read/Write workbooks using a simple text format.")))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
