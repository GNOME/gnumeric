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
			BIFF_SET_GUINT32 (data, 0x0600);
		else
			BIFF_SET_GUINT32 (data, 0x0500);
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

typedef struct {
	GPtrArray *gnum_sheets;
	GArray    *streamPos;
	guint32    boundsheet_pos;
} BOUNDSHEET_DATA;

static BOUNDSHEET_DATA *
biff_boundsheet_write_first (BIFF_PUT *bp, Workbook *wb, eBiff_version ver)
{
	BOUNDSHEET_DATA *bsd = g_new (BOUNDSHEET_DATA, 1) ;
/*
	

	ans->streamStartPos = BIFF_GETLONG (q->data);
	switch (BIFF_GETBYTE (q->data + 4)){
	case 00:
		ans->type = eBiffTWorksheet;
		break;
	case 01:
		ans->type = eBiffTMacrosheet;
		break;
	case 02:
		ans->type = eBiffTChart;
		break;
	case 06:
		ans->type = eBiffTVBModule;
		break;
	default:
		printf ("Unknown sheet type : %d\n", BIFF_GETBYTE (q->data + 4));
		ans->type = eBiffTUnknown;
		break;
	}
	switch ((BIFF_GETBYTE (q->data + 5)) & 0x3){
	case 00:
		ans->hidden = eBiffHVisible;
		break;
	case 01:
		ans->hidden = eBiffHHidden;
		break;
	case 02:
		ans->hidden = eBiffHVeryHidden;
		break;
	default:
		printf ("Unknown sheet hiddenness %d\n", (BIFF_GETBYTE (q->data + 4)) & 0x3);
		ans->hidden = eBiffHVisible;
		break;
	}
	if (ver == eBiffV8) {
		int slen = BIFF_GETWORD (q->data + 6);
		ans->name = biff_get_text (q->data + 8, slen, NULL);
	} else {
		int slen = BIFF_GETBYTE (q->data + 6);

		ans->name = biff_get_text (q->data + 7, slen, NULL);
	}

	ans->index = (guint16)g_hash_table_size (wb->boundsheet_data_by_index) ;
	g_hash_table_insert (wb->boundsheet_data_by_index,
			     &ans->index, ans) ;
	g_hash_table_insert (wb->boundsheet_data_by_stream, 
			     &ans->streamStartPos, ans) ;

	g_assert (ans->streamStartPos == BIFF_GETLONG (q->data)) ;
	ans->sheet = ms_excel_sheet_new (wb, ans->name);
	ms_excel_workbook_attach (wb, ans->sheet);*/
	return bsd;
}

static void
write_workbook (BIFF_PUT *bp, Workbook *wb,
		eBiff_version ver)
{
	int lp;

	biff_bof_write (bp, ver, eBiffTWorkbook);
	biff_eof_write (bp);

	for (lp=0;lp<10;lp++) { /* Sheets */
		biff_bof_write (bp, ver, eBiffTWorksheet);
		biff_eof_write (bp);
	}
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
	ms_biff_put_destroy (bp);

	ms_ole_stream_close (str);
	ms_ole_directory_destroy (dir);
	return 1;
}

