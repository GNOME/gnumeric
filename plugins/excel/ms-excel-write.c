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
#include <string.h>
#include "ms-formula-write.h"
#include "boot.h"
#include "ms-biff.h"
#include "excel.h"
#include "ranges.h"
#include "sheet-filter.h"
#include "ms-excel-write.h"
#include "ms-excel-xf.h"
#include "ms-escher.h"
#include "ms-obj.h"

#include <format.h>
#include <position.h>
#include <style-color.h>
#include <cell.h>
#include <sheet-view.h>
#include <sheet-object.h>
#include <sheet-object-cell-comment.h>
#include <application.h>
#include <style.h>
#include <validation.h>
#include <input-msg.h>
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

#include <math.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_write_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define N_ELEMENTS_BETWEEN_PROGRESS_UPDATES   20

static guint style_color_to_rgb888 (StyleColor const *c);
static gint  palette_get_index (ExcelWriteState *ewb, guint c);

/**
 * excel_write_string_len :
 * @str : The utf8 encoded string in question
 * @bytes :
 *
 * Returns the size of the string in _characters_ and stores the number of
 * bytes in @bytes.
 **/
unsigned
excel_write_string_len (guint8 const *str, unsigned *bytes)
{
	guint8 const *p = str;
	unsigned i = 0;

	g_return_val_if_fail (str != NULL, 0);

	for (; *p ; i++)
		p = g_utf8_next_char (p);

	if (bytes != NULL)
		*bytes = p - str;
	return i;
}

/**
 * excel_write_string :
 * @bp :
 * @txt :
 * @flags :
 *
 * The number of bytes used to write the len, header, and text
 **/
unsigned
excel_write_string (BiffPut *bp, guint8 const *txt,
		    WriteStringFlags flags)
{
	unsigned byte_len, out_bytes, offset;
	unsigned char_len = excel_write_string_len (txt, &byte_len);
	guint8 const *in_bytes = txt;
	char *tmp;

	/* before biff8 all lengths were in bytes */
	if (bp->version < MS_BIFF_V8)
		flags |= STR_LEN_IN_BYTES;

	if (char_len != byte_len) {
		/* TODO : think about what to do with LEN_IN_BYTES */
		if ((flags & STR_LENGTH_MASK) == STR_ONE_BYTE_LENGTH) {
			if (char_len > 0xff)
				char_len = 0xff;
		}
		out_bytes = char_len * 2;

		if ((out_bytes + 3) > bp->buf_len) {
			bp->buf_len = ((char_len >> 2) + 1) << 2;
			bp->buf = g_realloc (bp->buf, bp->buf_len);
		}

		offset = (flags & STR_LENGTH_MASK);
		if (bp->version >= MS_BIFF_V8 && !(flags & STR_SUPPRESS_HEADER))
			bp->buf [offset++] = '\1';	/* flag as unicode */

		/* who cares about the extra couple of bytes */
		out_bytes = bp->buf_len - 3;

		tmp = bp->buf + offset;
		g_iconv (bp->convert, (char **)&in_bytes, &byte_len, &tmp, &out_bytes);
		out_bytes = tmp - bp->buf;

		switch (flags & STR_LENGTH_MASK) {
		default:
		case STR_NO_LENGTH:
			if (byte_len != 0)
				g_warning (_("This is somewhat corrupt.\n"
					     "We already wrote a length for a string that is being truncated due to encoding problems."));
			break;
		case STR_ONE_BYTE_LENGTH:
			if (flags & STR_LEN_IN_BYTES) {
				GSF_LE_SET_GUINT8 (bp->buf, out_bytes-offset);
				break;
			}
			if (byte_len > 0)
				char_len = g_utf8_pointer_to_offset (txt, in_bytes);
			GSF_LE_SET_GUINT8 (bp->buf, char_len);
			break;
		case STR_TWO_BYTE_LENGTH:
			if (flags & STR_LEN_IN_BYTES) {
				GSF_LE_SET_GUINT16 (bp->buf, out_bytes-offset);
				break;
			}
			if (byte_len > 0)
				char_len = g_utf8_pointer_to_offset (txt, in_bytes);
			GSF_LE_SET_GUINT16 (bp->buf, char_len);
			break;
		}
		ms_biff_put_var_write (bp, bp->buf, out_bytes);
	} else {
		/* char_len == byte_len here, so just use char_len */
		tmp = bp->buf;
		switch (flags & STR_LENGTH_MASK) {
		default:
		case STR_NO_LENGTH:		break;
		case STR_ONE_BYTE_LENGTH:
			*tmp++ = (char_len > 255) ? 255 : char_len;
			break;
		case STR_TWO_BYTE_LENGTH:
			GSF_LE_SET_GUINT16 (tmp, char_len);
			tmp += 2;
			break;
		}
		if (bp->version >= MS_BIFF_V8 && !(flags & STR_SUPPRESS_HEADER))
			*tmp++ = 0;	/* flag as not unicode */
		ms_biff_put_var_write (bp, bp->buf, tmp - bp->buf);
		ms_biff_put_var_write (bp, txt, char_len);
		out_bytes = char_len + tmp - bp->buf;
	}

	return out_bytes;
}

static unsigned
excel_write_BOF (BiffPut *bp, MsBiffFileType type)
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
		if (bp->version == MS_BIFF_V8)
			GSF_LE_SET_GUINT16 (data, 0x0600);
		else
			GSF_LE_SET_GUINT16 (data, 0x0500);
		break;
	default:
		g_warning ("Unknown version.");
		break;
	}

	switch (type) {
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
		GSF_LE_SET_GUINT16 (data+4, 0x239f);
		GSF_LE_SET_GUINT16 (data+6, 0x07cd);
		GSF_LE_SET_GUINT32 (data+ 8, 0x000040c1);
		GSF_LE_SET_GUINT16 (data+12, 0x00000106);
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
excel_write_EOF (BiffPut *bp)
{
	ms_biff_put_len_next (bp, BIFF_EOF, 0);
	ms_biff_put_commit (bp);
}

static double
points_to_inches (double pts)
{
	return pts / 72.0;
}


/* See: S59DE3.HTM */
static void
excel_write_SETUP (BiffPut *bp, ExcelWriteSheet *esheet)
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
	header = points_to_inches (header);
	footer = points_to_inches (footer);

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
excel_write_externsheets_v7 (ExcelWriteState *ewb, ExcelWriteSheet *container)
{
	/* 2 byte expression #REF! */
	static guint8 const expr_ref []   = { 0x02, 0, 0x1c, 0x17 };
	static guint8 const zeros []	  = { 0, 0, 0, 0, 0 ,0 };
	static guint8 const magic_addin[] = { 0x01, 0x3a };
	static guint8 const magic_self[]  = { 0x01, 0x4 };
	unsigned i, num_sheets = ewb->sheets->len;
	guint8 *data;
	GnmFunc *func;

	data = ms_biff_put_len_next (ewb->bp, BIFF_EXTERNCOUNT, 2);
	GSF_LE_SET_GUINT16 (data, num_sheets + ((container == NULL) ? 2 : 1));
	ms_biff_put_commit (ewb->bp);

	for (i = 0; i < num_sheets; i++) {
		ExcelWriteSheet const *esheet = g_ptr_array_index (ewb->sheets, i);
		unsigned len;
		guint8 data[2];

		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
		len = excel_write_string_len (
				esheet->gnum_sheet->name_unquoted, NULL);

		GSF_LE_SET_GUINT8 (data, len);
		GSF_LE_SET_GUINT8 (data + 1, 3); /* undocumented */
		ms_biff_put_var_write (ewb->bp, data, 2);
		excel_write_string (ewb->bp,
			esheet->gnum_sheet->name_unquoted,
			STR_NO_LENGTH);
		ms_biff_put_commit (ewb->bp);
	}

	/* Add magic externsheets for addin functions and self refs */
	ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
	ms_biff_put_var_write (ewb->bp, magic_addin, sizeof magic_addin);
	ms_biff_put_commit (ewb->bp);

	for (i = 0; i < ewb->externnames->len ; i++) {
		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNNAME);
		ms_biff_put_var_write (ewb->bp, zeros, 6);

		/* write the name and the 1 byte length */
		func = g_ptr_array_index (ewb->externnames, i);
		excel_write_string (ewb->bp, func->name,
			STR_ONE_BYTE_LENGTH);

		ms_biff_put_var_write (ewb->bp, expr_ref, sizeof (expr_ref));
		ms_biff_put_commit (ewb->bp);
	}
	if (container == NULL) {
		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
		ms_biff_put_var_write (ewb->bp, magic_self, sizeof magic_self);
		ms_biff_put_commit (ewb->bp);
	}
}

static void
cb_write_sheet_pairs (ExcelSheetPair *sp, gconstpointer dummy, ExcelWriteState *ewb)
{
	guint8 data[6];

	GSF_LE_SET_GUINT16 (data + 0, 1);
	GSF_LE_SET_GUINT16 (data + 2, sp->a->index_in_wb);
	GSF_LE_SET_GUINT16 (data + 4, sp->b->index_in_wb);
	ms_biff_put_var_write (ewb->bp, data, 6);

	sp->idx_a = ewb->tmp_counter++;
}

static void
excel_write_externsheets_v8 (ExcelWriteState *ewb)
{
	static guint8 const expr_ref []   = { 0x02, 0, 0x1c, 0x17 };
	static guint8 const zeros []	  = { 0, 0, 0, 0, 0 ,0 };
	static guint8 const magic_addin[] = { 0x01, 0x00, 0x01, 0x3a };
	static guint8 const magic_self[]  = { 0x03, 0x00, 0x01, 0x4 };
	unsigned i;
	guint8 data [6];
	GnmFunc *func;

	ms_biff_put_var_next (ewb->bp, BIFF_SUPBOOK);
	ms_biff_put_var_write (ewb->bp, magic_addin, sizeof (magic_addin));
	ms_biff_put_commit (ewb->bp);

	for (i = 0; i < ewb->externnames->len ; i++) {
		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNNAME);
		ms_biff_put_var_write (ewb->bp, zeros, 6);

		/* write the name and the 1 byte length */
		func = g_ptr_array_index (ewb->externnames, i);
		excel_write_string (ewb->bp, func->name, STR_ONE_BYTE_LENGTH);
		ms_biff_put_var_write (ewb->bp, expr_ref, sizeof (expr_ref));
		ms_biff_put_commit (ewb->bp);
	}

	ms_biff_put_var_next (ewb->bp, BIFF_SUPBOOK);
	ms_biff_put_var_write (ewb->bp, magic_self, sizeof (magic_self));
	ms_biff_put_commit (ewb->bp);

	i = g_hash_table_size (ewb->sheet_pairs) + 1 /* for the extennames */;
	/* Now do the EXTERNSHEET */
	ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
	GSF_LE_SET_GUINT16 (data + 0, i);
	ms_biff_put_var_write (ewb->bp, data, 2);
	GSF_LE_SET_GUINT16 (data + 0, 0x0000);
	GSF_LE_SET_GUINT16 (data + 2, 0xfffe);
	GSF_LE_SET_GUINT16 (data + 4, 0xfffe);
	ms_biff_put_var_write (ewb->bp, data, 6);

	ewb->tmp_counter = 1;
	g_hash_table_foreach (ewb->sheet_pairs,
		(GHFunc) cb_write_sheet_pairs, ewb);
	ms_biff_put_commit (ewb->bp);
}

static void
excel_write_WINDOW1 (BiffPut *bp, WorkbookView const *wb_view)
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
excel_write_WINDOW2 (BiffPut *bp, ExcelWriteSheet *esheet)
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
excel_write_PANE (BiffPut *bp, ExcelWriteSheet *esheet)
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
excel_write_MERGECELLS (BiffPut *bp, ExcelWriteSheet *esheet)
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

/****************************************************************************/

typedef struct {
	Validation  *v;
	GnmInputMsg *msg;
	GSList	    *ranges;
} ValInputPair;

static guint
vip_hash (ValInputPair const *vip)
{
	/* bogus, but who cares */
	return GPOINTER_TO_UINT (vip->v) ^ GPOINTER_TO_UINT (vip->msg);
}

static gint
vip_equal (ValInputPair const *a, ValInputPair const *b)
{
	return a->v == b->v && a->msg == b->msg;
}

static void
excel_write_DV (ValInputPair const *vip, gpointer dummy, ExcelWriteSheet *esheet)
{
	GSList *ptr;
	BiffPut *bp = esheet->ewb->bp;
	guint16 range_count;
	guint32 options;
	guint8 data[8];
	int col, row;
	Range const *r;

	ms_biff_put_var_next (bp, BIFF_DV);

	options = 0;
	if (vip->v != NULL) {
		switch (vip->v->type) {
		case VALIDATION_TYPE_ANY:		options = 0; break;
		case VALIDATION_TYPE_AS_INT:		options = 1; break;
		case VALIDATION_TYPE_AS_NUMBER:		options = 2; break;
		case VALIDATION_TYPE_IN_LIST:		options = 3; break;
		case VALIDATION_TYPE_AS_DATE:		options = 4; break;
		case VALIDATION_TYPE_AS_TIME:		options = 5; break;
		case VALIDATION_TYPE_TEXT_LENGTH:	options = 6; break;
		case VALIDATION_TYPE_CUSTOM:		options = 7; break;
		default : g_warning ("EXCEL : Unknown contraint type %d",
				     vip->v->type);
		};

		switch (vip->v->style) {
		case VALIDATION_STYLE_NONE: break;
		case VALIDATION_STYLE_STOP:	options |= (0 << 4); break;
		case VALIDATION_STYLE_WARNING:	options |= (1 << 4); break;
		case VALIDATION_STYLE_INFO:	options |= (2 << 4); break;
		default : g_warning ("EXCEL : Unknown validation style %d",
				     vip->v->style);
		};

		switch (vip->v->op) {
		case VALIDATION_OP_BETWEEN:	options |= (0 << 20); break;
		case VALIDATION_OP_NOT_BETWEEN:	options |= (1 << 20); break;
		case VALIDATION_OP_EQUAL:	options |= (2 << 20); break;
		case VALIDATION_OP_NOT_EQUAL:	options |= (3 << 20); break;
		case VALIDATION_OP_GT:		options |= (4 << 20); break;
		case VALIDATION_OP_LT:		options |= (5 << 20); break;
		case VALIDATION_OP_GTE:		options |= (6 << 20); break;
		case VALIDATION_OP_LTE:		options |= (7 << 20); break;
		default : g_warning ("EXCEL : Unknown contraint operator %d",
				     vip->v->op);
		};
		if (vip->v->allow_blank)
			options |= 0x100;
		if (vip->v->use_dropdown)
			options |= 0x200;
		if (vip->v->style != VALIDATION_STYLE_NONE)
			options |= 0x80000;
	}

	if (vip->msg != NULL)
		options |= 0x40000;

	GSF_LE_SET_GUINT32 (data, options);
	ms_biff_put_var_write (bp, data, 4);

	excel_write_string (bp,
		vip->msg ? gnm_input_msg_get_title (vip->msg) : "",
		STR_TWO_BYTE_LENGTH);
	excel_write_string (bp,
		vip->v ? vip->v->title->str : "",
		STR_TWO_BYTE_LENGTH);
	excel_write_string (bp,
		vip->msg ? gnm_input_msg_get_msg (vip->msg) : "",
		STR_TWO_BYTE_LENGTH);
	excel_write_string (bp,
		vip->v ? vip->v->msg->str : "",
		STR_TWO_BYTE_LENGTH);

	/* Things seem to parse relative to the top left of the first range */
	r = vip->ranges->data;
	col = r->start.col;
	row = r->start.row;

	GSF_LE_SET_GUINT16 (data  , 0); /* bogus len fill in later */
	GSF_LE_SET_GUINT16 (data+2, 0); /* Undocumented, use 0 for now */
	ms_biff_put_var_write (bp, data, 4);

	if (vip->v != NULL && vip->v->expr[0] != NULL) {
		unsigned pos = bp->curpos;
		guint16 len = excel_write_formula (esheet->ewb,
			vip->v->expr[0], esheet->gnum_sheet, col, row, TRUE);
		unsigned end_pos = bp->curpos;
		ms_biff_put_var_seekto (bp, pos-4);
		GSF_LE_SET_GUINT16 (data, len);
		ms_biff_put_var_write (bp, data, 2);
		ms_biff_put_var_seekto (bp, end_pos);
	}

	GSF_LE_SET_GUINT16 (data  , 0); /* bogus len fill in later */
	GSF_LE_SET_GUINT16 (data+2, 0); /* Undocumented, use 0 for now */
	ms_biff_put_var_write (bp, data, 4);
	if (vip->v != NULL && vip->v->expr[1] != NULL) {
		unsigned pos = bp->curpos;
		guint16 len = excel_write_formula (esheet->ewb,
			vip->v->expr[1], esheet->gnum_sheet, col, row, TRUE);
		unsigned end_pos = bp->curpos;
		ms_biff_put_var_seekto (bp, pos-4);
		GSF_LE_SET_GUINT16 (data, len);
		ms_biff_put_var_write (bp, data, 2);
		ms_biff_put_var_seekto (bp, end_pos);
	}

	range_count = g_slist_length (vip->ranges);
	GSF_LE_SET_GUINT16 (data, range_count);
	ms_biff_put_var_write (bp, data, 2);
	for (ptr = vip->ranges; ptr != NULL ; ptr = ptr->next) {
		Range const *r = ptr->data;
		GSF_LE_SET_GUINT16 (data+0, r->start.row);
		GSF_LE_SET_GUINT16 (data+2, r->end.row >= MsBiffMaxRowsV8 ? (MsBiffMaxRowsV8-1) : r->end.row);
		GSF_LE_SET_GUINT16 (data+4, r->start.col);
		GSF_LE_SET_GUINT16 (data+6, r->end.col >= 256 ? 255 : r->end.col);
		ms_biff_put_var_write (bp, data, 8);
	}
	ms_biff_put_commit (bp);

	g_slist_free (vip->ranges);
}

static void
excel_write_DVAL (BiffPut *bp, ExcelWriteSheet *esheet)
{
	StyleList *ptr;
	StyleRegion const *sr;
	GHashTable *group;
	guint8 *data;
	unsigned i;
	ValInputPair key, *tmp;

	ptr = esheet->validations;
	if (ptr == NULL)
		return;

	/* We store input msg and validation as distinct items, XL merges them
	 * find the pairs, and the regions that use them */
	group = g_hash_table_new_full ((GHashFunc)&vip_hash,
				       (GCompareFunc)&vip_equal, g_free, NULL);
	for (; ptr != NULL ; ptr = ptr->next) {
		sr = ptr->data;

		/* Clip here to avoid creating a DV record if there are no regions */
		if (sr->range.start.col >= esheet->max_col ||
		    sr->range.start.row >= esheet->max_row)
			continue;

		key.v   = mstyle_get_validation (sr->style);
		key.msg = mstyle_get_input_msg (sr->style);
		tmp = g_hash_table_lookup (group, &key);
		if (tmp == NULL) {
			tmp = g_new (ValInputPair, 1);
			tmp->v = key.v;
			tmp->msg = key.msg;
			tmp->ranges = NULL;
			g_hash_table_insert (group, tmp, tmp);
		}
		tmp->ranges = g_slist_prepend (tmp->ranges, (gpointer)&sr->range);
	}

	i = g_hash_table_size (group);
	data = ms_biff_put_len_next (bp, BIFF_DVAL, 18);
	GSF_LE_SET_GUINT16 (data +  0, 0); /* !cached, for mystery 2 bytes ? */
	GSF_LE_SET_GUINT32 (data +  2, 0); /* input X coord */
	GSF_LE_SET_GUINT32 (data +  6, 0); /* input Y coord */
	/* lie and say there is no drop down, this will require the user to
	 * move off the cell and back on to see it I think */
	GSF_LE_SET_GUINT32 (data +  10, 0xffffffff);
	GSF_LE_SET_GUINT32 (data +  14, i);	/* how many validations */
	ms_biff_put_commit (bp);

	g_hash_table_foreach (group, (GHFunc) excel_write_DV, esheet);
	g_hash_table_destroy (group);
	style_list_free (esheet->validations);
	esheet->validations = NULL;
}

/* Look for sheet references in validation expressions */
static void
excel_write_prep_validations (ExcelWriteSheet *esheet)
{
	StyleList *ptr = esheet->validations;
	StyleRegion const *sr;
	Validation  const *v;

	for (; ptr != NULL ; ptr = ptr->next) {
		sr = ptr->data;
		v  = mstyle_get_validation (sr->style);
		if (v->expr[0] != NULL)
			excel_write_prep_expr (esheet->ewb, v->expr [0]);
		if (v->expr [1] != NULL)
			excel_write_prep_expr (esheet->ewb, v->expr [1]);
	}
}

static int
excel_write_builtin_name (char const *ptr, MsBiffVersion version)
{
	static char const *builtins [] = {
		"Consolidate_Area",	"Auto_Open",
		"Auto_Close",		"Extract",
		"Database",		"Criteria",
		"Print_Area",		"Print_Titles",
		"Recorder",		"Data_Form",
		"Auto_Activate",	"Auto_Deactivate",
		"Sheet_Title",		"_FilterDatabase"
	};
	int i = G_N_ELEMENTS (builtins);

	/* _FilterDatabase does not seem to be in 95 */
	if (version < MS_BIFF_V8)
		i--;
	while (i-- > 0)
		if (!strcmp (builtins[i], ptr))
			return i;
	return -1;
}

static void
cb_enumerate_names (gpointer key, GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	ewb->tmp_counter++; /* pre increment to avoid 0 */
	g_hash_table_insert (ewb->names, (gpointer)nexpr,
		GUINT_TO_POINTER (ewb->tmp_counter));
}

static void
excel_write_NAME (G_GNUC_UNUSED gpointer key,
		  GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	guint8 data [16];
	guint16 flags = 0;
	unsigned name_len;
	char const *name;
	int builtin_index;

	g_return_if_fail (nexpr != NULL);

	ms_biff_put_var_next (ewb->bp, BIFF_NAME);
	memset (data, 0, sizeof data);

	name = nexpr->name->str;
	if (nexpr->pos.sheet != NULL) { /* sheet local */
		GSF_LE_SET_GUINT16 (data + 8,
			nexpr->pos.sheet->index_in_wb + 1);
		GSF_LE_SET_GUINT16 (data + 6,
			nexpr->pos.sheet->index_in_wb + 1);
	}

	builtin_index = excel_write_builtin_name (name, ewb->bp->version);
	if (nexpr->is_hidden)
		flags |= 0x01;
	if (builtin_index >= 0)
		flags |= 0x20;
	GSF_LE_SET_GUINT16 (data + 0, flags);
	if (builtin_index >= 0) {
		GSF_LE_SET_GUINT8  (data + 3, 1);    /* name_len */
		if (ewb->bp->version >= MS_BIFF_V8) {
			GSF_LE_SET_GUINT8  (data + 15, builtin_index);
			ms_biff_put_var_write (ewb->bp, data, 16);
		} else {
			GSF_LE_SET_GUINT8  (data + 14, builtin_index);
			ms_biff_put_var_write (ewb->bp, data, 15);
		}
	} else {
		excel_write_string_len (name, &name_len);
		GSF_LE_SET_GUINT8 (data + 3, name_len); /* name_len */
		ms_biff_put_var_write (ewb->bp, data, 14);
		excel_write_string (ewb->bp, name, STR_NO_LENGTH);
	}

	if (!expr_name_is_placeholder (nexpr)) {
		guint16 expr_len = excel_write_formula (ewb, nexpr->expr,
							nexpr->pos.sheet, 0, 0, TRUE);
		ms_biff_put_var_seekto (ewb->bp, 4);
		GSF_LE_SET_GUINT16 (data, expr_len);
		ms_biff_put_var_write (ewb->bp, data, 2);
	}
	ms_biff_put_commit (ewb->bp);
}

int
excel_write_get_externsheet_idx (ExcelWriteState *ewb,
				 Sheet *sheeta,
				 Sheet *sheetb)
{
	ExcelSheetPair key, *sp;
	key.a = sheeta;
	key.b = sheetb ? sheetb : sheeta;

	sp = g_hash_table_lookup (ewb->sheet_pairs, &key);

	g_return_val_if_fail (sp != NULL, 0);

	return sp->idx_a;
}

/**
 * Returns stream position of start.
 * See: S59D61.HTM
 **/
static guint32
excel_write_BOUNDSHEET (BiffPut *bp, MsBiffFileType type,
			char const *name)
{
	guint32 pos;
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
	excel_write_string (bp, name, STR_ONE_BYTE_LENGTH);
	ms_biff_put_commit (bp);
	return pos;
}

/**
 *  Update a previously written record with the correct
 * stream position.
 **/
static void
excel_fix_BOUNDSHEET (GsfOutput *output, guint32 pos, unsigned streamPos)
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

	idx = two_way_table_key_to_idx (ewb->pal.two_way_table, GUINT_TO_POINTER (c));
	if (idx >= EXCEL_DEF_PAL_LEN) {
		g_warning ("We lost colour #%d (%x), converting it to black\n", idx, c);
		return PALETTE_BLACK;
	}
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
				d (2, fprintf (stderr, "Custom color %d (0x%x)"
						" moved to unused index %d\n",
					      i, color, j););
				two_way_table_move (twt, j, i);
				upper_limit = j;
				ewb->pal.entry_in_use[j] = TRUE;
				break;
			}
		}

		if (j <= 1) {
			g_warning ("uh oh, we're going to lose a colour");
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
	static char buf[96];
	guint nused;

	nused = g_snprintf (buf, sizeof buf, "%s, %g", f->font_name, f->size_pts);

	if (nused < sizeof buf && f->is_bold)
		nused += snprintf (buf + nused, sizeof buf - nused, ", %s",
				   "bold");
	if (nused < sizeof buf && f->is_italic)
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
	f->font_name	 = mstyle_get_font_name   (st);
	f->size_pts	 = mstyle_get_font_size   (st);
	f->is_bold	 = mstyle_get_font_bold   (st);
	f->is_italic	 = mstyle_get_font_uline  (st);
	f->underline     = mstyle_get_font_uline  (st);
	f->strikethrough = mstyle_get_font_strike (st);
	c = mstyle_get_color (st, MSTYLE_COLOR_FORE);
	f->color = style_color_to_rgb888 (c);
	f->is_auto = c->is_auto;

	return f;
}

static guint
excel_font_hash (gconstpointer f)
{
	guint res = 0;
	ExcelFont *font = (ExcelFont *) f;

	if (f)
		res = (int)(font->size_pts + g_str_hash (font->font_name))
			^ font->color
			^ font->is_auto
			^ (font->underline << 1)
			^ (font->strikethrough << 2);

	return res;
}
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
		res = !strcmp (fa->font_name, fb->font_name)
			&& (fa->size_pts	== fb->size_pts)
			&& (fa->is_bold		== fb->is_bold)
			&& (fa->is_italic	== fb->is_italic)
			&& (fa->color		== fb->color)
			&& (fa->is_auto		== fb->is_auto)
			&& (fa->underline	== fb->underline)
			&& (fa->strikethrough	== fb->strikethrough);
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
		(GDestroyNotify) g_free);
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
	} else
		g_free (f);
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
 * excel_write_FONT
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
excel_write_FONT (ExcelWriteState *ewb, ExcelFont const *f)
{
	guint8 data[64];
	guint32 size_pts  = f->size_pts * 20;
	guint16 grbit = 0;
	guint16 color;

	guint16 boldstyle = 0x190; /* Normal boldness */
	guint16 subsuper  = 0;   /* 0: Normal, 1; Super, 2: Sub script*/
	guint8  underline = (guint8) f->underline; /* 0: None, 1: Single,
						      2: Double */
	guint8  family    = 0;
	guint8  charset   = 0;	 /* Seems OK. */
	char const *font_name = f->font_name;

	color = f->is_auto
		? PALETTE_AUTO_FONT
		: palette_get_index (ewb, f->color);
	d (1, fprintf (stderr, "Writing font %s, color idx %u\n",
		      excel_font_to_string (f), color););

	if (f->is_bold) {
		boldstyle = 0x2bc;
		grbit |= 1 << 0; /* undocumented */
	}
	if (f->is_italic)
		grbit |= 1 << 1;
	if (f->strikethrough)
		grbit |= 1 << 3;

	ms_biff_put_var_next (ewb->bp, BIFF_FONT);
	GSF_LE_SET_GUINT16 (data + 0, size_pts);
	GSF_LE_SET_GUINT16 (data + 2, grbit);
	GSF_LE_SET_GUINT16 (data + 4, color);
	GSF_LE_SET_GUINT16 (data + 6, boldstyle);
	GSF_LE_SET_GUINT16 (data + 8, subsuper);
	GSF_LE_SET_GUINT8  (data + 10, underline);
	GSF_LE_SET_GUINT8  (data + 11, family);
	GSF_LE_SET_GUINT8  (data + 12, charset);
	GSF_LE_SET_GUINT8  (data + 13, 0);
	ms_biff_put_var_write (ewb->bp, data, 14);
	excel_write_string (ewb->bp, font_name, STR_ONE_BYTE_LENGTH);
	ms_biff_put_commit (ewb->bp);
}

/**
 * excel_write_FONTs
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write all fonts to file
 **/
static void
excel_write_FONTs (BiffPut *bp, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->fonts.two_way_table;
	int nfonts = twt->idx_to_key->len;
	int i;
	ExcelFont *f;

	for (i = 0; i < nfonts; i++) {
		if (i != FONT_SKIP) {	/* FONT_SKIP is invalid, skip it */
			f = fonts_get_font (ewb, i);
			excel_write_FONT (ewb, f);
		}
	}

	if (nfonts < FONTS_MINIMUM + 1) { /* Add 1 to account for skip */
		/* Fill up until we've got the minimum number */
		f = fonts_get_font (ewb, 0);
		for (; i < FONTS_MINIMUM + 1; i++) {
			if (i != FONT_SKIP) {
				/* FONT_SKIP is invalid, skip it */
				excel_write_FONT (ewb, f);
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


static void
excel_write_FORMAT (ExcelWriteState *ewb, int fidx)
{
	guint8 data[64];
	StyleFormat const *sf = formats_get_format (ewb, fidx);

	char *format = style_format_as_XL (sf, FALSE);

	d (1, fprintf (stderr, "Writing format 0x%x: %s\n", fidx, format););

	/* Kludge for now ... */
	if (ewb->bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (ewb->bp, (0x400|BIFF_FORMAT));
	else
		ms_biff_put_var_next (ewb->bp, BIFF_FORMAT);

	GSF_LE_SET_GUINT16 (data, fidx);
	ms_biff_put_var_write (ewb->bp, data, 2);
	excel_write_string (ewb->bp, format, (ewb->bp->version >= MS_BIFF_V8)
		? STR_TWO_BYTE_LENGTH : STR_ONE_BYTE_LENGTH);
	ms_biff_put_commit (ewb->bp);
	g_free (format);
}

/**
 * excel_write_FORMATs
 * @bp BIFF buffer
 * @ewb workbook
 *
 * Write all formats to file.
 * Although we do, the formats apparently don't have to be written out in order
 **/
static void
excel_write_FORMATs (ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->formats.two_way_table;
	guint nformats = twt->idx_to_key->len;
	int magic_num [] = { 5, 6, 7, 8, 0x2a, 0x29, 0x2c, 0x2b };
	guint i;

	/* The built-in fonts which get localized */
	for (i = 0; i < sizeof magic_num / sizeof magic_num[0]; i++)
		excel_write_FORMAT (ewb, magic_num [i]);

	/* The custom fonts */
	for (i = EXCEL_BUILTIN_FORMAT_LEN; i < nformats; i++)
		excel_write_FORMAT (ewb, i);
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
	ExcelWriteSheet *esheet;

	for (i = 0; i < ewb->sheets->len; i++) {
		esheet = g_ptr_array_index (ewb->sheets, i);
		sheet_style_foreach (esheet->gnum_sheet, (GHFunc)cb_accum_styles, ewb);
		for (col = 0; col < esheet->max_col; col++)
			esheet->col_xf [col] = two_way_table_key_to_idx (ewb->xf.two_way_table,
									 esheet->col_style [col]);
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

static guint
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

static guint
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

static guint
border_type_to_excel (StyleBorderType btype, MsBiffVersion ver)
{
	guint ibtype = btype;

	if (btype <= STYLE_BORDER_NONE)
		ibtype = STYLE_BORDER_NONE;

	if (ver <= MS_BIFF_V7 && btype > STYLE_BORDER_HAIR)
		ibtype = STYLE_BORDER_MEDIUM;

	return ibtype;
}

static guint
rotation_to_excel (int rotation, MsBiffVersion ver)
{
	if (ver <= MS_BIFF_V7) {
		if (rotation < 0)
			return 1;
		if (rotation == 0)
			return 0;
		if (rotation <= 45)
			return 0;
		if (rotation <= 135)
			return 2;
		if (rotation <= 225)
			return 0;
		if (rotation <= 315)
			return 2;
		return 0;
	} else {
		if (rotation < 0)
			return 0xff;
		rotation = rotation % 360;
		if (rotation <= 90)
			return rotation;
		if (rotation <= 180)
			return 180 - rotation + 90;
		if (rotation <= 270)
			return rotation - 180;
		return 360 - rotation + 90;
	}
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
static guint16
style_color_to_pal_index (StyleColor *color, ExcelWriteState *ewb,
			  StyleColor *auto_back, StyleColor *auto_font)
{
	guint16 idx;

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
	fprintf (stderr, " fill fg color idx %d, fill bg color idx %d"
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

	mstyle_dump (xfd->mstyle);
}
#endif

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
	xfd->font_idx = fonts_get_index (ewb, f);
	g_free (f);

	xfd->style_format = mstyle_get_format (st);
	xfd->format_idx   = formats_get_index (ewb, xfd->style_format);

	xfd->locked	= mstyle_get_content_locked (st);
	xfd->hidden	= mstyle_get_content_hidden (st);
	xfd->halign	= mstyle_get_align_h (st);
	xfd->valign	= mstyle_get_align_v (st);
	xfd->wrap_text	= mstyle_get_wrap_text (st);
	xfd->rotation	= mstyle_get_rotation (st);
	xfd->indent	= mstyle_get_indent (st);

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
	xfd->fill_pattern_idx = map_pattern_index_to_excel (pat);

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
		guint16 c = xfd->pat_backgnd_col;
		xfd->pat_backgnd_col = xfd->pat_foregnd_col;
		xfd->pat_foregnd_col = c;
	}

	get_xf_differences (ewb, xfd, ewb->xf.default_style);

	style_color_unref (auto_font);
	style_color_unref (auto_back);
}

static void
excel_write_XF (BiffPut *bp, ExcelWriteState *ewb, BiffXFData *xfd)
{
	guint8 data[256];
	guint16 tmp16;
	guint32 tmp32;
	int btype;
	int diag;

	memset (data, 0, sizeof (data));

	if (bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+0, xfd->font_idx);
		GSF_LE_SET_GUINT16 (data+2, xfd->format_idx);

		/*********** Byte 4&5 */
		tmp16 = 0;
		if (xfd->locked)
			tmp16 |= (1 << 0);
		if (xfd->hidden)
			tmp16 |= (1 << 1);

		tmp16 |= (0 << 2);	/* Cell style */
		/* tmp16 |= (0 << 3);	lotus123 transition */
		/* tmp16 |= (0 << 4);	style 0 == parent */
		GSF_LE_SET_GUINT16(data+4, tmp16);

		/*********** Byte 6&7 */
		tmp16  = halign_to_excel (xfd->halign) & 0x7;
		if (xfd->wrap_text)
			tmp16 |= (1 << 3);
		tmp16 |= (valign_to_excel (xfd->valign) << 4) & 0x70;
		/* tmp16 |= (0 << 7);	fjustLast from far east ? */
		tmp16 |= (rotation_to_excel (xfd->rotation, bp->version) << 8) & 0xff00;
		GSF_LE_SET_GUINT16(data+6, tmp16);

		/*********** Byte 8&9 */
		tmp16  = xfd->indent & 0xf;
		if (xfd->shrink_to_fit)
			tmp16 |= (1 << 4);
		/* tmp16 |= (0 << 5);	flag merges in MERGECELL */
		/* tmp16 |= (0 << 6);	Read order by context */
		tmp16 |= 0xFC00;		/* we store everything */
		GSF_LE_SET_GUINT16 (data+8, tmp16);

		/*********** Byte 10&11 */
		tmp16 = 0;
		btype = xfd->border_type[STYLE_LEFT];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (border_type_to_excel (btype, bp->version) & 0xf) << 0;
		btype = xfd->border_type[STYLE_RIGHT];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (border_type_to_excel (btype, bp->version) & 0xf) << 4;
		btype = xfd->border_type[STYLE_TOP];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (border_type_to_excel (btype, bp->version) & 0xf) << 8;
		btype = xfd->border_type[STYLE_BOTTOM];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (border_type_to_excel (btype, bp->version) & 0xf) << 12;
		GSF_LE_SET_GUINT16 (data+10, tmp16);

		/*********** Byte 12&13 */
		tmp16 = 0;
		btype = xfd->border_type[STYLE_LEFT];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (xfd->border_color[STYLE_LEFT] & 0x7f) << 0;
		btype = xfd->border_type[STYLE_RIGHT];
		if (btype != STYLE_BORDER_NONE)
			tmp16 |= (xfd->border_color[STYLE_RIGHT] & 0x7f) << 7;

		diag = 0;
		btype = xfd->border_type[STYLE_DIAGONAL];
		if (btype != STYLE_BORDER_NONE)
			diag |= 1;
		btype = xfd->border_type[STYLE_REV_DIAGONAL];
		if (btype != STYLE_BORDER_NONE)
			diag |= 2;

		tmp16 |= diag << 14;
		GSF_LE_SET_GUINT16 (data+12, tmp16);

		/*********** Byte 14-17 */
		tmp32 = 0;
		btype = xfd->border_type[STYLE_TOP];
		if (btype != STYLE_BORDER_NONE)
			tmp32 |= (xfd->border_color[STYLE_TOP] & 0x7f) << 0;
		btype = xfd->border_type[STYLE_BOTTOM];
		if (btype != STYLE_BORDER_NONE)
			tmp32 |= (xfd->border_color[STYLE_BOTTOM] & 0x7f) << 7;

		diag = (diag & 1) ? STYLE_DIAGONAL : ((diag & 2) ? STYLE_REV_DIAGONAL : 0);
		if (diag != 0) {
			btype = xfd->border_type [diag];
			if (btype != STYLE_BORDER_NONE) {
				tmp32 |= (xfd->border_color[diag] & 0x7f) << 14;
				tmp32 |= (border_type_to_excel (btype, bp->version) & 0xf) << 21;
			}
		}
		/* tmp32 |= 0 | << 25; reservered 0 */
		tmp32 |= (xfd->fill_pattern_idx & 0x3f) << 26;
		GSF_LE_SET_GUINT32 (data+14, tmp32);

		tmp16 = 0;
		/* Fill pattern foreground color */
		tmp16 |= xfd->pat_foregnd_col & 0x7f;
		/* Fill pattern background color */
		tmp16 |= (xfd->pat_backgnd_col << 7) & 0x3f80;
		GSF_LE_SET_GUINT16(data+18, tmp16);

		ms_biff_put_var_write (bp, data, 20);
	} else {
		GSF_LE_SET_GUINT16 (data+0, xfd->font_idx);
		GSF_LE_SET_GUINT16 (data+2, xfd->format_idx);

		tmp16 = 0x0001;
		if (xfd->hidden)
			tmp16 |= 1 << 1;
		if (xfd->locked)
			tmp16 |= 1;
		tmp16 |= (xfd->parentstyle << 4) & 0xFFF0; /* Parent style */
		GSF_LE_SET_GUINT16(data+4, tmp16);

		/* Horizontal alignment */
		tmp16  = halign_to_excel (xfd->halign) & 0x7;
		if (xfd->wrap_text)	/* Wrapping */
			tmp16 |= 1 << 3;
		/* Vertical alignment */
		tmp16 |= (valign_to_excel (xfd->valign) << 4) & 0x70;
		tmp16 |= (rotation_to_excel (xfd->rotation, bp->version) << 8)
			 & 0x300;
		tmp16 |= xfd->differences & 0xFC00; /* Difference bits */
		GSF_LE_SET_GUINT16(data+6, tmp16);

		/* Documentation is wrong - the fSxButton bit is 1 bit higher.
		 * which is consistent with biff8. */
		tmp16 = 0;
		/* Fill pattern foreground color */
		tmp16 |= xfd->pat_foregnd_col & 0x7f;
		/* Fill pattern background color */
		tmp16 |= (xfd->pat_backgnd_col << 7) & 0x3f80;
		GSF_LE_SET_GUINT16(data+8, tmp16);

		tmp16  = xfd->fill_pattern_idx & 0x3f;

		/* Borders */
		btype = xfd->border_type[STYLE_BOTTOM];
		if (btype != STYLE_BORDER_NONE) {
			tmp16 |= (border_type_to_excel (btype, bp->version) << 6)
				& 0x1c0;
			tmp16 |= (xfd->border_color[STYLE_BOTTOM] << 9)
				& 0xfe00;
		}
		GSF_LE_SET_GUINT16(data+10, tmp16);

		tmp16  = 0;
		btype = xfd->border_type[STYLE_TOP];
		if (btype != STYLE_BORDER_NONE) {
			tmp16 |= border_type_to_excel (btype, bp->version) & 0x7;
			tmp16 |= (xfd->border_color[STYLE_TOP] << 9) & 0xfe00;
		}
		tmp16 |= (border_type_to_excel (xfd->border_type[STYLE_LEFT],
					       bp->version)
			 << 3) & 0x38;
		tmp16 |= (border_type_to_excel (xfd->border_type[STYLE_RIGHT],
					       bp->version)
			 << 6) & 0x1c0;
		GSF_LE_SET_GUINT16(data+12, tmp16);

		tmp16  = 0;
		if (xfd->border_type[STYLE_LEFT] != STYLE_BORDER_NONE)
			tmp16  |= xfd->border_color[STYLE_LEFT] & 0x7f;
		if (xfd->border_type[STYLE_RIGHT] != STYLE_BORDER_NONE)
			tmp16 |= (xfd->border_color[STYLE_RIGHT] << 7) & 0x3f80;
		GSF_LE_SET_GUINT16(data+14, tmp16);

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

static void
excel_write_XFs (ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->xf.two_way_table;
	unsigned nxf = twt->idx_to_key->len;
	unsigned i;
	MStyle *st;

	/* it is more compact to just spew the default representations than
	 * to store a readable form, and generate the constant data.
	 * At some point it would be good to generate the default style in
	 * entry 0 but not crucial given that col default xf handles most of it
	 */
	static guint8 const builtin_xf_biff8 [XF_RESERVED][20] = {
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 2, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 2, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf4, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 0, 0,    0, 0,    1,    0, 0x20, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0, 0x2b, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0, 0x29, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0, 0x2c, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0, 0x2a, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 },
		{ 1, 0, 0x09, 0, 0xf5, 0xff, 0x20, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x20 }
	};

	static guint8 const builtin_xf_biff7 [XF_RESERVED][16] = {
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20,    0, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 2, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 2, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0, 0xf5, 0xff, 0x20, 0xf4, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 0, 0,    0, 0,    1,    0, 0x20,    0, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0, 0x2b, 0, 0xf5, 0xff, 0x20, 0xf8, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0, 0x29, 0, 0xf5, 0xff, 0x20, 0xf8, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0, 0x2c, 0, 0xf5, 0xff, 0x20, 0xf8, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0, 0x2a, 0, 0xf5, 0xff, 0x20, 0xf8, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 },
		{ 1, 0, 0x09, 0, 0xf5, 0xff, 0x20, 0xf8, 0xc0, 0x20, 0, 0, 0, 0, 0, 0 }
	};

	static guint8 const builtin_style[][6] = {
		{ 0x10, 0x80, 0x03, 0xff },
		{ 0x11, 0x80, 0x06, 0xff },
		{ 0x12, 0x80, 0x04, 0xff },
		{ 0x13, 0x80, 0x07, 0xff },
		{ 0x00, 0x80, 0x00, 0xff },
		{ 0x14, 0x80, 0x05, 0xff }
	};

	/* write the builtins style */
	for (i = 0; i < XF_RESERVED; i++) {
		ms_biff_put_var_next (ewb->bp, BIFF_XF);
		if (ewb->bp->version >= MS_BIFF_V8)
			ms_biff_put_var_write (ewb->bp, builtin_xf_biff8[i], 20);
		else
			ms_biff_put_var_write (ewb->bp, builtin_xf_biff7[i], 16);
		ms_biff_put_commit (ewb->bp);
	}

	/* now write our styles */
	for (; i < nxf + twt->base; i++) {
		BiffXFData xfd;
		st = xf_get_mstyle (ewb, i);
		build_xf_data (ewb, &xfd, st);
		d (3, log_xf_data (ewb, &xfd, i););
		excel_write_XF (ewb->bp, ewb, &xfd);
	}

	/* and the trailing STYLE records (not used) */
	for (i = 0; i < 6; i++) {
		ms_biff_put_var_next (ewb->bp, 0x200|BIFF_STYLE);
		ms_biff_put_var_write  (ewb->bp, builtin_style[i], 4);
		ms_biff_put_commit (ewb->bp);
	}
}

int
excel_write_map_errcode (Value const *v)
{
	switch (value_error_classify (v)) {
	case GNM_ERROR_NULL: return 0;
	case GNM_ERROR_DIV0: return 7;
	case GNM_ERROR_VALUE: return 15;
	case GNM_ERROR_REF: return 23;
	default:
	/* Map non-excel errors to #!NAME */
	case GNM_ERROR_NAME: return 29;
	case GNM_ERROR_NUM: return 36;
	case GNM_ERROR_NA: return 42;
	}
}

/**
 * excel_write_value
 * @ewb
 * @v   value
 * @col column
 * @row row
 * @xf  XF index
 *
 * Write cell value to file
 **/
static void
excel_write_value (ExcelWriteState *ewb, Value *v, guint32 col, guint32 row, guint16 xf)
{
	switch (v->type) {

	case VALUE_EMPTY: {
		guint8 *data = ms_biff_put_len_next (ewb->bp, (0x200 | BIFF_BLANK), 6);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		ms_biff_put_commit (ewb->bp);
		break;
	}
	case VALUE_BOOLEAN:
	case VALUE_ERROR: {
		guint8 *data = ms_biff_put_len_next (ewb->bp, (0x200 | BIFF_BOOLERR), 8);
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
		ms_biff_put_commit (ewb->bp);
		break;
	}
	case VALUE_INTEGER: {
		int vint = v->v_int.val;
		guint8 *data;

		d (3, fprintf (stderr, "Writing %d %d\n", vint, v->v_int.val););
		if (((vint<<2)>>2) != vint) { /* Chain to floating point then. */
			Value *vf = value_new_float (v->v_int.val);
			excel_write_value (ewb, vf, col, row, xf);
			value_release (vf);
		} else {
			data = ms_biff_put_len_next (ewb->bp, (0x200 | BIFF_RK), 10);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			/* Integers can always be represented as integers.
			 * Use RK form 2 */
			GSF_LE_SET_GUINT32 (data + 6, (vint<<2) + 2);
			ms_biff_put_commit (ewb->bp);
		}
		break;
	}
	case VALUE_FLOAT: {
		gnm_float val = v->v_float.val;
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
			excel_write_value (ewb, vi, col, row, xf);
			value_release (vi);
		} else if (ewb->bp->version >= MS_BIFF_V7) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (ewb->bp, (0x200 | BIFF_NUMBER), 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			gsf_le_set_double (data + 6, val);
			ms_biff_put_commit (ewb->bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (ewb->bp, (0x200 | BIFF_RK));
			gsf_le_set_double (data+6-4, val);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			data[6] &= 0xfc;
			ms_biff_put_var_write  (ewb->bp, data, 10); /* Yes loose it. */
			ms_biff_put_commit (ewb->bp);
		}
		break;
	}
	case VALUE_STRING:
		g_return_if_fail (v->v_str.val->str);

		/* Use LABEL */
		if (ewb->bp->version < MS_BIFF_V8) {
			guint8 data[6];

			ms_biff_put_var_next (ewb->bp, (0x200 | BIFF_LABEL));

			EX_SETXF (data, xf);
			EX_SETCOL(data, col);
			EX_SETROW(data, row);
			ms_biff_put_var_write  (ewb->bp, data, 6);
			excel_write_string (ewb->bp, v->v_str.val->str,
					    STR_TWO_BYTE_LENGTH);
			ms_biff_put_commit (ewb->bp);
		} else {
			guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_LABELSST, 10);
			gpointer indx = g_hash_table_lookup (ewb->sst.strings, v->v_str.val);
			EX_SETXF (data, xf);
			EX_SETCOL(data, col);
			EX_SETROW(data, row);
			GSF_LE_SET_GUINT32 (data+6, GPOINTER_TO_INT (indx));
			ms_biff_put_commit (ewb->bp);
		}
		break;

	default:
		fprintf (stderr, "Unhandled value type %d\n", v->type);
		break;
	}
}

static void
excel_write_FORMULA (ExcelWriteState *ewb, ExcelWriteSheet *esheet, Cell const *cell, gint16 xf)
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
		g_warning ("Unhandled value->type (%d) in excel_write_FORMULA.", v->type);
	}

	GSF_LE_SET_GUINT16 (data + 14, 0x0); /* alwaysCalc & calcOnLoad */
	GSF_LE_SET_GUINT32 (data + 16, 0x0);
	GSF_LE_SET_GUINT16 (data + 20, 0x0); /* bogus len, fill in later */
	ms_biff_put_var_write (ewb->bp, data, 22);
	len = excel_write_formula (ewb, expr, esheet->gnum_sheet,
				   col, row, FALSE); /* unshared for now */

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
					   esheet->gnum_sheet, col, row, TRUE);

		ms_biff_put_var_seekto (ewb->bp, 12);
		GSF_LE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (ewb->bp, lendat, 2);
		ms_biff_put_commit (ewb->bp);
	}

	if (string_result) {
		char const *str = value_peek_string (v);
		ms_biff_put_var_next (ewb->bp, 0x200|BIFF_STRING);
		excel_write_string (ewb->bp, str,
			STR_TWO_BYTE_LENGTH);
		ms_biff_put_commit (ewb->bp);
	}
}

#define MAX_BIFF_NOTE_CHUNK	2048

/* biff7 and earlier */
static void
excel_write_comments_biff7 (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint8 data[6];
	GSList *l, *comments;

	comments = sheet_objects_get (esheet->gnum_sheet, NULL,
				      CELL_COMMENT_TYPE);

	for (l = comments; l; l = l->next) {
		CellComment const *cc = l->data;
		Range const *pos     = sheet_object_range_get (SHEET_OBJECT (cc));
		char const  *in = cell_comment_text_get (cc);
		unsigned in_bytes, out_bytes;
		unsigned len = excel_write_string_len (in, &in_bytes);
		char *buf;

		g_return_if_fail (in != NULL);
		g_return_if_fail (pos != NULL);

		ms_biff_put_var_next (bp, BIFF_NOTE);
		GSF_LE_SET_GUINT16 (data + 0, pos->start.row);
		GSF_LE_SET_GUINT16 (data + 2, pos->start.col);
		GSF_LE_SET_GUINT16 (data + 4, len);
		ms_biff_put_var_write (bp, data, 6);

repeat:
		buf = bp->buf;
		out_bytes = MAX_BIFF_NOTE_CHUNK; /* bp::buf is always at least this big */
		g_iconv (bp->convert, (char **)&in, &in_bytes, &buf, &out_bytes);
		if (in_bytes > 0) {
			ms_biff_put_var_write (bp, bp->buf, MAX_BIFF_NOTE_CHUNK);
			ms_biff_put_commit (bp);

			ms_biff_put_var_next (bp, BIFF_NOTE);
			GSF_LE_SET_GUINT16 (data + 0, 0xffff);
			GSF_LE_SET_GUINT16 (data + 2, 0);
			GSF_LE_SET_GUINT16 (data + 4, MIN (MAX_BIFF_NOTE_CHUNK, in_bytes));
			ms_biff_put_var_write (bp, data, 6);
			goto repeat;
		} else {
			ms_biff_put_var_write (bp, bp->buf, MAX_BIFF_NOTE_CHUNK);
			ms_biff_put_commit (bp);
		}
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
write_cell (ExcelWriteState *ewb, ExcelWriteSheet *esheet, Cell const *cell, unsigned xf)
{
	d (2, {
		ParsePos tmp;
		fprintf (stderr, "Writing cell at %s '%s' = '%s', xf = 0x%x\n",
			cell_name (cell),
			(cell_has_expr (cell)
			 ?  gnm_expr_as_string (cell->base.expression,
				parse_pos_init_cell (&tmp, cell),
				gnm_expr_conventions_default)
			 : "none"),
			(cell->value ?
			 value_get_as_string (cell->value) : "empty"), xf);
	});
	if (cell_has_expr (cell))
		excel_write_FORMULA (ewb, esheet, cell, xf);
	else if (cell->value != NULL)
		excel_write_value (ewb, cell->value, cell->pos.col, cell->pos.row, xf);
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
write_mulblank (BiffPut *bp, ExcelWriteSheet *esheet, guint32 end_col, guint32 row,
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
			d (3, fprintf (stderr, " xf(%s) = 0x%x\n",
				      cell_coord_name (end_col + 1 - i, row),
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
excel_write_GUTS (BiffPut *bp, ExcelWriteSheet *esheet)
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
excel_write_DEFAULT_ROW_HEIGHT (BiffPut *bp, ExcelWriteSheet *esheet)
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
	guint8 *data = ms_biff_put_len_next (bp, op, 8);
	gsf_le_set_double (data, points_to_inches (points));

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
excel_write_DEFCOLWIDTH (BiffPut *bp, ExcelWriteSheet *esheet)
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
excel_write_COLINFO (BiffPut *bp, ExcelWriteSheet *esheet,
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
excel_write_colinfos (BiffPut *bp, ExcelWriteSheet *esheet)
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

static char const *
excel_write_DOPER (GnmFilterCondition const *cond, int i, guint8 *buf)
{
	char const *str = NULL;
	Value const *v = cond->value[i];
	int tmp;

	if (cond->op[i] == GNM_FILTER_UNUSED)
		return NULL;
	switch (cond->value[i]->type) {
	case VALUE_BOOLEAN:	buf[0] = 8;
				buf[2] = 0;
				buf[3] = v->v_bool.val ? 1 : 0;
				break;

	case VALUE_INTEGER:
		tmp = v->v_int.val;
		if (((tmp << 2) >> 2) == tmp) {
			buf[0] = 2;
			GSF_LE_SET_GUINT32 (buf + 2, (tmp << 2) | 2);
			break;
		}
		/* fall through */
	case VALUE_FLOAT:	buf[0] = 4;
		gsf_le_set_double (buf + 2, value_get_as_float (v));
		break;

	case VALUE_ERROR:	buf[0] = 8;
				buf[2] = 1;
				buf[3] = excel_write_map_errcode (v);
				break;

	case VALUE_STRING:	buf[0] = 6;
				str = v->v_str.val->str;
				buf[6] = excel_write_string_len (str, NULL);
		break;
	default :
		/* ignore arrays, ranges, empties */
		break;
	};

	switch (cond->op[0]) {
	case GNM_FILTER_OP_EQUAL:	buf[1] = 2; break;
	case GNM_FILTER_OP_GT:		buf[1] = 4; break;
	case GNM_FILTER_OP_LT:		buf[1] = 1; break;
	case GNM_FILTER_OP_GTE:		buf[1] = 6; break;
	case GNM_FILTER_OP_LTE:		buf[1] = 3; break;
	case GNM_FILTER_OP_NOT_EQUAL:	buf[1] = 5; break;
	default :
		g_warning ("how did this happen");
	};

	return str;
}

static void
excel_write_AUTOFILTERINFO (BiffPut *bp, ExcelWriteSheet *esheet)
{
	GnmFilterCondition const *cond;
	GnmFilter const *filter;
	guint8  *data, buf [24];
	unsigned count, i;
	char const *str0 = NULL, *str1 = NULL;

	if (esheet->gnum_sheet->filters == NULL)
		return;
	filter = esheet->gnum_sheet->filters->data;

	data = ms_biff_put_len_next (bp, BIFF_FILTERMODE, 0);
	ms_biff_put_commit (bp);

	/* Write the autofilter flag */
	data = ms_biff_put_len_next (bp, BIFF_AUTOFILTERINFO, 2);
	count = range_width (&filter->r);
	GSF_LE_SET_GUINT16 (data, count);
	ms_biff_put_commit (bp);

	/* the fields */
	for (i = 0; i < filter->fields->len ; i++) {
		/* filter unused or bucket filters in excel5 */
		if (NULL == (cond = gnm_filter_get_condition (filter, i)) ||
		    cond->op[0] == GNM_FILTER_UNUSED ||
		    ((cond->op[0] & GNM_FILTER_OP_TYPE_MASK) == GNM_FILTER_OP_TOP_N &&
		     bp->version < MS_BIFF_V8))
			continue;

		ms_biff_put_var_next (bp, BIFF_AUTOFILTER);
		memset (buf, 0, sizeof buf);

		switch (cond->op[0]) {
		case GNM_FILTER_OP_BLANKS:
		case GNM_FILTER_OP_NON_BLANKS:
			data[5] = (cond->op[0] == GNM_FILTER_OP_BLANKS) ? 0xC : 0xE;
			break;

		case GNM_FILTER_OP_TOP_N:
		case GNM_FILTER_OP_BOTTOM_N:
		case GNM_FILTER_OP_TOP_N_PERCENT:
		case GNM_FILTER_OP_BOTTOM_N_PERCENT: {
			guint16 flags = 0x10; /* top/bottom n */
			int count = cond->count;

			/* not really necessary but lets be paranoid */
			if (count > 500) count = 500;
			else if (count < 1) count = 1;

			flags |= (count << 7);
			if ((cond->op[0] & 1) == 0)
				flags |= 0x20;
			if ((cond->op[0] & 2) != 0)
				flags |= 0x40;
			GSF_LE_SET_GUINT16 (buf+2, flags);
			break;
		}

		default :
			str0 = excel_write_DOPER (cond, 0, buf + 4);
			str1 = excel_write_DOPER (cond, 1, buf + 14);
			GSF_LE_SET_GUINT16 (buf+2, cond->is_and ? 1 : 0);
		};

		GSF_LE_SET_GUINT16 (buf, i);
		ms_biff_put_var_write (bp, buf, sizeof buf);

		if (str0 != NULL)
			excel_write_string (bp, str0, STR_NO_LENGTH);
		if (str1 != NULL)
			excel_write_string (bp, str1, STR_NO_LENGTH);

		ms_biff_put_commit (bp);
	}
}

static void
excel_write_autofilter_names (ExcelWriteState *ewb)
{
	unsigned i;
	Sheet	*sheet;
	ExcelWriteSheet const *esheet;
	GnmFilter const *filter;
	GnmNamedExpr nexpr;

	nexpr.name = string_get ("_FilterDatabase");
	nexpr.is_hidden = TRUE;
	nexpr.is_placeholder = FALSE;
	for (i = 0; i < ewb->sheets->len; i++) {
		esheet = g_ptr_array_index (ewb->sheets, i);
		sheet = esheet->gnum_sheet;
		if (sheet->filters != NULL) {
			filter = sheet->filters->data;
			nexpr.pos.sheet = sheet;
			nexpr.expr = gnm_expr_new_constant (
				value_new_cellrange_r (sheet, &filter->r));
			excel_write_NAME (NULL, &nexpr, ewb);
			gnm_expr_unref (nexpr.expr);
		}
	}
	string_unref (nexpr.name);
}

static void
excel_write_anchor (guint8 *buf, Range const *r)
{
	GSF_LE_SET_GUINT16 (buf +  0, r->start.col);
	GSF_LE_SET_GUINT16 (buf +  4, r->start.row);
	GSF_LE_SET_GUINT16 (buf +  8, r->end.col);
	GSF_LE_SET_GUINT16 (buf + 12, r->end.row);
}

static void
excel_write_autofilter_objs (ExcelWriteSheet *esheet)
{
	static guint8 const std_obj_v7[] = {
		0, 0, 0, 0,	/* count */
		0x14, 0,	/* its a combo (XL calls it dropdown) */
		0, 0,		/* ID (we just use count) */
		0x12, 0x82,	/* autosize, locked, visible, infilter */
		0, 0,		/* start column */
		0, 0,		/* start column offset */
		0, 0,		/* start row */
		0, 0,		/* start row offset */
		1, 0,		/* end col */
		0, 0,		/* end col offset */
		1, 0,		/* end row */
		0, 0,		/* end row offset */
		0, 0,
		0, 0, 0, 0, 0, 0,

		9, 9,    1, 0, 8, 0xff,    1,    0, 0, 0,    0, 0, 0, 0,
		0, 0, 0, 0, 0x64, 0, 1,    0, 0x0a,    0, 0, 0, 0x10, 0, 1, 0,
		0, 0, 0, 0,    0, 0, 1,    0,    1,    3, 0, 0,    0, 0, 0, 0,
		0, 0, 5, 0,    0, 0, 0,    0,    2,    0, 8, 0, 0x57, 0, 0, 0,
		0, 0, 0, 0,    0, 0, 0,    0,    0,    0, 0, 0,    0, 0, 0, 0,
		0, 0, 0, 0,    0, 0, 1,    0,    1,    3, 0, 0,
			2, 0, /* 0x000a if it has active condition */
		8, 0, 0x57, 0, 0, 0
	};

	static guint8 const header_obj_v8[] = {
/* DgContainers */  0xf, 0,   2, 0xf0,	   0, 0, 0, 0,	/* fill in length */
/* Dg */	   0x10, 0,   8, 0xf0,	   8, 0, 0, 0,	3, 0, 0, 0, 2, 4, 0, 0,
/* SpgrContainer */ 0xf, 0,   3, 0xf0,	   0, 0, 0, 0,	/* fill in length */
/* SpContainer */   0xf, 0,   4, 0xf0,	0x28, 0, 0, 0,
/* Spgr */	      1, 0,   9, 0xf0,	0x10, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Sp */	      2, 0, 0xa, 0xf0,     8, 0, 0, 0,	0, 4, 0, 0, 5, 0, 0, 0
	};
	static guint8 const obj_v8[] = {
/* SpContainer */   0xf,   0,   4, 0xf0,   0x58, 0, 0, 0,
/* Sp */	   0x92, 0xc, 0xa, 0xf0,      8, 0, 0, 0,
			1,  4,  0,  0,	/* fill in spid of the form obj | 0x400 */
			0,  0xa,  0,  0,
/* OPT */	   0x53,   0, 0xb, 0xf0,   0x1e, 0, 0, 0,
			0x7f, 0, 4, 1,   4, 1, /* bool LockAgainstGrouping 127 = 0x1040104; */
			0xbf, 0, 8, 0,   8, 0, /* bool fFitTextToShape 191 = 0x80008; */
			0xbf, 1, 0, 0,   1, 0, /* bool fNoFillHitTest  447 = 0x10000; */
			0xff, 1, 0, 0,   8, 0, /* bool fNoLineDrawDash 511 = 0x80000; */
			0xbf, 3, 0, 0, 0xa, 0, /* bool fPrint 959 = 0xa0000; */
/* ClientAnchor */    0, 0, 0x10, 0xf0,   0x12, 0, 0, 0, 1,0,
			0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
/* ClientData */      0, 0, 0x11, 0xf0,  0, 0, 0, 0
	};

	guint8 *data, buf [sizeof obj_v8];
	GnmFilter const *filter;
	GnmFilterCondition const *cond;
	BiffPut *bp = esheet->ewb->bp;
	unsigned i;
	Range r;
	
	if (esheet->gnum_sheet->filters == NULL)
		return;
	filter = esheet->gnum_sheet->filters->data;
	r.end.row = 1 + (r.start.row = filter->r.start.row);

	/* write combos for the fields */
	for (i = 0; i < filter->fields->len ; i++) {
		esheet->ewb->obj_count++;
		cond = gnm_filter_get_condition (filter, i);

		r.end.col = 1 + (r.start.col = filter->r.start.col + i);
		if (bp->version >= MS_BIFF_V8) {
			ms_biff_put_var_next (bp, BIFF_MS_O_DRAWING);
			if (i == 0) {
				int n = range_width (&filter->r);
				memcpy (buf, header_obj_v8, sizeof header_obj_v8);
				GSF_LE_SET_GUINT32 (buf +  4, 72 + 96*n);
				GSF_LE_SET_GUINT32 (buf + 28, 48 + 96*n);
				ms_biff_put_var_write (bp, buf, sizeof header_obj_v8);
			}
			memcpy (buf, obj_v8, sizeof obj_v8);
			GSF_LE_SET_GUINT32 (buf + 16, 0x400 | esheet->ewb->obj_count);
			excel_write_anchor (buf + 72, &r);
			ms_biff_put_var_write (bp, buf, sizeof obj_v8);
			ms_biff_put_commit (bp);

			ms_biff_put_var_next (bp, BIFF_OBJ);
			ms_objv8_write_common (bp,
				esheet->ewb->obj_count, 0x14, TRUE);
			ms_objv8_write_scrollbar (bp);
			ms_objv8_write_listbox (bp, cond != NULL); /* acts as an end */
		} else {
			data = ms_biff_put_len_next (bp, BIFF_OBJ, sizeof std_obj_v7);
			memcpy (data, std_obj_v7, sizeof std_obj_v7);

			GSF_LE_SET_GUINT32 (data +  0, esheet->ewb->obj_count);
			GSF_LE_SET_GUINT16 (data +  6, esheet->ewb->obj_count);
			excel_write_anchor (data + 10, &r);
			if (cond != NULL)
				GSF_LE_SET_GUINT16 (data + 124, 0xa);
		}
		ms_biff_put_commit (bp);
	}
}


/* See: S59D76.HTM */
static void
excel_write_DIMENSION (BiffPut *bp, ExcelWriteSheet *esheet)
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
excel_write_WSBOOL (BiffPut *bp, ExcelWriteSheet *esheet)
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
write_sheet_head (BiffPut *bp, ExcelWriteSheet *esheet)
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
	if (bp->version < MS_BIFF_V8)
		excel_write_externsheets_v7 (esheet->ewb, esheet);
	excel_write_DEFCOLWIDTH (bp, esheet);
	excel_write_colinfos (bp, esheet);
	excel_write_AUTOFILTERINFO (bp, esheet);
	excel_write_DIMENSION (bp, esheet);
}

static void
excel_write_SCL (ExcelWriteSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (esheet->ewb->bp, BIFF_SCL, 4);
	double whole, fractional = modf (esheet->gnum_sheet->last_zoom_factor_used, &whole);
	int num, denom;

	stern_brocot (fractional, 1000, &num, &denom);
	num += whole * denom;
	d (2, fprintf (stderr, "Zoom %g == %d/%d\n",
		esheet->gnum_sheet->last_zoom_factor_used, num, denom););
	GSF_LE_SET_GUINT16 (data + 0, (guint16)num);
	GSF_LE_SET_GUINT16 (data + 2, (guint16)denom);
	ms_biff_put_commit (esheet->ewb->bp);
}

static void
excel_write_SELECTION (BiffPut *bp, ExcelWriteSheet *esheet)
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

/* See: S59DDB.HTM */
static unsigned
excel_write_ROWINFO (BiffPut *bp, ExcelWriteSheet *esheet, guint32 row, guint32 last_col)
{
	guint8 *data;
	unsigned pos;
	ColRowInfo const *ri = sheet_row_get (esheet->gnum_sheet, row);
	guint16 height;

	/* FIXME: Find default style for row. Does it have to be common to
	 * all cells, or can a cell override? Do all cells have to be
	 * blank. */
	guint16 row_xf  = 0x000f; /* Magic */
	guint16 options	= 0x100; /* undocumented magic */

	/* FIXME: set bit 12 of row_xf if thick border on top, bit 13 if thick
	 * border on bottom. */

	if (ri == NULL)
		return bp->streamPos;

	/* We don't worry about standard height. I haven't seen it
	 * indicated in any actual esheet. */
	height = (guint16) (20. * ri->size_pts);
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

/* See: S59D99.HTM */
static void
excel_sheet_write_INDEX (ExcelWriteSheet *esheet, unsigned pos,
			 GArray *dbcells)
{
	GsfOutput *output = esheet->ewb->bp->output;
	guint8  data[4];
	gsf_off_t oldpos;
	unsigned i;

	g_return_if_fail (output);
	g_return_if_fail (esheet);

	oldpos = gsf_output_tell (output);
	if (esheet->ewb->bp->version >= MS_BIFF_V8)
		gsf_output_seek (output, pos+4+16, G_SEEK_SET);
	else
		gsf_output_seek (output, pos+4+12, G_SEEK_SET);

	for (i = 0; i < dbcells->len; i++) {
		unsigned pos = g_array_index (dbcells, unsigned, i);
		GSF_LE_SET_GUINT32 (data, pos - esheet->ewb->streamPos);
		d (2, fprintf (stderr, "Writing index record"
			      " 0x%4.4x - 0x%4.4x = 0x%4.4x\n",
			      pos, esheet->ewb->streamPos,
			      pos - esheet->ewb->streamPos););
		gsf_output_write (output, 4, data);
	}

	gsf_output_seek (output, oldpos, G_SEEK_SET);
}

/**
 * excel_sheet_write_DBCELL
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
excel_sheet_write_DBCELL (ExcelWriteSheet *esheet,
			  unsigned *ri_start, unsigned *rc_start, guint32 nrows,
			  GArray *dbcells)
{
	BiffPut *bp = esheet->ewb->bp;
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

	g_array_append_val (dbcells, pos);
}

/**
 * excel_sheet_write_block
 * @esheet  sheet
 * @begin   first row no
 * @nrows   no. of rows in block.
 *
 * Write a block of rows. Returns no. of last row written.
 *
 * We do not have to write row records for empty rows which use the
 * default style. But we do not test for this yet.
 *
 * See: 'Finding records in BIFF files': S59E28.HTM *
 */
static guint32
excel_sheet_write_block (ExcelWriteSheet *esheet, guint32 begin, int nrows,
			 GArray *dbcells)
{
	ExcelWriteState *ewb = esheet->ewb;
	int max_col = esheet->max_col;
	int col, row, max_row;
	unsigned  ri_start [2]; /* Row info start */
	unsigned *rc_start;	/* Row cells start */
	guint16   xf_list [SHEET_MAX_COLS];
	Cell const *cell;
	Sheet	   *sheet = esheet->gnum_sheet;
	int	    xf;
	TwoWayTable *twt = esheet->ewb->xf.two_way_table;
	gboolean has_content = FALSE;

	if (nrows > esheet->max_row - (int) begin) /* Incomplete final block? */
		nrows = esheet->max_row - (int) begin;
	max_row = begin + nrows - 1;

	ri_start [0] = excel_write_ROWINFO (ewb->bp, esheet, begin, max_col);
	ri_start [1] = ewb->bp->streamPos;
	for (row = begin + 1; row <= max_row; row++)
		(void) excel_write_ROWINFO (ewb->bp, esheet, row, max_col);

	rc_start = g_alloca (sizeof (unsigned) * nrows);
	for (row = begin; row <= max_row; row++) {
		guint32 run_size = 0;

		/* Save start pos of 1st cell in row */
		rc_start [row - begin] = ewb->bp->streamPos;
		if (NULL == sheet_row_get (sheet, row))
			continue;
		has_content = TRUE;
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

	excel_sheet_write_DBCELL (esheet, ri_start, rc_start,
				  has_content ? nrows : 0, dbcells);

	return row - 1;
}

/* See: 'Finding records in BIFF files': S59E28.HTM */
/* and S59D99.HTM */
static void
excel_write_sheet (ExcelWriteState *ewb, ExcelWriteSheet *esheet)
{
	GArray	*dbcells;
	guint32  block_end;
	gint32	 y;
	int	 rows_in_block = ROW_BLOCK_MAX_LEN;
	unsigned index_off;

	/* No. of blocks of rows. Only correct as long as all rows
	 * _including empties_ have row info records
	 */
	guint32 nblocks = (esheet->max_row - 1) / rows_in_block + 1;

	dbcells = g_array_new (FALSE, FALSE, sizeof (unsigned));
	esheet->streamPos = excel_write_BOF (ewb->bp, MS_BIFF_TYPE_Worksheet);

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
		block_end = excel_sheet_write_block (esheet, y, rows_in_block,
						     dbcells);

	if (ewb->bp->version < MS_BIFF_V8)
		excel_write_comments_biff7 (ewb->bp, esheet);
	excel_sheet_write_INDEX (esheet, index_off, dbcells);

	excel_write_autofilter_objs (esheet);

	excel_write_WINDOW1 (ewb->bp, esheet->ewb->gnum_wb_view);
	if (excel_write_WINDOW2 (ewb->bp, esheet))
		excel_write_PANE (ewb->bp, esheet);

	excel_write_SCL (esheet);
	excel_write_SELECTION (ewb->bp, esheet);

	/* These are actually specific to >= biff8
	 * but it can't hurt to have them here
	 * things will just ignore them */
	excel_write_MERGECELLS (ewb->bp, esheet);
	excel_write_DVAL (ewb->bp, esheet);

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

	excel_write_EOF (ewb->bp);
	g_array_free (dbcells, TRUE);
}

static ExcelWriteSheet *
excel_sheet_new (ExcelWriteState *ewb, Sheet *gnum_sheet,
		 gboolean biff7, gboolean biff8)
{
	int const maxrows = biff7 ? MsBiffMaxRowsV7 : MsBiffMaxRowsV8;
	ExcelWriteSheet *esheet = g_new (ExcelWriteSheet, 1);
	Range       extent;

	g_return_val_if_fail (gnum_sheet, NULL);
	g_return_val_if_fail (ewb, NULL);

	/* Ignore spans and merges past the bound */
	extent = sheet_get_extent (gnum_sheet, FALSE);

	if (extent.end.row >= maxrows) {
		gnm_io_warning (ewb->io_context,
				_("Some content will be lost when saving as excel 95."
				  "It only supports %d rows, and this workbook has %d"),
			  maxrows, extent.end.row);
		extent.end.row = maxrows;
	}
	if (extent.end.col >= 256) {
		gnm_io_warning (ewb->io_context,
				_("Some content will be lost when saving as excel."
				  "It only supports %d rows, and this workbook has %d"),
			  256, extent.end.col);
		extent.end.col = 256;
	}

	sheet_style_get_extent (gnum_sheet, &extent, esheet->col_style);

#warning dont lose cols/rows with attributes outside the useful region
	esheet->gnum_sheet = gnum_sheet;
	esheet->streamPos  = 0x0deadbee;
	esheet->ewb        = ewb;
	/* makes it easier to refer to 1 past the end */
	esheet->max_col    = extent.end.col + 1;
	esheet->max_row    = extent.end.row + 1;
	esheet->validations= biff8
		? sheet_style_get_validation_list (gnum_sheet, NULL)
		: NULL;

	/* It is ok to have formatting out of range, we can disregard that. */
	if (esheet->max_col > 256)
		esheet->max_col = 256;
	if (esheet->max_row > maxrows)
		esheet->max_row = maxrows;

	return esheet;
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
gather_style_info (ExcelWriteState *ewb)
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

typedef struct {
	guint32 streampos;
	guint16 record_pos;
} ISSTINF;

static void
excel_write_SST (ExcelWriteState *ewb)
{
	/* According to MSDN max SST sisze is 8224 */
	GPtrArray const *strings = ewb->sst.indicies;
	BiffPut		*bp = ewb->bp;
	ISSTINF *extsst;
	guint8 *ptr, data [8224];
	unsigned i, tmp;

	if (strings->len == 0)
		return;

	extsst = g_alloca (sizeof (ISSTINF) * (1 + ((strings->len - 1) / 8)));

	ms_biff_put_var_next (bp, BIFF_SST);
	GSF_LE_SET_GUINT32 (data + 0, strings->len);
	GSF_LE_SET_GUINT32 (data + 4, strings->len);

	ptr = data + 8;
	for (i = 0; i < strings->len ; i++) {
		String const *string = g_ptr_array_index (strings, i);
		char const *str = string->str;
		unsigned char_len, byte_len;

		if (0 == (i % 8)) {
			tmp = ptr - data + /* biff header */ 4;
			extsst[i/8].record_pos = tmp;
			extsst[i/8].streampos  = bp->streamPos + tmp;
		}

		char_len = excel_write_string_len (str, &byte_len);
		if (char_len == byte_len) {
			if ((ptr-data + char_len + 3) >= (int)sizeof (data)) {
				ms_biff_put_var_write (bp, data, ptr-data);
				ms_biff_put_commit (bp);

				ms_biff_put_var_next (bp, BIFF_CONTINUE);
				ptr = data;
			}
			GSF_LE_SET_GUINT16 (ptr, char_len);
			ptr[2] = 0;	/* unicode header == 0 */
			strncpy (ptr + 3, str, char_len);
			ptr += char_len + 3;
		} else {
			unsigned out_bytes = sizeof (data) - 3;

			if ((ptr-data + 2*char_len + 3) >= (int)sizeof (data)) {
				ms_biff_put_var_write (bp, data, ptr-data);
				ms_biff_put_commit (bp);

				ms_biff_put_var_next (bp, BIFF_CONTINUE);
				ptr = data;
			}
			GSF_LE_SET_GUINT16 (ptr, char_len);
			ptr[2] = 1;	/* unicode header == 1 */

			ptr += 3;
			g_iconv (bp->convert, (char **)&str, &byte_len, (char **)&ptr, &out_bytes);
		}
	}

	ms_biff_put_var_write (bp, data, ptr-data);
	ms_biff_put_commit (bp);

	/* Write EXTSST */
	ms_biff_put_var_next (bp, BIFF_EXTSST);
	GSF_LE_SET_GUINT16 (data + 0, 8); /* seems constant */
	ms_biff_put_var_write (bp, data, 2);

	tmp = 1 + ((strings->len - 1) / 8);
	for (i = 0; i < tmp; i++) {
		GSF_LE_SET_GUINT32 (data + 0, extsst[i].streampos);
		GSF_LE_SET_GUINT16 (data + 4, extsst[i].record_pos);
		ms_biff_put_var_write (bp, data, 6);
	}
	ms_biff_put_commit (bp);
}

static void
excel_write_WRITEACCESS (BiffPut *bp)
{
	guint8   pad [112];
	unsigned len;
	gchar *utf8_name = g_locale_to_utf8 (g_get_real_name (), -1, NULL, NULL, NULL);

	if (utf8_name == NULL)
		utf8_name = g_strdup ("");

	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	if (bp->version >= MS_BIFF_V8) {
		len = excel_write_string (bp, utf8_name, STR_TWO_BYTE_LENGTH);
		memset (pad, ' ', sizeof pad);
		ms_biff_put_var_write (bp, pad, sizeof pad - len);
		ms_biff_put_commit (bp);
	} else {
		len = excel_write_string (bp, utf8_name, STR_ONE_BYTE_LENGTH);
		memset (pad, ' ', 32);
		ms_biff_put_var_write (bp, pad, 32 - len - 1);
		ms_biff_put_commit (bp);
	}
	g_free (utf8_name);
}

static void
excel_foreach_name (ExcelWriteState *ewb, GHFunc func)
{
	Workbook const *wb = ewb->gnum_wb;
	Sheet const *sheet;
	unsigned i, num_sheets = workbook_sheet_count (wb);

	if (wb->names != NULL) {
		g_hash_table_foreach (wb->names->names, func, ewb);
		g_hash_table_foreach (wb->names->placeholders, func, ewb);
	}
	for (i = 0; i < num_sheets; i++) {
		sheet = workbook_sheet_by_index (wb, i);
		if (sheet->names != NULL) {
			g_hash_table_foreach (sheet->names->names,
				func, ewb);
			g_hash_table_foreach (sheet->names->placeholders,
				func, ewb);
		}
	}
}

static void
write_workbook (ExcelWriteState *ewb)
{
	BiffPut		*bp = ewb->bp;
	ExcelWriteSheet	*s = NULL;
	guint8 *data;
	guint i;

	ewb->streamPos = excel_write_BOF (ewb->bp, MS_BIFF_TYPE_Workbook);

	ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 0);
	if (bp->version >= MS_BIFF_V8) {
		data = ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 2);
		GSF_LE_SET_GUINT16 (data, bp->codepage);
	}
	ms_biff_put_commit (bp);

	data = ms_biff_put_len_next (bp, BIFF_MMS, 2);
	GSF_LE_SET_GUINT16(data, 0);
	ms_biff_put_commit (bp);

	if (bp->version < MS_BIFF_V8) {
		ms_biff_put_len_next (bp, BIFF_TOOLBARHDR, 0);
		ms_biff_put_commit (bp);

		ms_biff_put_len_next (bp, BIFF_TOOLBAREND, 0);
		ms_biff_put_commit (bp);
	}

	ms_biff_put_len_next (bp, BIFF_INTERFACEEND, 0);
	ms_biff_put_commit (bp);

	excel_write_WRITEACCESS (ewb->bp);

	data = ms_biff_put_len_next (bp, BIFF_CODEPAGE, 2);
	GSF_LE_SET_GUINT16 (data, bp->codepage);

	ms_biff_put_commit (bp);

	if (bp->version >= MS_BIFF_V8) {
		int i, len;

		data = ms_biff_put_len_next (bp, BIFF_DSF, 2);
		GSF_LE_SET_GUINT16 (data, ewb->double_stream_file ? 1 : 0);
		ms_biff_put_commit (bp);

		ms_biff_put_len_next (bp, BIFF_XL9FILE, 0);
		ms_biff_put_commit (bp);

		/* See: S59E09.HTM */
		len = ewb->sheets->len;
		data = ms_biff_put_len_next (bp, BIFF_TABID, len * 2);
		for (i = 0; i < len; i++)
			GSF_LE_SET_GUINT16 (data + i*2, i + 1);
		ms_biff_put_commit (bp);
	}

	data = ms_biff_put_len_next (bp, BIFF_FNGROUPCOUNT, 2);
	GSF_LE_SET_GUINT16 (data, 0x0e);
	ms_biff_put_commit (bp);

	if (bp->version < MS_BIFF_V8) {
		/* write externsheets for every sheet in the workbook
		 * to make our lives easier */
		excel_write_externsheets_v7 (ewb, NULL);

		/* assign indicies to the names before we export */
		ewb->tmp_counter = ewb->externnames->len;
		excel_foreach_name (ewb, (GHFunc)&cb_enumerate_names);
		excel_foreach_name (ewb, (GHFunc)&excel_write_NAME);
		excel_write_autofilter_names (ewb);
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

	if (bp->version >= MS_BIFF_V8) {
		data = ms_biff_put_len_next (bp, BIFF_PROT4REV, 2);
		GSF_LE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
		data = ms_biff_put_len_next (bp, BIFF_PROT4REVPASS, 2);
		GSF_LE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
	}

	excel_write_WINDOW1 (bp, ewb->gnum_wb_view);

	/* See: S59D5B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BACKUP, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D95.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HIDEOBJ, 2);
	GSF_LE_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	{
		GnmDateConventions const *conv = workbook_date_conv (ewb->gnum_wb);
		data = ms_biff_put_len_next (bp, BIFF_1904, 2);
		GSF_LE_SET_GUINT16 (data, conv->use_1904 ? 1 : 0);
		ms_biff_put_commit (bp);
	}

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

	excel_write_FONTs (bp, ewb);
	excel_write_FORMATs (ewb);
	excel_write_XFs (ewb);

	if (bp->version >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_USESELFS, 2);
		GSF_LE_SET_GUINT16 (data, 0x1); /* we are language naturals */
		ms_biff_put_commit (bp);
	}
	write_palette (bp, ewb);

	for (i = 0; i < ewb->sheets->len; i++) {
		s = g_ptr_array_index (ewb->sheets, i);
	        s->boundsheetPos = excel_write_BOUNDSHEET (bp,
			MS_BIFF_TYPE_Worksheet,
			s->gnum_sheet->name_unquoted);
	}

	if (bp->version >= MS_BIFF_V8) {
		Sheet const *sheet;

		excel_write_externsheets_v8 (ewb);

		ewb->tmp_counter = 0;
		excel_foreach_name (ewb, (GHFunc)&cb_enumerate_names);
		excel_foreach_name (ewb, (GHFunc)&excel_write_NAME);
		excel_write_autofilter_names (ewb);

		/* If there are any objects in the workbook add a header */
		i = workbook_sheet_count (ewb->gnum_wb);
		while (i-- > 0) {
			sheet = workbook_sheet_by_index	(ewb->gnum_wb, i);
			if (sheet->sheet_objects != NULL)
				break;
		}
		if (i >= 0)
			excel_write_MS_O_DRAWING_GROUP (ewb->bp);

		excel_write_SST (ewb);
	}

	excel_write_EOF (bp);

	workbook_io_progress_set (ewb->io_context, ewb->gnum_wb,
	                          N_ELEMENTS_BETWEEN_PROGRESS_UPDATES);
	for (i = 0; i < ewb->sheets->len; i++)
		excel_write_sheet (ewb, g_ptr_array_index (ewb->sheets, i));
	io_progress_unset (ewb->io_context);

	/* Finalise Workbook stuff */
	for (i = 0; i < ewb->sheets->len; i++) {
		ExcelWriteSheet *s = g_ptr_array_index (ewb->sheets, i);
		excel_fix_BOUNDSHEET (bp->output, s->boundsheetPos,
				      s->streamPos);
	}
}

/****************************************************************************/

void
excel_write_v7 (ExcelWriteState *ewb, GsfOutfile *outfile)
{
	GsfOutput   *content;
	int codepage = -1;
	gpointer tmp;

	g_return_if_fail (outfile != NULL);
	g_return_if_fail (ewb != NULL);
	g_return_if_fail (ewb->bp == NULL);

	content = gsf_outfile_new_child (outfile, "Book", FALSE);
	if (content != NULL) {
		tmp = g_object_get_data (G_OBJECT (ewb->gnum_wb), "excel-codepage");
		if (tmp != NULL)
			codepage = GPOINTER_TO_INT (tmp);
		ewb->bp = ms_biff_put_new (content, MS_BIFF_V7, codepage);
		write_workbook (ewb);
		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	} else
		gnumeric_error_save (COMMAND_CONTEXT (ewb->io_context),
			_("Couldn't open stream 'Book' for writing\n"));
}

void
excel_write_v8 (ExcelWriteState *ewb, GsfOutfile *outfile)
{
	GsfOutput   *content;

	g_return_if_fail (outfile != NULL);
	g_return_if_fail (ewb != NULL);
	g_return_if_fail (ewb->bp == NULL);

	content = gsf_outfile_new_child (outfile, "Workbook", FALSE);
	if (content != NULL) {
		ewb->bp = ms_biff_put_new (content, MS_BIFF_V8, -1);
		write_workbook (ewb);
		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	} else
		gnumeric_error_save (COMMAND_CONTEXT (ewb->io_context),
			_("Couldn't open stream 'Workbook' for writing\n"));
}

/****************************************************************************/

static void
sst_collect_str (gpointer ignored, Cell const *cell, ExcelWriteState *ewb)
{
	int index;
	String *str;

	if (cell_has_expr (cell) || cell->value == NULL ||
	    cell->value->type != VALUE_STRING)
		return;

	str = cell->value->v_str.val;
	if (!g_hash_table_lookup_extended (ewb->sst.strings, str, NULL, NULL)) {
		index = ewb->sst.indicies->len;
		g_ptr_array_add (ewb->sst.indicies, str);
		g_hash_table_insert (ewb->sst.strings, str,
			GINT_TO_POINTER (index));
	}
}

static void
cb_check_names (gpointer key, GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	if (nexpr->active && !nexpr->is_placeholder)
		excel_write_prep_expr (ewb, nexpr->expr);
}

ExcelWriteState *
excel_write_state_new (IOContext *context, WorkbookView const *gwb_view,
		       gboolean biff7, gboolean biff8)
{
	ExcelWriteState *ewb = g_new (ExcelWriteState, 1);
	ExcelWriteSheet *esheet;
	Sheet		*sheet;
	int i;

	g_return_val_if_fail (ewb != NULL, NULL);

	ewb->bp   	  = NULL;
	ewb->io_context   = context;
	ewb->gnum_wb      = wb_view_workbook (gwb_view);
	ewb->gnum_wb_view = gwb_view;
	ewb->sheets	  = g_ptr_array_new ();
	ewb->names	  = g_hash_table_new (g_direct_hash, g_direct_equal);
	ewb->externnames  = g_ptr_array_new ();
	ewb->function_map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, g_free);
	ewb->sheet_pairs  = NULL;
	ewb->double_stream_file = biff7 && biff8;

	fonts_init (ewb);
	formats_init (ewb);
	palette_init (ewb);
	xf_init (ewb);

	/* look for externsheet references in */
	excel_write_prep_expressions (ewb);			/* dependents */
	WORKBOOK_FOREACH_DEPENDENT (ewb->gnum_wb, dep, 
		excel_write_prep_expr (ewb, dep->expression););
	excel_foreach_name (ewb, (GHFunc) cb_check_names);	/* names */

	for (i = 0 ; i < workbook_sheet_count (ewb->gnum_wb) ; i++) {
		sheet = workbook_sheet_by_index (ewb->gnum_wb, i);
		esheet = excel_sheet_new (ewb, sheet, biff7, biff8);
		if (esheet != NULL)
			g_ptr_array_add (ewb->sheets, esheet);
		if (esheet->validations != NULL)
			excel_write_prep_validations (esheet); /* validation */
		if (sheet->filters != NULL)
			excel_write_prep_sheet (ewb, sheet);	/* filters */
	}

	gather_style_info (ewb);

	if (biff7) {
		ewb->sst.strings  = NULL;
		ewb->sst.indicies = NULL;
	}

	if (biff8) {
		ewb->sst.strings  = g_hash_table_new (g_direct_hash, g_direct_equal);
		ewb->sst.indicies = g_ptr_array_new ();

		for (i = 0 ; i < workbook_sheet_count (ewb->gnum_wb) ; i++) {
			Sheet *sheet = workbook_sheet_by_index (ewb->gnum_wb, i);
			g_hash_table_foreach (sheet->cell_hash,
				(GHFunc) sst_collect_str, ewb);
		}
	}
	ewb->obj_count = 0;

	return ewb;
}

void
excel_write_state_free (ExcelWriteState *ewb)
{
	unsigned i;

	fonts_free   (ewb);
	formats_free (ewb);
	palette_free (ewb);
	xf_free  (ewb);

	for (i = 0; i < ewb->sheets->len; i++)
		g_free (g_ptr_array_index (ewb->sheets, i));

	g_ptr_array_free (ewb->sheets, TRUE);
	g_hash_table_destroy (ewb->names);
	g_ptr_array_free (ewb->externnames, TRUE);
	g_hash_table_destroy (ewb->function_map);
	g_hash_table_destroy (ewb->sheet_pairs);

	if (ewb->sst.strings != NULL) {
		g_hash_table_destroy (ewb->sst.strings);
		g_ptr_array_free (ewb->sst.indicies, TRUE);
	}

	g_free (ewb);
}

