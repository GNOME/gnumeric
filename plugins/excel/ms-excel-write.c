/* vim: set sw=8: */
/**
 * ms-excel-write.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (mmeeks@gnu.org)
 *    Jon K Hellan  (hellan@acm.org)
 *
 * (C) 1998-2000 Michael Meeks, Jon K Hellan
 **/

/**
 * Read the comments to gather_styles to see how style information is
 * collected.
 **/

/*
 * FIXME: Check for errors and propagate upward. We've only started.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-formula-write.h"
#include "boot.h"
#include "ms-biff.h"
#include "excel.h"
#include "ms-excel-write.h"
#include "ms-excel-xf.h"

#include <format.h>
#include <position.h>
#include <style-color.h>
#include <cell.h>
#include <sheet-object.h>
#include <sheet-object-cell-comment.h>
#include <application.h>
#include <style.h>
#include <sheet-style.h>
#include <format.h>
#include <libgnumeric.h>
#include <value.h>
#include <parse-util.h>
#include <print-info.h>
#include <workbook-view.h>
#include <workbook.h>
#include <io-context.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <gutils.h>
#include <str.h>


#include <libole2/ms-ole.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include <libgnome/gnome-i18n.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_write_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define N_ELEMENTS_BETWEEN_PROGRESS_UPDATES   20

static excel_iconv_t current_workbook_iconv = NULL;

static guint style_color_to_rgb888 (const StyleColor *c);
static gint  palette_get_index (ExcelWorkbook *wb, guint c);

/**
 *  This function converts simple strings...
 **/
int
biff_convert_text (char **buf, const char *txt, MsBiffVersion ver)
{
	guint32 lp, len;

	g_return_val_if_fail (txt, 0);

	len = strlen (txt);
	if (len == 0)
		*buf = g_strdup ("");
	else if (ver >= MS_BIFF_V8) {	/* unicode */
		wchar_t* wcbuf;
		guint16 *outbuf;
		len = mbstowcs(NULL, txt, 0);
		g_return_val_if_fail (len > 0, 0);
		wcbuf = g_new(wchar_t, len + 1);
		mbstowcs(wcbuf,txt,len + 1);

		outbuf = g_new(guint16, len);
		*buf = (char *)outbuf;
		for (lp = 0; lp < len; lp++) {
			outbuf[lp] = wcbuf[lp];
		}		
		g_free(wcbuf);
		len = lp * 2;
	} else {
		size_t inbufleft = len, outbufleft = len*8;
		char *outbufptr;
		char const * inbufptr = txt;

		*buf = g_new(char, outbufleft);
		outbufptr = *buf;

		excel_iconv (current_workbook_iconv, &inbufptr, &inbufleft, 
			     &outbufptr, &outbufleft);
		len = outbufptr - *buf;
	};
	return len;
}

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
int
biff_put_text (BiffPut *bp, const char *txt, int len, MsBiffVersion ver,
	       gboolean write_len, PutType how)
{
#define BLK_LEN 16

	guint8 data[BLK_LEN];
	guint32 ans;
	int lp;

	gboolean sixteen_bit_len;
	gboolean unicode;
	guint32  off;

	g_return_val_if_fail (bp, 0);

	if (txt == NULL) {
		g_warning ("writing NULL string as \"\"");
		txt = "";
	}
	g_return_val_if_fail (txt, 0);

	ans = 0;
/*	printf ("Write '%s' len = %d\n", txt, len); */

	if ((how == AS_PER_VER &&
	     ver >= MS_BIFF_V8) ||
	    how == SIXTEEN_BIT)
		sixteen_bit_len = TRUE;
	else
		sixteen_bit_len = FALSE; /* 8 bit */

	if (ver >= MS_BIFF_V8)
		unicode = TRUE;
	else
		unicode = FALSE;

	off = 0;
	if (unicode) {
		guint16 *buf = (guint16 *)txt;

		if (write_len) {
			if (sixteen_bit_len) {
				MS_OLE_SET_GUINT16 (data, len/2);
				off = 2;
			} else {
				g_return_val_if_fail (len/2<256, 0);
				MS_OLE_SET_GUINT8  (data, len/2);
				off = 1;
			}
		}
		MS_OLE_SET_GUINT8  (data + off, 0x0);
		off++;
		ms_biff_put_var_write (bp, data, off);

		for (lp = 0; lp < len/2; lp++) {
			MS_OLE_SET_GUINT16 (data, buf[lp]);
			ms_biff_put_var_write (bp, data, 2);
		}
	} else {
		if (write_len) {
			if (sixteen_bit_len) {
				MS_OLE_SET_GUINT16 (data, len);
				off = 2;
			} else {
				g_return_val_if_fail (len<256, 0);
				MS_OLE_SET_GUINT8  (data, len);
				off = 1;
			}
			ms_biff_put_var_write (bp, data, off);
		}

		for (lp = 0; lp < len; lp++) {
			MS_OLE_SET_GUINT8 (data, txt[lp]);
			ms_biff_put_var_write (bp, data, 1);
		}
	};
	return off + len;
	/* An attempt at efficiency */
/*	chunks = len/BLK_LEN;
	pos    = 0;
	for (lpc = 0; lpc < chunks; lpc++) {
		for (lp = 0; lp < BLK_LEN; lp++, pos++)
			data[lp] = txt[pos];
		data[BLK_LEN] = '\0';
		printf ("Writing chunk '%s'\n", data);
		ms_biff_put_var_write (bp, data, BLK_LEN);
	}
	len = len-pos;
	if (len > 0) {
	        for (lp = 0; lp < len; lp++, pos++)
			data[lp] = txt[pos];
		data[lp] = '\0';
		printf ("Writing chunk '%s'\n", data);
		ms_biff_put_var_write (bp, data, lp);
	}
 ans is the length but do we need it ?
	return ans;*/
#undef BLK_LEN
}

/**
 * See S59D5D.HTM
 **/
static MsOlePos
biff_bof_write (BiffPut *bp, MsBiffVersion ver,
		MsBiffFileType type)
{
	guint   len;
	guint8 *data;
	MsOlePos ans;

	if (ver >= MS_BIFF_V8)
		len = 16;
	else
		len = 8;

	data = ms_biff_put_len_next (bp, 0, len);

	ans = bp->streamPos;

	bp->ls_op = BIFF_BOF;
	switch (ver) {
	case MS_BIFF_V2:
		bp->ms_op = 0;
		break;
	case MS_BIFF_V3:
		bp->ms_op = 2;
		break;
	case MS_BIFF_V4:
		bp->ms_op = 4;
		break;
	case MS_BIFF_V7:
	case MS_BIFF_V8:
		bp->ms_op = 8;
		if (ver == MS_BIFF_V8 || /* as per the spec. */
		    (ver == MS_BIFF_V7 && type == MS_BIFF_TYPE_Worksheet)) /* Wierd hey */
			MS_OLE_SET_GUINT16 (data, 0x0600);
		else
			MS_OLE_SET_GUINT16 (data, 0x0500);
		break;
	default:
		g_warning ("Unknown version.");
		break;
	}

	switch (type)
	{
	case MS_BIFF_TYPE_Workbook:
		MS_OLE_SET_GUINT16 (data+2, 0x0005);
		break;
	case MS_BIFF_TYPE_VBModule:
		MS_OLE_SET_GUINT16 (data+2, 0x0006);
		break;
	case MS_BIFF_TYPE_Worksheet:
		MS_OLE_SET_GUINT16 (data+2, 0x0010);
		break;
	case MS_BIFF_TYPE_Chart:
		MS_OLE_SET_GUINT16 (data+2, 0x0020);
		break;
	case MS_BIFF_TYPE_Macrosheet:
		MS_OLE_SET_GUINT16 (data+2, 0x0040);
		break;
	case MS_BIFF_TYPE_Workspace:
		MS_OLE_SET_GUINT16 (data+2, 0x0100);
		break;
	default:
		g_warning ("Unknown type.");
		break;
	}

	/* Magic version numbers: build date etc. */
	switch (ver) {
	case MS_BIFF_V8:
		MS_OLE_SET_GUINT16 (data+4, 0x0dbb);
		MS_OLE_SET_GUINT16 (data+6, 0x07cc);
		g_assert (len > 11);
		/* Quandry: can we tell the truth about our history */
		MS_OLE_SET_GUINT32 (data+ 8, 0x00000004);
		MS_OLE_SET_GUINT16 (data+12, 0x06000908); /* ? */
		break;
	case MS_BIFF_V7:
	case MS_BIFF_V5:
		MS_OLE_SET_GUINT16 (data+4, 0x096c);
		MS_OLE_SET_GUINT16 (data+6, 0x07c9);
		break;
	default:
		printf ("FIXME: I need some magic numbers\n");
		MS_OLE_SET_GUINT16 (data+4, 0x0);
		MS_OLE_SET_GUINT16 (data+6, 0x0);
		break;
	}
	ms_biff_put_commit (bp);

	return ans;
}

static void
biff_eof_write (BiffPut *bp)
{
	ms_biff_put_len_next (bp, BIFF_EOF, 0);
	ms_biff_put_commit (bp);
}

static void
write_magic_interface (BiffPut *bp, MsBiffVersion ver)
{
	guint8 *data;
	if (ver >= MS_BIFF_V7) {
		ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 0);
		ms_biff_put_commit (bp);
		data = ms_biff_put_len_next (bp, BIFF_MMS, 2);
		MS_OLE_SET_GUINT16(data, 0);
		ms_biff_put_commit (bp);
		ms_biff_put_len_next (bp, 0xbf, 0);
		ms_biff_put_commit (bp);
		ms_biff_put_len_next (bp, 0xc0, 0);
		ms_biff_put_commit (bp);
		ms_biff_put_len_next (bp, BIFF_INTERFACEEND, 0);
		ms_biff_put_commit (bp);
	}
}

/* See: S59DE3.HTM */
static void
write_setup (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 * data = ms_biff_put_len_next (bp, BIFF_SETUP, 34);
	MS_OLE_SET_GUINT32 (data +  0, 0x002c0000);
	MS_OLE_SET_GUINT32 (data +  4, 0x00010001);
	MS_OLE_SET_GUINT32 (data +  8, 0x00440001);
	MS_OLE_SET_GUINT32 (data + 12, 0x0000002f);
	MS_OLE_SET_GUINT32 (data + 16, 0x00000000);
	MS_OLE_SET_GUINT32 (data + 20, 0x3fe00000);
	MS_OLE_SET_GUINT32 (data + 24, 0x00000000);
	MS_OLE_SET_GUINT32 (data + 28, 0x3fe00000);
	MS_OLE_SET_GUINT16 (data + 32, excel_iconv_win_codepage());
	ms_biff_put_commit (bp);
}

/*
 * XL doesn't write externsheets as often as we do. The previous version of
 * this function causes XL to crash when we exported a spreadsheet function
 * which is not an XL builtin. This version is still wrong, but won't crash
 * XL.
 */
static void
write_externsheets (BiffPut *bp, ExcelWorkbook *wb, ExcelSheet *ignore)
{
	int num_sheets = wb->sheets->len;
	guint32 externcount;
	guint8 *data;
	int lp;

	if (wb->ver > MS_BIFF_V7) {
		printf ("Need some cunning BiffV8 stuff ?\n");
		return;
	}

	g_assert (num_sheets < 0xffff);

	/* Kluge upon kluge, but the externcount will now match the number
	 * of externsheets we store. */
	for (lp = 0, externcount = 0; lp < num_sheets; lp++) {
		ExcelSheet *esheet = g_ptr_array_index (wb->sheets, lp);

		if (esheet != ignore)
			externcount ++;
	}

	if (externcount == 0)
		return;

	data = ms_biff_put_len_next (bp, BIFF_EXTERNCOUNT, 2);
	MS_OLE_SET_GUINT16(data, externcount);
	ms_biff_put_commit (bp);

	for (lp = 0; lp < num_sheets; lp++) {
		ExcelSheet *esheet = g_ptr_array_index (wb->sheets, lp);
		gint len;
		char *buf;
		guint8 data[8];

		if (esheet == ignore) continue;

		len = biff_convert_text (&buf, esheet->gnum_sheet->name_quoted, wb->ver);
		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		MS_OLE_SET_GUINT8(data, len);
		MS_OLE_SET_GUINT8(data + 1, 3); /* Magic */
		ms_biff_put_var_write (bp, data, 2);
		biff_put_text (bp, buf, len, wb->ver, FALSE, AS_PER_VER);
		g_free(buf);
		ms_biff_put_commit (bp);
	}
}

/*
 * See: S59E17.HTM
 */
static void
write_window1 (BiffPut *bp, MsBiffVersion ver, WorkbookView *wb_view)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_WINDOW1, 18);
	float hdpi = application_display_dpi_get (TRUE) / (72. * 20.);
	float vdpi = application_display_dpi_get (FALSE) / (72. * 20.);
	guint16 width = .5 + wb_view->preferred_width / hdpi;
	guint16 height = .5 + wb_view->preferred_height / vdpi;
	guint16 options = 0;

	if (wb_view->show_horizontal_scrollbar)
		options |= 0x0008;
	if (wb_view->show_vertical_scrollbar)
		options |= 0x0010;
	if (wb_view->show_notebook_tabs)
		options |= 0x0020;

	MS_OLE_SET_GUINT16 (data+  0, 0x0000);
	MS_OLE_SET_GUINT16 (data+  2, 0x0000);
	MS_OLE_SET_GUINT16 (data+  4, width);
	MS_OLE_SET_GUINT16 (data+  6, height);
	MS_OLE_SET_GUINT16 (data+  8, options); /* various flags */
	MS_OLE_SET_GUINT16 (data+ 10, 0x0000); /* selected tab */
	MS_OLE_SET_GUINT16 (data+ 12, 0x0000); /* displayed tab */
	MS_OLE_SET_GUINT16 (data+ 14, 0x0001);
	MS_OLE_SET_GUINT16 (data+ 16, 0x0258);
	ms_biff_put_commit (bp);
}

/**
 * write_window2 :
 * returns TRUE if a PANE record is necessary.
 *
 * See: S59E18.HTM
 **/
static gboolean
write_window2 (BiffPut *bp, MsBiffVersion ver, ExcelSheet *esheet)
{
	/* 1	0x020 grids are the colour of the normal style */
	/* 0	0x040 arabic */
	/* 1	0x080 display outlines if they exist */
	/* 0	0x800 (biff8 only) no page break mode*/
	guint16 options = 0x0A0;
	guint8 *data;
	CellPos top_left;
	Sheet *sheet = esheet->gnum_sheet;
	StyleColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	StyleColor *default_auto = style_color_auto_pattern ();
	guint32 biff_pat_col = 0x40;	/* default grid color index == auto */

	if (sheet->display_formulas)
		options |= 0x0001;
	if (!sheet->hide_grid)
		options |= 0x0002;
	if (!sheet->hide_col_header || !sheet->hide_row_header)
		options |= 0x0004;
	if (sheet_is_frozen (sheet)) {
		options |= 0x0008;
		top_left = sheet->frozen_top_left;
	} else
		top_left = sheet->initial_top_left;	/* belongs in sheetView */
	if (!sheet->hide_zero)
		options |= 0x0010;
	/* Grid / auto pattern color */
	if (!style_color_equal (sheet_auto, default_auto)) {
		biff_pat_col = style_color_to_rgb888 (sheet_auto);
		if (ver > MS_BIFF_V7) {
			biff_pat_col = palette_get_index (esheet->wb,
							  biff_pat_col);
		}
		options &= ~0x0020;
	}
	if (sheet == wb_view_cur_sheet (esheet->wb->gnum_wb_view))
		options |= 0x600; /* assume selected if it is current */

	if (ver <= MS_BIFF_V7) {
		data = ms_biff_put_len_next (bp, 0x200|BIFF_WINDOW2, 10);

		MS_OLE_SET_GUINT16 (data +  0, options);
		MS_OLE_SET_GUINT16 (data +  2, top_left.row);
		MS_OLE_SET_GUINT16 (data +  4, top_left.col);
		MS_OLE_SET_GUINT32 (data +  6, biff_pat_col);
	} else {
		data = ms_biff_put_len_next (bp, 0x200|BIFF_WINDOW2, 18);

		MS_OLE_SET_GUINT16 (data +  0, options);
		MS_OLE_SET_GUINT16 (data +  2, top_left.row);
		MS_OLE_SET_GUINT16 (data +  4, top_left.col);
		MS_OLE_SET_GUINT32 (data +  6, biff_pat_col);
		MS_OLE_SET_GUINT16 (data + 10, 0x1);	/* print preview 100% */
		MS_OLE_SET_GUINT16 (data + 12, 0x0);	/* FIXME : why 0? */
		MS_OLE_SET_GUINT32 (data + 14, 0x0);	/* reserved 0 */
	}
	ms_biff_put_commit (bp);

	style_color_unref (sheet_auto);
	style_color_unref (default_auto);
	return (options & 0x0008);
}

/* See: S59DCA.HTM */
static void
write_pane (BiffPut *bp, MsBiffVersion ver, ExcelSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_PANE, 10);
	Sheet const *sheet = esheet->gnum_sheet;
	int const frozen_height = sheet->unfrozen_top_left.row -
		sheet->frozen_top_left.row;
	int const frozen_width = sheet->unfrozen_top_left.col -
		sheet->frozen_top_left.col;

	MS_OLE_SET_GUINT16 (data + 0, frozen_width);
	MS_OLE_SET_GUINT16 (data + 2, frozen_height);
	MS_OLE_SET_GUINT16 (data + 4, sheet->initial_top_left.row);
	MS_OLE_SET_GUINT16 (data + 6, sheet->initial_top_left.col);
	MS_OLE_SET_GUINT16 (data + 8, 0);	/* active pane */

	ms_biff_put_commit (bp);
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
write_mergecells (BiffPut *bp, MsBiffVersion ver, ExcelSheet *esheet)
{
	guint8 *record, *ptr;
	GSList *merged;
	guint16 len = 0;

	/* Find the set of regions that we can safely export */
	for (merged = esheet->gnum_sheet->list_merged; merged != NULL ; merged = merged->next) {
		/* TODO : Add a warning entry in the log about ignoring the missing elements */
		Range const *r = merged->data;
		if (r->start.row <= USHRT_MAX && r->end.row <= USHRT_MAX &&
		    r->start.col <= UCHAR_MAX && r->end.col <= UCHAR_MAX)
			len++;
	}

	/* Do not even write the record if there are no merged regions */
	if (len <= 0)
		return;

	record = ms_biff_put_len_next (bp, BIFF_MERGECELLS, 2+8*len);
	MS_OLE_SET_GUINT16 (record, len);

	ptr = record + 2;
	for (merged = esheet->gnum_sheet->list_merged; merged != NULL ; merged = merged->next) {
		Range const *r = merged->data;
		if (r->start.row <= USHRT_MAX && r->end.row <= USHRT_MAX &&
		    r->start.col <= UCHAR_MAX && r->end.col <= UCHAR_MAX) {
			MS_OLE_SET_GUINT16 (ptr+0, r->start.row);
			MS_OLE_SET_GUINT16 (ptr+2, r->end.row);
			MS_OLE_SET_GUINT16 (ptr+4, r->start.col);
			MS_OLE_SET_GUINT16 (ptr+6, r->end.col);
			ptr += 8;
		}
	}

	ms_biff_put_commit (bp);
}

static void
write_names (BiffPut *bp, ExcelWorkbook *wb)
{
	Workbook const *gwb = wb->gnum_wb;
	GList const *names = gwb->names;
	ExcelSheet *esheet;

	g_return_if_fail (wb->ver <= MS_BIFF_V7);

	/* excel crashes if this isn't here and the names have Ref3Ds */
#warning TODO support references to external workbooks, and only spew sheets that are referenced in names.
	if (names)
		write_externsheets (bp, wb, NULL);
	esheet = g_ptr_array_index (wb->sheets, 0);

	for ( ; names != NULL ; names = names->next) {
		guint8 data0 [20];
		guint8 data1 [2];
		guint16 len, name_len;

		char *text;
		GnmNamedExpr const *expr_name = names->data;

		g_return_if_fail (expr_name != NULL);

		if (wb->ver >= MS_BIFF_V8)
			ms_biff_put_var_next (bp, 0x200 | BIFF_NAME);
		else
			ms_biff_put_var_next (bp, BIFF_NAME);

		text = expr_name->name->str;
		name_len = strlen (expr_name->name->str);

		memset (data0, 0, sizeof (data0));
		MS_OLE_SET_GUINT8 (data0 + 3, name_len); /* name_len */

		/* This code will only work for MS_BIFF_V7. */
		ms_biff_put_var_write (bp, data0, 14);
		biff_put_text (bp, text, name_len, wb->ver, FALSE, AS_PER_VER);
		g_free(text);
		ms_biff_put_var_seekto (bp, 14 + name_len);
		len = ms_excel_write_formula (bp, esheet,
			expr_name->t.expr_tree, 0, 0, 0);

		g_return_if_fail (len <= 0xffff);

		ms_biff_put_var_seekto (bp, 4);
		MS_OLE_SET_GUINT16 (data1, len);
		ms_biff_put_var_write (bp, data1, 2);
		ms_biff_put_commit (bp);

		g_ptr_array_add (wb->names, g_strdup(text));
	}
}

static void
write_bits (BiffPut *bp, ExcelWorkbook *wb, MsBiffVersion ver)
{
	guint8 *data;
	char const *team = "The Gnumeric Development Team";
	gint len;
	char *buf;
	guint8 pad [WRITEACCESS_LEN];

	/* See: S59E1A.HTM */
	len = biff_convert_text (&buf, team, ver);
	g_assert (len < WRITEACCESS_LEN);
	memset (pad, ' ', sizeof pad);
	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	biff_put_text (bp, buf, len, ver, TRUE, AS_PER_VER);
	g_free(buf);
	ms_biff_put_var_write (bp, pad, WRITEACCESS_LEN - len - 1);
	ms_biff_put_commit (bp);

	/* See: S59D66.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CODEPAGE, 2);
	MS_OLE_SET_GUINT16 (data, excel_iconv_win_codepage());
	ms_biff_put_commit (bp);

	if (ver >= MS_BIFF_V8) { /* See S59D78.HTM */
		int lp, len;

		data = ms_biff_put_len_next (bp, BIFF_DSF, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);

		/* See: S59E09.HTM */
		len = wb->sheets->len;
		data = ms_biff_put_len_next (bp, BIFF_TABID, len * 2);
		for (lp = 0; lp < len; lp++)
			MS_OLE_SET_GUINT16 (data + lp*2, lp + 1);
		ms_biff_put_commit (bp);
	}
	/* See: S59D8A.HTM */
	data = ms_biff_put_len_next (bp, BIFF_FNGROUPCOUNT, 2);
	MS_OLE_SET_GUINT16 (data, 0x0e);
	ms_biff_put_commit (bp);

	write_names (bp, wb);

	/* See: S59E19.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WINDOWPROTECT, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DD1.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PROTECT, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCC.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PASSWORD, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	if (ver >= MS_BIFF_V8) {
		/* See: S59DD2.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PROT4REV, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);

		/* See: S59DD3.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PROT4REVPASS, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
	}

	write_window1 (bp, ver, wb->gnum_wb_view);

	/* See: S59D5B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BACKUP, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D95.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HIDEOBJ, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D54.HTM */
	data = ms_biff_put_len_next (bp, BIFF_1904, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCE.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRECISION, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD8.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFRESHALL, 2);
	MS_OLE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D5E.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BOOKBOOL, 2);
	MS_OLE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);
}

int
ms_excel_write_get_sheet_idx (ExcelWorkbook *wb, Sheet *gnum_sheet)
{
	guint lp;
	for (lp = 0; lp < wb->sheets->len; lp++) {
		ExcelSheet *esheet = g_ptr_array_index (wb->sheets, lp);
		g_return_val_if_fail (esheet, 0);
		if (esheet->gnum_sheet == gnum_sheet)
			return lp;
	}
	g_warning ("No associated esheet for %p.", (void *)gnum_sheet);
	return 0;
}

int
ms_excel_write_get_externsheet_idx (ExcelWorkbook *wb,
				    Sheet *sheeta,
				    Sheet *sheetb)
{
	g_warning ("Get Externsheet not implemented yet.");
	return 0;
}

/**
 * Returns stream position of start.
 * See: S59D61.HTM
 **/
static guint32
biff_boundsheet_write_first (BiffPut *bp, MsBiffFileType type,
			     char *name, MsBiffVersion ver)
{
	guint32 pos;
	gint len;
	char *buf;
	guint8 data[16];

	ms_biff_put_var_next (bp, BIFF_BOUNDSHEET);
	pos = bp->streamPos;

	MS_OLE_SET_GUINT32 (data, 0xdeadbeef); /* To be stream start pos */
	switch (type) {
	case MS_BIFF_TYPE_Worksheet:
		MS_OLE_SET_GUINT8 (data+4, 00);
		break;
	case MS_BIFF_TYPE_Macrosheet:
		MS_OLE_SET_GUINT8 (data+4, 01);
		break;
	case MS_BIFF_TYPE_Chart:
		MS_OLE_SET_GUINT8 (data+4, 02);
		break;
	case MS_BIFF_TYPE_VBModule:
		MS_OLE_SET_GUINT8 (data+4, 06);
		break;
	default:
		g_warning ("Duff type.");
		break;
	}
	MS_OLE_SET_GUINT8 (data+5, 0); /* Visible */
	ms_biff_put_var_write (bp, data, 6);
	len = biff_convert_text(&buf, name, ver);
	biff_put_text (bp, buf, len, ver, TRUE, AS_PER_VER);
	g_free(buf);

	ms_biff_put_commit (bp);
	return pos;
}

/**
 *  Update a previously written record with the correct
 * stream position.
 **/
static void
biff_boundsheet_write_last (MsOleStream *s, guint32 pos,
			    MsOlePos streamPos)
{
	guint8  data[4];
	MsOlePos oldpos;
	g_return_if_fail (s);

	oldpos = s->position;/* FIXME: tell function ? */
	s->lseek (s, pos+4, MsOleSeekSet);
	MS_OLE_SET_GUINT32 (data, streamPos);
	s->write (s, data, 4);
	s->lseek (s, oldpos, MsOleSeekSet);
}

/**
 * Convert EXCEL_PALETTE_ENTRY to guint representation used in BIFF file
 **/
static guint
palette_color_to_int (const EXCEL_PALETTE_ENTRY *c)
{
	return (c->b << 16) + (c->g << 8) + (c->r << 0);

}

/**
 * Convert StyleColor to guint representation used in BIFF file
 **/
static guint
style_color_to_rgb888 (const StyleColor *c)
{
	return ((c->blue & 0xff00) << 8) + (c->green & 0xff00) + (c->red >> 8);

}

/**
 * log_put_color
 * @c          color
 * @was_added  true if color was added
 * @index      index of color
 * @tmpl       printf template

 * Callback called when putting color to palette. Print to debug log when
 * color is added to table.
 **/
inline static void
log_put_color (guint c, gboolean was_added, gint index, const char *tmpl)
{
	d(2, if (was_added) printf (tmpl, index, c););
	}

/**
 * Put Excel default colors to palette table
 *
 * Ensure that black and white can't be swapped out.
 **/
static void
palette_put_defaults (ExcelWorkbook *wb)
{
	int i;
	const EXCEL_PALETTE_ENTRY *epe;
	guint num;

	for (i = 0; i < EXCEL_DEF_PAL_LEN; i++) {
		epe = &excel_default_palette[i];
		num = palette_color_to_int (epe);
		two_way_table_put (wb->pal->two_way_table,
				   GUINT_TO_POINTER (num), FALSE,
				   (AfterPutFunc) log_put_color,
				   "Default color %d - 0x%6.6x\n");
		if ((i == PALETTE_BLACK) || (i == PALETTE_WHITE))
			wb->pal->entry_in_use[i] = TRUE;
		else
			wb->pal->entry_in_use[i] = FALSE;
	}
}

/**
 * Initialize palette in worksheet. Fill in with default colors
 **/
static void
palette_init (ExcelWorkbook *wb)
{
	wb->pal = g_new (Palette, 1);
	wb->pal->two_way_table 	= two_way_table_new (g_direct_hash,
						     g_direct_equal, 0);
	palette_put_defaults (wb);

}

/**
 * Free palette table
 **/
static void
palette_free (ExcelWorkbook *wb)
{
	TwoWayTable *twt;

	if (wb && wb->pal) {
		twt = wb->pal->two_way_table;
		if (twt) {
			two_way_table_free (twt);
		}
		g_free (wb->pal);
		wb->pal = NULL;
	}
}

/**
 * palette_get_index
 * @wb workbook
 * @c  color
 *
 * Get index of color
 * The color index to use is *not* simply the index into the palette.
 * See comment to ms_excel_palette_get in ms-excel-read.c
 **/
static gint
palette_get_index (ExcelWorkbook *wb, guint c)
{
	gint idx;
	TwoWayTable *twt = wb->pal->two_way_table;

	if (c == 0) {
		idx = PALETTE_BLACK;
	} else if (c == 0xffffff) {
		idx = PALETTE_WHITE;
	} else {
		idx = two_way_table_key_to_idx (twt, (gconstpointer) c);
		if (idx < EXCEL_DEF_PAL_LEN) {
			idx += 8;
		} else {
			idx = PALETTE_BLACK;
		}
	}

	return idx;
}

/**
 * Add a color to palette if it is not already there
 **/
static void
put_color (ExcelWorkbook *wb, const StyleColor *c)
{
	TwoWayTable *twt = wb->pal->two_way_table;
	gpointer pc = GUINT_TO_POINTER (style_color_to_rgb888 (c));
	gint idx;

	two_way_table_put (twt, pc, TRUE,
			   (AfterPutFunc) log_put_color,
			   "Found unique color %d - 0x%6.6x\n");

	idx = two_way_table_key_to_idx (twt, pc);
	if (idx >= 0 && idx < EXCEL_DEF_PAL_LEN)
		wb->pal->entry_in_use [idx] = TRUE; /* Default entry in use */
}

/**
 * Add colors in mstyle to palette
 **/
static void
put_colors (MStyle *st, gconstpointer dummy, ExcelWorkbook *wb)
{
	int i;
	const StyleBorder * b;

	put_color (wb, mstyle_get_color (st, MSTYLE_COLOR_FORE));
	put_color (wb, mstyle_get_color (st, MSTYLE_COLOR_BACK));
	put_color (wb, mstyle_get_color (st, MSTYLE_COLOR_PATTERN));

	/* Borders */
	for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
		b = mstyle_get_border (st, MSTYLE_BORDER_TOP + i);
		if (b && b->color)
			put_color (wb, b->color);
	}
}

/**
 * gather_palette
 * @wb   workbook
 *
 * Add all colors in workbook to palette.
 *
 * The palette is apparently limited to EXCEL_DEF_PAL_LEN.  We start
 * with the default palette in the table. Next, we traverse all colors
 * in all styles. When we find a default color, we note that it is in
 * use. When we find a custom colors, we add it to the table. This
 * yields an oversize palette.
 * Finally, we knock out unused entries in the default palette, to make
 * room for the custom colors. We don't touch 0 or 1 (white/black)
 *
 * FIXME:
 * We are not able to recognize that we are actually using a color in the
 * default palette. The causes seem to be elsewhere in gnumeric and gnome.
 **/
static void
gather_palette (ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->xf->two_way_table;
	int i, j;
	int upper_limit = EXCEL_DEF_PAL_LEN;
	guint color;

	/* For each color in each style, get color index from hash. If
           none, it's not there yet, and we enter it. */
	g_hash_table_foreach (twt->key_to_idx, (GHFunc) put_colors, wb);

	twt = wb->pal->two_way_table;
	for (i = twt->idx_to_key->len - 1; i >= EXCEL_DEF_PAL_LEN; i--) {
		color = GPOINTER_TO_UINT (two_way_table_idx_to_key (twt, i));
		for (j = upper_limit - 1; j > 1; j--) {
			if (!wb->pal->entry_in_use[j]) {
				/* Replace table entry with color. */
				d (2, printf ("Custom color %d (0x%6.6x)"
						" moved to unused index %d\n",
					      i, color, j););
				(void) two_way_table_replace
					(twt, j, GUINT_TO_POINTER (color));
				upper_limit = j;
				wb->pal->entry_in_use[j] = TRUE;
				break;
			}
		}
	}
}

/**
 * write_palette
 * @bp BIFF buffer
 * @wb workbook
 *
 * Write palette to file
 **/
static void
write_palette (BiffPut *bp, ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->pal->two_way_table;
	guint8  data[8];
	guint   num, i;

	ms_biff_put_var_next (bp, BIFF_PALETTE);

	MS_OLE_SET_GUINT16 (data, EXCEL_DEF_PAL_LEN); /* Entries */

	ms_biff_put_var_write (bp, data, 2);
	for (i = 0; i < EXCEL_DEF_PAL_LEN; i++) {
		num = GPOINTER_TO_UINT (two_way_table_idx_to_key (twt, i));
		MS_OLE_SET_GUINT32 (data, num);
		ms_biff_put_var_write (bp, data, 4);
	}

	ms_biff_put_commit (bp);
}

#ifndef NO_DEBUG_EXCEL
/**
 * Return string description of font to print in debug log
 **/
static char *
excel_font_to_string (const ExcelFont *f)
{
	const StyleFont *sf = f->style_font;
	static char buf[96];
	guint nused;

	nused = snprintf (buf, sizeof buf, "%s, %g", sf->font_name, sf->size_pts);

	if (nused < sizeof buf && sf->is_bold)
		nused += snprintf (buf + nused, sizeof buf - nused, ", %s",
				   "bold");
	if (nused < sizeof buf && sf->is_italic)
		nused += snprintf (buf + nused, sizeof buf - nused, ", %s",
				   "italic");
	if (nused < sizeof buf) {
		if ((StyleUnderlineType) f->underline == UNDERLINE_SINGLE)
			nused += snprintf (buf + nused, sizeof buf - nused,
					   ", %s", "single underline");
		else if ((StyleUnderlineType) f->underline == UNDERLINE_DOUBLE)
			nused += snprintf (buf + nused, sizeof buf - nused,
					   ", %s", "double underline");
	}
	if (nused < sizeof buf && f->strikethrough)
		nused += snprintf (buf + nused, sizeof buf - nused, ", %s",
				   "strikethrough");

	return buf;
}
#endif

/**
 * excel_font_new
 * @st style - which includes style font and color
 *
 * Make an ExcelFont.
 * In, Excel, the foreground color is a font attribute. ExcelFont
 * consists of a StyleFont and a color.
 *
 * Style color is *not* unrefed. This is correct
 **/
static ExcelFont *
excel_font_new (MStyle *st)
{
	ExcelFont *f;
	StyleColor *c;

	if (!st)
		return NULL;

	f = g_new (ExcelFont, 1);
	f->style_font = mstyle_get_font (st, 1.0);
	c = mstyle_get_color (st, MSTYLE_COLOR_FORE);
	f->color = style_color_to_rgb888 (c);
	f->is_auto = c->is_auto;
	f->underline     = mstyle_get_font_uline (st);
	f->strikethrough = mstyle_get_font_strike (st);

	return f;
}

/**
 * Free an ExcelFont
 **/
static void
excel_font_free (ExcelFont *f)
{
	if (f) {
		style_font_unref (f->style_font);
		g_free (f);
	}
}

/**
 * excel_font_hash
 *
 * Hash function for ExcelFonts
 * @f font
 **/
static guint
excel_font_hash (gconstpointer f)
{
	guint res = 0;
	ExcelFont * font = (ExcelFont *) f;

	if (f)
		res = style_font_hash_func (font->style_font) ^ font->color
			^ font->is_auto ^ (font->underline << 1)
			^ (font->strikethrough << 2);

	return res;
}

/**
 * excel_font_equal
 * @a font a
 * @b font b
 *
 * ExcelFont comparison function used when hashing.
 **/
static gint
excel_font_equal (gconstpointer a, gconstpointer b)
{
	gint res;

	if (a == b)
		res = TRUE;
	else if (!a || !b)
		res = FALSE;	/* Recognize junk - inelegant, I know! */
	else {
		const ExcelFont *fa  = (const ExcelFont *) a;
		const ExcelFont *fb  = (const ExcelFont *) b;
		res = style_font_equal (fa->style_font, fb->style_font)
			&& (fa->color == fb->color)
			&& (fa->is_auto == fb->is_auto)
			&& (fa->underline == fb->underline)
			&& (fa->strikethrough == fb->strikethrough);
	}

	return res;
}

/**
 * Initialize table of fonts in worksheet.
 **/
static void
fonts_init (ExcelWorkbook *wb)
{
	wb->fonts = g_new (Fonts, 1);
	wb->fonts->two_way_table
		= two_way_table_new (excel_font_hash, excel_font_equal, 0);
}

/**
 * Get an ExcelFont, given index
 **/
static ExcelFont *
fonts_get_font (ExcelWorkbook *wb, gint idx)
{
	TwoWayTable *twt = wb->fonts->two_way_table;

	return (ExcelFont *) two_way_table_idx_to_key (twt, idx);
}

/**
 * Free font table
 **/
static void
fonts_free (ExcelWorkbook *wb)
{
	TwoWayTable *twt;
	guint i;
	ExcelFont *f;

	if (wb && wb->fonts) {
		twt = wb->fonts->two_way_table;
		if (twt) {
			for (i = 0; i < twt->idx_to_key->len; i++) {
				f = fonts_get_font (wb, i + twt->base);
				excel_font_free (f);
			}
			two_way_table_free (twt);
		}
		g_free (wb->fonts);
		wb->fonts = NULL;
	}
}

/**
 * Get index of an ExcelFont
 **/
static gint
fonts_get_index (ExcelWorkbook *wb, const ExcelFont *f)
{
	TwoWayTable *twt = wb->fonts->two_way_table;
	return two_way_table_key_to_idx (twt, f);
}

/**
 * Callback called when putting font to table. Print to debug log when
 * font is added. Free resources when it was already there.
 **/
static void
after_put_font (ExcelFont *f, gboolean was_added, gint index, gconstpointer dummy)
{
	if (was_added) {
		d (1, printf ("Found unique font %d - %s\n",
			      index, excel_font_to_string (f)););
	} else {
		excel_font_free (f);
	}
}

/**
 * Add a font to table if it is not already there.
 **/
static void
put_font (MStyle *st, gconstpointer dummy, ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->fonts->two_way_table;
	ExcelFont *f = excel_font_new (st);

	/* Occupy index FONT_SKIP with junk - Excel skips it */
	if (twt->idx_to_key->len == FONT_SKIP)
		(void) two_way_table_put (twt, NULL,
					  FALSE, NULL, NULL);

	two_way_table_put (twt, f, TRUE, (AfterPutFunc) after_put_font, NULL);
}

/**
 * Add all fonts in workbook to table
 **/
static void
gather_fonts (ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->xf->two_way_table;

	/* For each style, get fonts index from hash. If none, it's
           not there yet, and we enter it. */
	g_hash_table_foreach (twt->key_to_idx, (GHFunc) put_font, wb);
}

/**
 * write_font
 * @bp BIFF buffer
 * @wb workbook
 * @f  font
 *
 * Write a font to file
 * See S59D8C.HTM
 *
 * FIXME:
 * It would be useful to map well known fonts to Windows equivalents
 **/
static void
write_font (BiffPut *bp, ExcelWorkbook *wb, const ExcelFont *f)
{
	guint8 data[64];
	StyleFont  *sf  = f->style_font;
	guint32 size_pts  = sf->size_pts * 20;
	guint16 grbit = 0;
	guint16 color;

	guint16 boldstyle = 0x190; /* Normal boldness */
	guint16 subsuper  = 0;   /* 0: Normal, 1; Super, 2: Sub script*/
	guint8  underline = (guint8) f->underline; /* 0: None, 1: Single,
						      2: Double */
	guint8  family    = 0;
	guint8  charset   = 0;	 /* Seems OK. */
	char    *font_name = sf->font_name;
	gint    len;
	char    *buf;

	color = f->is_auto
		? PALETTE_AUTO_FONT
		: palette_get_index (wb, f->color);
	d (1, printf ("Writing font %s, color idx %u\n",
		      excel_font_to_string (f), color););

	if (sf->is_italic)
		grbit |= 1 << 1;
	if (f->strikethrough)
		grbit |= 1 << 3;
	if (sf->is_bold)
		boldstyle = 0x2bc;

	ms_biff_put_var_next (bp, BIFF_FONT);
	MS_OLE_SET_GUINT16 (data + 0, size_pts);
	MS_OLE_SET_GUINT16 (data + 2, grbit);
	MS_OLE_SET_GUINT16 (data + 4, color);
	MS_OLE_SET_GUINT16 (data + 6, boldstyle);
	MS_OLE_SET_GUINT16 (data + 8, subsuper);
	MS_OLE_SET_GUINT8  (data + 10, underline);
	MS_OLE_SET_GUINT8  (data + 11, family);
	MS_OLE_SET_GUINT8  (data + 12, charset);
	MS_OLE_SET_GUINT8  (data +13, 0);
	ms_biff_put_var_write (bp, data, 14);
	len = biff_convert_text(&buf, font_name, wb->ver);
	biff_put_text (bp, buf, len, wb->ver, TRUE, EIGHT_BIT);
	g_free(buf);

	ms_biff_put_commit (bp);
}

/**
 * write_fonts
 * @bp BIFF buffer
 * @wb workbook
 *
 * Write all fonts to file
 **/
static void
write_fonts (BiffPut *bp, ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->fonts->two_way_table;
	int nfonts = twt->idx_to_key->len;
	int lp;
	ExcelFont *f;

	for (lp = 0; lp < nfonts; lp++) {
		if (lp != FONT_SKIP) {	/* FONT_SKIP is invalid, skip it */
			f = fonts_get_font (wb, lp);
			write_font (bp, wb, f);
		}
	}

	if (nfonts < FONTS_MINIMUM + 1) { /* Add 1 to account for skip */
		/* Fill up until we've got the minimum number */
		f = fonts_get_font (wb, 0);
		for (; lp < FONTS_MINIMUM + 1; lp++) {
			if (lp != FONT_SKIP) {
				/* FONT_SKIP is invalid, skip it */
				write_font (bp, wb, f);
			}
		}
	}
}

/**
 * after_put_format
 * @format     format
 * @was_added  true if format was added
 * @index      index of format
 * @tmpl       printf template
 *
 * Callback called when putting format to table. Print to debug log when
 * format is added. Free resources when it was already there.
 **/
static void
after_put_format (StyleFormat *format, gboolean was_added, gint index,
		  const char *tmpl)
{
	if (was_added) {
		d (2, printf (tmpl, index, format););
	} else {
		style_format_unref (format);
	}
}

/**
 * Add built-in formats to format table
 **/
static void
formats_put_magic (ExcelWorkbook *wb)
{
	int i;
	char const *fmt;

	for (i = 0; i < EXCEL_BUILTIN_FORMAT_LEN; i++) {
		fmt = excel_builtin_formats[i];
		if (!fmt || strlen (fmt) == 0)
			fmt = "General";
		two_way_table_put (wb->formats->two_way_table,
				   style_format_new_XL (fmt, FALSE),
				   FALSE, /* Not unique */
				   (AfterPutFunc) after_put_format,
				   "Magic format %d - 0x%x\n");
	}
}

/**
 * Initialize format table
 **/
static void
formats_init (ExcelWorkbook *wb)
{

	wb->formats = g_new (Formats, 1);
	wb->formats->two_way_table
		= two_way_table_new (g_direct_hash, g_direct_equal, 0);

	formats_put_magic (wb);
}


/**
 * Get a format, given index
 **/
static StyleFormat const *
formats_get_format (ExcelWorkbook *wb, gint idx)
{
	TwoWayTable *twt = wb->formats->two_way_table;

	return two_way_table_idx_to_key (twt, idx);
}

/**
 * Free format table
 **/
static void
formats_free (ExcelWorkbook *wb)
{
	TwoWayTable *twt;

	if (wb && wb->formats) {
		twt = wb->formats->two_way_table;
		if (twt)
			two_way_table_free (twt);

		g_free (wb->formats);
		wb->formats = NULL;
	}
}

/**
 * Get index of a format
 **/
static gint
formats_get_index (ExcelWorkbook *wb, StyleFormat const *format)
{
	TwoWayTable *twt = wb->formats->two_way_table;
	return two_way_table_key_to_idx (twt, format);
}

/**
 * Add a format to table if it is not already there.
 *
 * Style format is *not* unrefed. This is correct
 **/
static void
put_format (MStyle *mstyle, gconstpointer dummy, ExcelWorkbook *wb)
{
	StyleFormat *fmt = mstyle_get_format (mstyle);
	style_format_ref (fmt);
	two_way_table_put (wb->formats->two_way_table,
			   (gpointer)fmt, TRUE,
			   (AfterPutFunc) after_put_format,
			   "Found unique format %d - 0x%x\n");
}


/**
 * Add all formats in workbook to table
 **/
static void
gather_formats (ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->xf->two_way_table;
	/* For each style, get fonts index from hash. If none, it's
           not there yet, and we enter it. */
	g_hash_table_foreach (twt->key_to_idx, (GHFunc) put_format, wb);
}


/**
 * write_format
 * @bp   BIFF buffer
 * @wb   workbook
 * @fidx format index
 *
 * Write a format to file
 * See S59D8E.HTM
 **/
static void
write_format (BiffPut *bp, ExcelWorkbook *wb, int fidx)
{
	gint len;
	char *buf;
	guint8 data[64];
	StyleFormat const *sf = formats_get_format(wb, fidx);

	char *format = style_format_as_XL (sf, FALSE);

	d (1, printf ("Writing format 0x%x: %s\n", fidx, format););

	/* Kludge for now ... */
	if (wb->ver >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, (0x400|BIFF_FORMAT));
	else
		ms_biff_put_var_next (bp, BIFF_FORMAT);

	MS_OLE_SET_GUINT16 (data, fidx);
	ms_biff_put_var_write (bp, data, 2);

	len = biff_convert_text(&buf, format, MS_BIFF_V7);
	biff_put_text (bp, buf, len, MS_BIFF_V7, TRUE, AS_PER_VER);
	ms_biff_put_commit (bp);
	g_free (buf);
	g_free (format);
}

/**
 * write_formats
 * @bp BIFF buffer
 * @wb workbook
 *
 * Write all formats to file.
 * Although we do, the formats apparently don't have to be written out in order
 **/
static void
write_formats (BiffPut *bp, ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->formats->two_way_table;
	guint nformats = twt->idx_to_key->len;
	int magic_num [] = { 5, 6, 7, 8, 0x2a, 0x29, 0x2c, 0x2b };
	guint i;

	/* The built-in fonts which get localized */
	for (i = 0; i < sizeof magic_num / sizeof magic_num[0]; i++)
		write_format (bp, wb, magic_num [i]);

	/* The custom fonts */
	for (i = EXCEL_BUILTIN_FORMAT_LEN; i < nformats; i++)
		write_format (bp, wb, i);
}

/**
 * Make bitmap for keeping track of cells in use
 **/
static gpointer
cell_used_map_new (ExcelSheet *esheet)
{
	long nwords;
	gpointer ptr = NULL;
	if (esheet->max_col > 0 && esheet->max_row > 0) {
		nwords = (esheet->max_col * esheet->max_row - 1) / 32 + 1;
		ptr = g_malloc0 (nwords * 4);
	}
	return ptr;
}

/**
 * Mark cell in use in bitmap
 **/
static void
cell_mark_used (ExcelSheet *esheet, int col, int row)
{
	long bit_ix = row * esheet->max_col + col;
	long word_ix = bit_ix / 32;
	int  rem     = bit_ix % 32;

	if (esheet && esheet->cell_used_map)
		*((guint32 *) esheet->cell_used_map + word_ix) |= 1 << rem;
}

/**
 * Return true if cell marked in use in bitmap
 **/
static gboolean
cell_is_used (const ExcelSheet *esheet, int col, int row)
{
	long bit_ix = row * esheet->max_col + col;
	long word_ix = bit_ix / 32;
	int  rem     = bit_ix % 32;
	gboolean ret = FALSE;

	if (esheet && esheet->cell_used_map)
		ret = 1 & *((guint32 *) esheet->cell_used_map + word_ix) >> rem;

	return ret;
}

/**
 * Get default MStyle of esheet
 *
 * FIXME: This works now. But only because the default style for a
 * sheet or workbook can't be changed. Unfortunately, there is no
 * proper API for accessing the default style of an existing sheet.
 **/
static MStyle *
get_default_mstyle (void)
{
	return mstyle_new_default ();
}

/**
 * Initialize XF/MStyle table.
 *
 * The table records MStyles. For each MStyle, an XF record will be
 * written to file.
 **/
static void
xf_init (ExcelWorkbook *wb)
{
	wb->xf = g_new (XF, 1);
	/* Excel starts at XF_RESERVED for user defined xf */
	wb->xf->two_way_table
		= two_way_table_new (mstyle_hash, (GCompareFunc) mstyle_equal,
				     XF_RESERVED);
	wb->xf->default_style = get_default_mstyle ();
}


/**
 * Get an mstyle, given index
 **/
static MStyle *
xf_get_mstyle (ExcelWorkbook *wb, gint idx)
{
	TwoWayTable *twt = wb->xf->two_way_table;

	return (MStyle *) two_way_table_idx_to_key (twt, idx);
}

/**
 * Free XF/MStyle table
 **/
static void
xf_free (ExcelWorkbook *wb)
{
	TwoWayTable *twt;
	MStyle *st;
	guint i;

	if (wb && wb->xf) {
		if (wb->xf->two_way_table) {
			twt = wb->xf->two_way_table;
			for (i = 0; i < twt->idx_to_key->len; i++) {
				st = xf_get_mstyle (wb, i + twt->base);
				mstyle_unref (st);
			}
			two_way_table_free (wb->xf->two_way_table);
		}
		mstyle_unref (wb->xf->default_style);
		g_free (wb->xf);
		wb->xf = NULL;
	}
}

/**
 * Callback called when putting MStyle to table. Print to debug log when
 * style is added. Add a reference when putting a style into the table.
 **/
static void
after_put_mstyle (MStyle *st, gboolean was_added, gint index, gconstpointer dummy)
{
	if (was_added) {
		d (1, {
			printf ("Found unique mstyle %d\n", index);
		   mstyle_dump (st);});
		mstyle_ref (st);
	}
}

/**
 * Add an MStyle to table if it is not already there.
 **/
static gint
put_mstyle (ExcelWorkbook *wb, MStyle *st)
{
	TwoWayTable *twt = wb->xf->two_way_table;

	return two_way_table_put (twt, st, TRUE,
				  (AfterPutFunc) after_put_mstyle, NULL);
}

/**
 * Get ExcelCell record for cell position.
 **/
inline static ExcelCell *
excel_cell_get (ExcelSheet *esheet, int col, int row)
{
	g_return_val_if_fail (col < esheet->max_col, NULL);
	g_return_val_if_fail (row < esheet->max_row, NULL);
	return *(esheet->cells + row) + col;
}

/**
 * Add MStyle of cell to table if not already there. Cache some info.
 **/
static void
pre_cell (gconstpointer dummy, Cell *cell, ExcelSheet *esheet)
{
	ExcelCell *c;
	int col, row;
	MStyle *cell_style;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (esheet != NULL);

	count_io_progress_update (esheet->wb->io_context, 1);

	col = cell->pos.col;
	row = cell->pos.row;

	d (3, printf ("Pre cell %s\n", cell_coord_name (col, row)););

	if (col >= esheet->max_col || row >= esheet->max_row) {
		/* sheet_get_extent clipped blank cells, this had better be blank. */
		g_return_if_fail (cell_is_blank (cell));
		return;
	}

	cell_mark_used (esheet, col, row);
	if (cell_has_expr (cell))
		ms_formula_build_pre_data (esheet, cell->base.expression);

	/* Save cell pointer */
	c = excel_cell_get (esheet, col, row);
	c->gnum_cell = cell;

	/* For the general format XL assigns the parse format */
	cell_style = cell_get_mstyle (cell);
	if (cell->value != NULL &&
	    VALUE_FMT (cell->value) != NULL &&
	    !style_format_is_general (VALUE_FMT (cell->value)) &&
	    style_format_is_general (mstyle_get_format (cell_style))) {
		cell_style = mstyle_copy (cell_style);
		mstyle_set_format (cell_style, VALUE_FMT (cell->value));
		c->xf = put_mstyle (esheet->wb, cell_style);
		mstyle_ref (cell_style);
	} else
		c->xf = put_mstyle (esheet->wb, cell_style);
}

/**
 * Add MStyle of blank cell to table if not already there.
 **/
static void
pre_blank (ExcelSheet *esheet, int col, int row)
{
	ExcelCell *c = excel_cell_get (esheet, col, row);

	MStyle *st = sheet_style_get (esheet->gnum_sheet,
				      col, row);

	d (3, printf ("Pre blank %s\n", cell_coord_name (col, row)););
	c->gnum_cell = NULL;
	c->xf = put_mstyle (esheet->wb, st);
}

/**
 * Add MStyles of all blank cells to table if not already there.
 **/
static void
pre_blanks (ExcelSheet *esheet)
{
	int row, col;

	for (row = 0; row < esheet->max_row; row++) {
		for (col = 0; col < esheet->max_col; col++)
			if (!cell_is_used (esheet, col, row))
				pre_blank (esheet, col, row);
		count_io_progress_update (esheet->wb->io_context, 1);
	}
}

/**
 * pre_colstyle :
 * @esheet :
 *
 * Find the xf record to associate with each column in the sheet.
 */
static void
pre_colstyle (ExcelSheet *esheet)
{
	int col;
	Sheet *sheet;

	g_return_if_fail (esheet != NULL);

	sheet = esheet->gnum_sheet;
	for (col = 0; col < esheet->max_col; col++)
		esheet->col_xf[col]  = put_mstyle (esheet->wb,
			sheet_style_most_common_in_col (sheet, col));
}

/**
 * Add all MStyles in workbook to table
 *
 * First, we invoke pre_cell on each cell in the cell hash. This
 * computes the style for each non-blank cell, and adds the style to
 * the table.
 *
 * We also need styles for blank cells, so we let pre_blanks scan all
 * blank cell positions, limited by max_col and max_row.
 *
 * To see what cells are blank, we use the cell_used_map, where
 * pre_cell sets a bit for each cell which is in use. This should be
 * somewhat faster than just visiting each cell postion in sequence.
 *
 * Another optimization we do: When writing to file, we need the cell
 * pointer and the XF style index for each cell. To avoid having to
 * locate the cell pointer and computing the style once more, we cache
 * the cell pointer and XF index in an ExcelCell in the cells table.
 **/
static void
gather_styles (ExcelWorkbook *wb)
{
	guint n = 0, i;
	enum {N_CELLS_BETWEEN_UPDATES = 20};

	for (i = 0; i < wb->sheets->len; i++) {
		ExcelSheet *s;

		s = g_ptr_array_index (wb->sheets, i);
		n += g_hash_table_size (s->gnum_sheet->cell_hash);
		n += s->max_row;
	}

	count_io_progress_set (wb->io_context, n, N_CELLS_BETWEEN_UPDATES);
	for (i = 0; i < wb->sheets->len; i++) {
		ExcelSheet *esheet = g_ptr_array_index (wb->sheets, i);

		pre_colstyle (esheet);

		/* Gather style info from cells and blanks */
		g_hash_table_foreach (esheet->gnum_sheet->cell_hash,
				      (GHFunc) pre_cell, esheet);
		pre_blanks (esheet);
	}
	io_progress_unset (wb->io_context);
}

/**
 * map_pattern_index_to_excel
 * @i Gnumeric pattern index
 *
 * Map Gnumeric pattern index to Excel ditto.
 *
 * FIXME:
 * Move this and ms-excel-read.c::excel_map_pattern_index_from_excel
 * to common utility file. Generate one map from the other for
 * consistency
 **/
static int
map_pattern_index_to_excel (int const i)
{
	static int const map_to_excel[] = {
		 0,
		 1,  3,  2,  4, 17, 18,
		 5,  6,  8,  7,  9, 10,
		11, 12, 13, 14, 15, 16
	};

	/* Default to Solid if out of range */
	g_return_val_if_fail (i >= 0 &&
			      i < (int) (sizeof(map_to_excel)/sizeof(int)), 0);

	return map_to_excel[i];
}

/**
 * halign_to_excel
 * @halign Gnumeric horizontal alignment
 *
 * Map Gnumeric horizontal alignment to Excel bitfield
 * See S59E1E.HTM
 **/
inline static guint
halign_to_excel (StyleHAlignFlags halign)
{
	guint ialign;

	switch (halign) {
	case HALIGN_GENERAL:
		ialign = MS_BIFF_H_A_GENERAL;
		break;
	case HALIGN_LEFT:
		ialign = MS_BIFF_H_A_LEFT;
		break;
	case HALIGN_RIGHT:
		ialign = MS_BIFF_H_A_RIGHT;
		break;
	case HALIGN_CENTER:
		ialign = MS_BIFF_H_A_CENTER;
		break;
	case HALIGN_FILL:
		ialign = MS_BIFF_H_A_FILL;
		break;
	case HALIGN_JUSTIFY:
		ialign = MS_BIFF_H_A_JUSTIFTY;
		break;
	case HALIGN_CENTER_ACROSS_SELECTION:
		ialign = MS_BIFF_H_A_CENTER_ACROSS_SELECTION;
		break;
	default:
		ialign = MS_BIFF_H_A_GENERAL;
	}

	return ialign;
}

/**
 * valign_to_excel
 * @valign Gnumeric vertical alignment
 *
 * Map Gnumeric vertical alignment to Excel bitfield
 * See S59E1E.HTM
 **/
inline static guint
valign_to_excel (StyleVAlignFlags valign)
{
	guint ialign;

	switch (valign) {
	case VALIGN_TOP:
		ialign = MS_BIFF_V_A_TOP;
		break;
	case VALIGN_BOTTOM:
		ialign = MS_BIFF_V_A_BOTTOM;
		break;
	case VALIGN_CENTER:
		ialign = MS_BIFF_V_A_CENTER;
		break;
	case VALIGN_JUSTIFY:
		ialign = MS_BIFF_V_A_JUSTIFY;
		break;
	default:
		ialign = MS_BIFF_V_A_TOP;
	}

	return ialign;
}

/**
 * orientation_to_excel
 * @orientation Gnumeric orientation
 *
 * Map Gnumeric orientation to Excel bitfield
 * See S59E1E.HTM
 **/
inline static guint
orientation_to_excel (StyleOrientation orientation)
{
	guint ior;

	switch (orientation) {
	case ORIENT_HORIZ:
		ior = MS_BIFF_O_HORIZ;
		break;
	case ORIENT_VERT_HORIZ_TEXT:
		ior = MS_BIFF_O_VERT_HORIZ;
		break;
	case ORIENT_VERT_VERT_TEXT:
		ior = MS_BIFF_O_VERT_VERT;
		break;
	case ORIENT_VERT_VERT_TEXT2:
		ior = MS_BIFF_O_VERT_VERT2;
		break;
	default:
		ior = MS_BIFF_O_HORIZ;
	}

	return ior;
}

/**
 * border_type_to_excel
 * @btype Gnumeric border type
 * @ver   Biff version
 *
 * Map Gnumeric border type to Excel bitfield
 * See S59E1E.HTM
 **/
inline static guint
border_type_to_excel (StyleBorderType btype, MsBiffVersion ver)
{
	guint ibtype = btype;

	if (btype <= STYLE_BORDER_NONE)
		ibtype = STYLE_BORDER_NONE;

	if (ver <= MS_BIFF_V7) {
		if (btype > STYLE_BORDER_HAIR)
			ibtype = STYLE_BORDER_MEDIUM;
	}

	return ibtype;
}

/**
 * style_color_to_pal_index
 * @color color
 * @wb    workbook
 * @auto_back     Auto colors to compare against.
 * @auto_font
 *
 * Return Excel color index, possibly auto, for a style color.
 * The auto colors are passed in by caller to avoid having to ref and unref
 * the same autocolors over and over.
 */
static guint8
style_color_to_pal_index (StyleColor *color, ExcelWorkbook *wb,
			  StyleColor *auto_back, StyleColor *auto_font)
{
	guint8 idx;
	
	if (color->is_auto) {
		if (color == auto_back)
			idx = PALETTE_AUTO_BACK;
		else if (color == auto_font)
				idx = PALETTE_AUTO_FONT;
		else
			idx = PALETTE_AUTO_PATTERN;
	} else 
		idx = palette_get_index	(wb, style_color_to_rgb888 (color));

	return idx;
}

/**
 * get_xf_differences
 * @wb   workbook
 * @xfd  XF data
 * @parentst parent style (Not used at present)
 *
 * Fill out map of differences to parent style
 * See S59E1E.HTM
 *
 * FIXME
 * At present, we are using a fixed XF record 0, which is the parent of all
 * others. Can we use the actual default style as XF 0?
 **/
static void
get_xf_differences (ExcelWorkbook *wb, BiffXFData *xfd, MStyle *parentst)
{
	int i;

	xfd->differences = 0;

	if (xfd->format_idx != FORMAT_MAGIC)
		xfd->differences |= 1 << MS_BIFF_D_FORMAT_BIT;
	if (xfd->font_idx != FONT_MAGIC)
		xfd->differences |= 1 << MS_BIFF_D_FONT_BIT;
	/* hmm. documentation doesn't say that alignment bit is
	   affected by vertical alignment, but it's a reasonable guess */
	if (xfd->halign != HALIGN_GENERAL || xfd->valign != VALIGN_TOP
	    || xfd->wrap_text)
		xfd->differences |= 1 << MS_BIFF_D_ALIGN_BIT;
	for (i = 0; i < STYLE_ORIENT_MAX; i++) {
		/* Should we also test colors? */
		if (xfd->border_type[i] != BORDER_MAGIC) {
			xfd->differences |= 1 << MS_BIFF_D_BORDER_BIT;
			break;
		}
	}
	if (xfd->pat_foregnd_col != PALETTE_AUTO_PATTERN
	    || (xfd->pat_backgnd_col) != PALETTE_AUTO_BACK
	    || xfd->fill_pattern_idx != FILL_MAGIC)
		xfd->differences |= 1 << MS_BIFF_D_FILL_BIT;
	if (xfd->hidden || !xfd->locked)
		xfd->differences |= 1 << MS_BIFF_D_LOCK_BIT;
}

#ifndef NO_DEBUG_EXCEL
/**
 * Log XF data for a record about to be written
 **/
static void
log_xf_data (ExcelWorkbook *wb, BiffXFData *xfd, int idx)
{
	if (ms_excel_write_debug > 1) {
		int i;
		ExcelFont *f = fonts_get_font (wb, xfd->font_idx);

		/* Formats are saved using the 'C' locale number format */
		char * desc = style_format_as_XL (xfd->style_format, FALSE);

		printf ("Writing xf 0x%x : font 0x%x (%s), format 0x%x (%s)\n",
			idx, xfd->font_idx, excel_font_to_string (f),
			xfd->format_idx, desc);
		g_free (desc);

		printf (" hor align 0x%x, ver align 0x%x, wrap_text %s\n",
			xfd->halign, xfd->valign, xfd->wrap_text ? "on" : "off");
		printf (" fill fg color idx 0x%x, fill bg color idx 0x%x"
			", pattern (Excel) %d\n",
			xfd->pat_foregnd_col, xfd->pat_backgnd_col,
			xfd->fill_pattern_idx);
		for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
			if (xfd->border_type[i] !=  STYLE_BORDER_NONE) {
				printf (" border_type[%d] : 0x%x"
					" border_color[%d] : 0x%x\n",
					i, xfd->border_type[i],
					i, xfd->border_color[i]);
			}
		}
		printf (" difference bits: 0x%x\n", xfd->differences);
	}
}
#endif

/**
 * build_xf_data
 * @wb   workbook
 * @xfd  XF data
 * @st   style
 *
 * Build XF data for a style
 * See S59E1E.HTM
 *
 * All BIFF V7 features are implemented, except:
 * - hidden - not yet in gnumeric.
 *
 * Apart from font, the style elements we retrieve do *not* need to be unrefed.
 **/
static void
build_xf_data (ExcelWorkbook *wb, BiffXFData *xfd, MStyle *st)
{
	ExcelFont *f;
	const StyleBorder *b;
	int pat;
	StyleColor *pattern_color;
	StyleColor *back_color;
	StyleColor *auto_back = style_color_auto_back ();
	StyleColor *auto_font = style_color_auto_font ();
	int i;

	memset (xfd, 0, sizeof *xfd);

	xfd->parentstyle  = XF_MAGIC;
	xfd->mstyle       = st;
	f = excel_font_new (st);
	xfd->font_idx     = fonts_get_index (wb, f);
	excel_font_free (f);
	xfd->style_format = mstyle_get_format (st);
	xfd->format_idx   = formats_get_index (wb, xfd->style_format);

	xfd->locked = mstyle_get_content_locked (st);
	xfd->hidden = mstyle_get_content_hidden (st);

	xfd->halign = mstyle_get_align_h (st);
	xfd->valign = mstyle_get_align_v (st);
	xfd->wrap_text   = mstyle_get_wrap_text (st);
	xfd->orientation = mstyle_get_orientation (st);
	xfd->indent = mstyle_get_indent (st);

	/* Borders */
	for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
		xfd->border_type[i]  = STYLE_BORDER_NONE;
		xfd->border_color[i] = 0;
		b = mstyle_get_border (st, MSTYLE_BORDER_TOP + i);
		if (b) {
			xfd->border_type[i] = b->line_type;
			xfd->border_color[i]
				= b->color
				? style_color_to_pal_index (b->color, wb,
							    auto_back,
							    auto_font)
				: PALETTE_AUTO_PATTERN;
		}
	}

	pat = mstyle_get_pattern (st);
	xfd->fill_pattern_idx = (map_pattern_index_to_excel (pat));

	pattern_color = mstyle_get_color (st, MSTYLE_COLOR_PATTERN);
	back_color   = mstyle_get_color (st, MSTYLE_COLOR_BACK);
	xfd->pat_foregnd_col
		= pattern_color
		? style_color_to_pal_index (pattern_color, wb, auto_back,
					    auto_font)
		: PALETTE_AUTO_PATTERN;
	xfd->pat_backgnd_col
		= back_color
		? style_color_to_pal_index (back_color, wb, auto_back,
					    auto_font)
		: PALETTE_AUTO_BACK;

	/* Solid patterns seem to reverse the meaning */
 	if (xfd->fill_pattern_idx == FILL_SOLID) {
		guint8 c = xfd->pat_backgnd_col;
		xfd->pat_backgnd_col = xfd->pat_foregnd_col;
		xfd->pat_foregnd_col = c;
	}

	get_xf_differences (wb, xfd, wb->xf->default_style);

	style_color_unref (auto_font);
	style_color_unref (auto_back);
}

/**
 * write_xf_magic_record
 * @bp  BIFF buffer
 * @ver BIFF version
 * @idx Index of record
 *
 * Write a built-in XF record to file
 * See S59E1E.HTM
 **/
static void
write_xf_magic_record (BiffPut *bp, MsBiffVersion ver, int idx)
{
	guint8 data[256];
	int lp;

	for (lp = 0; lp < 250; lp++)
		data[lp] = 0;

	if (ver >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (ver >= MS_BIFF_V8) {
		MS_OLE_SET_GUINT16(data+0, FONT_MAGIC);
		MS_OLE_SET_GUINT16(data+2, FORMAT_MAGIC);
		MS_OLE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		MS_OLE_SET_GUINT16(data+0, FONT_MAGIC);
		MS_OLE_SET_GUINT16(data+2, FORMAT_MAGIC);
		MS_OLE_SET_GUINT16(data+4, 0xfff5); /* FIXME: Magic */
		MS_OLE_SET_GUINT16(data+6, 0xf420);
		/* The "magic" 0x20c0 means:
		 * Fill patt foreground 64 = Autocontrast
		 * Fill patt background  1 = white */
		MS_OLE_SET_GUINT16(data+8, 0x20c0);

		if (idx == 1 || idx == 2)
			MS_OLE_SET_GUINT16 (data,1);
		if (idx == 3 || idx == 4)
			MS_OLE_SET_GUINT16 (data,2);
		if (idx == 15) {
			MS_OLE_SET_GUINT16 (data+4, 1);
			MS_OLE_SET_GUINT8  (data+7, 0x0);
		}
		if (idx == 16)
			MS_OLE_SET_GUINT32 (data, 0x002b0001); /* These turn up in the formats... */
		if (idx == 17)
			MS_OLE_SET_GUINT32 (data, 0x00290001);
		if (idx == 18)
			MS_OLE_SET_GUINT32 (data, 0x002c0001);
		if (idx == 19)
			MS_OLE_SET_GUINT32 (data, 0x002a0001);
		if (idx == 20)
			MS_OLE_SET_GUINT32 (data, 0x00090001);
		if (idx < 21 && idx > 15) /* Style bit ? */
			MS_OLE_SET_GUINT8  (data+7, 0xf8);
		if (idx == 0)
			MS_OLE_SET_GUINT8  (data+7, 0);

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

/**
 * write_xf_record
 * @bp  BIFF buffer
 * @wb  Workbook
 * @xfd XF data
 *
 * Write an XF record to file
 * See S59E1E.HTM
 * For BIFF V8, only font and format are written.
 **/
static void
write_xf_record (BiffPut *bp, ExcelWorkbook *wb, BiffXFData *xfd)
{
	guint8 data[256];
	guint16 itmp;
	int lp;
	int btype;

	for (lp = 0; lp < 250; lp++)
		data[lp] = 0;

	if (wb->ver >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (wb->ver >= MS_BIFF_V8) {
		MS_OLE_SET_GUINT16 (data+0, xfd->font_idx);
		MS_OLE_SET_GUINT16 (data+2, xfd->format_idx);
		MS_OLE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		MS_OLE_SET_GUINT16 (data+0, xfd->font_idx);
		MS_OLE_SET_GUINT16 (data+2, xfd->format_idx);

		itmp = 0x0001;
		if (xfd->hidden)
			itmp |= 1 << 1;
		if (xfd->locked)
			itmp |= 1;
		itmp |= (xfd->parentstyle << 4) & 0xFFF0; /* Parent style */
		MS_OLE_SET_GUINT16(data+4, itmp);

		/* Horizontal alignment */
		itmp  = halign_to_excel (xfd->halign) & 0x7;
		if (xfd->wrap_text)	/* Wrapping */
			itmp |= 1 << 3;
		/* Vertical alignment */
		itmp |= (valign_to_excel (xfd->valign) << 4) & 0x70;
		itmp |= (orientation_to_excel (xfd->orientation) << 8)
			 & 0x300;
		itmp |= xfd->differences & 0xFC00; /* Difference bits */
		MS_OLE_SET_GUINT16(data+6, itmp);

		/* Documentation is wrong - there is no fSxButton bit.
		 * The bg color uses the bit */
		itmp = 0;
		/* Fill pattern foreground color */
		itmp |= xfd->pat_foregnd_col & 0x7f;
		/* Fill pattern background color */
		itmp |= (xfd->pat_backgnd_col << 7) & 0x3f80;
		MS_OLE_SET_GUINT16(data+8, itmp);

		itmp  = xfd->fill_pattern_idx & 0x3f;

		/* Borders */
		btype = xfd->border_type[STYLE_BOTTOM];
		if (btype != STYLE_BORDER_NONE) {
			itmp |= (border_type_to_excel (btype, wb->ver) << 6)
				& 0x1c0;
			itmp |= (xfd->border_color[STYLE_BOTTOM] << 9)
				& 0xfe00;
		}
		MS_OLE_SET_GUINT16(data+10, itmp);

		itmp  = 0;
		btype = xfd->border_type[STYLE_TOP];
		if (btype != STYLE_BORDER_NONE) {
			itmp |= border_type_to_excel (btype, wb->ver) & 0x7;
			itmp |= (xfd->border_color[STYLE_TOP] << 9) & 0xfe00;
		}
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_LEFT],
					       wb->ver)
			 << 3) & 0x38;
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_RIGHT],
					       wb->ver)
			 << 6) & 0x1c0;
		MS_OLE_SET_GUINT16(data+12, itmp);

		itmp  = 0;
		if (xfd->border_type[STYLE_LEFT] != STYLE_BORDER_NONE)
			itmp  |= xfd->border_color[STYLE_LEFT] & 0x7f;
		if (xfd->border_type[STYLE_RIGHT] != STYLE_BORDER_NONE)
			itmp |= (xfd->border_color[STYLE_RIGHT] << 7) & 0x3f80;
		MS_OLE_SET_GUINT16(data+14, itmp);

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

/**
 * write_xf
 * @bp BIFF buffer
 * @wb workbook
 *
 * Write XF records to file for all MStyles
 **/
static void
write_xf (BiffPut *bp, ExcelWorkbook *wb)
{
	TwoWayTable *twt = wb->xf->two_way_table;
	int nxf = twt->idx_to_key->len;
	int lp;
	MStyle *st;
	BiffXFData xfd;

	guint32 style_magic[6] = { 0xff038010, 0xff068011, 0xff048012, 0xff078013,
				   0xff008000, 0xff058014 };

	/* Need at least 16 apparently */
	for (lp = 0; lp < XF_RESERVED; lp++)
		write_xf_magic_record (bp, wb->ver,lp);

	/* Scan through all the Styles... */
	for (; lp < nxf + twt->base; lp++) {
		st = xf_get_mstyle (wb, lp);
		build_xf_data (wb, &xfd, st);
#ifndef NO_DEBUG_EXCEL
		log_xf_data (wb, &xfd, lp);
#endif
		write_xf_record (bp, wb, &xfd);
	}

	/* See: S59DEA.HTM */
	for (lp = 0; lp < 6; lp++) {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_STYLE, 4);
		MS_OLE_SET_GUINT32 (data, style_magic[lp]); /* cop out */
		ms_biff_put_commit (bp);
	}

	/* See: S59E14.HTM */
	if (wb->ver >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_USESELFS, 2);
		MS_OLE_SET_GUINT16 (data, 0x1); /* we are language naturals */
		ms_biff_put_commit (bp);
	}
}

int
ms_excel_write_map_errcode (Value const * const v)
{
	char const * const mesg = v->v_err.mesg->str;
	if (!strcmp (gnumeric_err_NULL, mesg))
		return 0;
	if (!strcmp (gnumeric_err_DIV0, mesg))
		return 7;
	if (!strcmp (gnumeric_err_VALUE, mesg))
		return 15;
	if (!strcmp (gnumeric_err_REF, mesg))
		return 23;
	if (!strcmp (gnumeric_err_NAME, mesg))
		return 29;
	if (!strcmp (gnumeric_err_NUM, mesg))
		return 36;
	if (!strcmp (gnumeric_err_NA, mesg))
		return 42;

	/* Map non-excel errors to #!NAME */
	return 29;
}

/**
 * write_value
 * @bp  BIFF buffer
 * @v   value
 * @ver BIFF version
 * @col column
 * @row row
 * @xf  XF index
 *
 * Write cell value to file
 **/
static void
write_value (BiffPut *bp, Value *v, MsBiffVersion ver,
	     guint32 col, guint32 row, guint16 xf)
{
	switch (v->type) {

	case VALUE_EMPTY: {
		guint8 *data = ms_biff_put_len_next (bp, (0x200 | BIFF_BLANK), 6);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		ms_biff_put_commit (bp);
		break;
	}
	case VALUE_BOOLEAN:
	case VALUE_ERROR: {
		guint8 *data = ms_biff_put_len_next (bp, (0x200 | BIFF_BOOLERR), 8);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		if (v->type == VALUE_ERROR) {
			MS_OLE_SET_GUINT8 (data + 6, ms_excel_write_map_errcode (v));
			MS_OLE_SET_GUINT8 (data + 7, 1); /* Mark as a err */
		} else {
			MS_OLE_SET_GUINT8 (data + 6, v->v_bool.val ? 1 : 0);
			MS_OLE_SET_GUINT8 (data + 7, 0); /* Mark as a bool */
		}
		ms_biff_put_commit (bp);
		break;
	}
	case VALUE_INTEGER: {
		gnum_int vint = v->v_int.val;
		guint8 *data;

		d (3, printf ("Writing %d %d\n", vint, v->v_int.val););
		if (((vint<<2)>>2) != vint) { /* Chain to floating point then. */
			Value *vf = value_new_float (v->v_int.val);
			write_value (bp, vf, ver, col, row, xf);
			value_release (vf);
		} else {
			data = ms_biff_put_len_next (bp, (0x200 | BIFF_RK), 10);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			/* Integers can always be represented as integers.
			 * Use RK form 2 */
			MS_OLE_SET_GUINT32 (data + 6, (vint<<2) + 2);
			ms_biff_put_commit (bp);
		}
		break;
	}
	case VALUE_FLOAT: {
		gnum_float val = v->v_float.val;
		gboolean is_int = ((val - (int)val) == 0.0) &&
			(((((int)val)<<2)>>2) == ((int)val));

		d (3, printf ("Writing %g is (%g %g) is int ? %d\n",
			      (double)val,
			      (double)(1.0 * (int)val),
			      (double)(1.0 * (val - (int)val)),
			      is_int););

		/* FIXME : Add test for double with 2 digits of fraction
		 * and represent it as a mode 3 RK (val*100) construct */
		if (is_int) { /* not nice but functional */
			Value *vi = value_new_int (val);
			write_value (bp, vi, ver, col, row, xf);
			value_release (vi);
		} else if (ver >= MS_BIFF_V7) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (bp, (0x200 | BIFF_NUMBER), 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			gnumeric_set_le_double (data + 6, val);
			ms_biff_put_commit (bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (bp, (0x200 | BIFF_RK));
			gnumeric_set_le_double (data+6-4, val);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			data[6] &= 0xfc;
			ms_biff_put_var_write  (bp, data, 10); /* Yes loose it. */
			ms_biff_put_commit (bp);
		}
		break;
	}
	case VALUE_STRING: {
		guint8 data[16];
		gint len;
		char *buf;

		g_return_if_fail (v->v_str.val->str);

		g_return_if_fail (ver < MS_BIFF_V8); /* Use SST stuff in fulness of time */

		/* See: S59DDC.HTM ( for RSTRING ) */
		/* See: S59D9D.HTM ( for LABEL ) */
		len = biff_convert_text(&buf, v->v_str.val->str, MS_BIFF_V7);
		ms_biff_put_var_next   (bp, (0x200 | BIFF_LABEL));
		EX_SETXF (data, xf);
		EX_SETCOL(data, col);
		EX_SETROW(data, row);
		EX_SETSTRLEN (data, len);
		ms_biff_put_var_write  (bp, data, 8);
		biff_put_text (bp, buf, len, MS_BIFF_V7, FALSE, AS_PER_VER);
		g_free(buf);
		ms_biff_put_commit (bp);
		break;
	}
	default:
		printf ("Unhandled value type %d\n", v->type);
		break;
	}
}

/**
 * write_formula
 * @bp    BIFF buffer
 * @esheet sheet
 * @cell  cell
 * @xf    XF index
 *
 * Write formula to file
 **/
static void
write_formula (BiffPut *bp, ExcelSheet *esheet, const Cell *cell, gint16 xf)
{
	guint8   data[22];
	guint8   lendat[2];
	guint32  len;
	gboolean string_result = FALSE;
	gint     col, row;
	Value   *v;
	GnmExpr const *expr;

	g_return_if_fail (bp);
	g_return_if_fail (cell);
	g_return_if_fail (esheet);
	g_return_if_fail (cell_has_expr (cell));
	g_return_if_fail (cell->value);

	col = cell->pos.col;
	row = cell->pos.row;
	v = cell->value;
	expr = cell->base.expression;

	/* See: S59D8F.HTM */
	ms_biff_put_var_next (bp, BIFF_FORMULA);
	EX_SETROW (data, row);
	EX_SETCOL (data, col);
	EX_SETXF  (data, xf);
	switch (v->type) {
	case VALUE_INTEGER :
	case VALUE_FLOAT :
		gnumeric_set_le_double (data + 6, value_get_as_float (v));
		break;

	case VALUE_STRING :
		MS_OLE_SET_GUINT32 (data +  6, 0x00000000);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		string_result = TRUE;
		break;

	case VALUE_BOOLEAN :
		MS_OLE_SET_GUINT32 (data +  6,
				    v->v_bool.val ? 0x10001 : 0x1);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_ERROR :
		MS_OLE_SET_GUINT32 (data +  6,
				    0x00000002 | (ms_excel_write_map_errcode (v) << 16));
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_EMPTY :
		MS_OLE_SET_GUINT32 (data +  6, 0x00000003);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	default :
		g_warning ("Unhandled value->type (%d) in excel::write_formula.", v->type);
	}

	MS_OLE_SET_GUINT16 (data + 14, 0x0); /* alwaysCalc & calcOnLoad */
	MS_OLE_SET_GUINT32 (data + 16, 0x0);
	MS_OLE_SET_GUINT16 (data + 20, 0x0); /* bogus len, fill in later */
	ms_biff_put_var_write (bp, data, 22);
	len = ms_excel_write_formula (bp, esheet, expr, col, row, 0);

	g_assert (len <= 0xffff);

	ms_biff_put_var_seekto (bp, 20);
	MS_OLE_SET_GUINT16 (lendat, len);
	ms_biff_put_var_write (bp, lendat, 2);

	ms_biff_put_commit (bp);

	if (expr->any.oper == GNM_EXPR_OP_ARRAY &&
	    expr->array.x == 0 && expr->array.y == 0) {
		ms_biff_put_var_next (bp, BIFF_ARRAY);
		MS_OLE_SET_GUINT16 (data+0, cell->pos.row);
		MS_OLE_SET_GUINT16 (data+2, cell->pos.row + expr->array.rows-1);
		MS_OLE_SET_GUINT16 (data+4, cell->pos.col);
		MS_OLE_SET_GUINT16 (data+5, cell->pos.col + expr->array.cols-1);
		MS_OLE_SET_GUINT16 (data+6, 0x0); /* alwaysCalc & calcOnLoad */
		MS_OLE_SET_GUINT32 (data+8, 0);
		MS_OLE_SET_GUINT16 (data+12, 0); /* bogus len, fill in later */
		ms_biff_put_var_write (bp, data, 14);
		len = ms_excel_write_formula (bp, esheet,
			expr->array.corner.expr, col, row, 0);

		g_assert (len <= 0xffff);

		ms_biff_put_var_seekto (bp, 12);
		MS_OLE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (bp, lendat, 2);
		ms_biff_put_commit (bp);
	}

	if (string_result) {
		gint len;
		gchar *str, *buf;

		ms_biff_put_var_next (bp, 0x200|BIFF_STRING);
		str = value_get_as_string (v);
		len = biff_convert_text(&buf, str, MS_BIFF_V7);
		biff_put_text (bp, buf, len, MS_BIFF_V7, TRUE, SIXTEEN_BIT);
		g_free (buf);
		g_free (str);
		ms_biff_put_commit (bp);
	}
}

#define MAX_BIFF_NOTE_CHUNK	2048

/* biff7 an earlier */
static void
write_biff7_comments (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 data[6];
	GSList *l, *comments;
	MsBiffVersion ver = esheet->wb->ver;

	comments = sheet_objects_get (esheet->gnum_sheet, NULL,
				      CELL_COMMENT_TYPE);

	for (l = comments; l; l = l->next) {
		CellComment *cc = l->data;
		char *comment = (char *)cell_comment_text_get (cc);
		Range const *pos = sheet_object_range_get (SHEET_OBJECT (cc));

		guint16 len;
		char *buf, *p;

		g_return_if_fail (comment != NULL);
		g_return_if_fail (pos != NULL);

		len = biff_convert_text(&buf, comment, ver);
		p = buf;
		ms_biff_put_var_next (bp, BIFF_NOTE);
		MS_OLE_SET_GUINT16 (data + 0, pos->start.row);
		MS_OLE_SET_GUINT16 (data + 2, pos->start.col);
		MS_OLE_SET_GUINT16 (data + 4, len);
		ms_biff_put_var_write (bp, data, 6);

repeat:
		if (len > MAX_BIFF_NOTE_CHUNK) {
			biff_put_text (bp, p, MAX_BIFF_NOTE_CHUNK, ver, FALSE, AS_PER_VER);

			ms_biff_put_commit (bp);

			p += MAX_BIFF_NOTE_CHUNK;
			len -= MAX_BIFF_NOTE_CHUNK;

			ms_biff_put_var_next (bp, BIFF_NOTE);
			MS_OLE_SET_GUINT16 (data + 0, 0xffff);
			MS_OLE_SET_GUINT16 (data + 2, 0);
			MS_OLE_SET_GUINT16 (data + 4, MIN (MAX_BIFF_NOTE_CHUNK, len));
			ms_biff_put_var_write (bp, data, 6);

			goto repeat;
		} else {
			biff_put_text (bp, p, len, ver, FALSE, AS_PER_VER);
			ms_biff_put_commit (bp);
		}
		g_free(buf);
	}
	g_slist_free (comments);
}

/**
 * write_cell
 * @bp    biff buffer
 * @esheet sheet
 * @cell  cell
 *
 * Write cell to file
 **/
static void
write_cell (BiffPut *bp, ExcelSheet *esheet, const ExcelCell *cell)
{
	gint col, row;
	Cell *gnum_cell;

	g_return_if_fail (bp);
	g_return_if_fail (cell);
	g_return_if_fail (cell->gnum_cell);
	g_return_if_fail (esheet);

	gnum_cell = cell->gnum_cell;
	col = gnum_cell->pos.col;
	row = gnum_cell->pos.row;

	d (2, {
		ParsePos tmp;
		printf ("Writing cell at %s '%s' = '%s', xf = 0x%x\n",
			cell_name (gnum_cell),
			(cell_has_expr (gnum_cell) ?
			 gnm_expr_as_string (gnum_cell->base.expression,
					      parse_pos_init_cell (&tmp, gnum_cell)) :
			 "none"),
			(gnum_cell->value ?
			 value_get_as_string (gnum_cell->value) : "empty"),
			cell->xf);
	});
	if (cell_has_expr (gnum_cell))
		write_formula (bp, esheet, gnum_cell, cell->xf);
	else if (gnum_cell->value)
		write_value (bp, gnum_cell->value, esheet->wb->ver,
			     col, row, cell->xf);
}

/**
 * write_mulblank
 * @bp      BIFF buffer
 * @esheet   sheet
 * @end_col last blank column
 * @row     row
 * @xf_list list of XF indices - one per cell
 * @run     number of blank cells
 *
 * Write multiple blanks to file
 **/
static void
write_mulblank (BiffPut *bp, ExcelSheet *esheet, guint32 end_col, guint32 row,
		guint16 const *xf_list, int run)
{
	guint16 xf;
	g_return_if_fail (bp);
	g_return_if_fail (run);
	g_return_if_fail (esheet);

	if (run == 1) {
		guint8 *data;

		xf = xf_list [0];
		d (2, printf ("Writing blank at %s, xf = 0x%x\n",
			      cell_coord_name (end_col, row), xf););

		data = ms_biff_put_len_next (bp, 0x200|BIFF_BLANK, 6);
		EX_SETXF (data, xf);
		EX_SETCOL(data, end_col);
		EX_SETROW(data, row);
	} else { /* S59DA7.HTM */
		guint8 *ptr, *data;
		guint32 len = 4 + 2*run + 2;
		int i;

		d (2, {
			/* Strange looking code because the second
			 * cell_coord_name call overwrites the result of the
			 * first */
			printf ("Writing multiple blanks %s",
				cell_coord_name (end_col + 1 - run, row));
			printf (":%s\n", cell_coord_name (end_col, row));
		});

		data = ms_biff_put_len_next (bp, BIFF_MULBLANK, len);

		EX_SETCOL (data, end_col + 1 - run);
		EX_SETROW (data, row);
		MS_OLE_SET_GUINT16 (data + len - 2, end_col);
		ptr = data + 4;
		for (i = 0 ; i < run ; i++) {
			xf = xf_list [i];
			d (3, printf (" xf(%s) = 0x%x",
				      cell_coord_name (end_col + 1 - run, row),
				      xf););
			MS_OLE_SET_GUINT16 (ptr, xf);
			ptr += 2;
		}

		d (3, printf ("\n"););
	}

	ms_biff_put_commit (bp);
}

/**
 * write_guts
 * @bp    :  BIFF buffer
 * @esheet : sheet
 *
 * Write information about outline mode gutters.
 * See: S59D92.HTM
 */
static void
write_guts (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_GUTS, 8);
	int row_level = MIN (esheet->gnum_sheet->rows.max_outline_level, 0x7);
	int col_level = MIN (esheet->gnum_sheet->cols.max_outline_level, 0x7);
	int row_size = 0, col_size = 0;

	/* This seems to be what the default is */
	if (row_level > 0)
		row_size = 5 + 12 * row_level;
	if (col_level > 0)
		col_size = 5 + 12 * col_level;
	MS_OLE_SET_GUINT16 (data+0, row_size);
	MS_OLE_SET_GUINT16 (data+2, col_size);
	MS_OLE_SET_GUINT16 (data+4, row_level);
	MS_OLE_SET_GUINT16 (data+6, col_level);
	ms_biff_put_commit (bp);
}

/**
 * write_default_row_height
 * @bp  BIFF buffer
 * @esheet sheet
 *
 * Write default row height
 * See: S59D72.HTM
 */
static void
write_default_row_height (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	double def_height;
	guint16 options = 0x0;
	guint16 height;

	def_height = sheet_row_get_default_size_pts (esheet->gnum_sheet);
	height = (guint16) (20. * def_height);
	d (1, printf ("Default row height 0x%x;\n", height););
	data = ms_biff_put_len_next (bp, 0x200|BIFF_DEFAULTROWHEIGHT, 4);
	MS_OLE_SET_GUINT16 (data + 0, options);
	MS_OLE_SET_GUINT16 (data + 2, height);
	ms_biff_put_commit (bp);
}

/**
 * write_wsbool
 * @bp    : BIFF buffer
 * @esheet : sheet
 *
 * Write some of the sheet preference flags.  I have no idea why they are stuck
 * here in their own distinct record rather than with all the others.
 *
 * See: S59E1C.HTM
 */
static void
write_wsbool (BiffPut *bp, ExcelSheet *esheet)
{
	guint8  *data = ms_biff_put_len_next (bp, BIFF_WSBOOL, 2);
	guint16  options = 0;

	/* 0x0001 automatic page breaks are visible */
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	if (esheet->gnum_sheet->outline_symbols_below)
		options |= 0x040;
	if (esheet->gnum_sheet->outline_symbols_right)
		options |= 0x080;
	/* 0x0100 the Fit option is on (Page Setup dialog box, Page tab) */
	if (esheet->gnum_sheet->display_outlines)
		options |= 0x600;

	MS_OLE_SET_GUINT16 (data, options);
	ms_biff_put_commit (bp);
}

static void
margin_write (BiffPut *bp, guint16 op, double points)
{
	guint8 *data;
	double  margin;

	margin = unit_convert (points, UNIT_POINTS, UNIT_INCH);

	data = ms_biff_put_len_next (bp, op, 8);
	gnumeric_set_le_double (data, margin);

	ms_biff_put_commit (bp);
}

/**
 * init_base_char_width_for_write
 * @esheet sheet
 *
 * Initialize base character width
 */
static void
init_base_char_width_for_write (ExcelSheet *esheet)
{
	ExcelFont *f = NULL;
	/* default to Arial 10 */
	const char *name = "Arial";
	double size_pts = 20.* 10.;

	if (esheet && esheet->wb
	    && esheet->wb->xf && esheet->wb->xf->default_style) {
		f = excel_font_new (esheet->wb->xf->default_style);
		if (f) {
			name = f->style_font->font_name;
			size_pts = f->style_font->size_pts * 20.;
			excel_font_free (f);
		}
	}

	d (1, printf ("Font for column sizing: %s %.1f\n", name, size_pts););
	esheet->base_char_width =
		lookup_font_base_char_width_new (name, size_pts, FALSE);
	esheet->base_char_width_default =
		lookup_font_base_char_width_new (name, size_pts, TRUE);
}

/**
 * get_base_char_width
 * @esheet	the Excel sheet
 * @is_default  if true, this is for the default width.
 *
 * Excel uses the character width of the font in the "Normal" style.
 * The char width is based on the font in the "Normal" style.
 * This style is actually common to all sheets in the
 * workbook, but I find it more robust to treat it as a sheet
 * attribute.
 *
 * FIXME: There is a function with this name both in ms-excel-read.c and
 * ms-excel-write.c. The only difference is init_base_char_width_for_read
 * vs. init_base_char_width_for_write. Pass the function as parameter?
 * May be not. I don't like clever code.
 */
static double
get_base_char_width (ExcelSheet *esheet, gboolean const is_default)
{
	if (esheet->base_char_width <= 0)
		init_base_char_width_for_write (esheet);

	return is_default
		? esheet->base_char_width_default : esheet->base_char_width;
}

/**
 * write_default_col_width
 * @bp  BIFF buffer
 * @esheet sheet
 *
 * Write default column width
 * See: S59D73.HTM
 *
 * FIXME: Not yet roundtrip compatible. The problem is that the base
 * font when we export is the font in the default style. But this font
 * is hardcoded and is not changed when a worksheet is imported.
 */
static void
write_default_col_width (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	double def_width;
	double width_chars;
	guint16 width;

	def_width = sheet_col_get_default_size_pts (esheet->gnum_sheet);
	width_chars = def_width / get_base_char_width (esheet, TRUE);
	width = (guint16) (width_chars + .5);

	d (1, printf ("Default column width %d characters\n", width););

	data = ms_biff_put_len_next (bp, BIFF_DEFCOLWIDTH, 2);
	MS_OLE_SET_GUINT16 (data, width);
	ms_biff_put_commit (bp);
}

/**
 * write_colinfo
 * @bp:   BIFF buffer
 * @esheet:
 * @ci   : the descriptor of the first col
 * @last_index : the index of the last contiguous identical col
 * @xf_index   : the style index to the entire col (< 0 for none)
 *
 * Write column info for a run of identical columns
 */
static void
write_colinfo (BiffPut *bp, ExcelSheet *esheet,
	       ColRowInfo const *ci, int last_index, guint16 xf_index)
{
	guint8 *data;
	double  width_chars
		= ci->size_pts / get_base_char_width (esheet, FALSE);
	guint16 width = (guint16) (width_chars * 256.);

	guint16 options = 0;

	if (!ci->visible)
		options = 1;
	options |= (MIN (ci->outline_level, 0x7) << 8);
	if (ci->is_collapsed)
		options |= 0x1000;

	d (1, printf ("Column Formatting from col %d to %d of width "
		      "%f characters\n", ci->pos, last_index, width/256.0););

	/* NOTE : Docs lie.  length is 12 not 11 */
	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 12);
	MS_OLE_SET_GUINT16 (data +  0, ci->pos);	/* 1st  col formatted */
	MS_OLE_SET_GUINT16 (data +  2, last_index);	/* last col formatted */
	MS_OLE_SET_GUINT16 (data +  4, width);		/* width */
	MS_OLE_SET_GUINT16 (data +  6, xf_index);	/* XF index */
	MS_OLE_SET_GUINT16 (data +  8, options);	/* options */
	MS_OLE_SET_GUINT16 (data + 10, 0);		/* reserved = 0 */
	ms_biff_put_commit (bp);
}

/**
 * write_colinfos
 * @bp:     BIFF buffer
 * @esheet:
 *
 * Write column info for all columns
 * See: S59D72.HTM
 */
static void
write_colinfos (BiffPut *bp, ExcelSheet *esheet)
{
	ColRowInfo const *ci, *first = NULL;
	int i;
	guint16	new_xf, xf = 0;

	for (i = 0; i < esheet->max_col; i++) {
		ci = sheet_col_get (esheet->gnum_sheet, i);
		new_xf = esheet->col_xf [i];
		if (first == NULL) {
			first = ci;
			xf = new_xf;
		} else if (xf != new_xf || !colrow_equal (first, ci)) {
			write_colinfo (bp, esheet, first, i-1, xf);
			first = ci;
			xf = new_xf;
		}
	}
	if (first != NULL)
		write_colinfo (bp, esheet, first, i-1, xf);
}

/* See: S59D76.HTM */
static void
write_dimension (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	if (esheet->wb->ver >= MS_BIFF_V8) {
		data = ms_biff_put_len_next (bp, 0x200 | BIFF_DIMENSIONS, 14);
		MS_OLE_SET_GUINT32 (data +  0, 0);
		MS_OLE_SET_GUINT32 (data +  4, esheet->max_row);
		MS_OLE_SET_GUINT16 (data +  8, 0);
		MS_OLE_SET_GUINT16 (data + 10, esheet->max_col);
		MS_OLE_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 10);
		MS_OLE_SET_GUINT16 (data +  0, 0);
		MS_OLE_SET_GUINT16 (data +  2, esheet->max_row);
		MS_OLE_SET_GUINT16 (data +  4, 0);
		MS_OLE_SET_GUINT16 (data +  6, esheet->max_col);
		MS_OLE_SET_GUINT16 (data +  8, 0x0000);
}
	ms_biff_put_commit (bp);
}

static void
write_sheet_head (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	PrintInformation *pi;
	MsBiffVersion ver = esheet->wb->ver;
	Workbook *wb = esheet->gnum_sheet->workbook;
	double header = 0, footer = 0, left = 0, right = 0;

	g_return_if_fail (esheet != NULL);
	g_return_if_fail (esheet->gnum_sheet != NULL);
	g_return_if_fail (esheet->gnum_sheet->print_info != NULL);

	pi = esheet->gnum_sheet->print_info;

	/* See: S59D63.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCMODE, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D62.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCCOUNT, 2);
	MS_OLE_SET_GUINT16 (data, wb->iteration.max_number);
	ms_biff_put_commit (bp);

	/* See: S59DD7.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFMODE, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D9C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_ITERATION, 2);
	MS_OLE_SET_GUINT16 (data, wb->iteration.enabled ? 1 : 0);
	ms_biff_put_commit (bp);

	/* See: S59D75.HTM */
	data = ms_biff_put_len_next (bp, BIFF_DELTA, 8);
	gnumeric_set_le_double (data, wb->iteration.tolerance);
	ms_biff_put_commit (bp);

	/* See: S59DDD.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SAVERECALC, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD0.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTHEADERS, 2);
	MS_OLE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59DCF.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTGRIDLINES, 2);
	MS_OLE_SET_GUINT16 (data, pi->print_grid_lines);
	ms_biff_put_commit (bp);

	/* See: S59D91.HTM */
	data = ms_biff_put_len_next (bp, BIFF_GRIDSET, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	write_guts (bp, esheet);
	write_default_row_height (bp, esheet); /* Default row height */
	write_wsbool (bp, esheet);

	/* See: S59D6B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_COUNTRY, 4);
	MS_OLE_SET_GUINT32 (data, 0x002c0001); /* Made in the UK */
	ms_biff_put_commit (bp);

	/* See: S59E1C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WSBOOL, 2);
	MS_OLE_SET_GUINT16 (data, 0x04c1);
	ms_biff_put_commit (bp);

	/* See: S59D94.HTM */
	ms_biff_put_var_next (bp, BIFF_HEADER);
/*	biff_put_text (bp, "&A", MS_BIFF_V7, TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D8D.HTM */
	ms_biff_put_var_next (bp, BIFF_FOOTER);
/*	biff_put_text (bp, "&P", MS_BIFF_V7, TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D93.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HCENTER, 2);
	MS_OLE_SET_GUINT16 (data, pi->center_horizontally);
	ms_biff_put_commit (bp);

	/* See: S59E15.HTM */
	data = ms_biff_put_len_next (bp, BIFF_VCENTER, 2);
	MS_OLE_SET_GUINT16 (data, pi->center_vertically);
	ms_biff_put_commit (bp);

	print_info_get_margins (pi, &header, &footer, &left, &right);
	margin_write (bp, BIFF_LEFT_MARGIN,   left);
	margin_write (bp, BIFF_RIGHT_MARGIN,  right);
	margin_write (bp, BIFF_TOP_MARGIN,    pi->margins.top.points);
	margin_write (bp, BIFF_BOTTOM_MARGIN, pi->margins.bottom.points);

	write_setup (bp, esheet);
	write_externsheets (bp, esheet->wb, esheet);
	ms_formula_write_pre_data (bp, esheet, EXCEL_EXTERNNAME, ver);
	write_default_col_width (bp, esheet);
	write_colinfos (bp, esheet);
	write_dimension (bp, esheet);
}

static void
write_sheet_tail (IOContext *context, BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	MsBiffVersion ver = esheet->wb->ver;

	write_window1 (bp, ver, esheet->wb->gnum_wb_view);
	if (write_window2 (bp, ver, esheet))
		write_pane (bp, ver, esheet);

	if (ver >= MS_BIFF_V8) {
		/* Dont over think this just force the fraction into a/1000 */
		int const zoom = esheet->gnum_sheet->last_zoom_factor_used * 1000 + .5;
		data = ms_biff_put_len_next (bp, BIFF_SCL, 4);
		MS_OLE_SET_GUINT16 (data + 0, (guint16)zoom);
		MS_OLE_SET_GUINT16 (data + 2, 1000);
		ms_biff_put_commit (bp);
	}

	/* See: S59DE2.HTM */
#warning YUCK! store the real selection
	data = ms_biff_put_len_next (bp, BIFF_SELECTION, 15);
	MS_OLE_SET_GUINT32 (data +  0, 0x00000103);
	MS_OLE_SET_GUINT32 (data +  4, 0x01000000);
	MS_OLE_SET_GUINT32 (data +  8, 0x01000100);
	MS_OLE_SET_GUINT16 (data + 12, 0x0);
	MS_OLE_SET_GUINT8  (data + 14, 0x0);
	ms_biff_put_commit (bp);

	write_mergecells (bp, ver, esheet);

/* See: S59D90.HTM: Global Column Widths...  not cricual.
	data = ms_biff_put_len_next (bp, BIFF_GCW, 34);
	{
		int lp;
		for (lp = 0; lp < 34; lp++)
			MS_OLE_SET_GUINT8 (data+lp, 0xff);
		MS_OLE_SET_GUINT32 (data, 0xfffd0020);
	}
	ms_biff_put_commit (bp);
*/
}

/* See: S59D99.HTM */
static void
write_index (MsOleStream *s, ExcelSheet *esheet, MsOlePos pos)
{
	guint8  data[4];
	MsOlePos oldpos;
	guint lp;

	g_return_if_fail (s);
	g_return_if_fail (esheet);

	oldpos = s->position;/* FIXME: tell function ? */
	if (esheet->wb->ver >= MS_BIFF_V8)
		s->lseek (s, pos+4+16, MsOleSeekSet);
	else
		s->lseek (s, pos+4+12, MsOleSeekSet);

	for (lp = 0; lp < esheet->dbcells->len; lp++) {
		MsOlePos pos = g_array_index (esheet->dbcells, MsOlePos, lp);
		MS_OLE_SET_GUINT32 (data, pos - esheet->wb->streamPos);
		d (2, printf ("Writing index record"
			      " 0x%4.4x - 0x%4.4x = 0x%4.4x\n",
			      pos, esheet->wb->streamPos,
			      pos - esheet->wb->streamPos););
		s->write (s, data, 4);
	}

	s->lseek (s, oldpos, MsOleSeekSet);
}

/* See: S59DDB.HTM */
static MsOlePos
write_rowinfo (BiffPut *bp, ExcelSheet *esheet, guint32 row, guint32 last_col)
{
	guint8 *data;
	MsOlePos pos;
	ColRowInfo const *ri = sheet_row_get_info (esheet->gnum_sheet, row);

	/* We don't worry about standard height. I haven't seen it
	 * indicated in any actual esheet. */
	guint16 height = (guint16) (20. * ri->size_pts);
	guint16 options = 0x100; /* undocumented magic */

	/* FIXME: Find default style for row. Does it have to be common to
	 * all cells, or can a cell override? Do all cells have to be
	 * blank. */
	guint16 row_xf     = 0x000f; /* Magic */
	/* FIXME: set bit 12 of row_xf if thick border on top, bit 13 if thick
	 * border on bottom. */

	options |= (MIN (ri->outline_level, 0x7));
	if (ri->is_collapsed)
		options |= 0x10;
	if (!ri->visible)
		options |= 0x20;
	if (ri->hard_size)
		options |= 0x40;

	d (1, printf ("Row %d height 0x%x;\n", row+1, height););

	data = ms_biff_put_len_next (bp, (0x200 | BIFF_ROW), 16);
	pos = bp->streamPos;
	MS_OLE_SET_GUINT16 (data +  0, row);     /* Row number */
	MS_OLE_SET_GUINT16 (data +  2, 0);       /* first def. col */
	MS_OLE_SET_GUINT16 (data +  4, last_col);/* last  def. col */
	MS_OLE_SET_GUINT16 (data +  6, height);	 /* height */
	MS_OLE_SET_GUINT16 (data +  8, 0x00);    /* undocumented */
	MS_OLE_SET_GUINT16 (data + 10, 0x00);    /* reserved */
	MS_OLE_SET_GUINT16 (data + 12, options); /* options */
	MS_OLE_SET_GUINT16 (data + 14, row_xf);  /* default style */
	ms_biff_put_commit (bp);

	return pos;
}

/**
 * write_db_cell
 * @bp        BIFF buffer
 * @esheet
 * @ri_start  start positions of first 2 rowinfo records
 * @rc_start  start positions of first row in each cell in block
 * @nrows  no. of rows in block.
 *
 * Write DBCELL (Stream offsets) record for a block of rows.
 *
 * See: 'Finding records in BIFF files': S59E28.HTM
 *       and 'DBCELL': S59D6D.HTM
 */
static void
write_db_cell (BiffPut *bp, ExcelSheet *esheet,
	       MsOlePos *ri_start, MsOlePos *rc_start, guint32 nrows)
{
	MsOlePos pos;
	guint32 i;
	guint16 offset;

	guint8 *data = ms_biff_put_len_next (bp, BIFF_DBCELL, 4 + nrows * 2);
	pos = bp->streamPos;

	MS_OLE_SET_GUINT32 (data, pos - ri_start [0]);
	for (i = 0 ; i < nrows; i++) {
		offset = rc_start [0]
			- (i > 0 ? rc_start [i - 1] : ri_start [1]);
		MS_OLE_SET_GUINT16 (data + 4 + i * 2, offset);
	}

	ms_biff_put_commit (bp);

	g_array_append_val (esheet->dbcells, pos);
}

/**
 * write_block
 * @bp     BIFF buffer
 * @esheet  sheet
 * @begin  first row no
 * @nrows  no. of rows in block.
 *
 * Write a block of rows. Returns no. of last row written.
 *
 * We do not have to write row records for empty rows which use the
 * default style. But we do not test for this yet.
 *
 * See: 'Finding records in BIFF files': S59E28.HTM *
 */
static guint32
write_block (BiffPut *bp, ExcelSheet *esheet, guint32 begin, int nrows)
{
	int max_col = esheet->max_col;
	int col, row, max_row;
	MsOlePos  ri_start [2]; /* Row info start */
	MsOlePos *rc_start;	/* Row cells start */
	guint16   xf_list [SHEET_MAX_COLS];

	if (nrows > esheet->max_row - (int) begin) /* Incomplete final block? */
		nrows = esheet->max_row - (int) begin;
	max_row = begin + nrows - 1;

	ri_start [0] = write_rowinfo (bp, esheet, begin, max_col);
	ri_start [1] = bp->streamPos;
	for (row = begin + 1; row <= max_row; row++)
		(void) write_rowinfo (bp, esheet, row, max_col);

	rc_start = g_new0 (MsOlePos, nrows);
	for (row = begin; row <= max_row; row++) {
		guint32 run_size = 0;

		/* Save start pos of 1st cell in row */
		rc_start [row - begin] = bp->streamPos;
		for (col = 0; col < max_col; col++) {
			const ExcelCell *cell = excel_cell_get (esheet, col, row);
			if (!cell->gnum_cell) {
				if (cell->xf != esheet->col_xf [col]) {
					xf_list [run_size++] = cell->xf;
				} else if (run_size > 0) {
					write_mulblank (bp, esheet, col - 1, row,
							xf_list, run_size);
					run_size = 0;
				}
			} else {
				if (run_size > 0) {
					write_mulblank (bp, esheet, col - 1, row,
							xf_list, run_size);
					run_size = 0;
				}
				write_cell (bp, esheet, cell);
				workbook_io_progress_update (esheet->wb->io_context, 1);
			}
		}
		if (run_size > 0)
			write_mulblank (bp, esheet, col - 1, row,
					xf_list, run_size);
	}

	write_db_cell (bp, esheet, ri_start, rc_start, nrows);
	g_free (rc_start);

	return row - 1;
}

/* See: 'Finding records in BIFF files': S59E28.HTM */
/* and S59D99.HTM */
static void
write_sheet (IOContext *context, BiffPut *bp, ExcelSheet *esheet)
{
	guint32 block_end;
	gint32 maxrows, y;
	int rows_in_block = ROW_BLOCK_MAX_LEN;
	MsOlePos index_off;
	/* No. of blocks of rows. Only correct as long as all rows -
	   including empties - have row info records */
	guint32 nblocks = (esheet->max_row - 1) / rows_in_block + 1;

	esheet->streamPos = biff_bof_write (bp, esheet->wb->ver, MS_BIFF_TYPE_Worksheet);
	/* We catch too large sheets during write check, but leave this in: */
	maxrows = (esheet->wb->ver >= MS_BIFF_V8)
		? MsBiffMaxRowsV8 : MsBiffMaxRowsV7;
	g_assert (esheet->max_row <= maxrows);

	if (esheet->wb->ver >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 16);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT32 (data +  4, 0);
		MS_OLE_SET_GUINT32 (data +  8, esheet->max_row);
		MS_OLE_SET_GUINT32 (data + 12, 0);
	} else {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 12);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT16 (data + 4, 0);
		MS_OLE_SET_GUINT16 (data + 6, esheet->max_row);
		MS_OLE_SET_GUINT32 (data + 8, 0);
	}
	ms_biff_put_commit (bp);

	write_sheet_head (bp, esheet);

	d (1, printf ("Saving esheet '%s' geom (%d, %d)\n",
		      esheet->gnum_sheet->name_unquoted,
		      esheet->max_col, esheet->max_row););
	for (y = 0; y < esheet->max_row; y = block_end + 1)
		block_end = write_block (bp, esheet, y, rows_in_block);

	write_biff7_comments (bp, esheet);
	write_index (bp->pos, esheet, index_off);
	write_sheet_tail (context, bp, esheet);

	biff_eof_write (bp);
}

static ExcelSheet *
excel_sheet_new (ExcelWorkbook *ewb, Sheet *gnum_sheet, IOContext *context)
{
	ExcelSheet      *esheet = g_new (ExcelSheet, 1);
	Range           extent;
	ExcelCell       **p, **pmax;
	int const maxrows = (ewb->ver >= MS_BIFF_V8)
		? MsBiffMaxRowsV8 : MsBiffMaxRowsV7;

	g_return_val_if_fail (gnum_sheet, NULL);
	g_return_val_if_fail (ewb, NULL);

	/* Ignore spans and merges past the bound */
	extent = sheet_get_extent (gnum_sheet, FALSE);

	if (extent.end.col > maxrows) {
		char *msg = g_strdup_printf (
			_("Too many rows for this format (%d > %d)"),
			  extent.end.col, maxrows);
		gnumeric_io_error_save (context, msg);
		g_free (msg);
		return NULL;
	}

	sheet_style_get_extent (gnum_sheet, &extent);

	esheet->gnum_sheet = gnum_sheet;
	esheet->streamPos  = 0x0deadbee;
	esheet->wb         = ewb;
	esheet->max_col    = 1 + MAX (gnum_sheet->cols.max_used, extent.end.col);
	esheet->max_row    = 1 + MAX (gnum_sheet->rows.max_used, extent.end.row);
	esheet->dbcells    = g_array_new (FALSE, FALSE, sizeof (MsOlePos));
	esheet->base_char_width = 0;

	/* It is ok to have formatting out of range, we can disregard that. */
	if (esheet->max_row > maxrows)
		esheet->max_row = maxrows;

	ms_formula_cache_init (esheet);
	esheet->cell_used_map = cell_used_map_new (esheet);

	esheet->cells = g_new (ExcelCell *, esheet->max_row);
	for (p = esheet->cells, pmax = p + esheet->max_row; p < pmax; p++)
		*p = g_new0 (ExcelCell, esheet->max_col);

	return esheet;
}

static void
excel_sheet_free (ExcelSheet *esheet)
{
	ExcelCell     **p, **pmax;

	if (esheet) {
		g_free (esheet->cell_used_map);
		for (p = esheet->cells, pmax = p + esheet->max_row; p < pmax; p++)
			g_free (*p);
		g_free (esheet->cells);
		g_array_free (esheet->dbcells, TRUE);
		ms_formula_cache_shutdown (esheet);
		g_free (esheet);
	}
}

/**
 * pre_pass
 * @context:  Command context.
 * @wb:   The workbook to scan
 *
 * Scans all the workbook items. Adds all styles, fonts, formats and
 * colors to tables. Resolves any referencing problems before they
 * occur, hence the records can be written in a linear order.
 **/
static int
pre_pass (IOContext *context, ExcelWorkbook *wb)
{
	int ret = 0;

	if (wb->sheets->len == 0)
		return TRUE;

	/* The default style first */
	put_mstyle (wb, wb->xf->default_style);

	/* Its font and format */
	put_font (wb->xf->default_style, NULL, wb);
	put_format (wb->xf->default_style, NULL, wb);

	gather_styles (wb);     /* (and cache cells) */
	/* Gather Info from styles */
	gather_fonts (wb);
	gather_formats (wb);
	gather_palette (wb);

	return ret;
}

/*
 * free_workbook
 * @wb  Workbook
 *
 * Free various bits
 */
static void
free_workbook (ExcelWorkbook *wb)
{
	guint lp;

	fonts_free   (wb);
	formats_free (wb);
	palette_free (wb);
	xf_free  (wb);
	for (lp = 0; lp < wb->sheets->len; lp++) {
		ExcelSheet *s = g_ptr_array_index (wb->sheets, lp);
		excel_sheet_free (s);
	}

	g_free (wb);
}

static void
write_workbook (IOContext *context, BiffPut *bp, ExcelWorkbook *wb, MsBiffVersion ver)
{
	ExcelSheet *s  = 0;
	guint        lp;

	current_workbook_iconv = excel_iconv_open_for_export();
	/* Workbook */
	wb->streamPos = biff_bof_write (bp, ver, MS_BIFF_TYPE_Workbook);

	write_magic_interface (bp, ver);
	write_bits            (bp, wb, ver);

	write_fonts (bp, wb);
	write_formats (bp, wb);
	write_xf (bp, wb);
	write_palette (bp, wb);

	for (lp = 0; lp < wb->sheets->len; lp++) {
		s = g_ptr_array_index (wb->sheets, lp);
	        s->boundsheetPos = biff_boundsheet_write_first
			(bp, MS_BIFF_TYPE_Worksheet,
			 s->gnum_sheet->name_unquoted, wb->ver);

		ms_formula_write_pre_data (bp, s, EXCEL_NAME, wb->ver);
	}

	biff_eof_write (bp);
	/* End of Workbook */

	workbook_io_progress_set (context, wb->gnum_wb, WB_PROGRESS_CELLS,
	                          N_ELEMENTS_BETWEEN_PROGRESS_UPDATES);
	/* Sheets */
	for (lp = 0; lp < wb->sheets->len; lp++)
		write_sheet (context, bp, g_ptr_array_index (wb->sheets, lp));
	/* End of Sheets */
	io_progress_unset (context);

	/* Finalise Workbook stuff */
	for (lp = 0; lp < wb->sheets->len; lp++) {
		ExcelSheet *s = g_ptr_array_index (wb->sheets, lp);
		biff_boundsheet_write_last (bp->pos, s->boundsheetPos,
					    s->streamPos);
	}
	/* End Finalised workbook */
	excel_iconv_close (current_workbook_iconv);
	current_workbook_iconv = NULL;
}

/*
 * ms_excel_check_write
 * @context  Command context
 * @filename File name
 *
 * Check if we are able to save to the file. Return FALSE if we do not
 * have access or we would lose data.
 *
 * TODO: Ask before continuing if there are minor problems, like
 * losing style information.
 */
int
ms_excel_check_write (IOContext *context, void **state, WorkbookView *gwb_view,
		      MsBiffVersion ver)
{
	int res;
	ExcelWorkbook *wb = g_new (ExcelWorkbook, 1);
	GList    *sheets, *ptr;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (ver >= MS_BIFF_V7, -1);

	*state = wb;

	wb->ver          = ver;
	wb->io_context   = context;
	wb->gnum_wb      = wb_view_workbook (gwb_view);
	wb->gnum_wb_view = gwb_view;
	wb->sheets   = g_ptr_array_new ();
	wb->names    = g_ptr_array_new ();
	fonts_init (wb);
	formats_init (wb);
	palette_init (wb);
	xf_init (wb);

	sheets = workbook_sheets (wb->gnum_wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		ExcelSheet *esheet = excel_sheet_new (wb, ptr->data, context);
		if (esheet != NULL)
			g_ptr_array_add (wb->sheets, esheet);
	}
	g_list_free (sheets);

	res = pre_pass (context, wb);
	if (res != 0) {
		free_workbook (wb);
		*state = NULL;
	}

	return res;
}

void
ms_excel_write_workbook (IOContext *context, MsOle *file, void *state,
                         MsBiffVersion ver)
{
	MsOleErr     result;
	const char  *strname;
	MsOleStream *str;
	BiffPut     *bp;
	ExcelWorkbook *wb = state;

	g_return_if_fail (file != NULL);

	if (ver >= MS_BIFF_V8)
		strname = "Workbook";
	else
		strname = "Book";

	result = ms_ole_stream_open (&str, file, "/", strname, 'w');

	if (result != MS_OLE_ERR_OK) {
		free_workbook (wb);
		gnumeric_io_error_save (context,
			_("Can't open stream for writing\n"));
		return;
	}

	bp = ms_biff_put_new (str);

	write_workbook (context, bp, wb, ver);
	free_workbook (wb);

	/* Kludge to make sure the file is a Big Block file */
	while (bp->pos->size < 0x1000) {
		ms_biff_put_len_next   (bp, 0, 0);
		ms_biff_put_commit (bp);
	}

	ms_biff_put_destroy (bp);

	ms_ole_stream_close (&str);

	d (0, fflush (stdout););
}


void
ms_excel_write_free_state (void *state)
{
	if (state) free_workbook (state);
}
