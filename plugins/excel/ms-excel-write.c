/* vim: set sw=8: */
/**
 * ms-excel-write.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (mmeeks@gnu.org)
 *    Jon K Hellan  (hellan@acm.org)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2002 Michael Meeks, Jon K Hellan, Jody Goldberg
 **/

/*
 * FIXME: Check for errors and propagate upward. We've only started.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
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
#include <sheet-view.h>
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
#include <workbook-priv.h>
#include <io-context.h>
#include <command-context.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <gutils.h>
#include <str.h>
#include <mathfunc.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-msole-utils.h>

#include <ctype.h>
#include <math.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_write_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define N_ELEMENTS_BETWEEN_PROGRESS_UPDATES   20

static GIConv current_workbook_iconv = NULL;

static guint style_color_to_rgb888 (StyleColor const *c);
static gint  palette_get_index (ExcelWriteState *ewb, guint c);

/**
 *  This function converts simple strings...
 **/
int
biff_convert_text (char **buf, char const *txt, MsBiffVersion ver)
{
	guint32 i, len;

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
		for (i = 0; i < len; i++)
			outbuf[i] = wcbuf[i];
		g_free(wcbuf);
		len = i * 2;
	} else {
		size_t inbufleft = len, outbufleft = len*8;
		char *outbufptr;
		char const *inbufptr = txt;

		*buf = g_new(char, outbufleft);
		outbufptr = *buf;

		g_iconv (current_workbook_iconv,
			 (char **)&inbufptr, &inbufleft, 
			 &outbufptr, &outbufleft);
		len = outbufptr - *buf;
	}
	return len;
}

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
int
biff_put_text (BiffPut *bp, char const *txt, int len,
	       gboolean write_len, PutType how)
{
	guint8 data[4];
	guint32 ans;
	int i;

	gboolean sixteen_bit_len;
	gboolean unicode;
	guint32  off;

	g_return_val_if_fail (bp, 0);

	if (txt == NULL) {
		g_warning ("writing NULL string as \"\"");
		txt = "";
	}

	ans = 0;
/*	fprintf (stderr, "Write '%s' len = %d\n", txt, len); */

	if ((how == AS_PER_VER && bp->version >= MS_BIFF_V8) ||
	    how == SIXTEEN_BIT)
		sixteen_bit_len = TRUE;
	else
		sixteen_bit_len = FALSE; /* 8 bit */

	if (bp->version >= MS_BIFF_V8)
		unicode = TRUE;
	else
		unicode = FALSE;

	off = 0;
	if (unicode) {
		guint16 *buf = (guint16 *)txt;

		if (write_len) {
			if (sixteen_bit_len) {
				GSF_LE_SET_GUINT16 (data, len/2);
				off = 2;
			} else {
				g_return_val_if_fail (len/2<256, 0);
				GSF_LE_SET_GUINT8  (data, len/2);
				off = 1;
			}
		}
		GSF_LE_SET_GUINT8  (data + off, 0x1);
		off++;
		ms_biff_put_var_write (bp, data, off);

		for (i = 0; i < len/2; i++) {
			GSF_LE_SET_GUINT16 (data, buf[i]);
			ms_biff_put_var_write (bp, data, 2);
		}
	} else {
		if (write_len) {
			if (sixteen_bit_len) {
				GSF_LE_SET_GUINT16 (data, len);
				off = 2;
			} else {
				g_return_val_if_fail (len<256, 0);
				GSF_LE_SET_GUINT8  (data, len);
				off = 1;
			}
			ms_biff_put_var_write (bp, data, off);
		}

		ms_biff_put_var_write (bp, txt, len);
	}
	return off + len;
}

/**
 * See S59D5D.HTM
 **/
static unsigned
biff_bof_write (BiffPut *bp, MsBiffFileType type)
{
	guint   len;
	guint8 *data;
	unsigned ans;

	if (bp->version >= MS_BIFF_V8)
		len = 16;
	else
		len = 8;

	data = ms_biff_put_len_next (bp, 0, len);

	ans = bp->streamPos;

	bp->ls_op = BIFF_BOF;
	switch (bp->version) {
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
		if (bp->version == MS_BIFF_V8 || /* as per the spec. */
		    (bp->version == MS_BIFF_V7 && type == MS_BIFF_TYPE_Worksheet)) /* Wierd hey */
			GSF_LE_SET_GUINT16 (data, 0x0600);
		else
			GSF_LE_SET_GUINT16 (data, 0x0500);
		break;
	default:
		g_warning ("Unknown version.");
		break;
	}

	switch (type)
	{
	case MS_BIFF_TYPE_Workbook:
		GSF_LE_SET_GUINT16 (data+2, 0x0005);
		break;
	case MS_BIFF_TYPE_VBModule:
		GSF_LE_SET_GUINT16 (data+2, 0x0006);
		break;
	case MS_BIFF_TYPE_Worksheet:
		GSF_LE_SET_GUINT16 (data+2, 0x0010);
		break;
	case MS_BIFF_TYPE_Chart:
		GSF_LE_SET_GUINT16 (data+2, 0x0020);
		break;
	case MS_BIFF_TYPE_Macrosheet:
		GSF_LE_SET_GUINT16 (data+2, 0x0040);
		break;
	case MS_BIFF_TYPE_Workspace:
		GSF_LE_SET_GUINT16 (data+2, 0x0100);
		break;
	default:
		g_warning ("Unknown type.");
		break;
	}

	/* Magic version numbers: build date etc. */
	switch (bp->version) {
	case MS_BIFF_V8:
		GSF_LE_SET_GUINT16 (data+4, 0x0dbb);
		GSF_LE_SET_GUINT16 (data+6, 0x07cc);
		/* Quandry: can we tell the truth about our history */
		GSF_LE_SET_GUINT32 (data+ 8, 0x00000004);
		GSF_LE_SET_GUINT16 (data+12, 0x06000908); /* ? */
		break;
	case MS_BIFF_V7:
	case MS_BIFF_V5:
		GSF_LE_SET_GUINT16 (data+4, 0x096c);
		GSF_LE_SET_GUINT16 (data+6, 0x07c9);
		break;
	default:
		fprintf (stderr, "FIXME: I need some magic numbers\n");
		GSF_LE_SET_GUINT16 (data+4, 0x0);
		GSF_LE_SET_GUINT16 (data+6, 0x0);
		break;
	}
	ms_biff_put_commit (bp);

	return ans;
}

static void
ms_excel_write_EOF (BiffPut *bp)
{
	ms_biff_put_len_next (bp, BIFF_EOF, 0);
	ms_biff_put_commit (bp);
}

/* See: S59DE3.HTM */
static void
excel_write_SETUP (BiffPut *bp, ExcelSheet *esheet)
{
	PrintInformation *pi = esheet->gnum_sheet->print_info;
	double header, footer, dummy;
	guint8 * data = ms_biff_put_len_next (bp, BIFF_SETUP, 34);
	guint16 options = 0;

	if (pi->print_order == PRINT_ORDER_RIGHT_THEN_DOWN)
		options |= 0x01;
	if (pi->orientation == PRINT_ORIENT_VERTICAL)
		options |= 0x02;
	options |= 0x40; /* orientation is set */
	options |= 0x04;  /* mark the _invalid_ things as being invalid */
	if (pi->print_black_and_white)
		options |= 0x08;
	if (pi->print_as_draft)
		options |= 0x10;
	if (pi->print_comments)
		options |= 0x20;

	if (!print_info_get_margins (pi, &header, &footer, &dummy, &dummy))
		header = footer = 0.;
	header = unit_convert (header, UNIT_POINTS, UNIT_INCH);
	footer = unit_convert (footer, UNIT_POINTS, UNIT_INCH);

	GSF_LE_SET_GUINT16 (data +  0, 0);	/* _invalid_ paper size */
	GSF_LE_SET_GUINT16 (data +  2, 100);	/* scaling factor */
	GSF_LE_SET_GUINT16 (data +  4, 0);	/* start at page 0 */
	GSF_LE_SET_GUINT16 (data +  6, 1);	/* fit 1 page wide */
	GSF_LE_SET_GUINT16 (data +  8, 1);	/* fit 1 page high */
	GSF_LE_SET_GUINT32 (data + 10, options);
	GSF_LE_SET_GUINT32 (data + 12, 0);	/* _invalid_ x resolution */
	GSF_LE_SET_GUINT32 (data + 14, 0);	/* _invalid_ y resolution */
	gsf_le_set_double  (data + 16, header);
	gsf_le_set_double  (data + 24, footer);
	GSF_LE_SET_GUINT16 (data + 32, 1);	/* 1 copy */
	ms_biff_put_commit (bp);
}

static void
excel_write_EXTERNNAME_funcs (ExcelWriteState *ewb)
{
	/* 2 byte expression #REF! */
	static guint8 const expr_ref [] =
		{ 0x02, 0, 0x1c, 0x17 };
	static guint8 const zeros [] =
		{ 0, 0, 0, 0, 0 ,0 };
	unsigned i;
	gint    len;
	char   *buf;
	GnmFunc *func;

	for (i = 0; i < ewb->externnames->len ; i++) {
		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNNAME);
		ms_biff_put_var_write (ewb->bp, zeros, 6);

		/* write the name and the 1 byte length */
		func = g_ptr_array_index (ewb->externnames, i);
		len = biff_convert_text (&buf, func->name, MS_BIFF_V7);
		biff_put_text (ewb->bp, buf, len, TRUE, EIGHT_BIT);
		g_free (buf);

		ms_biff_put_var_write (ewb->bp, expr_ref, sizeof (expr_ref));
		ms_biff_put_commit (ewb->bp);
	}
}

static void
excel_write_externsheets_v7 (BiffPut *bp, ExcelWriteState *ewb, ExcelSheet *container)
{
	int i, num_sheets = ewb->sheets->len;
	guint8 *data;

	data = ms_biff_put_len_next (bp, BIFF_EXTERNCOUNT, 2);
	GSF_LE_SET_GUINT16 (data, num_sheets + 1);
	ms_biff_put_commit (bp);

	for (i = 0; i < num_sheets; i++) {
		ExcelSheet const *esheet = g_ptr_array_index (ewb->sheets, i);

		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		if (esheet == container) {
			static guint8 const magic_selfref[] = { 0x01, 0x04 };
			ms_biff_put_var_write (bp, magic_selfref, sizeof magic_selfref);
		} else {
			char *buf;
			gint len = biff_convert_text (&buf, esheet->gnum_sheet->name_unquoted, bp->version);
			guint8 data[2];

			GSF_LE_SET_GUINT8 (data, len);
			GSF_LE_SET_GUINT8 (data + 1, 3); /* undocumented */
			ms_biff_put_var_write (bp, data, 2);
			biff_put_text (bp, buf, len, FALSE, AS_PER_VER);
			g_free (buf);
		}
		ms_biff_put_commit (bp);
	}

	/* Add a magic addin externsheet */
	{
		static guint8 const magic_addin[] = { 0x01, 0x3a };
		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		ms_biff_put_var_write (bp, magic_addin, sizeof magic_addin);
		ms_biff_put_commit (bp);

		excel_write_EXTERNNAME_funcs (ewb);
	}
}

/*
 * See: S59E17.HTM
 */
static void
excel_write_WINDOW1 (BiffPut *bp, WorkbookView *wb_view)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_WINDOW1, 18);
	float hdpi = application_display_dpi_get (TRUE) / (72. * 20.);
	float vdpi = application_display_dpi_get (FALSE) / (72. * 20.);
	guint16 width = .5 + wb_view->preferred_width / hdpi;
	guint16 height = .5 + wb_view->preferred_height / vdpi;
	guint16 options = 0;
	guint16 active_page = 0;
	Sheet *sheet;

	if (wb_view->show_horizontal_scrollbar)
		options |= 0x0008;
	if (wb_view->show_vertical_scrollbar)
		options |= 0x0010;
	if (wb_view->show_notebook_tabs)
		options |= 0x0020;

	sheet = wb_view_cur_sheet (wb_view);
	if (sheet != NULL)
		active_page = sheet->index_in_wb;

	GSF_LE_SET_GUINT16 (data+  0, 0x0000);
	GSF_LE_SET_GUINT16 (data+  2, 0x0000);
	GSF_LE_SET_GUINT16 (data+  4, width);
	GSF_LE_SET_GUINT16 (data+  6, height);
	GSF_LE_SET_GUINT16 (data+  8, options); /* various flags */
	GSF_LE_SET_GUINT16 (data+ 10, active_page); /* selected tab */
	/* We don't know the scroll state of the notebook tabs at this level */
	GSF_LE_SET_GUINT16 (data+ 12, 0x0000);
	GSF_LE_SET_GUINT16 (data+ 14, 0x0001);
	GSF_LE_SET_GUINT16 (data+ 16, 0x0258);
	ms_biff_put_commit (bp);
}

/**
 * excel_write_WINDOW2 :
 * returns TRUE if a PANE record is necessary.
 *
 * See: S59E18.HTM
 **/
static gboolean
excel_write_WINDOW2 (BiffPut *bp, ExcelSheet *esheet)
{
	/* 1	0x020 grids are the colour of the normal style */
	/* 0	0x040 arabic */
	/* 1	0x080 display outlines if they exist */
	/* 0	0x800 (biff8 only) no page break mode*/
	guint16 options = 0x0A0;
	guint8 *data;
	CellPos top_left;
	Sheet const *sheet = esheet->gnum_sheet;
	SheetView const *sv = sheet_get_view (sheet, esheet->ewb->gnum_wb_view);
	StyleColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	StyleColor *default_auto = style_color_auto_pattern ();
	guint32 biff_pat_col = 0x40;	/* default grid color index == auto */

	if (sheet->display_formulas)
		options |= 0x0001;
	if (!sheet->hide_grid)
		options |= 0x0002;
	if (!sheet->hide_col_header || !sheet->hide_row_header)
		options |= 0x0004;
	if (sv_is_frozen (sv)) {
		options |= 0x0008;
		top_left = sv->frozen_top_left;
	} else
		top_left = sv->initial_top_left;
	if (!sheet->hide_zero)
		options |= 0x0010;
	/* Grid / auto pattern color */
	if (!style_color_equal (sheet_auto, default_auto)) {
		biff_pat_col = style_color_to_rgb888 (sheet_auto);
		if (bp->version > MS_BIFF_V7)
			biff_pat_col = palette_get_index (esheet->ewb,
							  biff_pat_col);
		options &= ~0x0020;
	}
	if (sheet == wb_view_cur_sheet (esheet->ewb->gnum_wb_view))
		options |= 0x600; /* Excel ignores this and uses WINDOW1 */

	if (bp->version <= MS_BIFF_V7) {
		data = ms_biff_put_len_next (bp, 0x200|BIFF_WINDOW2, 10);

		GSF_LE_SET_GUINT16 (data +  0, options);
		GSF_LE_SET_GUINT16 (data +  2, top_left.row);
		GSF_LE_SET_GUINT16 (data +  4, top_left.col);
		GSF_LE_SET_GUINT32 (data +  6, biff_pat_col);
	} else {
		data = ms_biff_put_len_next (bp, 0x200|BIFF_WINDOW2, 18);

		GSF_LE_SET_GUINT16 (data +  0, options);
		GSF_LE_SET_GUINT16 (data +  2, top_left.row);
		GSF_LE_SET_GUINT16 (data +  4, top_left.col);
		GSF_LE_SET_GUINT32 (data +  6, biff_pat_col);
		GSF_LE_SET_GUINT16 (data + 10, 0x1);	/* print preview 100% */
		GSF_LE_SET_GUINT16 (data + 12, 0x0);	/* FIXME : why 0? */
		GSF_LE_SET_GUINT32 (data + 14, 0x0);	/* reserved 0 */
	}
	ms_biff_put_commit (bp);

	style_color_unref (sheet_auto);
	style_color_unref (default_auto);
	return (options & 0x0008);
}

/* See: S59DCA.HTM */
static void
excel_write_PANE (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_PANE, 10);
	SheetView const *sv = sheet_get_view (esheet->gnum_sheet,
		esheet->ewb->gnum_wb_view);
	int const frozen_height = sv->unfrozen_top_left.row -
		sv->frozen_top_left.row;
	int const frozen_width = sv->unfrozen_top_left.col -
		sv->frozen_top_left.col;

	GSF_LE_SET_GUINT16 (data + 0, frozen_width);
	GSF_LE_SET_GUINT16 (data + 2, frozen_height);
	GSF_LE_SET_GUINT16 (data + 4, sv->initial_top_left.row);
	GSF_LE_SET_GUINT16 (data + 6, sv->initial_top_left.col);
	GSF_LE_SET_GUINT16 (data + 8, 0);	/* active pane */

	ms_biff_put_commit (bp);
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
excel_write_MERGECELLS (BiffPut *bp, ExcelSheet *esheet)
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
	GSF_LE_SET_GUINT16 (record, len);

	ptr = record + 2;
	for (merged = esheet->gnum_sheet->list_merged; merged != NULL ; merged = merged->next) {
		Range const *r = merged->data;
		if (r->start.row <= USHRT_MAX && r->end.row <= USHRT_MAX &&
		    r->start.col <= UCHAR_MAX && r->end.col <= UCHAR_MAX) {
			GSF_LE_SET_GUINT16 (ptr+0, r->start.row);
			GSF_LE_SET_GUINT16 (ptr+2, r->end.row);
			GSF_LE_SET_GUINT16 (ptr+4, r->start.col);
			GSF_LE_SET_GUINT16 (ptr+6, r->end.col);
			ptr += 8;
		}
	}

	ms_biff_put_commit (bp);
}

static void
excel_write_NAME_v7 (gpointer key, GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	guint8 data0 [20];
	guint8 data1 [2];
	guint16 len, name_len;
	char const *name;

	g_return_if_fail (nexpr != NULL);

	ms_biff_put_var_next (ewb->bp, BIFF_NAME);

	name = nexpr->name->str;
	name_len = strlen (name);

	memset (data0, 0, sizeof (data0));
	GSF_LE_SET_GUINT8 (data0 + 3, name_len); /* name_len */
	ms_biff_put_var_write (ewb->bp, data0, 14);

	biff_put_text (ewb->bp, name, name_len, FALSE, AS_PER_VER);
	ms_biff_put_var_seekto (ewb->bp, 14 + name_len);
	len = excel_write_formula (ewb, nexpr->expr_tree,
				   nexpr->pos.sheet, 0, 0, 0);

	g_return_if_fail (len <= 0xffff);

	ms_biff_put_var_seekto (ewb->bp, 4);
	GSF_LE_SET_GUINT16 (data1, len);
	ms_biff_put_var_write (ewb->bp, data1, 2);
	ms_biff_put_commit (ewb->bp);

	g_ptr_array_add (ewb->names, (gpointer)nexpr);
}

int
excel_write_get_externsheet_idx (ExcelWriteState *ewb,
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
			     char *name)
{
	guint32 pos;
	gint len;
	char *buf;
	guint8 data[16];

	ms_biff_put_var_next (bp, BIFF_BOUNDSHEET);
	pos = bp->streamPos;

	GSF_LE_SET_GUINT32 (data, 0xdeadbeef); /* To be stream start pos */
	switch (type) {
	case MS_BIFF_TYPE_Worksheet :	GSF_LE_SET_GUINT8 (data+4, 0); break;
	case MS_BIFF_TYPE_Macrosheet :	GSF_LE_SET_GUINT8 (data+4, 1); break;
	case MS_BIFF_TYPE_Chart :	GSF_LE_SET_GUINT8 (data+4, 2); break;
	case MS_BIFF_TYPE_VBModule :	GSF_LE_SET_GUINT8 (data+4, 6); break;
	default:
		g_warning ("Duff type.");
		break;
	}
	GSF_LE_SET_GUINT8 (data+5, 0); /* Visible */
	ms_biff_put_var_write (bp, data, 6);
	len = biff_convert_text (&buf, name, bp->version);
	biff_put_text (bp, buf, len, TRUE, EIGHT_BIT);
	g_free(buf);

	ms_biff_put_commit (bp);
	return pos;
}

/**
 *  Update a previously written record with the correct
 * stream position.
 **/
static void
biff_boundsheet_write_last (GsfOutput *output, guint32 pos,
			    unsigned streamPos)
{
	guint8  data[4];
	gsf_off_t oldpos;
	g_return_if_fail (output);

	oldpos = gsf_output_tell (output);
	gsf_output_seek (output, pos+4, G_SEEK_SET);
	GSF_LE_SET_GUINT32 (data, streamPos);
	gsf_output_write (output, 4, data);
	gsf_output_seek (output, oldpos, G_SEEK_SET);
}

/***************************************************************************/

/**
 * Convert ExcelPaletteEntry to guint representation used in BIFF file
 **/
inline static guint
palette_color_to_int (ExcelPaletteEntry const *c)
{
	return (c->b << 16) + (c->g << 8) + (c->r << 0);

}

/**
 * Convert StyleColor to guint representation used in BIFF file
 **/
static guint
style_color_to_rgb888 (StyleColor const *c)
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
log_put_color (guint c, gboolean was_added, gint index, char const *tmpl)
{
	d(2, if (was_added) fprintf (stderr, tmpl, index, c););
}

static void
palette_init (ExcelWriteState *ewb)
{
	int i;
	ExcelPaletteEntry const *epe;
	guint num;

	ewb->pal.two_way_table =
		two_way_table_new (g_direct_hash, g_direct_equal,
				   0, NULL);
	/* Ensure that black and white can't be swapped out */

	for (i = 0; i < EXCEL_DEF_PAL_LEN; i++) {
		epe = &excel_default_palette[i];
		num = palette_color_to_int (epe);
		two_way_table_put (ewb->pal.two_way_table,
				   GUINT_TO_POINTER (num), FALSE,
				   (AfterPutFunc) log_put_color,
				   "Default color %d - 0x%6.6x\n");
		if ((i == PALETTE_BLACK) || (i == PALETTE_WHITE))
			ewb->pal.entry_in_use[i] = TRUE;
		else
			ewb->pal.entry_in_use[i] = FALSE;
	}
}

static void
palette_free (ExcelWriteState *ewb)
{
	if (ewb->pal.two_way_table != NULL) {
		two_way_table_free (ewb->pal.two_way_table);
		ewb->pal.two_way_table = NULL;
	}
}

/**
 * palette_get_index
 * @ewb workbook
 * @c  color
 *
 * Get index of color
 * The color index to use is *not* simply the index into the palette.
 * See comment to ms_excel_palette_get in ms-excel-read.c
 **/
static gint
palette_get_index (ExcelWriteState *ewb, guint c)
{
	gint idx;

	if (c == 0)
		return PALETTE_BLACK;
	if (c == 0xffffff)
		return PALETTE_WHITE;

	idx = two_way_table_key_to_idx (ewb->pal.two_way_table, (gconstpointer) c);
	if (idx >= EXCEL_DEF_PAL_LEN)
		return PALETTE_BLACK;
	return idx + 8;
}

/**
 * Add a color to palette if it is not already there
 **/
static void
put_color (ExcelWriteState *ewb, StyleColor const *c)
{
	TwoWayTable *twt = ewb->pal.two_way_table;
	gpointer pc = GUINT_TO_POINTER (style_color_to_rgb888 (c));
	gint idx;

	two_way_table_put (twt, pc, TRUE,
			   (AfterPutFunc) log_put_color,
			   "Found unique color %d - 0x%6.6x\n");

	idx = two_way_table_key_to_idx (twt, pc);
	if (idx >= 0 && idx < EXCEL_DEF_PAL_LEN)
		ewb->pal.entry_in_use [idx] = TRUE; /* Default entry in use */
}

/**
 * Add colors in mstyle to palette
 **/
static void
put_colors (MStyle *st, gconstpointer dummy, ExcelWriteState *ewb)
{
	int i;
	StyleBorder const *b;

	put_color (ewb, mstyle_get_color (st, MSTYLE_COLOR_FORE));
	put_color (ewb, mstyle_get_color (st, MSTYLE_COLOR_BACK));
	put_color (ewb, mstyle_get_color (st, MSTYLE_COLOR_PATTERN));

	/* Borders */
	for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
		b = mstyle_get_border (st, MSTYLE_BORDER_TOP + i);
		if (b && b->color)
			put_color (ewb, b->color);
	}
}

/**
 * gather_palette
 * @ewb   workbook
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
gather_palette (ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->xf.two_way_table;
	int i, j;
	int upper_limit = EXCEL_DEF_PAL_LEN;
	guint color;

	/* For each color in each style, get color index from hash. If
           none, it's not there yet, and we enter it. */
	g_hash_table_foreach (twt->unique_keys, (GHFunc) put_colors, ewb);

	twt = ewb->pal.two_way_table;
	for (i = twt->idx_to_key->len - 1; i >= EXCEL_DEF_PAL_LEN; i--) {
		color = GPOINTER_TO_UINT (two_way_table_idx_to_key (twt, i));
		for (j = upper_limit - 1; j > 1; j--) {
			if (!ewb->pal.entry_in_use[j]) {
				/* Replace table entry with color. */
				d (2, fprintf (stderr, "Custom color %d (0x%6.6x)"
						" moved to unused index %d\n",
					      i, color, j););
				(void) two_way_table_replace
					(twt, j, GUINT_TO_POINTER (color));
				upper_limit = j;
				ewb->pal.entry_in_use[j] = TRUE;
				break;
			}
		}
	}
}

/**
 * write_palette
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write palette to file
 **/
static void
write_palette (BiffPut *bp, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->pal.two_way_table;
	guint8  data[8];
	guint   num, i;

	ms_biff_put_var_next (bp, BIFF_PALETTE);

	GSF_LE_SET_GUINT16 (data, EXCEL_DEF_PAL_LEN); /* Entries */

	ms_biff_put_var_write (bp, data, 2);
	for (i = 0; i < EXCEL_DEF_PAL_LEN; i++) {
		num = GPOINTER_TO_UINT (two_way_table_idx_to_key (twt, i));
		GSF_LE_SET_GUINT32 (data, num);
		ms_biff_put_var_write (bp, data, 4);
	}

	ms_biff_put_commit (bp);
}

/***************************************************************************/

#ifndef NO_DEBUG_EXCEL
/**
 * Return string description of font to print in debug log
 **/
static char *
excel_font_to_string (ExcelFont const *f)
{
	StyleFont const *sf = f->style_font;
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
		ExcelFont const *fa  = (ExcelFont const *) a;
		ExcelFont const *fb  = (ExcelFont const *) b;
		res = style_font_equal (fa->style_font, fb->style_font)
			&& (fa->color == fb->color)
			&& (fa->is_auto == fb->is_auto)
			&& (fa->underline == fb->underline)
			&& (fa->strikethrough == fb->strikethrough);
	}

	return res;
}

static ExcelFont *
fonts_get_font (ExcelWriteState *ewb, gint idx)
{

	return two_way_table_idx_to_key (ewb->fonts.two_way_table, idx);
}

static void
fonts_init (ExcelWriteState *ewb)
{
	ewb->fonts.two_way_table = two_way_table_new (
		excel_font_hash, excel_font_equal, 0,
		(GDestroyNotify) excel_font_free);
}

static void
fonts_free (ExcelWriteState *ewb)
{
	if (ewb->fonts.two_way_table != NULL) {
		two_way_table_free (ewb->fonts.two_way_table);
		ewb->fonts.two_way_table = NULL;
	}
}

/**
 * Get index of an ExcelFont
 **/
static gint
fonts_get_index (ExcelWriteState *ewb, ExcelFont const *f)
{
	return two_way_table_key_to_idx (ewb->fonts.two_way_table, f);
}

/**
 * Callback called when putting font to table. Print to debug log when
 * font is added. Free resources when it was already there.
 **/
static void
after_put_font (ExcelFont *f, gboolean was_added, gint index, gconstpointer dummy)
{
	if (was_added) {
		d (1, fprintf (stderr, "Found unique font %d - %s\n",
			      index, excel_font_to_string (f)););
	} else {
		excel_font_free (f);
	}
}

/**
 * Add a font to table if it is not already there.
 **/
static void
put_font (MStyle *st, gconstpointer dummy, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->fonts.two_way_table;
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
gather_fonts (ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->xf.two_way_table;

	/* For each style, get fonts index from hash. If none, it's
           not there yet, and we enter it. */
	g_hash_table_foreach (twt->unique_keys, (GHFunc) put_font, ewb);
}

/**
 * write_font
 * @bp BIFF buffer
 * @ewb workbook
 * @f  font
 *
 * Write a font to file
 * See S59D8C.HTM
 *
 * FIXME:
 * It would be useful to map well known fonts to Windows equivalents
 **/
static void
write_font (BiffPut *bp, ExcelWriteState *ewb, ExcelFont const *f)
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
		: palette_get_index (ewb, f->color);
	d (1, fprintf (stderr, "Writing font %s, color idx %u\n",
		      excel_font_to_string (f), color););

	if (sf->is_italic)
		grbit |= 1 << 1;
	if (f->strikethrough)
		grbit |= 1 << 3;
	if (sf->is_bold)
		boldstyle = 0x2bc;

	ms_biff_put_var_next (bp, BIFF_FONT);
	GSF_LE_SET_GUINT16 (data + 0, size_pts);
	GSF_LE_SET_GUINT16 (data + 2, grbit);
	GSF_LE_SET_GUINT16 (data + 4, color);
	GSF_LE_SET_GUINT16 (data + 6, boldstyle);
	GSF_LE_SET_GUINT16 (data + 8, subsuper);
	GSF_LE_SET_GUINT8  (data + 10, underline);
	GSF_LE_SET_GUINT8  (data + 11, family);
	GSF_LE_SET_GUINT8  (data + 12, charset);
	GSF_LE_SET_GUINT8  (data + 13, 0);
	ms_biff_put_var_write (bp, data, 14);
	len = biff_convert_text (&buf, font_name, bp->version);
	biff_put_text (bp, buf, len, TRUE, EIGHT_BIT);
	g_free(buf);

	ms_biff_put_commit (bp);
}

/**
 * write_fonts
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write all fonts to file
 **/
static void
write_fonts (BiffPut *bp, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->fonts.two_way_table;
	int nfonts = twt->idx_to_key->len;
	int i;
	ExcelFont *f;

	for (i = 0; i < nfonts; i++) {
		if (i != FONT_SKIP) {	/* FONT_SKIP is invalid, skip it */
			f = fonts_get_font (ewb, i);
			write_font (bp, ewb, f);
		}
	}

	if (nfonts < FONTS_MINIMUM + 1) { /* Add 1 to account for skip */
		/* Fill up until we've got the minimum number */
		f = fonts_get_font (ewb, 0);
		for (; i < FONTS_MINIMUM + 1; i++) {
			if (i != FONT_SKIP) {
				/* FONT_SKIP is invalid, skip it */
				write_font (bp, ewb, f);
			}
		}
	}
}

/***************************************************************************/

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
		  char const *tmpl)
{
	if (was_added) {
		d (2, fprintf (stderr, tmpl, index, format););
	} else {
		style_format_unref (format);
	}
}

static void
formats_init (ExcelWriteState *ewb)
{
	int i;
	char const *fmt;

	ewb->formats.two_way_table
		= two_way_table_new (g_direct_hash, g_direct_equal, 0,
				     (GDestroyNotify)style_format_unref);

	/* Add built-in formats to format table */
	for (i = 0; i < EXCEL_BUILTIN_FORMAT_LEN; i++) {
		fmt = excel_builtin_formats[i];
		if (!fmt || strlen (fmt) == 0)
			fmt = "General";
		two_way_table_put (ewb->formats.two_way_table,
				   style_format_new_XL (fmt, FALSE),
				   FALSE, /* Not unique */
				   (AfterPutFunc) after_put_format,
				   "Magic format %d - 0x%x\n");
	}
}

static void
formats_free (ExcelWriteState *ewb)
{
	if (ewb->formats.two_way_table != NULL) {
		two_way_table_free (ewb->formats.two_way_table);
		ewb->formats.two_way_table = NULL;
	}
}

static StyleFormat const *
formats_get_format (ExcelWriteState *ewb, gint idx)
{
	return two_way_table_idx_to_key (ewb->formats.two_way_table, idx);
}

/**
 * Get index of a format
 **/
static gint
formats_get_index (ExcelWriteState *ewb, StyleFormat const *format)
{
	return two_way_table_key_to_idx (ewb->formats.two_way_table, format);
}

/**
 * Add a format to table if it is not already there.
 *
 * Style format is *not* unrefed. This is correct
 **/
static void
put_format (MStyle *mstyle, gconstpointer dummy, ExcelWriteState *ewb)
{
	StyleFormat *fmt = mstyle_get_format (mstyle);
	style_format_ref (fmt);
	two_way_table_put (ewb->formats.two_way_table,
			   (gpointer)fmt, TRUE,
			   (AfterPutFunc) after_put_format,
			   "Found unique format %d - 0x%x\n");
}


/**
 * Add all formats in workbook to table
 **/
static void
gather_formats (ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->xf.two_way_table;
	/* For each style, get fonts index from hash. If none, it's
           not there yet, and we enter it. */
	g_hash_table_foreach (twt->unique_keys, (GHFunc) put_format, ewb);
}


/**
 * write_format
 * @bp   BIFF buffer
 * @ewb   workbook
 * @fidx format index
 *
 * Write a format to file
 * See S59D8E.HTM
 **/
static void
write_format (BiffPut *bp, ExcelWriteState *ewb, int fidx)
{
	gint len;
	char *buf;
	guint8 data[64];
	StyleFormat const *sf = formats_get_format(ewb, fidx);

	char *format = style_format_as_XL (sf, FALSE);

	d (1, fprintf (stderr, "Writing format 0x%x: %s\n", fidx, format););

	/* Kludge for now ... */
	if (bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, (0x400|BIFF_FORMAT));
	else
		ms_biff_put_var_next (bp, BIFF_FORMAT);

	GSF_LE_SET_GUINT16 (data, fidx);
	ms_biff_put_var_write (bp, data, 2);

	len = biff_convert_text(&buf, format, MS_BIFF_V7);
	biff_put_text (bp, buf, len, TRUE, AS_PER_VER);
	ms_biff_put_commit (bp);
	g_free (buf);
	g_free (format);
}

/**
 * write_formats
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write all formats to file.
 * Although we do, the formats apparently don't have to be written out in order
 **/
static void
write_formats (BiffPut *bp, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->formats.two_way_table;
	guint nformats = twt->idx_to_key->len;
	int magic_num [] = { 5, 6, 7, 8, 0x2a, 0x29, 0x2c, 0x2b };
	guint i;

	/* The built-in fonts which get localized */
	for (i = 0; i < sizeof magic_num / sizeof magic_num[0]; i++)
		write_format (bp, ewb, magic_num [i]);

	/* The custom fonts */
	for (i = EXCEL_BUILTIN_FORMAT_LEN; i < nformats; i++)
		write_format (bp, ewb, i);
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
xf_init (ExcelWriteState *ewb)
{
	/* Excel starts at XF_RESERVED for user defined xf */
	ewb->xf.two_way_table = two_way_table_new (mstyle_hash,
						   (GCompareFunc) mstyle_equal_XL,
						   XF_RESERVED,
						   NULL);
	ewb->xf.default_style = get_default_mstyle ();
}

static void
xf_free (ExcelWriteState *ewb)
{
	if (ewb->xf.two_way_table != NULL) {
		two_way_table_free (ewb->xf.two_way_table);
		ewb->xf.two_way_table = NULL;
		mstyle_unref (ewb->xf.default_style);
		ewb->xf.default_style = NULL;
	}
}


/**
 * Get an mstyle, given index
 **/
static MStyle *
xf_get_mstyle (ExcelWriteState *ewb, gint idx)
{
	return two_way_table_idx_to_key (ewb->xf.two_way_table, idx);
}

static void
after_put_mstyle (MStyle *st, gboolean was_added, gint index, gconstpointer dummy)
{
	if (was_added) {
		d (1, { fprintf (stderr, "Found unique mstyle %d\n", index); mstyle_dump (st);});
	}
}

/**
 * Add an MStyle to table if it is not already there.
 **/
static gint
put_mstyle (ExcelWriteState *ewb, MStyle *st)
{
	return two_way_table_put (ewb->xf.two_way_table, st, TRUE,
				  (AfterPutFunc) after_put_mstyle, NULL);
}

static void
cb_accum_styles (MStyle *st, gconstpointer dummy, ExcelWriteState *ewb)
{
	put_mstyle (ewb, st);
}

static void
gather_styles (ExcelWriteState *ewb)
{
	unsigned i;
	int	 col;
	ExcelSheet *esheet;

	for (i = 0; i < ewb->sheets->len; i++) {
		esheet = g_ptr_array_index (ewb->sheets, i);
		sheet_style_foreach (esheet->gnum_sheet, (GHFunc)cb_accum_styles, ewb);
		for (col = 0; col < esheet->max_col; col++)
			esheet->col_xf [col] = two_way_table_key_to_idx (ewb->xf.two_way_table,
				sheet_style_most_common_in_col (esheet->gnum_sheet, col));
	}
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
map_pattern_index_to_excel (int i)
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
 * @ewb    workbook
 * @auto_back     Auto colors to compare against.
 * @auto_font
 *
 * Return Excel color index, possibly auto, for a style color.
 * The auto colors are passed in by caller to avoid having to ref and unref
 * the same autocolors over and over.
 */
static guint8
style_color_to_pal_index (StyleColor *color, ExcelWriteState *ewb,
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
		idx = palette_get_index	(ewb, style_color_to_rgb888 (color));

	return idx;
}

/**
 * get_xf_differences
 * @ewb   workbook
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
get_xf_differences (ExcelWriteState *ewb, BiffXFData *xfd, MStyle *parentst)
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
log_xf_data (ExcelWriteState *ewb, BiffXFData *xfd, int idx)
{
	if (ms_excel_write_debug > 1) {
		int i;
		ExcelFont *f = fonts_get_font (ewb, xfd->font_idx);

		/* Formats are saved using the 'C' locale number format */
		char * desc = style_format_as_XL (xfd->style_format, FALSE);

		fprintf (stderr, "Writing xf 0x%x : font 0x%x (%s), format 0x%x (%s)\n",
			idx, xfd->font_idx, excel_font_to_string (f),
			xfd->format_idx, desc);
		g_free (desc);

		fprintf (stderr, " hor align 0x%x, ver align 0x%x, wrap_text %s\n",
			xfd->halign, xfd->valign, xfd->wrap_text ? "on" : "off");
		fprintf (stderr, " fill fg color idx 0x%x, fill bg color idx 0x%x"
			", pattern (Excel) %d\n",
			xfd->pat_foregnd_col, xfd->pat_backgnd_col,
			xfd->fill_pattern_idx);
		for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
			if (xfd->border_type[i] !=  STYLE_BORDER_NONE) {
				fprintf (stderr, " border_type[%d] : 0x%x"
					" border_color[%d] : 0x%x\n",
					i, xfd->border_type[i],
					i, xfd->border_color[i]);
			}
		}
		fprintf (stderr, " difference bits: 0x%x\n", xfd->differences);
	}
}
#endif

/**
 * build_xf_data
 * @ewb   workbook
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
build_xf_data (ExcelWriteState *ewb, BiffXFData *xfd, MStyle *st)
{
	ExcelFont *f;
	StyleBorder const *b;
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
	xfd->font_idx     = fonts_get_index (ewb, f);
	excel_font_free (f);
	xfd->style_format = mstyle_get_format (st);
	xfd->format_idx   = formats_get_index (ewb, xfd->style_format);

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
				? style_color_to_pal_index (b->color, ewb,
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
		? style_color_to_pal_index (pattern_color, ewb, auto_back,
					    auto_font)
		: PALETTE_AUTO_PATTERN;
	xfd->pat_backgnd_col
		= back_color
		? style_color_to_pal_index (back_color, ewb, auto_back,
					    auto_font)
		: PALETTE_AUTO_BACK;

	/* Solid patterns seem to reverse the meaning */
 	if (xfd->fill_pattern_idx == FILL_SOLID) {
		guint8 c = xfd->pat_backgnd_col;
		xfd->pat_backgnd_col = xfd->pat_foregnd_col;
		xfd->pat_foregnd_col = c;
	}

	get_xf_differences (ewb, xfd, ewb->xf.default_style);

	style_color_unref (auto_font);
	style_color_unref (auto_back);
}

/**
 * write_xf_magic_record
 * @bp  BIFF buffer
 * @idx Index of record
 *
 * Write a built-in XF record to file
 * See S59E1E.HTM
 **/
static void
write_xf_magic_record (BiffPut *bp, int idx)
{
	guint8 data[256];

	memset (data, 0, sizeof (data));

	if (bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16(data+0, FONT_MAGIC);
		GSF_LE_SET_GUINT16(data+2, FORMAT_MAGIC);
		GSF_LE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		GSF_LE_SET_GUINT16(data+0, FONT_MAGIC);
		GSF_LE_SET_GUINT16(data+2, FORMAT_MAGIC);
		GSF_LE_SET_GUINT16(data+4, 0xfff5); /* FIXME: Magic */
		GSF_LE_SET_GUINT16(data+6, 0xf420);
		/* The "magic" 0x20c0 means:
		 * Fill patt foreground 64 = Autocontrast
		 * Fill patt background  1 = white */
		GSF_LE_SET_GUINT16(data+8, 0x20c0);

		if (idx == 1 || idx == 2)
			GSF_LE_SET_GUINT16 (data,1);
		if (idx == 3 || idx == 4)
			GSF_LE_SET_GUINT16 (data,2);
		if (idx == 15) {
			GSF_LE_SET_GUINT16 (data+4, 1);
			GSF_LE_SET_GUINT8  (data+7, 0x0);
		}
		if (idx == 16)
			GSF_LE_SET_GUINT32 (data, 0x002b0001); /* These turn up in the formats... */
		if (idx == 17)
			GSF_LE_SET_GUINT32 (data, 0x00290001);
		if (idx == 18)
			GSF_LE_SET_GUINT32 (data, 0x002c0001);
		if (idx == 19)
			GSF_LE_SET_GUINT32 (data, 0x002a0001);
		if (idx == 20)
			GSF_LE_SET_GUINT32 (data, 0x00090001);
		if (idx < 21 && idx > 15) /* Style bit ? */
			GSF_LE_SET_GUINT8  (data+7, 0xf8);
		if (idx == 0)
			GSF_LE_SET_GUINT8  (data+7, 0);

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

/**
 * write_xf_record
 * @bp  BIFF buffer
 * @ewb  Workbook
 * @xfd XF data
 *
 * Write an XF record to file
 * See S59E1E.HTM
 * For BIFF V8, only font and format are written.
 **/
static void
write_xf_record (BiffPut *bp, ExcelWriteState *ewb, BiffXFData *xfd)
{
	guint8 data[256];
	guint16 itmp;
	int btype;

	memset (data, 0, sizeof (data));

	if (bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+0, xfd->font_idx);
		GSF_LE_SET_GUINT16 (data+2, xfd->format_idx);
		GSF_LE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		GSF_LE_SET_GUINT16 (data+0, xfd->font_idx);
		GSF_LE_SET_GUINT16 (data+2, xfd->format_idx);

		itmp = 0x0001;
		if (xfd->hidden)
			itmp |= 1 << 1;
		if (xfd->locked)
			itmp |= 1;
		itmp |= (xfd->parentstyle << 4) & 0xFFF0; /* Parent style */
		GSF_LE_SET_GUINT16(data+4, itmp);

		/* Horizontal alignment */
		itmp  = halign_to_excel (xfd->halign) & 0x7;
		if (xfd->wrap_text)	/* Wrapping */
			itmp |= 1 << 3;
		/* Vertical alignment */
		itmp |= (valign_to_excel (xfd->valign) << 4) & 0x70;
		itmp |= (orientation_to_excel (xfd->orientation) << 8)
			 & 0x300;
		itmp |= xfd->differences & 0xFC00; /* Difference bits */
		GSF_LE_SET_GUINT16(data+6, itmp);

		/* Documentation is wrong - there is no fSxButton bit.
		 * The bg color uses the bit */
		itmp = 0;
		/* Fill pattern foreground color */
		itmp |= xfd->pat_foregnd_col & 0x7f;
		/* Fill pattern background color */
		itmp |= (xfd->pat_backgnd_col << 7) & 0x3f80;
		GSF_LE_SET_GUINT16(data+8, itmp);

		itmp  = xfd->fill_pattern_idx & 0x3f;

		/* Borders */
		btype = xfd->border_type[STYLE_BOTTOM];
		if (btype != STYLE_BORDER_NONE) {
			itmp |= (border_type_to_excel (btype, bp->version) << 6)
				& 0x1c0;
			itmp |= (xfd->border_color[STYLE_BOTTOM] << 9)
				& 0xfe00;
		}
		GSF_LE_SET_GUINT16(data+10, itmp);

		itmp  = 0;
		btype = xfd->border_type[STYLE_TOP];
		if (btype != STYLE_BORDER_NONE) {
			itmp |= border_type_to_excel (btype, bp->version) & 0x7;
			itmp |= (xfd->border_color[STYLE_TOP] << 9) & 0xfe00;
		}
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_LEFT],
					       bp->version)
			 << 3) & 0x38;
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_RIGHT],
					       bp->version)
			 << 6) & 0x1c0;
		GSF_LE_SET_GUINT16(data+12, itmp);

		itmp  = 0;
		if (xfd->border_type[STYLE_LEFT] != STYLE_BORDER_NONE)
			itmp  |= xfd->border_color[STYLE_LEFT] & 0x7f;
		if (xfd->border_type[STYLE_RIGHT] != STYLE_BORDER_NONE)
			itmp |= (xfd->border_color[STYLE_RIGHT] << 7) & 0x3f80;
		GSF_LE_SET_GUINT16(data+14, itmp);

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

/**
 * write_xf
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write XF records to file for all MStyles
 **/
static void
write_xf (BiffPut *bp, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->xf.two_way_table;
	int nxf = twt->idx_to_key->len;
	int i;
	MStyle *st;
	BiffXFData xfd;

	guint32 style_magic[6] = { 0xff038010, 0xff068011, 0xff048012, 0xff078013,
				   0xff008000, 0xff058014 };

	/* Need at least 16 apparently */
	for (i = 0; i < XF_RESERVED; i++)
		write_xf_magic_record (bp, i);

	/* Scan through all the Styles... */
	for (; i < nxf + twt->base; i++) {
		st = xf_get_mstyle (ewb, i);
		build_xf_data (ewb, &xfd, st);
#ifndef NO_DEBUG_EXCEL
		log_xf_data (ewb, &xfd, i);
#endif
		write_xf_record (bp, ewb, &xfd);
	}

	/* See: S59DEA.HTM */
	for (i = 0; i < 6; i++) {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_STYLE, 4);
		GSF_LE_SET_GUINT32 (data, style_magic[i]); /* cop out */
		ms_biff_put_commit (bp);
	}

	/* See: S59E14.HTM */
	if (bp->version >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_USESELFS, 2);
		GSF_LE_SET_GUINT16 (data, 0x1); /* we are language naturals */
		ms_biff_put_commit (bp);
	}
}

int
excel_write_map_errcode (Value const *v)
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
 * @col column
 * @row row
 * @xf  XF index
 *
 * Write cell value to file
 **/
static void
write_value (BiffPut *bp, Value *v, guint32 col, guint32 row, guint16 xf)
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
			GSF_LE_SET_GUINT8 (data + 6, excel_write_map_errcode (v));
			GSF_LE_SET_GUINT8 (data + 7, 1); /* Mark as a err */
		} else {
			GSF_LE_SET_GUINT8 (data + 6, v->v_bool.val ? 1 : 0);
			GSF_LE_SET_GUINT8 (data + 7, 0); /* Mark as a bool */
		}
		ms_biff_put_commit (bp);
		break;
	}
	case VALUE_INTEGER: {
		int vint = v->v_int.val;
		guint8 *data;

		d (3, fprintf (stderr, "Writing %d %d\n", vint, v->v_int.val););
		if (((vint<<2)>>2) != vint) { /* Chain to floating point then. */
			Value *vf = value_new_float (v->v_int.val);
			write_value (bp, vf, col, row, xf);
			value_release (vf);
		} else {
			data = ms_biff_put_len_next (bp, (0x200 | BIFF_RK), 10);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			/* Integers can always be represented as integers.
			 * Use RK form 2 */
			GSF_LE_SET_GUINT32 (data + 6, (vint<<2) + 2);
			ms_biff_put_commit (bp);
		}
		break;
	}
	case VALUE_FLOAT: {
		gnum_float val = v->v_float.val;
		gboolean is_int = ((val - (int)val) == 0.0) &&
			(((((int)val)<<2)>>2) == ((int)val));

		d (3, fprintf (stderr, "Writing %g is (%g %g) is int ? %d\n",
			      (double)val,
			      (double)(1.0 * (int)val),
			      (double)(1.0 * (val - (int)val)),
			      is_int););

		/* FIXME : Add test for double with 2 digits of fraction
		 * and represent it as a mode 3 RK (val*100) construct */
		if (is_int) { /* not nice but functional */
			Value *vi = value_new_int (val);
			write_value (bp, vi, col, row, xf);
			value_release (vi);
		} else if (bp->version >= MS_BIFF_V7) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (bp, (0x200 | BIFF_NUMBER), 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			gsf_le_set_double (data + 6, val);
			ms_biff_put_commit (bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (bp, (0x200 | BIFF_RK));
			gsf_le_set_double (data+6-4, val);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			data[6] &= 0xfc;
			ms_biff_put_var_write  (bp, data, 10); /* Yes loose it. */
			ms_biff_put_commit (bp);
		}
		break;
	}
	case VALUE_STRING:
		g_return_if_fail (v->v_str.val->str);

		/* Use LABEL */
		if (bp->version < MS_BIFF_V8) {
			guint8 data[16];
			gint len;
			char *buf;

			len = biff_convert_text(&buf, v->v_str.val->str, MS_BIFF_V7);

			if (len > 0xff)
				len = 0xff;
			ms_biff_put_var_next   (bp, (0x200 | BIFF_LABEL));

			EX_SETXF (data, xf);
			EX_SETCOL(data, col);
			EX_SETROW(data, row);
			EX_SETSTRLEN (data, len);
			ms_biff_put_var_write  (bp, data, 8);
			biff_put_text (bp, buf, len, FALSE, AS_PER_VER);
			g_free (buf);
			ms_biff_put_commit (bp);
		} else {
			/* Use SST */
		}
		break;

	default:
		fprintf (stderr, "Unhandled value type %d\n", v->type);
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
write_formula (ExcelWriteState *ewb, ExcelSheet *esheet, Cell const *cell, gint16 xf)
{
	guint8   data[22];
	guint8   lendat[2];
	guint32  len;
	gboolean string_result = FALSE;
	gint     col, row;
	Value   *v;
	GnmExpr const *expr;

	g_return_if_fail (ewb);
	g_return_if_fail (cell);
	g_return_if_fail (esheet);
	g_return_if_fail (cell_has_expr (cell));
	g_return_if_fail (cell->value);

	col = cell->pos.col;
	row = cell->pos.row;
	v = cell->value;
	expr = cell->base.expression;

	/* See: S59D8F.HTM */
	ms_biff_put_var_next (ewb->bp, BIFF_FORMULA);
	EX_SETROW (data, row);
	EX_SETCOL (data, col);
	EX_SETXF  (data, xf);
	switch (v->type) {
	case VALUE_INTEGER :
	case VALUE_FLOAT :
		gsf_le_set_double (data + 6, value_get_as_float (v));
		break;

	case VALUE_STRING :
		GSF_LE_SET_GUINT32 (data +  6, 0x00000000);
		GSF_LE_SET_GUINT32 (data + 10, 0xffff0000);
		string_result = TRUE;
		break;

	case VALUE_BOOLEAN :
		GSF_LE_SET_GUINT32 (data +  6,
				    v->v_bool.val ? 0x10001 : 0x1);
		GSF_LE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_ERROR :
		GSF_LE_SET_GUINT32 (data +  6,
				    0x00000002 | (excel_write_map_errcode (v) << 16));
		GSF_LE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_EMPTY :
		GSF_LE_SET_GUINT32 (data +  6, 0x00000003);
		GSF_LE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	default :
		g_warning ("Unhandled value->type (%d) in excel::write_formula.", v->type);
	}

	GSF_LE_SET_GUINT16 (data + 14, 0x0); /* alwaysCalc & calcOnLoad */
	GSF_LE_SET_GUINT32 (data + 16, 0x0);
	GSF_LE_SET_GUINT16 (data + 20, 0x0); /* bogus len, fill in later */
	ms_biff_put_var_write (ewb->bp, data, 22);
	len = excel_write_formula (ewb, expr, esheet->gnum_sheet, col, row, 0);

	ms_biff_put_var_seekto (ewb->bp, 20);
	GSF_LE_SET_GUINT16 (lendat, len);
	ms_biff_put_var_write (ewb->bp, lendat, 2);

	ms_biff_put_commit (ewb->bp);

	if (expr->any.oper == GNM_EXPR_OP_ARRAY &&
	    expr->array.x == 0 && expr->array.y == 0) {
		ms_biff_put_var_next (ewb->bp, BIFF_ARRAY);
		GSF_LE_SET_GUINT16 (data+0, cell->pos.row);
		GSF_LE_SET_GUINT16 (data+2, cell->pos.row + expr->array.rows-1);
		GSF_LE_SET_GUINT16 (data+4, cell->pos.col);
		GSF_LE_SET_GUINT16 (data+5, cell->pos.col + expr->array.cols-1);
		GSF_LE_SET_GUINT16 (data+6, 0x0); /* alwaysCalc & calcOnLoad */
		GSF_LE_SET_GUINT32 (data+8, 0);
		GSF_LE_SET_GUINT16 (data+12, 0); /* bogus len, fill in later */
		ms_biff_put_var_write (ewb->bp, data, 14);
		len = excel_write_formula (ewb, expr->array.corner.expr,
					   esheet->gnum_sheet, col, row, 0);

		ms_biff_put_var_seekto (ewb->bp, 12);
		GSF_LE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (ewb->bp, lendat, 2);
		ms_biff_put_commit (ewb->bp);
	}

	if (string_result) {
		gint len;
		gchar *str, *buf;

		ms_biff_put_var_next (ewb->bp, 0x200|BIFF_STRING);
		str = value_get_as_string (v);
		len = biff_convert_text(&buf, str, MS_BIFF_V7);
		biff_put_text (ewb->bp, buf, len, TRUE, SIXTEEN_BIT);
		g_free (buf);
		g_free (str);
		ms_biff_put_commit (ewb->bp);
	}
}

#define MAX_BIFF_NOTE_CHUNK	2048

/* biff7 and earlier */
static void
excel_write_comments_biff7 (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 data[6];
	GSList *l, *comments;

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

		len = biff_convert_text(&buf, comment, bp->version);
		p = buf;
		ms_biff_put_var_next (bp, BIFF_NOTE);
		GSF_LE_SET_GUINT16 (data + 0, pos->start.row);
		GSF_LE_SET_GUINT16 (data + 2, pos->start.col);
		GSF_LE_SET_GUINT16 (data + 4, len);
		ms_biff_put_var_write (bp, data, 6);

repeat:
		if (len > MAX_BIFF_NOTE_CHUNK) {
			biff_put_text (bp, p, MAX_BIFF_NOTE_CHUNK, FALSE, AS_PER_VER);

			ms_biff_put_commit (bp);

			p += MAX_BIFF_NOTE_CHUNK;
			len -= MAX_BIFF_NOTE_CHUNK;

			ms_biff_put_var_next (bp, BIFF_NOTE);
			GSF_LE_SET_GUINT16 (data + 0, 0xffff);
			GSF_LE_SET_GUINT16 (data + 2, 0);
			GSF_LE_SET_GUINT16 (data + 4, MIN (MAX_BIFF_NOTE_CHUNK, len));
			ms_biff_put_var_write (bp, data, 6);

			goto repeat;
		} else {
			biff_put_text (bp, p, len, FALSE, AS_PER_VER);
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
write_cell (ExcelWriteState *ewb, ExcelSheet *esheet, Cell const *cell, unsigned xf)
{
	d (2, {
		ParsePos tmp;
		fprintf (stderr, "Writing cell at %s '%s' = '%s', xf = 0x%x\n",
			cell_name (cell),
			(cell_has_expr (cell) ?
			 gnm_expr_as_string (cell->base.expression,
					     parse_pos_init_cell (&tmp, cell)) : "none"),
			(cell->value ?
			 value_get_as_string (cell->value) : "empty"), xf);
	});
	if (cell_has_expr (cell))
		write_formula (ewb, esheet, cell, xf);
	else if (cell->value != NULL)
		write_value (ewb->bp, cell->value, cell->pos.col, cell->pos.row, xf);
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
		d (2, fprintf (stderr, "Writing blank at %s, xf = 0x%x\n",
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
			fprintf (stderr, "Writing multiple blanks %s",
				cell_coord_name (end_col + 1 - run, row));
			fprintf (stderr, ":%s\n", cell_coord_name (end_col, row));
		});

		data = ms_biff_put_len_next (bp, BIFF_MULBLANK, len);

		EX_SETCOL (data, end_col + 1 - run);
		EX_SETROW (data, row);
		GSF_LE_SET_GUINT16 (data + len - 2, end_col);
		ptr = data + 4;
		for (i = 0 ; i < run ; i++) {
			xf = xf_list [i];
			d (3, fprintf (stderr, " xf(%s) = 0x%x",
				      cell_coord_name (end_col + 1 - run, row),
				      xf););
			GSF_LE_SET_GUINT16 (ptr, xf);
			ptr += 2;
		}

		d (3, fprintf (stderr, "\n"););
	}

	ms_biff_put_commit (bp);
}

/**
 * excel_write_GUTS
 * @bp    :  BIFF buffer
 * @esheet : sheet
 *
 * Write information about outline mode gutters.
 * See: S59D92.HTM
 */
static void
excel_write_GUTS (BiffPut *bp, ExcelSheet *esheet)
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
	GSF_LE_SET_GUINT16 (data+0, row_size);
	GSF_LE_SET_GUINT16 (data+2, col_size);
	GSF_LE_SET_GUINT16 (data+4, row_level);
	GSF_LE_SET_GUINT16 (data+6, col_level);
	ms_biff_put_commit (bp);
}

static void
excel_write_DEFAULT_ROW_HEIGHT (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	double def_height;
	guint16 options = 0x0;
	guint16 height;

	def_height = sheet_row_get_default_size_pts (esheet->gnum_sheet);
	height = (guint16) (20. * def_height);
	d (1, fprintf (stderr, "Default row height 0x%x;\n", height););
	data = ms_biff_put_len_next (bp, 0x200|BIFF_DEFAULTROWHEIGHT, 4);
	GSF_LE_SET_GUINT16 (data + 0, options);
	GSF_LE_SET_GUINT16 (data + 2, height);
	ms_biff_put_commit (bp);
}

static void
excel_write_margin (BiffPut *bp, guint16 op, double points)
{
	guint8 *data;
	double  margin;

	margin = unit_convert (points, UNIT_POINTS, UNIT_INCH);

	data = ms_biff_put_len_next (bp, op, 8);
	gsf_le_set_double (data, margin);

	ms_biff_put_commit (bp);
}

/**
 * style_get_char_width
 * @style	the src of the font to use
 * @is_default  if true, this is for the default width.
 *
 * Utility 
 */
static double
style_get_char_width (MStyle const *style, gboolean is_default)
{
	return lookup_font_base_char_width (
		mstyle_get_font_name (style), 20. * mstyle_get_font_size (style),
		is_default);
}

/**
 * excel_write_DEFCOLWIDTH
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
excel_write_DEFCOLWIDTH (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	guint16 width;
	double  def_font_width, width_chars;
	MStyle	*def_style;

	/* Use the 'Normal' Style which is by definition the 0th */
	def_style = sheet_style_default	(esheet->gnum_sheet);
	def_font_width = sheet_col_get_default_size_pts (esheet->gnum_sheet);
	width_chars = def_font_width / style_get_char_width (def_style, TRUE);
	mstyle_unref (def_style);
	width = (guint16) (width_chars + .5);

	d (1, fprintf (stderr, "Default column width %d characters\n", width););

	data = ms_biff_put_len_next (bp, BIFF_DEFCOLWIDTH, 2);
	GSF_LE_SET_GUINT16 (data, width);
	ms_biff_put_commit (bp);
}

/**
 * excel_write_COLINFO
 * @bp:   BIFF buffer
 * @esheet:
 * @ci   : the descriptor of the first col
 * @last_index : the index of the last contiguous identical col
 * @xf_index   : the style index to the entire col (< 0 for none)
 *
 * Write column info for a run of identical columns
 */
static void
excel_write_COLINFO (BiffPut *bp, ExcelSheet *esheet,
		     ColRowInfo const *ci, int last_index, guint16 xf_index)
{
	guint8 *data;
	MStyle *style = two_way_table_idx_to_key (
		esheet->ewb->xf.two_way_table, xf_index);
	double  width_chars
		= ci->size_pts / style_get_char_width (style, FALSE);
	guint16 width = (guint16) (width_chars * 256.);

	guint16 options = 0;

	if (!ci->visible)
		options = 1;
	options |= (MIN (ci->outline_level, 0x7) << 8);
	if (ci->is_collapsed)
		options |= 0x1000;

	d (1, {
		fprintf (stderr, "Column Formatting %s!%s of width "
		      "%f/256 characters (%f pts) of size %f\n",
		      esheet->gnum_sheet->name_quoted,
		      cols_name (ci->pos, last_index), width / 256.,
		      ci->size_pts, style_get_char_width (style, FALSE));
		fprintf (stderr, "Options %hd, default style %hd\n", options, xf_index);
	});

	/* NOTE : Docs lie.  length is 12 not 11 */
	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 12);
	GSF_LE_SET_GUINT16 (data +  0, ci->pos);	/* 1st  col formatted */
	GSF_LE_SET_GUINT16 (data +  2, last_index);	/* last col formatted */
	GSF_LE_SET_GUINT16 (data +  4, width);		/* width */
	GSF_LE_SET_GUINT16 (data +  6, xf_index);	/* XF index */
	GSF_LE_SET_GUINT16 (data +  8, options);	/* options */
	GSF_LE_SET_GUINT16 (data + 10, 0);		/* reserved = 0 */
	ms_biff_put_commit (bp);
}

static void
excel_write_colinfos (BiffPut *bp, ExcelSheet *esheet)
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
			excel_write_COLINFO (bp, esheet, first, i-1, xf);
			first = ci;
			xf = new_xf;
		}
	}
	if (first != NULL)
		excel_write_COLINFO (bp, esheet, first, i-1, xf);
}

/* See: S59D76.HTM */
static void
excel_write_DIMENSION (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	if (bp->version >= MS_BIFF_V8) {
		data = ms_biff_put_len_next (bp, 0x200 | BIFF_DIMENSIONS, 14);
		GSF_LE_SET_GUINT32 (data +  0, 0);
		GSF_LE_SET_GUINT32 (data +  4, esheet->max_row);
		GSF_LE_SET_GUINT16 (data +  8, 0);
		GSF_LE_SET_GUINT16 (data + 10, esheet->max_col);
		GSF_LE_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 10);
		GSF_LE_SET_GUINT16 (data +  0, 0);
		GSF_LE_SET_GUINT16 (data +  2, esheet->max_row);
		GSF_LE_SET_GUINT16 (data +  4, 0);
		GSF_LE_SET_GUINT16 (data +  6, esheet->max_col);
		GSF_LE_SET_GUINT16 (data +  8, 0x0000);
	}
	ms_biff_put_commit (bp);
}

static void
excel_write_COUNTRY (BiffPut *bp)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_COUNTRY, 4);
	GSF_LE_SET_GUINT16 (data, 1); /* flag as made in US */
	GSF_LE_SET_GUINT16 (data, 1);
	ms_biff_put_commit (bp);
}

static void
excel_write_WSBOOL (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_WSBOOL, 2);
	guint16 flags = 0;

	/* 0x0001 automatic page breaks are visible */
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	if (esheet->gnum_sheet->outline_symbols_below)	flags |= 0x040;
	if (esheet->gnum_sheet->outline_symbols_right)	flags |= 0x080;
	/* 0x0100 the Fit option is on (Page Setup dialog box, Page tab) */
	if (esheet->gnum_sheet->display_outlines)	flags |= 0x600;

	GSF_LE_SET_GUINT16 (data, 0x04c1);
	ms_biff_put_commit (bp);

}

static void
write_sheet_head (BiffPut *bp, ExcelSheet *esheet)
{
	guint8 *data;
	PrintInformation *pi;
	Workbook *ewb = esheet->gnum_sheet->workbook;
	double header = 0, footer = 0, left = 0, right = 0;

	g_return_if_fail (esheet != NULL);
	g_return_if_fail (esheet->gnum_sheet != NULL);
	g_return_if_fail (esheet->gnum_sheet->print_info != NULL);

	pi = esheet->gnum_sheet->print_info;

	/* See: S59D63.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCMODE, 2);
	GSF_LE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D62.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCCOUNT, 2);
	GSF_LE_SET_GUINT16 (data, ewb->iteration.max_number);
	ms_biff_put_commit (bp);

	/* See: S59DD7.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFMODE, 2);
	GSF_LE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D9C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_ITERATION, 2);
	GSF_LE_SET_GUINT16 (data, ewb->iteration.enabled ? 1 : 0);
	ms_biff_put_commit (bp);

	/* See: S59D75.HTM */
	data = ms_biff_put_len_next (bp, BIFF_DELTA, 8);
	gsf_le_set_double (data, ewb->iteration.tolerance);
	ms_biff_put_commit (bp);

	/* See: S59DDD.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SAVERECALC, 2);
	GSF_LE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD0.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTHEADERS, 2);
	GSF_LE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59DCF.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTGRIDLINES, 2);
	GSF_LE_SET_GUINT16 (data, pi->print_grid_lines);
	ms_biff_put_commit (bp);

	/* See: S59D91.HTM */
	data = ms_biff_put_len_next (bp, BIFF_GRIDSET, 2);
	GSF_LE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	excel_write_GUTS (bp, esheet);
	excel_write_DEFAULT_ROW_HEIGHT (bp, esheet);
	excel_write_COUNTRY (bp);
	excel_write_WSBOOL (bp, esheet);

	/* See: S59D94.HTM */
	ms_biff_put_var_next (bp, BIFF_HEADER);
/*	biff_put_text (bp, "&A", TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D8D.HTM */
	ms_biff_put_var_next (bp, BIFF_FOOTER);
/*	biff_put_text (bp, "&P", TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D93.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HCENTER, 2);
	GSF_LE_SET_GUINT16 (data, pi->center_horizontally);
	ms_biff_put_commit (bp);

	/* See: S59E15.HTM */
	data = ms_biff_put_len_next (bp, BIFF_VCENTER, 2);
	GSF_LE_SET_GUINT16 (data, pi->center_vertically);
	ms_biff_put_commit (bp);

	print_info_get_margins (pi, &header, &footer, &left, &right);
	excel_write_margin (bp, BIFF_LEFT_MARGIN,   left);
	excel_write_margin (bp, BIFF_RIGHT_MARGIN,  right);
	excel_write_margin (bp, BIFF_TOP_MARGIN,    pi->margins.top.points);
	excel_write_margin (bp, BIFF_BOTTOM_MARGIN, pi->margins.bottom.points);

	excel_write_SETUP (bp, esheet);
	excel_write_externsheets_v7 (bp, esheet->ewb, esheet);
	excel_write_DEFCOLWIDTH (bp, esheet);
	excel_write_colinfos (bp, esheet);
	excel_write_DIMENSION (bp, esheet);
}

static void
excel_write_SELECTION (BiffPut *bp, ExcelSheet *esheet)
{
	SheetView const *sv = sheet_get_view (esheet->gnum_sheet,
		esheet->ewb->gnum_wb_view);
	int n = g_list_length (sv->selections);
	GList *ptr;
	guint8 *data;

	data = ms_biff_put_len_next (bp, BIFF_SELECTION, 15);
	GSF_LE_SET_GUINT8  (data +  0, 3); /* no split == pane 3 ? */
	GSF_LE_SET_GUINT16 (data +  1, sv->edit_pos.row);
	GSF_LE_SET_GUINT16 (data +  3, sv->edit_pos.col);
	GSF_LE_SET_GUINT16 (data +  5, 0); /* our edit_pos is in 1st range */
	GSF_LE_SET_GUINT16 (data +  7, n);

	data += 9;
	for (ptr = sv->selections ; ptr != NULL ; ptr = ptr->next, data += 6) {
		Range const *r = ptr->data;
		GSF_LE_SET_GUINT16 (data + 0, r->start.row);
		GSF_LE_SET_GUINT16 (data + 2, r->end.row);
		GSF_LE_SET_GUINT8  (data + 4, r->start.col);
		GSF_LE_SET_GUINT8  (data + 5, r->end.col);
	}
	ms_biff_put_commit (bp);
}

/* See: S59D99.HTM */
static void
write_index (GsfOutput *output, ExcelSheet *esheet, unsigned pos,
	     MsBiffVersion ver)
{
	guint8  data[4];
	gsf_off_t oldpos;
	unsigned i;

	g_return_if_fail (output);
	g_return_if_fail (esheet);

	oldpos = gsf_output_tell (output);
	if (ver >= MS_BIFF_V8)
		gsf_output_seek (output, pos+4+16, G_SEEK_SET);
	else
		gsf_output_seek (output, pos+4+12, G_SEEK_SET);

	for (i = 0; i < esheet->dbcells->len; i++) {
		unsigned pos = g_array_index (esheet->dbcells, unsigned, i);
		GSF_LE_SET_GUINT32 (data, pos - esheet->ewb->streamPos);
		d (2, fprintf (stderr, "Writing index record"
			      " 0x%4.4x - 0x%4.4x = 0x%4.4x\n",
			      pos, esheet->ewb->streamPos,
			      pos - esheet->ewb->streamPos););
		gsf_output_write (output, 4, data);
	}

	gsf_output_seek (output, oldpos, G_SEEK_SET);
}

/* See: S59DDB.HTM */
static unsigned
excel_write_ROWINFO (BiffPut *bp, ExcelSheet *esheet, guint32 row, guint32 last_col)
{
	guint8 *data;
	unsigned pos;
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

	d (1, fprintf (stderr, "Row %d height 0x%x;\n", row+1, height););

	data = ms_biff_put_len_next (bp, (0x200 | BIFF_ROW), 16);
	pos = bp->streamPos;
	GSF_LE_SET_GUINT16 (data +  0, row);     /* Row number */
	GSF_LE_SET_GUINT16 (data +  2, 0);       /* first def. col */
	GSF_LE_SET_GUINT16 (data +  4, last_col);/* last  def. col */
	GSF_LE_SET_GUINT16 (data +  6, height);	 /* height */
	GSF_LE_SET_GUINT16 (data +  8, 0x00);    /* undocumented */
	GSF_LE_SET_GUINT16 (data + 10, 0x00);    /* reserved */
	GSF_LE_SET_GUINT16 (data + 12, options); /* options */
	GSF_LE_SET_GUINT16 (data + 14, row_xf);  /* default style */
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
	       unsigned *ri_start, unsigned *rc_start, guint32 nrows)
{
	unsigned pos;
	guint32 i;
	guint16 offset;

	guint8 *data = ms_biff_put_len_next (bp, BIFF_DBCELL, 4 + nrows * 2);
	pos = bp->streamPos;

	GSF_LE_SET_GUINT32 (data, pos - ri_start [0]);
	for (i = 0 ; i < nrows; i++) {
		offset = rc_start [0]
			- (i > 0 ? rc_start [i - 1] : ri_start [1]);
		GSF_LE_SET_GUINT16 (data + 4 + i * 2, offset);
	}

	ms_biff_put_commit (bp);

	g_array_append_val (esheet->dbcells, pos);
}

/**
 * write_block
 * @ewb     BIFF buffer
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
write_block (ExcelWriteState *ewb, ExcelSheet *esheet, guint32 begin, int nrows)
{
	int max_col = esheet->max_col;
	int col, row, max_row;
	unsigned  ri_start [2]; /* Row info start */
	unsigned *rc_start;	/* Row cells start */
	guint16   xf_list [SHEET_MAX_COLS];
	Cell const *cell;
	Sheet	   *sheet = esheet->gnum_sheet;
	int	    xf;
	TwoWayTable *twt = esheet->ewb->xf.two_way_table;

	if (nrows > esheet->max_row - (int) begin) /* Incomplete final block? */
		nrows = esheet->max_row - (int) begin;
	max_row = begin + nrows - 1;

	ri_start [0] = excel_write_ROWINFO (ewb->bp, esheet, begin, max_col);
	ri_start [1] = ewb->bp->streamPos;
	for (row = begin + 1; row <= max_row; row++)
		(void) excel_write_ROWINFO (ewb->bp, esheet, row, max_col);

	rc_start = g_new0 (unsigned, nrows);
	for (row = begin; row <= max_row; row++) {
		guint32 run_size = 0;

		/* Save start pos of 1st cell in row */
		rc_start [row - begin] = ewb->bp->streamPos;
		for (col = 0; col < max_col; col++) {
			cell = sheet_cell_get (sheet, col, row);
			xf = two_way_table_key_to_idx (twt,
				sheet_style_get (sheet, col, row));
			if (cell == NULL) {
				if (xf != esheet->col_xf [col])
					xf_list [run_size++] = xf;
				else if (run_size > 0) {
					write_mulblank (ewb->bp, esheet, col - 1, row,
							xf_list, run_size);
					run_size = 0;
				}
			} else {
				if (run_size > 0) {
					write_mulblank (ewb->bp, esheet, col - 1, row,
							xf_list, run_size);
					run_size = 0;
				}
				write_cell (ewb, esheet, cell, xf);
				workbook_io_progress_update (esheet->ewb->io_context, 1);
			}
		}
		if (run_size > 0)
			write_mulblank (ewb->bp, esheet, col - 1, row,
					xf_list, run_size);
	}

	write_db_cell (ewb->bp, esheet, ri_start, rc_start, nrows);
	g_free (rc_start);

	return row - 1;
}

/* See: 'Finding records in BIFF files': S59E28.HTM */
/* and S59D99.HTM */
static void
excel_write_sheet (ExcelWriteState *ewb, ExcelSheet *esheet)
{
	guint32 block_end;
	gint32 y;
	int rows_in_block = ROW_BLOCK_MAX_LEN;
	unsigned index_off;
	guint8 *data;
	int num, denom;

	/* No. of blocks of rows. Only correct as long as all rows
	 * _including empties_ have row info records
	 */
	guint32 nblocks = (esheet->max_row - 1) / rows_in_block + 1;

	esheet->streamPos = biff_bof_write (ewb->bp, MS_BIFF_TYPE_Worksheet);

	if (ewb->bp->version >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (ewb->bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 16);
		index_off = ewb->bp->streamPos;
		GSF_LE_SET_GUINT32 (data, 0);
		GSF_LE_SET_GUINT32 (data +  4, 0);
		GSF_LE_SET_GUINT32 (data +  8, esheet->max_row);
		GSF_LE_SET_GUINT32 (data + 12, 0);
	} else {
		guint8 *data = ms_biff_put_len_next (ewb->bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 12);
		index_off = ewb->bp->streamPos;
		GSF_LE_SET_GUINT32 (data, 0);
		GSF_LE_SET_GUINT16 (data + 4, 0);
		GSF_LE_SET_GUINT16 (data + 6, esheet->max_row);
		GSF_LE_SET_GUINT32 (data + 8, 0);
	}
	ms_biff_put_commit (ewb->bp);

	write_sheet_head (ewb->bp, esheet);

	d (1, fprintf (stderr, "Saving esheet '%s' geom (%d, %d)\n",
		      esheet->gnum_sheet->name_unquoted,
		      esheet->max_col, esheet->max_row););
	for (y = 0; y < esheet->max_row; y = block_end + 1)
		block_end = write_block (ewb, esheet, y, rows_in_block);

	if (ewb->bp->version < MS_BIFF_V8)
		excel_write_comments_biff7 (ewb->bp, esheet);
	write_index (ewb->bp->output, esheet, index_off, ewb->bp->version);


	excel_write_WINDOW1 (ewb->bp, esheet->ewb->gnum_wb_view);
	if (excel_write_WINDOW2 (ewb->bp, esheet))
		excel_write_PANE (ewb->bp, esheet);

	/* Zoom */
	stern_brocot (esheet->gnum_sheet->last_zoom_factor_used,
		      1000, &num, &denom);
	data = ms_biff_put_len_next (ewb->bp, BIFF_SCL, 4);
	GSF_LE_SET_GUINT16 (data + 0, (guint16)num);
	GSF_LE_SET_GUINT16 (data + 2, denom);
	ms_biff_put_commit (ewb->bp);

	excel_write_SELECTION (ewb->bp, esheet);
	excel_write_MERGECELLS (ewb->bp, esheet);

/* See: S59D90.HTM: Global Column Widths...  not cricual.
	data = ms_biff_put_len_next (ewb->bp, BIFF_GCW, 34);
	{
		int i;
		for (i = 0; i < 34; i++)
			GSF_LE_SET_GUINT8 (data+i, 0xff);
		GSF_LE_SET_GUINT32 (data, 0xfffd0020);
	}
	ms_biff_put_commit (ewb->bp);
*/

	ms_excel_write_EOF (ewb->bp);
}

static ExcelSheet *
excel_sheet_new (ExcelWriteState *ewb, Sheet *gnum_sheet, MsBiffVersion ver)
{
	ExcelSheet      *esheet = g_new (ExcelSheet, 1);
	Range           extent;
	int const maxrows = (ver >= MS_BIFF_V8)
		? MsBiffMaxRowsV8 : MsBiffMaxRowsV7;

	g_return_val_if_fail (gnum_sheet, NULL);
	g_return_val_if_fail (ewb, NULL);

	/* Ignore spans and merges past the bound */
	extent = sheet_get_extent (gnum_sheet, FALSE);

	if (extent.end.row > maxrows) {
		gnm_io_warning (ewb->io_context, _("Too many rows for this format (%d > %d)"),
			  extent.end.col, maxrows);
		extent.end.row = maxrows;
	}
	if (extent.end.col > 256) {
		gnm_io_warning (ewb->io_context, _("Too many rows for this format (%d > %d)"),
			  extent.end.col, maxrows);
		extent.end.col = maxrows;
	}

	sheet_style_get_extent (gnum_sheet, &extent);

	esheet->gnum_sheet = gnum_sheet;
	esheet->streamPos  = 0x0deadbee;
	esheet->ewb        = ewb;
	esheet->max_col    = 1 + MAX (gnum_sheet->cols.max_used, extent.end.col);
	esheet->max_row    = 1 + MAX (gnum_sheet->rows.max_used, extent.end.row);
	esheet->dbcells    = g_array_new (FALSE, FALSE, sizeof (unsigned));

	/* It is ok to have formatting out of range, we can disregard that. */
	if (esheet->max_row > maxrows)
		esheet->max_row = maxrows;

	return esheet;
}

static void
excel_sheet_free (ExcelSheet *esheet)
{
	if (esheet) {
		g_array_free (esheet->dbcells, TRUE);
		g_free (esheet);
	}
}

/**
 * pre_pass
 * @context:  Command context.
 * @ewb:   The workbook to scan
 *
 * Scans all the workbook items. Adds all styles, fonts, formats and
 * colors to tables. Resolves any referencing problems before they
 * occur, hence the records can be written in a linear order.
 **/
static int
pre_pass (ExcelWriteState *ewb)
{
	int ret = 0;

	if (ewb->sheets->len == 0)
		return TRUE;

	/* The default style first */
	put_mstyle (ewb, ewb->xf.default_style);

	/* Its font and format */
	put_font (ewb->xf.default_style, NULL, ewb);
	put_format (ewb->xf.default_style, NULL, ewb);

	gather_styles (ewb);     /* (and cache cells) */
	/* Gather Info from styles */
	gather_fonts (ewb);
	gather_formats (ewb);
	gather_palette (ewb);

	return ret;
}

/*
 * excel_write_state_free
 * @ewb  Workbook
 *
 * Free various bits
 */
static void
excel_write_state_free (ExcelWriteState *ewb)
{
	unsigned i;

	fonts_free   (ewb);
	formats_free (ewb);
	palette_free (ewb);
	xf_free  (ewb);

	for (i = 0; i < ewb->sheets->len; i++)
		excel_sheet_free (g_ptr_array_index (ewb->sheets, i));

	g_ptr_array_free (ewb->sheets, TRUE);
	g_ptr_array_free (ewb->names, TRUE);
	g_ptr_array_free (ewb->externnames, TRUE);

	if (ewb->bp != NULL) {
		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	}
	g_free (ewb);
}

/* no need to use a full fledged two table, we already know that the Strings
 * are unique.
 */
typedef struct
{
	GHashTable *strings;
	GPtrArray  *indicies;
} SSTCollection;

static Value *
sst_collect_str (Sheet *sheet, int col, int row, Cell *cell,
		 gpointer user_data)
{
	SSTCollection *accum = user_data;
	int index;
	String *str;

	if (cell_has_expr (cell) || cell->value == NULL ||
	    cell->value->type != VALUE_STRING)
		return NULL;

	str = cell->value->v_str.val;
	index = accum->indicies->len;
	g_ptr_array_add (accum->indicies, str);
	g_hash_table_insert (accum->strings, str, GINT_TO_POINTER (index));
	return NULL;
}

/* According to MSDN 'Excel BIFF8 CONTINUE Record Information Is Incomplete' */
#define MAX_SST_SIZE	8224

static void
ms_excel_write_SST (BiffPut *bp, ExcelWriteState *ewb)
{
	int i;
	SSTCollection accum;

	accum.strings  = g_hash_table_new (g_direct_hash, g_direct_equal);
	accum.indicies = g_ptr_array_new ();

	for (i = 0 ; i < workbook_sheet_count (ewb->gnum_wb) ; i++)
		sheet_foreach_cell_in_range (
			workbook_sheet_by_index	 (ewb->gnum_wb, i),
			CELL_ITER_IGNORE_BLANK,
			0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
			sst_collect_str, &accum);

	g_hash_table_destroy (accum.strings);
	g_ptr_array_free (accum.indicies, TRUE);
}

static void
write_workbook_v7 (ExcelWriteState *ewb, BiffPut *bp)
{
	guint8 *data;
	gint len;
	char *buf;
	guint8 pad [WRITEACCESS_LEN];

	ExcelSheet *s  = 0;
	guint        i;

	current_workbook_iconv = gsf_msole_iconv_open_for_export();

	ewb->streamPos = biff_bof_write (ewb->bp, MS_BIFF_TYPE_Workbook);

	ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 0);
	ms_biff_put_commit (bp);

/* for biff8
		if (bp->version >= MS_BIFF_V8) {
			data = ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 2);
			GSF_LE_SET_GUINT16 (data, gsf_msole_iconv_win_codepage());
		}
		*/

	data = ms_biff_put_len_next (bp, BIFF_MMS, 2);
	GSF_LE_SET_GUINT16(data, 0);
	ms_biff_put_commit (bp);

	ms_biff_put_len_next (bp, BIFF_TOOLBARHDR, 0);
	ms_biff_put_commit (bp);

	ms_biff_put_len_next (bp, BIFF_TOOLBAREND, 0);
	ms_biff_put_commit (bp);

	ms_biff_put_len_next (bp, BIFF_INTERFACEEND, 0);
	ms_biff_put_commit (bp);

	len = biff_convert_text (&buf, "The Gnumeric Development Team",
				 bp->version);

	memset (pad, ' ', sizeof pad);
	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	biff_put_text (bp, buf, len, TRUE, AS_PER_VER);
	g_free(buf);
	ms_biff_put_var_write (bp, pad, WRITEACCESS_LEN - len - 1);
	ms_biff_put_commit (bp);

	/* See: S59D66.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CODEPAGE, 2);
	GSF_LE_SET_GUINT16 (data, gsf_msole_iconv_win_codepage());
	ms_biff_put_commit (bp);

	if (bp->version >= MS_BIFF_V8) { /* See S59D78.HTM */
		int i, len;

		data = ms_biff_put_len_next (bp, BIFF_DSF, 2);
		GSF_LE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);

		/* See: S59E09.HTM */
		len = ewb->sheets->len;
		data = ms_biff_put_len_next (bp, BIFF_TABID, len * 2);
		for (i = 0; i < len; i++)
			GSF_LE_SET_GUINT16 (data + i*2, i + 1);
		ms_biff_put_commit (bp);
	}
	/* See: S59D8A.HTM */
	data = ms_biff_put_len_next (bp, BIFF_FNGROUPCOUNT, 2);
	GSF_LE_SET_GUINT16 (data, 0x0e);
	ms_biff_put_commit (bp);

	/* write externsheets for every sheet in the workbook
	 * to make our lives easier
	 */
	excel_write_externsheets_v7 (bp, ewb, NULL);

	if (ewb->gnum_wb->names != NULL) {
		g_hash_table_foreach (ewb->gnum_wb->names->names,
			(GHFunc) excel_write_NAME_v7, ewb);
		for (i = 0; i < ewb->sheets->len; i++) {
		}
	}

	/* See: S59E19.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WINDOWPROTECT, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DD1.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PROTECT, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCC.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PASSWORD, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	excel_write_WINDOW1 (bp, ewb->gnum_wb_view);

	/* See: S59D5B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BACKUP, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D95.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HIDEOBJ, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D54.HTM */
	data = ms_biff_put_len_next (bp, BIFF_1904, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCE.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRECISION, 2);
	GSF_LE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD8.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFRESHALL, 2);
	GSF_LE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D5E.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BOOKBOOL, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	write_fonts (bp, ewb);
	write_formats (bp, ewb);
	write_xf (bp, ewb);
	write_palette (bp, ewb);

	for (i = 0; i < ewb->sheets->len; i++) {
		s = g_ptr_array_index (ewb->sheets, i);
	        s->boundsheetPos = biff_boundsheet_write_first (bp,
			MS_BIFF_TYPE_Worksheet,
			s->gnum_sheet->name_unquoted);
	}

	ms_excel_write_EOF (bp);

	workbook_io_progress_set (ewb->io_context, ewb->gnum_wb, WB_PROGRESS_CELLS,
	                          N_ELEMENTS_BETWEEN_PROGRESS_UPDATES);
	for (i = 0; i < ewb->sheets->len; i++)
		excel_write_sheet (ewb, g_ptr_array_index (ewb->sheets, i));
	io_progress_unset (ewb->io_context);

	/* Finalise Workbook stuff */
	for (i = 0; i < ewb->sheets->len; i++) {
		ExcelSheet *s = g_ptr_array_index (ewb->sheets, i);
		biff_boundsheet_write_last (bp->output, s->boundsheetPos,
					    s->streamPos);
	}
	/* End Finalised workbook */
	gsf_iconv_close (current_workbook_iconv);
	current_workbook_iconv = NULL;
}

/*
 * excel_check_write
 * @context  Command context
 * @filename File name
 *
 * Check if we are able to save to the file. Return FALSE if we do not
 * have access or we would lose data.
 *
 * TODO: Ask before continuing if there are minor problems, like
 * losing style information.
 */
ExcelWriteState *
excel_write_init_v7 (IOContext *context, WorkbookView *gwb_view)
{
	ExcelWriteState *ewb = g_new (ExcelWriteState, 1);
	GList    *sheets, *ptr;

	g_return_val_if_fail (ewb != NULL, NULL);

	ewb->bp   	  = NULL;
	ewb->io_context   = context;
	ewb->gnum_wb      = wb_view_workbook (gwb_view);
	ewb->gnum_wb_view = gwb_view;
	ewb->sheets	  = g_ptr_array_new ();
	ewb->names	  = g_ptr_array_new ();
	ewb->externnames  = g_ptr_array_new ();
	ewb->function_map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, g_free);
	fonts_init (ewb);
	formats_init (ewb);
	palette_init (ewb);
	xf_init (ewb);

	sheets = workbook_sheets (ewb->gnum_wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		ExcelSheet *esheet = excel_sheet_new (ewb, ptr->data, MS_BIFF_V7);
		if (esheet != NULL)
			g_ptr_array_add (ewb->sheets, esheet);
	excel_write_prep_expressions (ewb);

	}
	g_list_free (sheets);

	if (pre_pass (ewb) != 0) {
		excel_write_state_free (ewb);
		return NULL;
	}

	return ewb;
}

void
excel_write_v7 (ExcelWriteState *ewb, GsfOutfile *outfile)
{
	GsfOutput   *content;

	g_return_if_fail (outfile != NULL);

	content = gsf_outfile_new_child (outfile, "Book", FALSE);
	if (content != NULL) {
		ewb->bp = ms_biff_put_new (content, MS_BIFF_V7);
		write_workbook_v7 (ewb, ewb->bp);
	} else
		gnumeric_error_save (COMMAND_CONTEXT (ewb->io_context),
			_("Couldn't open stream for writing\n"));
	excel_write_state_free (ewb);
}
