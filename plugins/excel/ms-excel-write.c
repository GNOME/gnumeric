/**
 * ms-excel-write.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "gnumeric-sheet.h"
#include "format.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"
#include "main.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "excel.h"
#include "ms-excel-write.h"

#define EXCEL_DEBUG 0

typedef struct _SHEET    SHEET;
typedef struct _WORKBOOK WORKBOOK;

struct _SHEET {
	WORKBOOK *wb;
	Sheet    *gnum_sheet;
	guint32   streamPos;
	guint32   boundsheetPos;
};

struct _WORKBOOK {
	Workbook      *gnum_wb;
	GPtrArray     *sheets;
	eBiff_version  ver;
};

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
static void
biff_put_text (BIFF_PUT *bp, char *txt, eBiff_version ver, gboolean write_len)
{
#define BLK_LEN 16

	guint8 data[BLK_LEN];
	guint32 lpi, lpo, len, ans ;

	g_return_if_fail (bp);
	g_return_if_fail (txt);

	len = strlen (txt);
	if (ver >= eBiffV8) { /* Write header & word length*/
		data[0] = 0;
		if (write_len) {
			BIFF_SET_GUINT16(data+1, len);
			ms_biff_put_var_write (bp, data, 3);
			ans = len + 3;
		} else {
			ms_biff_put_var_write (bp, data, 1);
			ans = len;
		}
	} else { /* Byte length */
		if (write_len) {
			g_return_if_fail (len<256);
			BIFF_SET_GUINT8(data, len);
			ms_biff_put_var_write (bp, data, 1);
			ans = len + 1;
		} else
			ans = len;
	}

	/* An attempt at efficiency */
	for (lpo=0;lpo<(len+BLK_LEN-1)/BLK_LEN;lpo++) {
		guint cpy = (len-lpo*BLK_LEN)%BLK_LEN;
		for (lpi=0;lpi<cpy;lpi++)
			data[lpi]=*txt++;
		ms_biff_put_var_write (bp, data, cpy);
	}
/* ans is the length but do we need it ? */
#undef BLK_LEN
}

/**
 * See S59D5D.HTM
 **/
static void
biff_bof_write (BIFF_PUT *bp, eBiff_version ver,
		eBiff_filetype type)
{
	guint8 *data = ms_biff_put_len_next (bp, 0, 4);

	bp->ls_op = BIFF_BOF;
	switch (ver) {
	case eBiffV2:
		bp->ms_op = 0;
		break;
	case eBiffV3:
		bp->ms_op = 2;
		break;
	case eBiffV4:
		bp->ms_op = 4;
		break;
	case eBiffV7:
	case eBiffV8:
		bp->ms_op = 8;
		if (ver == eBiffV8)
			BIFF_SET_GUINT16 (data, 0x0600);
		else
			BIFF_SET_GUINT16 (data, 0x0500);
		break;
	default:
		g_warning ("Unknown version\n");
		break;
	}

	switch (type)
	{
	case eBiffTWorkbook:
		BIFF_SET_GUINT16 (data+2, 0x0005);
		break;
	case eBiffTVBModule:
		BIFF_SET_GUINT16 (data+2, 0x0006);
		break;
	case eBiffTWorksheet:
		BIFF_SET_GUINT16 (data+2, 0x0010);
		break;
	case eBiffTChart:
		BIFF_SET_GUINT16 (data+2, 0x0020);
		break;
	case eBiffTMacrosheet:
		BIFF_SET_GUINT16 (data+2, 0x0040);
		break;
	case eBiffTWorkspace:
		BIFF_SET_GUINT16 (data+2, 0x0100);
		break;
	default:
		g_warning ("Unknown type\n");
		break;
	}
	ms_biff_put_len_commit (bp);
}

static void
biff_eof_write (BIFF_PUT *bp)
{
	ms_biff_put_len_next (bp, BIFF_EOF, 0);
	ms_biff_put_len_commit (bp);
}

/**
 * Returns stream position of start.
 * See: S59D61.HTM
 **/
static guint32
biff_boundsheet_write_first (BIFF_PUT *bp, eBiff_filetype type,
			     char *name, eBiff_version ver)
{
	guint32 pos;
	guint8 data[16];

	ms_biff_put_var_next (bp, BIFF_BOUNDSHEET);
	pos = bp->streamPos;	

	BIFF_SET_GUINT32 (data, 0xdeadbeef); /* To be stream start pos */
	switch (type) {
	case eBiffTWorksheet:
		BIFF_SET_GUINT8 (data+4, 00);
		break;
	case eBiffTMacrosheet:
		BIFF_SET_GUINT8 (data+4, 01);
		break;
	case eBiffTChart:
		BIFF_SET_GUINT8 (data+4, 02);
		break;
	case eBiffTVBModule:
		BIFF_SET_GUINT8 (data+4, 06);
		break;
	default:
		g_warning ("Duff type\n");
		break;
	}
	BIFF_SET_GUINT8 (data+5, 0); /* Visible */
	ms_biff_put_var_write (bp, data, 6);

	biff_put_text (bp, name, ver, TRUE);

	ms_biff_put_var_commit (bp);
	return pos;
}

/**
 *  Update a previously written record with the correct
 * stream position.
 **/
static void
biff_boundsheet_write_last (MS_OLE_STREAM *s, guint32 pos,
			    guint32 streamPos)
{
	guint8  data[4];
	guint32 oldpos;
	g_return_if_fail (s);
	
	oldpos = s->position;/* FIXME: tell function ? */
	s->lseek (s, pos+4, MS_OLE_SEEK_SET);
	BIFF_SET_GUINT32 (data, streamPos);
	s->write (s, data, 4);
	s->lseek (s, oldpos, MS_OLE_SEEK_SET);
}

static void
write_value (BIFF_PUT *bp, Value *v, eBiff_version ver, guint32 col, guint32 row, guint16 xf)
{
	switch (v->type) {

	case VALUE_INTEGER:
	{
		int_t vint = v->v.v_int;
		guint head = 3;
		guint8 *data;

		if (vint%100==0)
			vint/=100;
		else
			head = 2;
#if EXCEL_DEBUG > 0
		printf ("writing %d %d %d\n", vint, v->v.v_int, head);
#endif
		if (vint & ~0x3fff) /* Chain to floating point then. */
			printf ("FIXME: Serious loss of precision saving number\n");
		data = ms_biff_put_len_next (bp, BIFF_RK, 10);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		BIFF_SET_GUINT32 (data + 6, (vint<<2) + head);
		ms_biff_put_len_commit (bp);
		break;
	}
	case VALUE_FLOAT:
	{
		if (ver >= eBiffV8) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (bp, BIFF_NUMBER, 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			BIFF_SETDOUBLE (data + 6, v->v.v_float);
			ms_biff_put_len_commit (bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (bp, BIFF_RK);
			BIFF_SETDOUBLE (data+6-4, v->v.v_float);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			data[6] &= 0xfc;
			ms_biff_put_var_write  (bp, data, 10); /* Yes loose it. */
			ms_biff_put_var_commit (bp);
		}
		break;
	}
	case VALUE_STRING:
	{
		char data[16];
		g_return_if_fail (v->v.str->str);

		if (ver >= eBiffV8); /* Use SST stuff in fulness of time */

		/* See: S59DDC.HTM */
		ms_biff_put_var_next   (bp, BIFF_RSTRING);
		EX_SETXF (data, xf);
		EX_SETCOL(data, col);
		EX_SETROW(data, row);
		EX_SETSTRLEN (data, strlen(v->v.str->str));
		ms_biff_put_var_write  (bp, data, 8);
		biff_put_text (bp, v->v.str->str, eBiffV7, FALSE);
		ms_biff_put_var_commit (bp);
		break;
	}
	default:
		printf ("Unhandled value type %d\n", v->type);
		break;
	}
}

typedef struct {
	SHEET    *sheet;
	BIFF_PUT *bp;
} CellArgs;
static void
write_cell (gpointer key, Cell *cell, CellArgs *a)
{
	BIFF_PUT *bp = a->bp;

	g_return_if_fail (a);
	g_return_if_fail (cell);

	if (cell->parsed_node)
#if EXCEL_DEBUG > 0
		printf ("FIXME: Skipping function\n");
#else
;
#endif
	else if (cell->value)
		write_value (bp, cell->value, a->sheet->wb->ver,
			     cell->col->pos, cell->row->pos, 0);
}

static void
write_sheet (BIFF_PUT *bp, SHEET *sheet)
{
	CellArgs args;

	sheet->streamPos = bp->streamPos;
	args.sheet       = sheet;
	args.bp          = bp;

	biff_bof_write (bp, sheet->wb->ver, eBiffTWorksheet);

/* FIXME: INDEX, UG! see S59D99.HTM */
/* Finding cell records in Biff files see: S59E28.HTM */

	g_hash_table_foreach (sheet->gnum_sheet->cell_hash,
			      (GHFunc)write_cell, &args);

	biff_eof_write (bp);
}

static void
new_sheet (gpointer key, gpointer val, gpointer iwb)
{
	SHEET     *sheet = g_new (SHEET, 1);
	Sheet     *value = (Sheet *)val;
	WORKBOOK  *wb    = (WORKBOOK *)iwb;

	g_return_if_fail (value);
	g_return_if_fail (wb);

	sheet->gnum_sheet = value;
	sheet->streamPos  = 0x0deadbee;
	sheet->wb         = wb;

	printf ("Workbook  %d %p\n", wb->ver, wb->gnum_wb);
	g_ptr_array_add (wb->sheets, sheet);
}

static void
write_workbook (BIFF_PUT *bp, Workbook *gwb, eBiff_version ver)
{
	WORKBOOK *wb = g_new (WORKBOOK, 1);
	SHEET    *s  = 0;
	int       lp;

	wb->ver      = ver;
	wb->gnum_wb  = gwb;
	wb->sheets   = g_ptr_array_new ();
	
	g_hash_table_foreach (gwb->sheets, new_sheet, wb);

	/* Workbook */
	biff_bof_write (bp, ver, eBiffTWorkbook);

	for (lp=0;lp<wb->sheets->len;lp++) {
		s = g_ptr_array_index (wb->sheets, lp);
	        s->boundsheetPos = biff_boundsheet_write_first (bp, eBiffTWorksheet,
								s->gnum_sheet->name, wb->ver);
	}

	biff_eof_write (bp);
	/* End of Workbook */
	
	/* Sheets */
	for (lp=0;lp<wb->sheets->len;lp++)
		write_sheet (bp, g_ptr_array_index (wb->sheets, lp));
	/* End of Sheets */

	/* Finalise Workbook stuff */
	for (lp=0;lp<wb->sheets->len;lp++) {
		SHEET *s = g_ptr_array_index (wb->sheets, lp);
		biff_boundsheet_write_last (bp->pos, s->boundsheetPos,
					    s->streamPos);
	}
	/* End Finalised workbook */
}

int
ms_excel_write_workbook (MS_OLE *file, Workbook *wb,
			 eBiff_version ver)
{
	MS_OLE_DIRECTORY *dir;
	MS_OLE_STREAM    *str;
	BIFF_PUT *bp;
	char *strname;

	g_return_val_if_fail (wb, 0);
	g_return_val_if_fail (file, 0);
	g_return_val_if_fail (ver>=eBiffV7, 0);

	if (!file || !wb) {
		printf ("Can't write Null pointers\n");
		return 0;
	}

	if (ver>=eBiffV8)
		strname = "Workbook";
	else
		strname = "Book";
	dir = ms_ole_directory_create (ms_ole_get_root (file),
				       strname,
				       MS_OLE_PPS_STREAM);
	if (!dir) {
		printf ("Can't create stream\n");
		return 0;
	}

	str = ms_ole_stream_open (dir, 'w');
	if (!str) {
		printf ("Can't open stream for writing\n");
		return 0;
	}

	bp = ms_biff_put_new (str);

	write_workbook (bp, wb, ver);

	/* Kludge to make sure the file is a Big Block file */
	ms_biff_put_len_next   (bp, 0,0x1000);
	ms_biff_put_len_commit (bp);

	ms_biff_put_destroy    (bp);

	ms_ole_stream_close (str);
	ms_ole_directory_destroy (dir);
	return 1;
}

