/* vim: set sw=8: */
/**
 * ms-excel-write.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (mmeeks@gnu.org)
 *    Jon K Hellan  (hellan@acm.org)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2004 Michael Meeks, Jon K Hellan, Jody Goldberg
 **/

/*
 * FIXME: Check for errors and propagate upward. We've only started.
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
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
#include "ms-chart.h"
#include "formula-types.h"

#include <format.h>
#include <position.h>
#include <style-color.h>
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-object.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <gnm-so-filled.h>
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
#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-axis.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-marker.h>
#include <goffice/utils/go-units.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-msole-utils.h>

#include <math.h>
#include <zlib.h>
#include <crypt-md4.h>

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_write_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define BLIP_ID_LEN         16
#define BSE_HDR_LEN         44
#define RASTER_BLIP_HDR_LEN 25
#define VECTOR_BLIP_HDR_LEN 58

#define N_ELEMENTS_BETWEEN_PROGRESS_UPDATES   20

typedef struct {
	char const    *type;
	GByteArray    bytes;
	gint32        uncomp_len;
	gint32        header_len;
	gboolean      needs_free;
	guint8        id[BLIP_ID_LEN];
	SheetObject   *so;
} BlipInf;

typedef struct _BlipType BlipType;

struct _BlipType {
	const char *type_name;
	guint8 type;
	guint8 blip_tag[2];
	void (*handler) (ExcelWriteState *ewb, 
				BlipInf *blip,
				BlipType *bt);
};

static guint
gnm_color_to_bgr (GnmColor const *c)
{
	return ((c->color.blue & 0xff00) << 8) + (c->color.green & 0xff00) + (c->color.red >> 8);

}
static guint
go_color_to_bgr (GOColor const c)
{
	guint32 abgr;
	abgr  = UINT_RGBA_R(c);
	abgr |= UINT_RGBA_G(c) << 8;
	abgr |= UINT_RGBA_B(c) << 16;
	return abgr;
}


/**
 * excel_write_string_len :
 * @str : The utf8 encoded string in question
 * @bytes :
 *
 * Returns the size of the string in _characters_ and stores the number of
 * bytes in @bytes.
 **/
unsigned
excel_write_string_len (guint8 const *str, size_t *bytes)
{
	guint8 const *p = str;
	size_t i = 0;

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
 * @flags :
 * @txt :
 *
 * NOTE : I considered putting markup here too to be strictly correct and
 * export rich text directly.  But it was easier to just use RSTRING.
 *
 * The number of bytes used to write the len, header, and text
 **/
unsigned
excel_write_string (BiffPut *bp, WriteStringFlags flags,
		    guint8 const *txt)
{
	size_t byte_len, out_bytes, offset;
	unsigned char_len = excel_write_string_len (txt, &byte_len);
	char *in_bytes = (char *)txt; /* bloody strict-aliasing is broken */
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

unsigned
excel_write_BOF (BiffPut *bp, MsBiffFileType type)
{
	guint8 *data;
	unsigned ans;
	guint    len = 8;
	guint16  record;

	switch (bp->version) {
	case MS_BIFF_V2: record = BIFF_BOF_v0; break;
	case MS_BIFF_V3: record = BIFF_BOF_v2; break;
	case MS_BIFF_V4: record = BIFF_BOF_v4; break;

	case MS_BIFF_V8: len = 16;
	case MS_BIFF_V7: record = BIFF_BOF_v8; break;
	default:
		g_warning ("Unknown biff version '%d' requested.", bp->version);
		return 0;
	}
	data = ms_biff_put_len_next (bp, record, len);
	ans = bp->streamPos;

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

	switch (bp->version) {
	case MS_BIFF_V8:
		GSF_LE_SET_GUINT16 (data+ 0, 0x0600);		/* worksheet */
		GSF_LE_SET_GUINT16 (data+ 4, 0x2775);		/* build id == XP SP3 */
		GSF_LE_SET_GUINT16 (data+ 6, 0x07cd);		/* build year (= 1997) */
		GSF_LE_SET_GUINT32 (data+ 8, 0x000080c9);	/* flags */
		GSF_LE_SET_GUINT32 (data+12, 0x00000206);
		break;

	case MS_BIFF_V7:
		GSF_LE_SET_GUINT16 (data, 0x0500);	/* worksheet */
		/* fall through */

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

static double
points_to_inches (double pts)
{
	return pts / 72.0;
}

void
excel_write_SETUP (BiffPut *bp, ExcelWriteSheet *esheet)
{
	PrintInformation const *pi = NULL;
	double header, footer, dummy;
	guint8 * data = ms_biff_put_len_next (bp, BIFF_SETUP, 34);
	guint16 options = 0;

	if (esheet != NULL)
		pi = esheet->gnum_sheet->print_info;
	if (pi != NULL && pi->print_order == PRINT_ORDER_RIGHT_THEN_DOWN)
		options |= 0x01;
	if (pi != NULL && print_info_get_orientation (pi) == PRINT_ORIENT_VERTICAL)
		options |= 0x02;
	options |= 0x40; /* orientation is set */
	options |= 0x04;  /* mark the _invalid_ things as being invalid */
	if (pi != NULL && pi->print_black_and_white)
		options |= 0x08;
	if (pi != NULL && pi->print_as_draft)
		options |= 0x10;
	if (pi != NULL && pi->print_comments)
		options |= 0x20;

	if (NULL != pi)
		print_info_get_margins (pi, &header, &footer, &dummy, &dummy);
	else
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
excel_write_externsheets_v7 (ExcelWriteState *ewb)
{
	/* 2 byte expression #REF! */
	static guint8 const expr_ref []   = { 0x02, 0, 0x1c, 0x17 };
	static guint8 const zeros []	  = { 0, 0, 0, 0, 0 ,0 };
	static guint8 const magic_addin[] = { 0x01, 0x3a };
	static guint8 const magic_self[]  = { 0x01, 0x4 };
	unsigned i, num_sheets = ewb->sheets->len;
	GnmFunc *func;

	ms_biff_put_2byte (ewb->bp, BIFF_EXTERNCOUNT, num_sheets + 2);

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
		excel_write_string (ewb->bp, STR_NO_LENGTH,
			esheet->gnum_sheet->name_unquoted);
		ms_biff_put_commit (ewb->bp);
	}

	/* Add magic externsheets for addin functions and self refs */
	ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
	ms_biff_put_var_write (ewb->bp, magic_addin, sizeof magic_addin);
	ms_biff_put_commit (ewb->bp);

	for (i = 0; i < ewb->externnames->len ; i++) {
		ms_biff_put_var_next (ewb->bp, BIFF_EXTERNNAME_v0); /* yes v0 */
		ms_biff_put_var_write (ewb->bp, zeros, 6);

		/* write the name and the 1 byte length */
		func = g_ptr_array_index (ewb->externnames, i);
		excel_write_string (ewb->bp, STR_ONE_BYTE_LENGTH, func->name);

		ms_biff_put_var_write (ewb->bp, expr_ref, sizeof (expr_ref));
		ms_biff_put_commit (ewb->bp);
	}
	ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
	ms_biff_put_var_write (ewb->bp, magic_self, sizeof magic_self);
	ms_biff_put_commit (ewb->bp);
}

static void
cb_write_sheet_pairs (ExcelSheetPair *sp, gconstpointer dummy, ExcelWriteState *ewb)
{
	guint8 data[6];

	GSF_LE_SET_GUINT16 (data + 0, ewb->supbook_idx);
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
	guint8 data [8];
	GnmFunc *func;

	/* XL appears to get irrate if we export an addin SUPBOOK with
	 * no names.  So be extra tidy and only export it if necessary */
	if (ewb->externnames->len > 0) {
		ms_biff_put_var_next (ewb->bp, BIFF_SUPBOOK);
		ms_biff_put_var_write (ewb->bp, magic_addin, sizeof (magic_addin));
		ms_biff_put_commit (ewb->bp);

		for (i = 0; i < ewb->externnames->len ; i++) {
			ms_biff_put_var_next (ewb->bp, BIFF_EXTERNNAME_v0); /* yes v0 */
			ms_biff_put_var_write (ewb->bp, zeros, 6);

			/* write the name and the 1 byte length */
			func = g_ptr_array_index (ewb->externnames, i);
			excel_write_string (ewb->bp, STR_ONE_BYTE_LENGTH, func->name);
			ms_biff_put_var_write (ewb->bp, expr_ref, sizeof (expr_ref));
			ms_biff_put_commit (ewb->bp);
		}
		ewb->supbook_idx = 1;
	} else
		ewb->supbook_idx = 0;

	ms_biff_put_var_next (ewb->bp, BIFF_SUPBOOK);
	ms_biff_put_var_write (ewb->bp, magic_self, sizeof (magic_self));
	ms_biff_put_commit (ewb->bp);

	/* Now do the EXTERNSHEET */
	ms_biff_put_var_next (ewb->bp, BIFF_EXTERNSHEET);
	i = g_hash_table_size (ewb->sheet_pairs);

	if (ewb->externnames->len > 0) {
		GSF_LE_SET_GUINT16 (data + 0, i+1);	/* the magic self we're about to add */
		GSF_LE_SET_GUINT16 (data + 2, 0);	/* magic self */
		GSF_LE_SET_GUINT16 (data + 4, 0xfffe);
		GSF_LE_SET_GUINT16 (data + 6, 0xfffe);
		ms_biff_put_var_write (ewb->bp, data, 8);
		ewb->tmp_counter = 1;
	} else {
		GSF_LE_SET_GUINT16 (data + 0, i);
		ms_biff_put_var_write (ewb->bp, data, 2);
		ewb->tmp_counter = 0;
	}

	g_hash_table_foreach (ewb->sheet_pairs,
		(GHFunc) cb_write_sheet_pairs, ewb);
	ms_biff_put_commit (ewb->bp);
}

static void
excel_write_WINDOW1 (BiffPut *bp, WorkbookView const *wb_view)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_WINDOW1, 18);
	float hdpi = gnm_app_display_dpi_get (TRUE) / (72. * 20.);
	float vdpi = gnm_app_display_dpi_get (FALSE) / (72. * 20.);
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

/* returns TRUE if a PANE record is necessary. */
static gboolean
excel_write_WINDOW2 (BiffPut *bp, ExcelWriteSheet *esheet, SheetView *sv)
{
	/* 1	0x020 grids are the colour of the normal style */
	/* 1	0x080 display outlines if they exist */
	/* 0	0x800 (biff8 only) no page break mode*/
	guint16 options = 0x0A0;
	guint8 *data;
	GnmCellPos top_left;
	Sheet const *sheet = esheet->gnum_sheet;
	GnmColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	GnmColor *default_auto = style_color_auto_pattern ();
	guint32 biff_pat_col = 0x40;	/* default grid color index == auto */

	if (sheet->display_formulas)
		options |= 0x0001;
	if (!sheet->hide_grid)
		options |= 0x0002;
	if (!sheet->hide_col_header || !sheet->hide_row_header)
		options |= 0x0004;
	if (sv_is_frozen (sv)) {
		options |= 0x0108;
		top_left = sv->frozen_top_left;
	} else
		top_left = sv->initial_top_left;
	if (!sheet->hide_zero)
		options |= 0x0010;
#if 0
	if (sheet->rtl)
		options |= 0x0040;
#endif
	/* Grid / auto pattern color */
	if (!style_color_equal (sheet_auto, default_auto)) {
		biff_pat_col = gnm_color_to_bgr (sheet_auto);
		if (bp->version > MS_BIFF_V7)
			biff_pat_col = palette_get_index (esheet->ewb,
							  biff_pat_col);
		options &= ~0x0020;
	}
	if (sheet == wb_view_cur_sheet (esheet->ewb->gnum_wb_view))
		options |= 0x600; /* Excel ignores this and uses WINDOW1 */

	if (bp->version <= MS_BIFF_V7) {
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2_v2, 10);

		GSF_LE_SET_GUINT16 (data +  0, options);
		GSF_LE_SET_GUINT16 (data +  2, top_left.row);
		GSF_LE_SET_GUINT16 (data +  4, top_left.col);
		GSF_LE_SET_GUINT32 (data +  6, biff_pat_col);
	} else {
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2_v2, 18);

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

static void
excel_write_PANE (BiffPut *bp, ExcelWriteSheet *esheet, SheetView *sv)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_PANE, 10);
	int const frozen_height = sv->unfrozen_top_left.row -
		sv->frozen_top_left.row;
	int const frozen_width = sv->unfrozen_top_left.col -
		sv->frozen_top_left.col;
	guint16 freeze_type; /* NOTE docs lie, this is not 'active pane' */

	if (sv->unfrozen_top_left.col > 0)
		freeze_type = (sv->unfrozen_top_left.row > 0) ? 0 : 1;
	else
		freeze_type = (sv->unfrozen_top_left.row > 0) ? 2 : 3;

	GSF_LE_SET_GUINT16 (data + 0, frozen_width);
	GSF_LE_SET_GUINT16 (data + 2, frozen_height);
	GSF_LE_SET_GUINT16 (data + 4, sv->initial_top_left.row);
	GSF_LE_SET_GUINT16 (data + 6, sv->initial_top_left.col);
	GSF_LE_SET_GUINT16 (data + 8, freeze_type);

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
	guint16 len;
	int remainder = 0;
	int const max_records = (ms_biff_max_record_len (bp) - 2) / 8;

	/* Find the set of regions that we can safely export */
	for (merged = esheet->gnum_sheet->list_merged; merged != NULL ; merged = merged->next) {
		/* TODO : Add a warning entry in the log about ignoring the missing elements */
		GnmRange const *r = merged->data;
		if (r->start.row <= USHRT_MAX && r->end.row <= USHRT_MAX &&
		    r->start.col <= UCHAR_MAX && r->end.col <= UCHAR_MAX)
			remainder++;
	}

	/* Do not even write the record if there are no merged regions */
	if (remainder <= 0)
		return;

	merged = esheet->gnum_sheet->list_merged;

	for (; remainder > 0 ; remainder -= max_records) {
		len = (remainder > max_records) ? max_records : remainder;

		record = ms_biff_put_len_next (bp, BIFF_MERGECELLS, 2+8*len);
		GSF_LE_SET_GUINT16 (record, len);
		ptr = record + 2;
		for (; merged != NULL && len-- > 0 ; merged = merged->next) {
			GnmRange const *r = merged->data;
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
}

/****************************************************************************/

typedef struct {
	GnmValidation  *v;
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
	GnmRange const *r;

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
		case VALIDATION_OP_NONE:
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

	excel_write_string (bp, STR_TWO_BYTE_LENGTH,
		vip->msg ? gnm_input_msg_get_title (vip->msg) : "");
	excel_write_string (bp, STR_TWO_BYTE_LENGTH,
		(vip->v && vip->v->title) ? vip->v->title->str : "");
	excel_write_string (bp, STR_TWO_BYTE_LENGTH,
		vip->msg ? gnm_input_msg_get_msg (vip->msg) : "");
	excel_write_string (bp, STR_TWO_BYTE_LENGTH,
		(vip->v && vip->v->msg) ? vip->v->msg->str : "");

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
				vip->v->expr[0], esheet->gnum_sheet, col, row,
				EXCEL_CALLED_FROM_VALIDATION);
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
				vip->v->expr[1], esheet->gnum_sheet, col, row,
				EXCEL_CALLED_FROM_VALIDATION);
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
		GnmRange const *r = ptr->data;
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
	GnmStyleList *ptr;
	GnmStyleRegion const *sr;
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
	GnmStyleList *ptr = esheet->validations;
	GnmStyleRegion const *sr;
	GnmValidation  const *v;

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
excel_write_NAME (G_GNUC_UNUSED gpointer key,
		  GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	guint8 data [16];
	guint16 flags = 0;
	size_t name_len;
	char const *name;
	int builtin_index;

	g_return_if_fail (nexpr != NULL);

	ms_biff_put_var_next (ewb->bp, BIFF_NAME_v0); /* yes v0 */
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
		excel_write_string (ewb->bp, STR_NO_LENGTH, name);
	}

	if (!expr_name_is_placeholder (nexpr)) {
		guint16 expr_len = excel_write_formula (ewb, nexpr->expr,
				nexpr->pos.sheet, 0, 0, EXCEL_CALLED_FROM_NAME);
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

/* Returns stream position of start **/
static guint32
excel_write_BOUNDSHEET (BiffPut *bp, Sheet *sheet)
{
	guint32 pos;
	guint8 data[16];

	ms_biff_put_var_next (bp, BIFF_BOUNDSHEET);
	pos = bp->streamPos;

	GSF_LE_SET_GUINT32 (data, 0xdeadbeef); /* To be stream start pos */

	/* NOTE : MS Docs appear wrong.  It is visiblity _then_ type */
	GSF_LE_SET_GUINT8 (data+4, sheet->is_visible ? 0 : 1);

	switch (sheet->sheet_type) {
	default:
		g_warning ("unknown sheet type %d (assuming WorkSheet)", sheet->sheet_type);
		break;
	case GNM_SHEET_DATA :	GSF_LE_SET_GUINT8 (data+5, 0); break;
	case GNM_SHEET_OBJECT : GSF_LE_SET_GUINT8 (data+5, 2); break;
	case GNM_SHEET_XLM :	GSF_LE_SET_GUINT8 (data+5, 1); break;
	/* case MS_BIFF_TYPE_VBModule :	GSF_LE_SET_GUINT8 (data+5, 6); break; */
	}
	ms_biff_put_var_write (bp, data, 6);
	excel_write_string (bp, STR_ONE_BYTE_LENGTH, sheet->name_unquoted);
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
		/* Just use biff8 palette.  We're going to dump a custom
		 * palette anyway so it does not really matter */
		epe = &excel_default_palette_v8 [i];
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
gint
palette_get_index (ExcelWriteState *ewb, guint c)
{
	gint idx;

	if (c == 0)
		return PALETTE_BLACK;
	if (c == 0xffffff)
		return PALETTE_WHITE;

	idx = two_way_table_key_to_idx (ewb->pal.two_way_table, GUINT_TO_POINTER (c));
	if (idx < 0) {
		g_warning ("Unknown color (%x), converting it to black\n", c);
		return PALETTE_BLACK;
	}

	if (idx >= EXCEL_DEF_PAL_LEN) {
		g_warning ("We lost colour #%d (%x), converting it to black\n", idx, c);
		return PALETTE_BLACK;
	}
	return idx + 8;
}

static void
put_color_bgr (ExcelWriteState *ewb, guint32 bgr)
{
	TwoWayTable *twt = ewb->pal.two_way_table;
	gpointer pc = GUINT_TO_POINTER (bgr);
	gint idx = two_way_table_put (twt, pc, TRUE,
			   (AfterPutFunc) log_put_color,
			   "Found unique color %d - 0x%6.6x\n");
	if (idx >= 0 && idx < EXCEL_DEF_PAL_LEN)
		ewb->pal.entry_in_use [idx] = TRUE; /* Default entry in use */
}

static void
put_color_gnm (ExcelWriteState *ewb, GnmColor const *c)
{
	put_color_bgr (ewb, gnm_color_to_bgr (c));
}

/**
 * Add colors in mstyle to palette
 **/
static void
put_colors (GnmStyle *st, gconstpointer dummy, ExcelWriteState *ewb)
{
	int i;
	GnmBorder const *b;

	put_color_gnm (ewb, mstyle_get_color (st, MSTYLE_COLOR_FORE));
	put_color_gnm (ewb, mstyle_get_color (st, MSTYLE_COLOR_BACK));
	put_color_gnm (ewb, mstyle_get_color (st, MSTYLE_COLOR_PATTERN));

	/* Borders */
	for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
		b = mstyle_get_border (st, MSTYLE_BORDER_TOP + i);
		if (b && b->color)
			put_color_gnm (ewb, b->color);
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

static void
excel_font_free (ExcelFont *efont)
{
	/* FONT_SKIP has value == NULL */
	d (3, fprintf (stderr, "free %p", efont););
	if (efont != NULL) {
		d (3, fprintf (stderr, "freeing %s", excel_font_to_string (efont)););
		g_free (efont->font_name_copy);
		g_free (efont);
	}
}

static ExcelFont *
excel_font_new (GnmStyle const *base_style)
{
	ExcelFont *efont;
	GnmColor  *c;

	if (base_style == NULL)
		return NULL;

	efont = g_new (ExcelFont, 1);
	efont->font_name	= mstyle_get_font_name   (base_style);
	efont->font_name_copy	= NULL;
	efont->size_pts		= mstyle_get_font_size   (base_style);
	efont->is_bold		= mstyle_get_font_bold   (base_style);
	efont->is_italic	= mstyle_get_font_uline  (base_style);
	efont->underline	= mstyle_get_font_uline  (base_style);
	efont->strikethrough	= mstyle_get_font_strike (base_style);

	c = mstyle_get_color (base_style, MSTYLE_COLOR_FORE);
	efont->color = gnm_color_to_bgr (c);
	efont->is_auto = c->is_auto;

	return efont;
}

static void
excel_font_overlay_pango (ExcelFont *efont, GSList *pango)
{
	PangoColor const *c;
	PangoAttribute *attr;
	GSList *ptr;

	for (ptr = pango ; ptr != NULL ; ptr = ptr->next) {
		attr = (PangoAttribute *)(ptr->data);
		switch (attr->klass->type) {
		case PANGO_ATTR_FAMILY :
			g_free (efont->font_name_copy);
			efont->font_name = efont->font_name_copy =
				g_strdup (((PangoAttrString *)attr)->value);
			break;
		case PANGO_ATTR_SIZE : efont->size_pts	=
			(double )(((PangoAttrInt *)attr)->value) / PANGO_SCALE;
			break;
		case PANGO_ATTR_STYLE : efont->is_italic =
			((PangoAttrInt *)attr)->value == PANGO_STYLE_ITALIC;
			break;
		case PANGO_ATTR_WEIGHT : efont->is_bold	=
			((PangoAttrInt *)attr)->value >= PANGO_WEIGHT_BOLD;
			break;
		case PANGO_ATTR_STRIKETHROUGH : efont->strikethrough =
			((PangoAttrInt *)attr)->value != 0;
			break;
		case PANGO_ATTR_UNDERLINE :
			switch (((PangoAttrInt *)attr)->value) {
			case PANGO_UNDERLINE_NONE :
				efont->underline = UNDERLINE_NONE;
				break;
			case PANGO_UNDERLINE_SINGLE :
				efont->underline = UNDERLINE_SINGLE;
				break;
			case PANGO_UNDERLINE_DOUBLE :
				efont->underline = UNDERLINE_DOUBLE;
				break;
			}
			break;

		case PANGO_ATTR_FOREGROUND :
			c = &((PangoAttrColor *)attr)->color;
			efont->is_auto = FALSE;
			efont->color = ((c->blue & 0xff00) << 8) + (c->green & 0xff00) + (c->red >> 8);
			break;
		default :
			break; /* ignored */
		}
	}
	g_slist_free (pango);
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
after_put_font (ExcelFont *f, gboolean was_added, gint index, gconstpointer dummy)
{
	if (was_added) {
		d (1, fprintf (stderr, "Found unique font %d - %s\n",
			      index, excel_font_to_string (f)););
	} else
		excel_font_free (f);
}

/**
 * put_efont :
 * @efont : #ExcelFont
 * @ewb : #ExcelWriteState
 *
 * Absorbs ownership of @efont potentially freeing it.
 *
 * Returns the index of the font
 **/
static inline gint
put_efont (ExcelFont *efont, ExcelWriteState *ewb)
{
	TwoWayTable *twt = ewb->fonts.two_way_table;

	d (2, fprintf (stderr, "adding %s\n", excel_font_to_string (efont)););

	/* Occupy index FONT_SKIP with junk - Excel skips it */
	if (twt->idx_to_key->len == FONT_SKIP)
		two_way_table_put (twt, NULL, FALSE, NULL, NULL);

	return two_way_table_put (twt, efont, TRUE, (AfterPutFunc) after_put_font, NULL);
}
static void
put_style_font (GnmStyle *style, gconstpointer dummy, ExcelWriteState *ewb)
{
	put_efont (excel_font_new (style), ewb);
}

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

	ms_biff_put_var_next (ewb->bp, BIFF_FONT_v0); /* yes v0 */
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
	excel_write_string (ewb->bp, STR_ONE_BYTE_LENGTH, font_name);
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
after_put_format (GnmFormat *format, gboolean was_added, gint index,
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

static GnmFormat const *
formats_get_format (ExcelWriteState *ewb, gint idx)
{
	return two_way_table_idx_to_key (ewb->formats.two_way_table, idx);
}

static gint
formats_get_index (ExcelWriteState *ewb, GnmFormat const *format)
{
	return two_way_table_key_to_idx (ewb->formats.two_way_table, format);
}
static void
put_format (GnmStyle *mstyle, gconstpointer dummy, ExcelWriteState *ewb)
{
	GnmFormat *fmt = mstyle_get_format (mstyle);
	style_format_ref (fmt);
	two_way_table_put (ewb->formats.two_way_table,
			   (gpointer)fmt, TRUE,
			   (AfterPutFunc) after_put_format,
			   "Found unique format %d - 0x%x\n");
}

static void
excel_write_FORMAT (ExcelWriteState *ewb, int fidx)
{
	guint8 data[64];
	GnmFormat const *sf = formats_get_format (ewb, fidx);

	char *format = style_format_as_XL (sf, FALSE);

	d (1, fprintf (stderr, "Writing format 0x%x: %s\n", fidx, format););

	if (ewb->bp->version >= MS_BIFF_V7)
		ms_biff_put_var_next (ewb->bp, BIFF_FORMAT_v4);
	else
		ms_biff_put_var_next (ewb->bp, BIFF_FORMAT_v0);

	GSF_LE_SET_GUINT16 (data, fidx);
	ms_biff_put_var_write (ewb->bp, data, 2);
	excel_write_string (ewb->bp, (ewb->bp->version >= MS_BIFF_V8)
		? STR_TWO_BYTE_LENGTH : STR_ONE_BYTE_LENGTH, format);
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

	/* The built-in formats which get localized */
	for (i = 0; i < G_N_ELEMENTS (magic_num); i++)
		excel_write_FORMAT (ewb, magic_num [i]);

	/* The custom formats */
	for (i = EXCEL_BUILTIN_FORMAT_LEN; i < nformats; i++)
		excel_write_FORMAT (ewb, i);
}

/**
 * Initialize XF/GnmStyle table.
 *
 * The table records MStyles. For each GnmStyle, an XF record will be
 * written to file.
 **/
static void
xf_init (ExcelWriteState *ewb)
{
	/* Excel starts at XF_RESERVED for user defined xf */
	ewb->xf.two_way_table = two_way_table_new (mstyle_hash_XL,
		(GCompareFunc) mstyle_equal_XL, XF_RESERVED, NULL);

	/* We store the default style for the workbook on xls import, use it if
	 * it's available.  While we have a default style per sheet, we don't
	 * have one for the workbook.
	 *
	 * NOTE : This is extremely important to get right.  Columns use
	 * the font from the default style (which becomes XF 0) for sizing */
	ewb->xf.default_style = g_object_get_data (G_OBJECT (ewb->gnum_wb),
						   "xls-default-style");
	if (ewb->xf.default_style == NULL)
		ewb->xf.default_style = mstyle_new_default ();
	else
		mstyle_ref (ewb->xf.default_style);

	ewb->xf.value_fmt_styles = g_hash_table_new_full (
		g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)mstyle_unlink);
	/* Register default style, its font and format */
	two_way_table_put (ewb->xf.two_way_table, ewb->xf.default_style,
		TRUE, NULL, NULL);
	put_style_font (ewb->xf.default_style, NULL, ewb);
	put_format (ewb->xf.default_style, NULL, ewb);
}

static void
xf_free (ExcelWriteState *ewb)
{
	if (ewb->xf.two_way_table != NULL) {
		two_way_table_free (ewb->xf.two_way_table);
		ewb->xf.two_way_table = NULL;
		mstyle_unref (ewb->xf.default_style);
		ewb->xf.default_style = NULL;
		g_hash_table_destroy (ewb->xf.value_fmt_styles);
	}
}

static GnmStyle *
xf_get_mstyle (ExcelWriteState *ewb, gint idx)
{
	return two_way_table_idx_to_key (ewb->xf.two_way_table, idx);
}

static GArray *
txomarkup_new (ExcelWriteState *ewb, PangoAttrList *markup, GnmStyle *style)
{
	ExcelFont *efont;
	gint tmp[2];
	PangoAttrIterator *iter = pango_attr_list_get_iterator (markup);
	GArray *txo = g_array_sized_new (FALSE, FALSE, sizeof (int), 8);
	GSList *attrs;

	do {
		/* trim start */
		attrs = pango_attr_iterator_get_attrs (iter);
		if (txo->len == 0 && attrs == NULL)
			continue;
		
		efont = excel_font_new (style);
		excel_font_overlay_pango (efont, attrs);
		pango_attr_iterator_range (iter, tmp, NULL);
		tmp[1] = put_efont (efont, ewb);
		g_array_append_vals (txo, (gpointer)tmp, 2);
	} while (pango_attr_iterator_next (iter));
	/* trim end */
	if (txo->len > 2 && attrs == NULL)
		g_array_set_size (txo, txo->len - 2);
	pango_attr_iterator_destroy (iter);

	return txo;
}

/***************************************************************************/

static void
cb_cell_pre_pass (gpointer ignored, GnmCell const *cell, ExcelWriteState *ewb)
{
	int index;
	GnmStyle  *style;
	GnmFormat *fmt;
	GnmString *str;

	if (cell_has_expr (cell) || cell->value == NULL)
		return;

	if ((fmt = VALUE_FMT (cell->value)) != NULL) {
		style = cell_get_mstyle (cell);

		/* Collect unique fonts in rich text */
		if (cell->value->type == VALUE_STRING &&
		    style_format_is_markup (fmt)) {
			GArray *txo = txomarkup_new (ewb, 
						     fmt->markup, style);

			g_hash_table_insert (ewb->cell_markup, 
					     (gpointer)cell, txo);
			return; /* we use RSTRING, no need to add to SST */
		}

		/* XL has no notion of value format.  We need to create
		 * imaginary styles with the value format substituted into the
		 * current style.  Otherwise an entry like '10:00' gets loaded
		 * as a raw number.  */
		else if (style_format_is_general (mstyle_get_format (style))) {
			style = mstyle_copy (style);
			mstyle_set_format (style, fmt);
			g_hash_table_insert (ewb->xf.value_fmt_styles,
				(gpointer)cell,
				sheet_style_find (cell->base.sheet, style));
		}
	}

	/* Collect strings for the SST if we need them */
	if (ewb->sst.strings != NULL && cell->value->type == VALUE_STRING) {
		str = cell->value->v_str.val;
		if (!g_hash_table_lookup_extended (ewb->sst.strings, str, NULL, NULL)) {
			index = ewb->sst.indicies->len;
			g_ptr_array_add (ewb->sst.indicies, str);
			g_hash_table_insert (ewb->sst.strings, str,
				GINT_TO_POINTER (index));
		}
	}
}

static void
cb_accum_styles (GnmStyle *st, gconstpointer dummy, ExcelWriteState *ewb)
{
	two_way_table_put (ewb->xf.two_way_table, st, TRUE, NULL, NULL);
}
static void
gather_styles (ExcelWriteState *ewb)
{
	unsigned i;
	int	 col;
	ExcelWriteSheet *esheet;

	for (i = 0; i < ewb->sheets->len; i++) {
		esheet = g_ptr_array_index (ewb->sheets, i);

		g_hash_table_foreach (esheet->gnum_sheet->cell_hash,
			(GHFunc) cb_cell_pre_pass, ewb);

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
	g_return_val_if_fail (i >= 0 && i < (int)G_N_ELEMENTS (map_to_excel),
			      0);

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
style_color_to_pal_index (GnmColor *color, ExcelWriteState *ewb,
			  GnmColor *auto_back, GnmColor *auto_font)
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
		idx = palette_get_index	(ewb, gnm_color_to_bgr (color));

	return idx;
}

/**
 * get_xf_differences
 * @ewb   workbook
 * @xfd  XF data
 * @parentst parent style (Not used at present)
 *
 * Fill out map of differences to parent style
 *
 * FIXME
 * At present, we are using a fixed XF record 0, which is the parent of all
 * others. Can we use the actual default style as XF 0?
 **/
static void
get_xf_differences (ExcelWriteState *ewb, BiffXFData *xfd, GnmStyle *parentst)
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
build_xf_data (ExcelWriteState *ewb, BiffXFData *xfd, GnmStyle *st)
{
	ExcelFont *f;
	GnmBorder const *b;
	int pat;
	GnmColor *pattern_color;
	GnmColor *back_color;
	GnmColor *auto_back = style_color_auto_back ();
	GnmColor *auto_font = style_color_auto_font ();
	int i;

	memset (xfd, 0, sizeof *xfd);

	xfd->parentstyle  = XF_MAGIC;
	xfd->mstyle       = st;

	f = excel_font_new (st);
	xfd->font_idx = two_way_table_key_to_idx (ewb->fonts.two_way_table, f);
	excel_font_free (f);

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
		ms_biff_put_var_next (bp, BIFF_XF_OLD_v4);

	if (bp->version >= MS_BIFF_V8) {
		GSF_LE_SET_GUINT16 (data+0, xfd->font_idx);
		GSF_LE_SET_GUINT16 (data+2, xfd->format_idx);

		/*********** Byte 4&5 */
		tmp16 = 0;
		if (xfd->locked)
			tmp16 |= (1 << 0);
		if (xfd->hidden)
			tmp16 |= (1 << 1);

		tmp16 |= (0 << 2);	/* GnmCell style */
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
	GnmStyle *st;

	/* it is more compact to just spew the default representations than
	 * to store a readable form, and generate the constant data. */
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
excel_write_map_errcode (GnmValue const *v)
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

static void
excel_write_value (ExcelWriteState *ewb, GnmValue *v, guint32 col, guint32 row, guint16 xf)
{
	switch (v->type) {

	case VALUE_EMPTY: {
		guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_BLANK_v2, 6);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		ms_biff_put_commit (ewb->bp);
		break;
	}
	case VALUE_BOOLEAN:
	case VALUE_ERROR: {
		guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_BOOLERR_v2, 8);
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
			GnmValue *vf = value_new_float (v->v_int.val);
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
			GnmValue *vi = value_new_int (val);
			excel_write_value (ewb, vi, col, row, xf);
			value_release (vi);
		} else if (ewb->bp->version >= MS_BIFF_V7) {
			guint8 *data =ms_biff_put_len_next (ewb->bp, BIFF_NUMBER_v2, 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			gsf_le_set_double (data + 6, val);
			ms_biff_put_commit (ewb->bp);
		} else {
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

			ms_biff_put_var_next (ewb->bp, BIFF_LABEL_v2);

			EX_SETXF (data, xf);
			EX_SETCOL(data, col);
			EX_SETROW(data, row);
			ms_biff_put_var_write  (ewb->bp, data, 6);
			excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH,
				v->v_str.val->str);
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
excel_write_FORMULA (ExcelWriteState *ewb, ExcelWriteSheet *esheet, GnmCell const *cell, gint16 xf)
{
	guint8   data[22];
	guint8   lendat[2];
	guint32  len;
	gboolean string_result = FALSE;
	gint     col, row;
	GnmValue   *v;
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

	ms_biff_put_var_next (ewb->bp, BIFF_FORMULA_v0);
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

	GSF_LE_SET_GUINT16 (data + 14, /* alwaysCalc & calcOnLoad */
	        (cell->base.flags & DEPENDENT_HAS_DYNAMIC_DEPS) ? 1 : 0);

	/***  This is why XL produces a warning when exiting with files we generate
	 * and complains about them being from 'older' versions.  The numbers
	 * have no obvious pattern but I have not looked terribly hard. */
	GSF_LE_SET_GUINT32 (data + 16, 0x0);

	GSF_LE_SET_GUINT16 (data + 20, 0x0); /* bogus len, fill in later */
	ms_biff_put_var_write (ewb->bp, data, 22);
	len = excel_write_formula (ewb, expr, esheet->gnum_sheet,
				col, row, EXCEL_CALLED_FROM_CELL); /* unshared for now */

	ms_biff_put_var_seekto (ewb->bp, 20);
	GSF_LE_SET_GUINT16 (lendat, len);
	ms_biff_put_var_write (ewb->bp, lendat, 2);

	ms_biff_put_commit (ewb->bp);

	if (expr->any.oper == GNM_EXPR_OP_ARRAY &&
	    expr->array.x == 0 && expr->array.y == 0) {
		ms_biff_put_var_next (ewb->bp, BIFF_ARRAY_v2);
		GSF_LE_SET_GUINT16 (data+0, cell->pos.row);
		GSF_LE_SET_GUINT16 (data+2, cell->pos.row + expr->array.rows-1);
		GSF_LE_SET_GUINT16 (data+4, cell->pos.col);
		GSF_LE_SET_GUINT16 (data+5, cell->pos.col + expr->array.cols-1);
		GSF_LE_SET_GUINT16 (data+6, 0x0); /* alwaysCalc & calcOnLoad */
		GSF_LE_SET_GUINT32 (data+8, 0);
		GSF_LE_SET_GUINT16 (data+12, 0); /* bogus len, fill in later */
		ms_biff_put_var_write (ewb->bp, data, 14);
		len = excel_write_formula (ewb, expr->array.corner.expr,
				esheet->gnum_sheet, col, row, EXCEL_CALLED_FROM_ARRAY);

		ms_biff_put_var_seekto (ewb->bp, 12);
		GSF_LE_SET_GUINT16 (lendat, len);
		ms_biff_put_var_write (ewb->bp, lendat, 2);
		ms_biff_put_commit (ewb->bp);
	}

	if (string_result) {
		char const *str = value_peek_string (v);
		ms_biff_put_var_next (ewb->bp, BIFF_STRING_v2);
		excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH, str);
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
		GnmComment const *cc = l->data;
		GnmRange const *pos     = sheet_object_get_range (SHEET_OBJECT (cc));
		char const  *in = cell_comment_text_get (cc);
		size_t in_bytes, out_bytes;
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

static void
excel_write_RSTRING (ExcelWriteState *ewb, GnmCell const *cell, unsigned xf)
{
	GArray *txo = g_hash_table_lookup (ewb->cell_markup, cell);
	guint8 buf [6];
	unsigned i, n;

	g_return_if_fail (txo != NULL);

	ms_biff_put_var_next (ewb->bp, BIFF_RSTRING);
	EX_SETROW (buf, cell->pos.row);
	EX_SETCOL (buf, cell->pos.col);
	EX_SETXF  (buf, xf);
	ms_biff_put_var_write  (ewb->bp, buf, 6);
	excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH,
		cell->value->v_str.val->str);

	n = txo->len / 2;
	if (ewb->bp->version < MS_BIFF_V8) {
		GSF_LE_SET_GUINT8 (buf, n);
		ms_biff_put_var_write  (ewb->bp, buf, 1);
		for (i = 0; i < n ; i++) {
			GSF_LE_SET_GUINT8 (buf,
				g_array_index (txo, gint, i*2));
			GSF_LE_SET_GUINT8 (buf + 1,
				g_array_index (txo, gint, i*2+1));
			ms_biff_put_var_write  (ewb->bp, buf, 2);
		}
	} else {
		GSF_LE_SET_GUINT16 (buf, n);
		ms_biff_put_var_write  (ewb->bp, buf, 2);
		for (i = 0; i < n ; i++) {
			GSF_LE_SET_GUINT16 (buf,
				g_array_index (txo, gint, i*2));
			GSF_LE_SET_GUINT16 (buf + 2,
				g_array_index (txo, gint, i*2+1));
			ms_biff_put_var_write  (ewb->bp, buf, 4);
		}
	}

	ms_biff_put_commit (ewb->bp);
}

static void
excel_write_cell (ExcelWriteState *ewb, ExcelWriteSheet *esheet,
		  GnmCell const *cell, unsigned xf)
{
	GnmValue *v;

	d (2, {
		GnmParsePos tmp;
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
	else if ((v = cell->value) != NULL) {
		if (v->type == VALUE_STRING &&
		    VALUE_FMT (v) != NULL &&
		    style_format_is_markup (VALUE_FMT (v)))
			excel_write_RSTRING (ewb, cell, xf);
		else
			excel_write_value (ewb, cell->value, cell->pos.col, cell->pos.row, xf);
	}
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

		data = ms_biff_put_len_next (bp, BIFF_BLANK_v2, 6);
		EX_SETXF (data, xf);
		EX_SETCOL(data, end_col);
		EX_SETROW(data, row);
	} else {
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
 **/
static void
excel_write_GUTS (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_GUTS, 8);
	int row_level = MIN (esheet->gnum_sheet->rows.max_outline_level, 0x7);
	int col_level = MIN (esheet->gnum_sheet->cols.max_outline_level, 0x7);
	int row_size = 0, col_size = 0;

	/* This seems to be what the default is */
	if (row_level > 0) {
		row_level++;
		row_size = 5 + 12 * row_level;
	}
	if (col_level > 0) {
		col_level++;
		col_size = 5 + 12 * col_level;
	}
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
	data = ms_biff_put_len_next (bp, BIFF_DEFAULTROWHEIGHT_v2, 4);
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

static XL_font_width const *
xl_find_fontspec (ExcelWriteSheet *esheet, float *scale)
{
	/* Use the 'Normal' Style which is by definition the 0th */
	GnmStyle const *def_style = esheet->ewb->xf.default_style;
	*scale = mstyle_get_font_size (def_style) / 10.;
	return xl_lookup_font_specs (mstyle_get_font_name (def_style));
}

static void
excel_write_DEFCOLWIDTH (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint16 charwidths;
	float  width, scale;
	XL_font_width const *spec = xl_find_fontspec (esheet, &scale);
	
	/* pts to avoid problems when zooming */
	width = sheet_col_get_default_size_pts (esheet->gnum_sheet);
	width *= 96./72.; /* pixels at 96dpi */

	charwidths = (guint16)((width / (scale * spec->defcol_unit)) + .5);

	d (1, fprintf (stderr, "Default column width %hu characters (%f XL pixels)\n", charwidths, width););

	ms_biff_put_2byte (bp, BIFF_DEFCOLWIDTH, charwidths);
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
 **/
static void
excel_write_COLINFO (BiffPut *bp, ExcelWriteSheet *esheet,
		     ColRowInfo const *ci, int last_index, guint16 xf_index)
{
	guint8 *data;
	guint16 charwidths, options = 0;
	float   width, scale;
	XL_font_width const *spec = xl_find_fontspec (esheet, &scale);

	width = ci->size_pts;		/* pts to avoid problems when zooming */
	width /= scale * 72. / 96;	/* pixels at 96dpi */
	/* center the measurement on the known default size */
	charwidths = (guint16)((width - 8. * spec->defcol_unit) * spec->colinfo_step +
			       spec->colinfo_baseline + .5);

	if (!ci->visible)
		options = 1;
	options |= (MIN (ci->outline_level, 0x7) << 8);
	if (ci->is_collapsed)
		options |= 0x1000;

	d (1, {
		fprintf (stderr, "Column Formatting %s!%s of width "
		      "%hu/256 characters (%f pts)\n",
		      esheet->gnum_sheet->name_quoted,
		      cols_name (ci->pos, last_index), charwidths,
		      ci->size_pts);
		fprintf (stderr, "Options %hd, default style %hd\n", options, xf_index);
	});

	/* NOTE : Docs are wrong, length is 12 not 11 */
	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 12);
	GSF_LE_SET_GUINT16 (data +  0, ci->pos);	/* 1st  col formatted */
	GSF_LE_SET_GUINT16 (data +  2, last_index);	/* last col formatted */
	GSF_LE_SET_GUINT16 (data +  4, charwidths);
	GSF_LE_SET_GUINT16 (data +  6, xf_index);
	GSF_LE_SET_GUINT16 (data +  8, options);
	GSF_LE_SET_GUINT16 (data + 10, 0);
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
	GnmValue const *v = cond->value[i];
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
	guint8  buf[24];
	unsigned count, i;
	char const *str0 = NULL, *str1 = NULL;

	if (esheet->gnum_sheet->filters == NULL)
		return;
	filter = esheet->gnum_sheet->filters->data;

	ms_biff_put_empty (bp, BIFF_FILTERMODE);

	/* Write the autofilter flag */
	count = range_width (&filter->r);
	ms_biff_put_2byte (bp, BIFF_AUTOFILTERINFO, count);

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
			buf[5] = (cond->op[0] == GNM_FILTER_OP_BLANKS) ? 0xC : 0xE;
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
			excel_write_string (bp, STR_NO_LENGTH, str0);
		if (str1 != NULL)
			excel_write_string (bp, STR_NO_LENGTH, str1);

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

	nexpr.name = gnm_string_get ("_FilterDatabase");
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
	gnm_string_unref (nexpr.name);
}

static void
excel_write_anchor (guint8 *buf, SheetObjectAnchor const *anchor)
{
	GSF_LE_SET_GUINT16 (buf +  0, anchor->cell_bound.start.col);
	GSF_LE_SET_GUINT16 (buf +  2, (guint16)(anchor->offset[0]*1024. + .5));
	GSF_LE_SET_GUINT16 (buf +  4, anchor->cell_bound.start.row);
	GSF_LE_SET_GUINT16 (buf +  6, (guint16)(anchor->offset[1]*256. + .5));
	GSF_LE_SET_GUINT16 (buf +  8, anchor->cell_bound.end.col);
	GSF_LE_SET_GUINT16 (buf + 10, (guint16)(anchor->offset[2]*1024. + .5));
	GSF_LE_SET_GUINT16 (buf + 12, anchor->cell_bound.end.row);
	GSF_LE_SET_GUINT16 (buf + 14, (guint16)(anchor->offset[3]*256. + .5));
}

static guint32
excel_write_start_drawing (ExcelWriteSheet *esheet)
{
	if (esheet->cur_obj++ > 0)
		ms_biff_put_var_next (esheet->ewb->bp, BIFF_MS_O_DRAWING);
	return 0x400*esheet->ewb->cur_obj_group + esheet->cur_obj;
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

	static guint8 const obj_v8[] = {
/* SpContainer */  0x0f,   0,   4, 0xf0,   0x52, 0, 0, 0,
/* Sp */	   0x92, 0xc, 0xa, 0xf0,      8, 0, 0, 0,
			0,   0, 0, 0,	/* fill in spid */
			0, 0xa, 0, 0,
/* OPT */	   0x43,   0, 0xb, 0xf0,   0x18, 0, 0, 0,
			0x7f, 0, 4, 1,	4, 1, /* bool LockAgainstGrouping 127 = 0x1040104; */
			0xbf, 0, 8, 0,	8, 0, /* bool fFitTextToShape 191 = 0x80008; */
			0xff, 1, 0, 0,	8, 0, /* bool fNoLineDrawDash 511 = 0x80000; */
			0xbf, 3, 0, 0,	2, 0, /* bool fPrint 959 = 0x20000; */
/* ClientAnchor */    0, 0, 0x10, 0xf0,	   0x12, 0, 0, 0,	1,0,
			0,0,  0,0,	0,0,  0,0,	0,0,  0,0,	0,0,  0,0,
/* ClientData */      0, 0, 0x11, 0xf0,  0, 0, 0, 0
	};
	static SheetObjectAnchorType const anchor_types[] = {
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START
	};
	static float offsets[] = { 0., 0., 0., 0. };

	guint8 *data, buf [sizeof obj_v8];
	GnmFilter const *filter;
	GnmFilterCondition const *cond;
	BiffPut *bp = esheet->ewb->bp;
	unsigned i;
	SheetObjectAnchor anchor;
	GnmRange r;

	if (esheet->gnum_sheet->filters == NULL)
		return;
	filter = esheet->gnum_sheet->filters->data;
	r.end.row = 1 + (r.start.row = filter->r.start.row);

	/* write combos for the fields */
	for (i = 0; i < filter->fields->len ; i++) {
		cond = gnm_filter_get_condition (filter, i);

		r.end.col = 1 + (r.start.col = filter->r.start.col + i);
		sheet_object_anchor_init (&anchor, &r, offsets, anchor_types,
			SO_DIR_DOWN_RIGHT);
		if (bp->version >= MS_BIFF_V8) {
			guint32 id = excel_write_start_drawing (esheet);
			memcpy (buf, obj_v8, sizeof obj_v8);
			GSF_LE_SET_GUINT32 (buf + 16, id);
			excel_write_anchor (buf + 66, &anchor);
			ms_biff_put_var_write (bp, buf, sizeof obj_v8);
			ms_biff_put_commit (bp);

			ms_biff_put_var_next (bp, BIFF_OBJ);
			/* autofill, locked, with undocumented flag 0x100 that
			 * I am guessing is tied to the fact that XL created
			 * this. not the user*/
			ms_objv8_write_common (bp,
				esheet->cur_obj, 0x14, 0x2101);
			ms_objv8_write_scrollbar (bp);
			ms_objv8_write_listbox (bp, cond != NULL); /* acts as an end */
		} else {
			data = ms_biff_put_len_next (bp, BIFF_OBJ, sizeof std_obj_v7);
			memcpy (data, std_obj_v7, sizeof std_obj_v7);

			esheet->cur_obj++;
			GSF_LE_SET_GUINT32 (data +  0, esheet->cur_obj);
			GSF_LE_SET_GUINT16 (data +  6, esheet->cur_obj);
			excel_write_anchor (data + 10, &anchor);
			if (cond != NULL)
				GSF_LE_SET_GUINT16 (data + 124, 0xa);
		}
		ms_biff_put_commit (bp);
	}
}

static void
excel_write_chart_v8 (ExcelWriteSheet *esheet, SheetObject *so)
{
	static guint8 const obj_v8[] = {
/* SpContainer */   0xf,   0,   4, 0xf0,   0x6a, 0, 0, 0,
/* Sp */	   0x92, 0xc, 0xa, 0xf0,      8, 0, 0, 0,
			0,   0, 0, 0,	/* fill in spid */
			0, 0xa, 0, 0,
/* OPT */	   0x83,   0, 0xb, 0xf0,   0x30, 0, 0, 0,
			0x7f, 0,    4, 1,  4, 1, /* bool   LockAgainstGrouping 127 = 0x1040104; */
			0xbf, 0,    8, 0,  8, 0, /* bool   fFitTextToShape 191	= 0x0080008; */
			0x81, 1, 0x4e, 0,  0, 8, /* Colour fillColor 385	= 0x800004e; */
			0x83, 1, 0x4d, 0,  0, 8, /* Colour fillBackColor 387	= 0x800004d; */
			0xbf, 1, 0x10, 0,0x10,0, /* bool   fNoFillHitTest 447	= 0x0100010; */
			0xc0, 1, 0x4d, 0,  0, 8, /* Colour lineColor 448	= 0x800004d; */
			0xff, 1,    8, 0,  8, 0, /* bool   fNoLineDrawDash 511	= 0x0080008; */
			0x3f, 2,    0, 0,  2, 0, /* bool   fshadowObscured 575	= 0x0020000; */
/* ClientAnchor */    0, 0, 0x10, 0xf0,   0x12, 0, 0, 0, 0,0,
			0,0,  0,0,	0,0,  0,0,	0,0,  0,0,	0,0,  0,0,
/* ClientData */      0, 0, 0x11, 0xf0,  0, 0, 0, 0
	};

	guint8 buf [sizeof obj_v8];
	BiffPut *bp = esheet->ewb->bp;
	guint32 id = excel_write_start_drawing (esheet);

	memcpy (buf, obj_v8, sizeof obj_v8);
	GSF_LE_SET_GUINT32 (buf + 16, id);
	excel_write_anchor (buf + 0x5a, sheet_object_get_anchor (so));
	ms_biff_put_var_write (bp, buf, sizeof obj_v8);
	ms_biff_put_commit (bp);

	ms_biff_put_var_next (bp, BIFF_OBJ);
	ms_objv8_write_common (bp, esheet->cur_obj, 5, 0x6011);
	GSF_LE_SET_GUINT32 (buf, 0); /* end */
	ms_biff_put_var_write (bp, buf, 4);

	ms_biff_put_commit (bp);
	ms_excel_chart_write (esheet->ewb, so);
}

/* Return NULL when we cannot export. The NULL will be added to the list to
 * indicate that we shouldn't write a DRAWING record for the corresponding
 * image. */
static BlipInf *
blipinf_new (SheetObjectImage *soi)
{
	BlipInf *blip;
	GByteArray *bytes;

	blip = g_new0 (BlipInf, 1);
	blip->uncomp_len = -1;
	blip->needs_free = FALSE;
	blip->so         = SHEET_OBJECT (soi);

	g_object_get (G_OBJECT (soi), 
		      "image-type", &blip->type, 
		      "image-data", &bytes,
		      NULL);
	blip->bytes = *bytes;	/* Need to copy, we may change it. */
	
	if (strcmp (blip->type, "jpeg") == 0 || /* Raster format */
	    strcmp (blip->type, "png")  == 0 ||	/* understood by Excel */
	    strcmp (blip->type, "dib")  == 0) {
		blip->header_len = BSE_HDR_LEN + RASTER_BLIP_HDR_LEN;
	} else if (strcmp (blip->type, "wmf") == 0 || /* Vector format */
		   strcmp (blip->type, "emf") == 0 || /* - compress */
		   strcmp (blip->type, "pict") == 0) { 

		int res;
		gulong dest_len = blip->bytes.len * 1.01 + 12;
		guint8 *buffer = g_malloc (dest_len);

		blip->uncomp_len = blip->bytes.len;
		res = compress (buffer, &dest_len, 
				blip->bytes.data, blip->bytes.len);
		if (res != Z_OK) {
			g_free (buffer);
			g_warning ("compression failure %d;", res);
		} else {
			blip->needs_free = TRUE;
			blip->bytes.data = buffer;
			blip->bytes.len  = dest_len;
		}
		blip->header_len = BSE_HDR_LEN + VECTOR_BLIP_HDR_LEN;
	} else {
		/* Fall back to png */
		GdkPixbuf *pixbuf;
		char      *buffer = NULL;

		g_object_get (G_OBJECT (soi), "pixbuf", &pixbuf, NULL);
		
		if (pixbuf) {
			gdk_pixbuf_save_to_buffer (pixbuf,
						   &buffer,
						   &blip->bytes.len,
						   "png",
						   NULL,
						   NULL);
			g_object_unref (G_OBJECT (pixbuf));
		}
		
		if (buffer) {
			blip->type = "png";
			blip->bytes.data = buffer;
			blip->needs_free = TRUE;
			blip->header_len = BSE_HDR_LEN + RASTER_BLIP_HDR_LEN;
		} else {
			g_warning 
				("Unable to export %s image as png to Excel", 
				 blip->type);
			g_free (blip);
			blip = NULL;
		}
	}

	return blip;
}

static void
blipinf_free (BlipInf *blip)
{
	if (blip) {		/* It is not a bug if blip == NULL */
		blip->type = NULL;
		if (blip->needs_free) {
			g_free (blip->bytes.data);
			blip->needs_free = FALSE;
		}
		blip->bytes.data = NULL;
		g_free (blip);
	}
}

static void
excel_write_image_v8 (ExcelWriteSheet *esheet, BlipInf *bi)
{
	static guint8 const obj_v8[] = {
/* SpContainer */   0xf,   0,   4, 0xf0,   0x4c, 0, 0, 0,
/* Sp */	   0xb2,   4, 0xa, 0xf0,      8, 0, 0, 0,
			0,   0, 0, 0,	/* fill in spid */
			0, 0xa, 0, 0,
/* OPT */	   0x33,   0, 0xb, 0xf0,   0x12, 0, 0, 0,
			0x7f,    0, 0x80,  0, 0x80, 0, /* bool   LockAgainstGrouping 127 = 0x800080; */
			   4, 0x41,    1, 0,     0, 0, /* blip x is blip (fill in); */
			0x80,    1,    3, 0,     0, 0, /* FillType fillType 384 = 0x3; */
/* ClientAnchor */    0, 0, 0x10, 0xf0,   0x12, 0, 0, 0, 0,0,
			0,0,  0,0,	0,0,  0,0,	0,0,  0,0,	0,0,  0,0,
/* ClientData */      0, 0, 0x11, 0xf0,  0, 0, 0, 0
	};
	guint8 buf [sizeof obj_v8];
	ExcelWriteState *ewb = esheet->ewb;
	BiffPut *bp = ewb->bp;
	guint32 id = excel_write_start_drawing (esheet);
	guint32 blip_id = ewb->cur_blip + 1; 

	memcpy (buf, obj_v8, sizeof obj_v8);
	GSF_LE_SET_GUINT32 (buf + 16, id);
	GSF_LE_SET_GUINT32 (buf + 40, blip_id);
	excel_write_anchor (buf + 0x3c, sheet_object_get_anchor (bi->so));
	ms_biff_put_var_write (bp, buf, sizeof obj_v8);
	ms_biff_put_commit (bp);

	ms_biff_put_var_next (bp, BIFF_OBJ);
	ms_objv8_write_common (bp, esheet->cur_obj, 8, 0x6011);
	GSF_LE_SET_GUINT32 (buf, 0); /* end */
	ms_biff_put_var_write (bp, buf, 4);

	ms_biff_put_commit (bp);
	ewb->cur_blip++;
}

static void
excel_write_ClientTextbox(ExcelWriteState *ewb, SheetObject *so)
{
	guint8 buf [18];
	int txo_len = 18;
	int draw_len = 8;
	int char_len;
	char *label;
	int markuplen;
	BiffPut *bp = ewb->bp;
	GArray *markup = g_hash_table_lookup (ewb->cell_markup, so);
		
	ms_biff_put_var_next (bp,  BIFF_MS_O_DRAWING);
	memset (buf, 0, draw_len);
	GSF_LE_SET_GUINT16 (buf + 2, 0xf00d); /* ClientTextbox */
	ms_biff_put_var_write (bp, buf, draw_len);
	ms_biff_put_commit (bp);
	
	ms_biff_put_var_next (bp, BIFF_TXO);
	memset (buf, 0, txo_len);
	GSF_LE_SET_GUINT16 (buf, 0x212); /* end */
	g_object_get (G_OBJECT (so), "label", &label, NULL);
	char_len = excel_write_string_len (label, NULL);
	GSF_LE_SET_GUINT16 (buf + 10, char_len);
	if (markup)
		markuplen = 8 + markup->len * 4;
	else
		markuplen = 16;
	GSF_LE_SET_GUINT16 (buf + 12, markuplen);
	ms_biff_put_var_write (bp, buf, txo_len);
	ms_biff_put_commit (bp);

	ms_biff_put_var_next (bp, BIFF_CONTINUE);
	excel_write_string(bp, STR_NO_LENGTH, label);
	ms_biff_put_commit (bp);
	
	ms_biff_put_var_next (bp, BIFF_CONTINUE);
	memset (buf, 0, 8);
	if (markup ) {
		int n = markup->len / 2;
		int i;

		for (i = 0; i < n ; i++) {
			GSF_LE_SET_GUINT16 (buf,
				g_array_index (markup, gint, i*2));
			GSF_LE_SET_GUINT16 (buf + 2,
				g_array_index (markup, gint, i*2+1));
			ms_biff_put_var_write  (ewb->bp, buf, 8);
		}
	} else {
		ms_biff_put_var_write  (ewb->bp, buf, 8);
	}
	memset (buf, 0, 8);
	GSF_LE_SET_GUINT16 (buf, char_len);
	ms_biff_put_var_write (bp, buf, 8);
	ms_biff_put_commit (bp);
}

static void
excel_write_textbox_v8 (ExcelWriteSheet *esheet, SheetObject *so)
{
	static guint8 const obj_v8[] = {
/* SpContainer */   0xf,   0,   4, 0xf0,    0x6c, 0, 0, 0,
/* Sp */	   0xa2,  0xc, 0xa, 0xf0,      8, 0, 0, 0,
			0,   0, 0, 0,	/* fill in spid */
			0, 0xa, 0, 0,

/* OPT */	   0x73,   0, 0xb, 0xf0,   0x2a, 0, 0, 0,
                        0x80,    0, 0xa0,    0, 0xc6, 0, /* Txid */
                        0x85,    0,    1,    0,    0, 0, /* wrap_text_at_margin */

                        0xbf,    0,    8,    0,  0xa, 0, /* fFitTextToShape */
                        0x81,    1, 0x41,    0,    0, 8, /* fillColor */
                        0xbf,    1,    0,    0,    1, 0, /* fNoFillHitTest */
                        0xc0,    1, 0x40,    0,    0, 8, /* lineColor */
                        0xbf,    3,    0,    0,    8, 0, /* fPrint */
/* ClientAnchor */    0, 0, 0x10, 0xf0,   0x12, 0, 0, 0, 0,0,
			0,0,  0,0,	0,0,  0,0,	0,0,  0,0,	0,0,  0,0,
/* ClientData */      0, 0, 0x11, 0xf0,  0, 0, 0, 0
	};

	guint8 buf [sizeof obj_v8];

	ExcelWriteState *ewb = esheet->ewb;
	BiffPut *bp = ewb->bp;
	guint32 id = excel_write_start_drawing (esheet);

	memcpy (buf, obj_v8, sizeof obj_v8);
	GSF_LE_SET_GUINT32 (buf + 16, id);
	GSF_LE_SET_GUINT32 (buf + 4, sizeof obj_v8);
	excel_write_anchor (buf + 0x54, sheet_object_get_anchor (so));
	ms_biff_put_var_write (bp, buf, sizeof obj_v8);
	ms_biff_put_commit (bp);

	ms_biff_put_var_next (bp, BIFF_OBJ);
	ms_objv8_write_common (bp, esheet->cur_obj, 6, 0x6011);
	GSF_LE_SET_GUINT32 (buf, 0); /* end */
	ms_biff_put_var_write (bp, buf, 4);

	ms_biff_put_commit (bp);

	excel_write_ClientTextbox(ewb, so);
}

static void
excel_write_DIMENSION (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint8 *data;
	if (bp->version >= MS_BIFF_V8) {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS_v2, 14);
		GSF_LE_SET_GUINT32 (data +  0, 0);
		GSF_LE_SET_GUINT32 (data +  4, esheet->max_row-1);
		GSF_LE_SET_GUINT16 (data +  8, 0);
		GSF_LE_SET_GUINT16 (data + 10, esheet->max_col-1);
		GSF_LE_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS_v2, 10);
		GSF_LE_SET_GUINT16 (data +  0, 0);
		GSF_LE_SET_GUINT16 (data +  2, esheet->max_row-1);
		GSF_LE_SET_GUINT16 (data +  4, 0);
		GSF_LE_SET_GUINT16 (data +  6, esheet->max_col-1);
		GSF_LE_SET_GUINT16 (data +  8, 0x0000);
	}
	ms_biff_put_commit (bp);
}

static void
excel_write_COUNTRY (BiffPut *bp)
{
	guint8 *data = ms_biff_put_len_next (bp, BIFF_COUNTRY, 4);
	GSF_LE_SET_GUINT16 (data, 1); /* flag as made in US */
	GSF_LE_SET_GUINT16 (data+2, 1);
	ms_biff_put_commit (bp);
}

static void
excel_write_WSBOOL (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint16 flags = 0;

	/* 0x0001 automatic page breaks are visible */
	flags |= 1;
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	if (esheet->gnum_sheet->outline_symbols_below)	flags |= 0x040;
	if (esheet->gnum_sheet->outline_symbols_right)	flags |= 0x080;
	/* 0x0100 the Fit option is on (Page Setup dialog box, Page tab) */
	if (esheet->gnum_sheet->display_outlines)	flags |= 0x400;

	ms_biff_put_2byte (bp, BIFF_WSBOOL, flags);
}

static void
write_sheet_head (BiffPut *bp, ExcelWriteSheet *esheet)
{
	guint8 *data;
	PrintInformation *pi;
	Sheet const *sheet = esheet->gnum_sheet;
	Workbook const *wb = sheet->workbook;
	double header = 0, footer = 0, left = 0, right = 0;

	pi = sheet->print_info;
	g_return_if_fail (pi != NULL);

	ms_biff_put_2byte (bp, BIFF_CALCMODE, wb->recalc_auto ? 1 : 0);
	ms_biff_put_2byte (bp, BIFF_CALCCOUNT, wb->iteration.max_number);
	ms_biff_put_2byte (bp, BIFF_REFMODE, sheet->r1c1_addresses ? 0 : 1);
	ms_biff_put_2byte (bp, BIFF_ITERATION, wb->iteration.enabled ? 1 : 0);

	data = ms_biff_put_len_next (bp, BIFF_DELTA, 8);
	gsf_le_set_double (data, wb->iteration.tolerance);
	ms_biff_put_commit (bp);

	ms_biff_put_2byte (bp, BIFF_SAVERECALC,	0x0001);
	ms_biff_put_2byte (bp, BIFF_PRINTHEADERS,  0x0000);
	ms_biff_put_2byte (bp, BIFF_PRINTGRIDLINES, pi->print_grid_lines ? 1 : 0);
	ms_biff_put_2byte (bp, BIFF_GRIDSET, 	0x0001);

	excel_write_GUTS (bp, esheet);
	excel_write_DEFAULT_ROW_HEIGHT (bp, esheet);
	if (bp->version < MS_BIFF_V8)
		excel_write_COUNTRY (bp);
	excel_write_WSBOOL (bp, esheet);

#warning export the header and footer
	ms_biff_put_var_next (bp, BIFF_HEADER);
/*	biff_put_text (bp, "&A", TRUE); */
	ms_biff_put_commit (bp);

	ms_biff_put_var_next (bp, BIFF_FOOTER);
/*	biff_put_text (bp, "&P", TRUE); */
	ms_biff_put_commit (bp);

	ms_biff_put_2byte (bp, BIFF_HCENTER, pi->center_horizontally ? 1 : 0);
	ms_biff_put_2byte (bp, BIFF_VCENTER, pi->center_vertically ? 1 : 0);

	print_info_get_margins (pi, &header, &footer, &left, &right);
	excel_write_margin (bp, BIFF_LEFT_MARGIN,   left);
	excel_write_margin (bp, BIFF_RIGHT_MARGIN,  right);
	excel_write_margin (bp, BIFF_TOP_MARGIN,    pi->margins.top.points);
	excel_write_margin (bp, BIFF_BOTTOM_MARGIN, pi->margins.bottom.points);

	excel_write_SETUP (bp, esheet);
	if (bp->version < MS_BIFF_V8) {
		/* write externsheets for every sheet in the workbook
		 * to make our lives easier */
		excel_write_externsheets_v7 (esheet->ewb);
	}
	excel_write_DEFCOLWIDTH (bp, esheet);
	excel_write_colinfos (bp, esheet);
	excel_write_AUTOFILTERINFO (bp, esheet);
	excel_write_DIMENSION (bp, esheet);
}

void
excel_write_SCL (BiffPut *bp, double zoom, gboolean force)
{
	guint8 *data;
	double whole, fractional = modf (zoom, &whole);
	int num, denom;

	stern_brocot (fractional, 1000, &num, &denom);
	num += whole * denom;
	d (2, fprintf (stderr, "Zoom %g == %d/%d\n", zoom, num, denom););

	if (num == denom && !force)
		return;

	data = ms_biff_put_len_next (bp, BIFF_SCL, 4);
	GSF_LE_SET_GUINT16 (data + 0, (guint16)num);
	GSF_LE_SET_GUINT16 (data + 2, (guint16)denom);
	ms_biff_put_commit (bp);
}

static void
excel_write_SELECTION (BiffPut *bp, GList *selections,
		       GnmCellPos const *pos, int pane)
{
	int n = g_list_length (selections);
	GList *ptr;
	guint8 *data;

	data = ms_biff_put_len_next (bp, BIFF_SELECTION, 9 + 6*n);
	GSF_LE_SET_GUINT8  (data +  0, pane);
	GSF_LE_SET_GUINT16 (data +  1, pos->row);
	GSF_LE_SET_GUINT16 (data +  3, pos->col);
	GSF_LE_SET_GUINT16 (data +  5, 0); /* our edit_pos is in 1st range */
	GSF_LE_SET_GUINT16 (data +  7, n);

	data += 9;
	for (ptr = selections ; ptr != NULL ; ptr = ptr->next, data += 6) {
		GnmRange const *r = ptr->data;
		GSF_LE_SET_GUINT16 (data + 0, r->start.row);
		GSF_LE_SET_GUINT16 (data + 2, r->end.row);
		GSF_LE_SET_GUINT8  (data + 4, r->start.col);
		GSF_LE_SET_GUINT8  (data + 5, r->end.col);
	}
	ms_biff_put_commit (bp);
}
static void
excel_write_selections (BiffPut *bp, ExcelWriteSheet *esheet, SheetView *sv)
{
	GnmRange  r;
	GnmCellPos pos;
	GList *tmp;

	excel_write_SELECTION (bp, sv->selections, &sv->edit_pos, 3);

	if (sv->unfrozen_top_left.col > 0) {
		pos = sv->edit_pos;
		if (pos.col < sv->unfrozen_top_left.col)
			pos.col = sv->unfrozen_top_left.col;
		tmp = g_list_prepend (NULL,
			      range_init_cellpos (&r, &pos, &pos));
		excel_write_SELECTION (bp, tmp, &pos, 1);
		g_list_free (tmp);
	}
	if (sv->unfrozen_top_left.row > 0) {
		pos = sv->edit_pos;
		if (pos.row < sv->unfrozen_top_left.row)
			pos.row = sv->unfrozen_top_left.row;
		tmp = g_list_prepend (NULL,
			      range_init_cellpos (&r, &pos, &pos));
		excel_write_SELECTION (bp, tmp, &pos, 2);
		g_list_free (tmp);
	}
	if (sv->unfrozen_top_left.col > 0 && sv->unfrozen_top_left.row > 0) {
		pos = sv->edit_pos;	/* apparently no bounds check needed */
		tmp = g_list_prepend (NULL,
			      range_init_cellpos (&r, &pos, &pos));
		excel_write_SELECTION (bp, tmp, &pos, 0);
		g_list_free (tmp);
	}
}

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

	data = ms_biff_put_len_next (bp, BIFF_ROW_v2, 16);
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
 * See: 'Finding records in BIFF files' and 'DBCELL'
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
 * See: 'Finding records in BIFF files'
 */
static guint32
excel_sheet_write_block (ExcelWriteSheet *esheet, guint32 begin, int nrows,
			 GArray *dbcells)
{
	GnmStyle *style;
	ExcelWriteState *ewb = esheet->ewb;
	int col, row, max_row, max_col = esheet->max_col;
	unsigned  ri_start [2]; /* Row info start */
	unsigned *rc_start;	/* Row cells start */
	guint16   xf_list [SHEET_MAX_COLS];
	GnmRange  r;
	GnmCell const	*cell;
	Sheet		*sheet = esheet->gnum_sheet;
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

	r.start.col = 0;
	r.end.col = max_col-1;

	rc_start = g_alloca (sizeof (unsigned) * nrows);
	for (row = begin; row <= max_row; row++) {
		guint32 run_size = 0;

		/* Save start pos of 1st cell in row */
		r.start.row = r.end.row = row;
		rc_start [row - begin] = ewb->bp->streamPos;
		if (! (NULL != sheet_row_get (sheet, row) ||
		       sheet_style_has_visible_content (sheet, &r)))
			continue;
		has_content = TRUE;
		for (col = 0; col < max_col; col++) {
			cell = sheet_cell_get (sheet, col, row);

			/* check for a magic value_fmt override*/
			style = g_hash_table_lookup (ewb->xf.value_fmt_styles, cell);
			if (style == NULL)
				style = sheet_style_get (sheet, col, row);
			xf = two_way_table_key_to_idx (twt, style);
			if (xf < 0) {
				g_warning ("Can't find style %p for cell %s!%s",
					   style, sheet->name_unquoted, cell_name (cell));
				xf = 0;
			}
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
				excel_write_cell (ewb, esheet, cell, xf);
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

static void
excel_write_CODENAME (ExcelWriteState *ewb, GObject *src)
{
	if (ewb->export_macros) {
		char const *codename = g_object_get_data (src, CODENAME_KEY);
		/* it does not appear to always exist */
		if (codename != NULL) {
			ms_biff_put_var_next (ewb->bp, BIFF_CODENAME);
			excel_write_string (ewb->bp, STR_TWO_BYTE_LENGTH, codename);
			ms_biff_put_commit (ewb->bp);
		}
	}
}

static void
excel_write_objs_v8 (ExcelWriteSheet *esheet)
{
	BiffPut *bp = esheet->ewb->bp;
	GSList  *ptr, *charts = sheet_objects_get (esheet->gnum_sheet,
		NULL, SHEET_OBJECT_GRAPH_TYPE);
	int	 len;

	if (esheet->num_objs == 0)
		return;
	/* The header */
	if (bp->version >= MS_BIFF_V8) {
		static guint8 const header_obj_v8[] = {
/* DgContainers */ 0x0f, 0,   2, 0xf0,	   0, 0, 0, 0,	/* fill in length */
/* Dg */	   0x10, 0,   8, 0xf0,	   8, 0, 0, 0,
			0, 0, 0, 0,			/* fill num objects in this group + 1 */
			0, 0, 0, 0,			/* fill last spid in this group */
/* SpgrContainer */0x0f, 0,   3, 0xf0,	   0, 0, 0, 0,	/* fill in length */
/* SpContainer */  0x0f, 0,   4, 0xf0,	0x28, 0, 0, 0,
/* Spgr */	      1, 0,   9, 0xf0,	0x10, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Sp */	      2, 0, 0xa, 0xf0,     8, 0, 0, 0,	0, 4, 0, 0, 5, 0, 0, 0
		};
		guint8 buf [sizeof header_obj_v8];
		unsigned last_id, num_filters = 0;
		unsigned num_charts = g_slist_length (charts);
		unsigned num_texts = g_slist_length (esheet->textboxes);

		if (esheet->gnum_sheet->filters != NULL) {
			GnmFilter const *f = esheet->gnum_sheet->filters->data;
			num_filters = range_width (&f->r);

			if (esheet->gnum_sheet->filters->next != NULL) {
				g_warning ("MS Excel does not support multiple autofilters in one sheet (%s), only the first will be saved", esheet->gnum_sheet->name_unquoted);
			}
		}

		esheet->ewb->cur_obj_group++;
		last_id = 0x400*esheet->ewb->cur_obj_group + esheet->num_objs;

		ms_biff_put_var_next (bp, BIFF_MS_O_DRAWING);
		memcpy (buf, header_obj_v8, sizeof header_obj_v8);
		len = 90*num_filters + 114*num_charts+ 84*esheet->num_blips;
		len += 116*num_texts;
		GSF_LE_SET_GUINT32 (buf +  4, 72 + len);
		GSF_LE_SET_GUINT32 (buf + 16, esheet->num_objs + 1);
		GSF_LE_SET_GUINT32 (buf + 20, last_id);	/* last spid in this group */
		GSF_LE_SET_GUINT32 (buf + 28, 48 + len);
		ms_biff_put_var_write (bp, buf, sizeof header_obj_v8);
	}

#warning handle multiple charts in a graph by creating multiple objects
	for (ptr = charts; ptr != NULL ; ptr = ptr->next)
		excel_write_chart_v8 (esheet, ptr->data);
	g_slist_free (charts);

	for (ptr = esheet->blips; ptr != NULL ; ptr = ptr->next)
		if (ptr->data)
			excel_write_image_v8 (esheet, ptr->data);

	for (ptr = esheet->textboxes; ptr != NULL ; ptr = ptr->next)
		excel_write_textbox_v8 (esheet, ptr->data);

	excel_write_autofilter_objs (esheet);
}

static void
excel_write_sheet (ExcelWriteState *ewb, ExcelWriteSheet *esheet)
{
	GArray	*dbcells;
	guint32  block_end;
	gint32	 y;
	int	 rows_in_block = ROW_BLOCK_MAX_LEN;
	unsigned index_off;
	MsBiffFileType type;

	/* No. of blocks of rows. Only correct as long as all rows
	 * _including empties_ have row info records
	 */
	guint32 nblocks = (esheet->max_row - 1) / rows_in_block + 1;

	switch (esheet->gnum_sheet->sheet_type) {
	default :
		g_warning ("unknown sheet type %d (assuming WorkSheet)",
			   esheet->gnum_sheet->sheet_type);
	case GNM_SHEET_DATA :	type = MS_BIFF_TYPE_Worksheet; break;
	case GNM_SHEET_OBJECT : type = MS_BIFF_TYPE_Chart; break;
	case GNM_SHEET_XLM :	type = MS_BIFF_TYPE_Macrosheet; break;
	}
	esheet->streamPos = excel_write_BOF (ewb->bp, type);
	if (esheet->gnum_sheet->sheet_type == GNM_SHEET_OBJECT) {
		GSList *objs = sheet_objects_get (esheet->gnum_sheet,
			NULL, SHEET_OBJECT_GRAPH_TYPE);
		g_return_if_fail (objs != NULL);
		ms_excel_chart_write (ewb, objs->data);
		g_slist_free (objs);
		return;
	}

	if (ewb->bp->version >= MS_BIFF_V8) {
		guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_INDEX_v2,
						     nblocks * 4 + 16);
		index_off = ewb->bp->streamPos;
		GSF_LE_SET_GUINT32 (data, 0);
		GSF_LE_SET_GUINT32 (data +  4, 0);
		GSF_LE_SET_GUINT32 (data +  8, esheet->max_row);
		GSF_LE_SET_GUINT32 (data + 12, 0);
	} else {
		guint8 *data = ms_biff_put_len_next (ewb->bp, BIFF_INDEX_v2,
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
	dbcells = g_array_new (FALSE, FALSE, sizeof (unsigned));
	for (y = 0; y < esheet->max_row; y = block_end + 1)
		block_end = excel_sheet_write_block (esheet, y, rows_in_block,
						     dbcells);

	if (ewb->bp->version < MS_BIFF_V8)
		excel_write_comments_biff7 (ewb->bp, esheet);
	excel_sheet_write_INDEX (esheet, index_off, dbcells);

	if (ewb->num_obj_groups > 0)
		excel_write_objs_v8 (esheet);

	SHEET_FOREACH_VIEW (esheet->gnum_sheet, view, {
		if (excel_write_WINDOW2 (ewb->bp, esheet, view))
			excel_write_PANE (ewb->bp, esheet, view);
		excel_write_SCL (ewb->bp, /* zoom will move to view eentually */
			esheet->gnum_sheet->last_zoom_factor_used, FALSE);
		excel_write_selections (ewb->bp, esheet, view);
	});

	/* These are actually specific to >= biff8
	 * but it can't hurt to have them here
	 * things will just ignore them */
	excel_write_MERGECELLS (ewb->bp, esheet);
	excel_write_DVAL (ewb->bp, esheet);

/* See: Global Column Widths...  not cricual.
	data = ms_biff_put_len_next (ewb->bp, BIFF_GCW, 34);
	{
		int i;
		for (i = 0; i < 34; i++)
			GSF_LE_SET_GUINT8 (data+i, 0xff);
		GSF_LE_SET_GUINT32 (data, 0xfffd0020);
	}
	ms_biff_put_commit (ewb->bp);
*/

	excel_write_CODENAME (ewb, G_OBJECT (esheet->gnum_sheet));

	ms_biff_put_empty (ewb->bp, BIFF_EOF);
	g_array_free (dbcells, TRUE);
}

static ExcelWriteSheet *
excel_sheet_new (ExcelWriteState *ewb, Sheet *sheet,
		 gboolean biff7, gboolean biff8)
{
	int const maxrows = biff7 ? MsBiffMaxRowsV7 : MsBiffMaxRowsV8;
	ExcelWriteSheet *esheet = g_new (ExcelWriteSheet, 1);
	GnmRange extent;
	GSList *objs, *img;
	int i;

	g_return_val_if_fail (sheet, NULL);
	g_return_val_if_fail (ewb, NULL);

	/* Ignore spans and merges past the bound */
	extent = sheet_get_extent (sheet, FALSE);

	if (extent.end.row >= maxrows) {
		gnm_io_warning (ewb->io_context,
				_("Some content will be lost when saving as MS Excel (tm) 95. "
				  "It only supports %d rows, and this workbook has %d"),
			  maxrows, extent.end.row);
		extent.end.row = maxrows;
	}
	if (extent.end.col >= 256) {
		gnm_io_warning (ewb->io_context,
				_("Some content will be lost when saving as MS Excel (tm). "
				  "It only supports %d rows, and this workbook has %d"),
			  256, extent.end.col);
		extent.end.col = 256;
	}

	sheet_style_get_extent (sheet, &extent, esheet->col_style);

	/* include collapsed or hidden rows */
	for (i = maxrows ; i-- > extent.end.row ; )
		if (!colrow_is_empty (sheet_row_get (sheet, i))) {
			extent.end.row = i;
			break;
		}
	/* include collapsed or hidden rows */
	for (i = 256 ; i-- > extent.end.col ; )
		if (!colrow_is_empty (sheet_col_get (sheet, i))) {
			extent.end.col = i;
			break;
		}

	esheet->gnum_sheet = sheet;
	esheet->streamPos  = 0x0deadbee;
	esheet->ewb        = ewb;
	/* makes it easier to refer to 1 past the end */
	esheet->max_col    = extent.end.col + 1;
	esheet->max_row    = extent.end.row + 1;
	esheet->validations= biff8
		? sheet_style_get_validation_list (sheet, NULL)
		: NULL;

	/* It is ok to have formatting out of range, we can disregard that. */
	if (esheet->max_col > 256)
		esheet->max_col = 256;
	if (esheet->max_row > maxrows)
		esheet->max_row = maxrows;

	/* we only export charts & images for now */
	esheet->cur_obj = esheet->num_objs = esheet->num_blips = 0;
	esheet->blips = NULL;
	objs = sheet_objects_get (sheet, NULL, SHEET_OBJECT_GRAPH_TYPE);
	esheet->num_objs += g_slist_length (objs);
	g_slist_free (objs);
	objs = sheet_objects_get (sheet, NULL, SHEET_OBJECT_IMAGE_TYPE);
	for (img = objs ; img != NULL ; img = img->next) {
		SheetObjectImage *soi = SHEET_OBJECT_IMAGE (img->data);
		BlipInf *bi = blipinf_new (soi);

		/* Images we can't export have a NULL BlipInf */
		if (bi)
			esheet->num_blips++;
		esheet->blips = g_slist_prepend (esheet->blips, bi);
	}
	esheet->blips = g_slist_reverse (esheet->blips);
	esheet->num_objs += esheet->num_blips;

	/* Text boxes */
	esheet->textboxes = sheet_objects_get (sheet, NULL, GNM_SO_FILLED_TYPE);
#warning TODO TODO FIXME FIXME FIXME : filter for boxes with text

	esheet->num_objs += g_slist_length (objs);
	g_slist_free (objs);

	/* And the autofilters */
	if (sheet->filters != NULL) {
		GnmFilter const *filter = sheet->filters->data;
		esheet->num_objs += filter->fields->len;
	}

	return esheet;
}

static void
excel_sheet_free (ExcelWriteSheet *esheet)
{
	g_slist_free (esheet->textboxes);
	gnm_slist_free_custom (esheet->blips, (GFreeFunc) blipinf_free);
	g_free (esheet);
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
static void
pre_pass (ExcelWriteState *ewb)
{
	TwoWayTable *twt;

	if (ewb->sheets->len == 0)
		return;

	gather_styles (ewb);     /* (and cache cells) */

	/* Gather Info from styles */
	twt = ewb->xf.two_way_table;
	g_hash_table_foreach (twt->unique_keys, (GHFunc) put_style_font, ewb);
	twt = ewb->xf.two_way_table;
	g_hash_table_foreach (twt->unique_keys, (GHFunc) put_format, ewb);
	gather_palette (ewb);
}

typedef struct {
	guint32 streampos;
	guint16 record_pos;
} SSTInf;

static void
excel_write_SST (ExcelWriteState *ewb)
{
	/* According to MSDN max SST sisze is 8224 */
	GPtrArray const *strings = ewb->sst.indicies;
	BiffPut		*bp = ewb->bp;
	SSTInf		*extsst = NULL;
	char *ptr, data [8224];
	char const * const last = data + sizeof (data);
	size_t out_bytes, char_len, byte_len;
	unsigned i, tmp, blocks, scale;
	GnmString const *string;
	char *str;

	if (strings->len > 0) {
		blocks = 1 + ((strings->len - 1) / 8);
		extsst = g_alloca (sizeof (SSTInf) * blocks);
	} else
		blocks = 0;

	ms_biff_put_var_next (bp, BIFF_SST);
	GSF_LE_SET_GUINT32 (data + 0, strings->len);
	GSF_LE_SET_GUINT32 (data + 4, strings->len);

	ptr = data + 8;
	for (i = 0; i < strings->len ; i++) {
		string = g_ptr_array_index (strings, i);
		str = string->str;

		if (0 == (i % 8)) {
			tmp = ptr - data + /* biff header */ 4;
			extsst[i/8].record_pos = tmp;
			extsst[i/8].streampos  = bp->streamPos + tmp;
		}

		char_len = excel_write_string_len (str, &byte_len);

		/* get the size, the marker, and at least 1 character out */
		if ((ptr + 5) >= last) {
			ms_biff_put_var_write (bp, data, ptr-data);
			ms_biff_put_commit (bp);
			ms_biff_put_var_next (bp, BIFF_CONTINUE);
			ptr = data;
		}
		GSF_LE_SET_GUINT16 (ptr, char_len);
		ptr += 2;

		if (char_len == byte_len) {
			while ((ptr + 1 + char_len) > last) {
				*ptr++ = 0;	/* unicode header == 0 */
				strncpy (ptr, str, last - ptr);
				str += (last - ptr);
				char_len -= (last - ptr);
				ptr = data;

				ms_biff_put_var_write (bp, data, sizeof (data));
				ms_biff_put_commit (bp);
				ms_biff_put_var_next (bp, BIFF_CONTINUE);
			}

			*ptr = 0;	/* unicode header == 0 */
			strncpy (ptr + 1, str, char_len);
			ptr += char_len + 1;
		} else {
			size_t old_out_bytes, count = 0;
			unsigned old_byte_len = INT_MAX;
			guint8  *len = ptr - 2; /* stash just in case of problem */

unicode_loop :
			*ptr++ = 1;	/* unicode header == 1 */
			old_out_bytes = out_bytes = last - ptr;
			g_iconv (bp->convert, &str, &byte_len, (char **)&ptr, &out_bytes);
			count += old_out_bytes - out_bytes;

			if (byte_len > 0) {
				if (old_byte_len == byte_len) {
					g_warning ("hmm we could not represent character 0x%x, skipping it.",
						   g_utf8_get_char (str));
					str = g_utf8_next_char (str);
				} else {
					old_byte_len = byte_len;
					ms_biff_put_var_write (bp, data, ptr - data);
					ms_biff_put_commit (bp);
					ms_biff_put_var_next (bp, BIFF_CONTINUE);
					ptr = data;
					len = NULL;
				}
				goto unicode_loop;
			}

			if (count != (char_len*2)) {
				if (len != NULL) {
					if (count % 1) {
						g_warning ("W.T.F ???  utf-16 should be 2 byte aligned");
						*ptr++ = 0;
						count++;
					} else {
						g_warning ("We exported a string containg unicode characters > 0xffff (%s).\n"
							   "Expect some funky characters to show up.", str);
					}
					GSF_LE_SET_GUINT16 (len, (count/2));
				} else {
					g_warning ("We're toast a string containg unicode characters > 0xffff crossed a record boundary.");
				}
			}
		}
	}

	ms_biff_put_var_write (bp, data, ptr-data);
	ms_biff_put_commit (bp);

	/* EXSST must fit in 1 record, no CONTINUEs */
	scale = 1;
	while (((blocks / scale) * 8) >= (ms_biff_max_record_len (bp) - 2))
		scale *= 2;
	ms_biff_put_var_next (bp, BIFF_EXTSST);
	GSF_LE_SET_GUINT16 (data + 0, 8*scale);
	ms_biff_put_var_write (bp, data, 2);

	GSF_LE_SET_GUINT16 (data + 6, 0);	/* constant ignored */
	for (i = 0; i < blocks; i += scale) {
		GSF_LE_SET_GUINT32 (data + 0, extsst[i].streampos);
		GSF_LE_SET_GUINT16 (data + 4, extsst[i].record_pos);
		ms_biff_put_var_write (bp, data, 8);
	}
	ms_biff_put_commit (bp);
}

static void
excel_write_WRITEACCESS (BiffPut *bp)
{
	guint8   pad [112];
	unsigned len;
	char const *utf8_name = gnm_get_real_name ();

	if (utf8_name == NULL)
		utf8_name = "";

	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	if (bp->version >= MS_BIFF_V8) {
		len = excel_write_string (bp, STR_TWO_BYTE_LENGTH, utf8_name);
		memset (pad, ' ', sizeof pad);
		ms_biff_put_var_write (bp, pad, sizeof pad - len);
		ms_biff_put_commit (bp);
	} else {
		len = excel_write_string (bp, STR_ONE_BYTE_LENGTH, utf8_name);
		memset (pad, ' ', 32);
		ms_biff_put_var_write (bp, pad, 32 - len - 1);
		ms_biff_put_commit (bp);
	}
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
cb_enumerate_names (gpointer key, GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	ewb->tmp_counter++; /* pre increment to avoid 0 */
	g_hash_table_insert (ewb->names, (gpointer)nexpr,
		GUINT_TO_POINTER (ewb->tmp_counter));
}

static void
cb_enumerate_macros (gpointer key, ExcelFunc *efunc, ExcelWriteState *ewb)
{
	if (efunc->macro_name != NULL)
		efunc->idx = ++ewb->tmp_counter;
}

static void
cb_write_macro_NAME (gpointer key, ExcelFunc *efunc, ExcelWriteState *ewb)
{
	if (efunc->macro_name != NULL) {
		guint8	data[14] = {
			0xE, 0x0,	/* flag vba macro */
			0x0,		/* key */
			0x0,		/* namelen <FILLIN> */
			0x0, 0x0,	/* no expr */
			0x0, 0x0,	/* not sheet local */
			0x0, 0x0,	/* not sheet local */
			0x0,		/* menu */
			0x0,		/* description */
			0x0,		/* help */
			0x0		/* status */
		};
		unsigned len = excel_write_string_len (efunc->macro_name, NULL);

		if (len > 255)
			len = 255;
		ms_biff_put_var_next (ewb->bp, BIFF_NAME_v0); /* yes v0 */
		GSF_LE_SET_GUINT8 (data+3, len);
		ms_biff_put_var_write (ewb->bp, data, sizeof (data));
		excel_write_string (ewb->bp, STR_NO_LENGTH, efunc->macro_name);
		ms_biff_put_commit (ewb->bp);

		g_free (efunc->macro_name);	/* INVALIDATE THE NAME */
	}
}

static void
excel_write_names (ExcelWriteState *ewb)
{
	excel_foreach_name (ewb, (GHFunc)&cb_enumerate_names);
	g_hash_table_foreach (ewb->function_map,
		(GHFunc)cb_enumerate_macros, ewb);

	excel_foreach_name (ewb, (GHFunc)&excel_write_NAME);
	g_hash_table_foreach (ewb->function_map,
		(GHFunc)cb_write_macro_NAME, ewb);
	excel_write_autofilter_names (ewb);
}

static void
excel_write_image_bytes (BiffPut *bp, GByteArray *bytes)

{
	int chunk    = ms_biff_max_record_len (bp) - bp->curpos;
	guint8 *data = bytes->data;
	gint32 len   = bytes->len;

	while (len > 0) {
		ms_biff_put_var_write (bp, data, MIN (chunk, len));
		data += chunk;
		len  -= chunk;
		chunk =  ms_biff_max_record_len (bp);
	}
}

/* 
 * FIXME: Excel doesn't read images written by this (but will open the files).
 *        OpenOffice.org can read the images.
 */
static void 
excel_write_vector_blip (ExcelWriteState *ewb, BlipInf *blip, BlipType *bt)
{
	BiffPut	 *bp = ewb->bp;

	if (bp->version >= MS_BIFF_V8) {
		guint8 buf [VECTOR_BLIP_HDR_LEN];
		double coords [4];
		double width, height;

		sheet_object_position_pts_get (blip->so, coords);
		width  = fabs (coords[2] - coords[0]);
		height = fabs (coords[3] - coords[1]);

		d(2, 
		{
			g_message ("emu_width=%d (0x%x)", 
				   (guint32) GO_PT_TO_EMU(width),
				   (guint32) GO_PT_TO_EMU (width));
			g_message ("emu_height=%d (0x%x)",
				   (guint32) GO_PT_TO_EMU(height), 
				   (guint32) GO_PT_TO_EMU (height));
			g_message ("cm_width=%d (0x%x)",
				   (guint32) GO_PT_TO_CM(width*1000),
				   (guint32) GO_PT_TO_CM (width*1000));
			g_message ("cm_height=%d (0x%x)",
				   (guint32) GO_PT_TO_CM(height*1000), 
				   (guint32) GO_PT_TO_CM (height*1000));
		});
		memset (buf, 0, sizeof buf);
		memcpy (buf, bt->blip_tag, sizeof bt->blip_tag);
		GSF_LE_SET_GUINT16 (buf + 2, 0xf018 + bt->type);
		GSF_LE_SET_GUINT32 (buf + 4, 
				    blip->bytes.len + VECTOR_BLIP_HDR_LEN - 8);
		memcpy(buf + 8, blip->id, sizeof blip->id);
		/* buf + 24: uncompressed length */
		GSF_LE_SET_GUINT32 (buf +  24, blip->uncomp_len);
		/* buf + 28: metafile bounds (rectangle) */
		/* unit for rectangle is 1/1000 cm */
		GSF_LE_SET_GUINT32 (buf +  36, 
				    (guint32) GO_PT_TO_CM(width*1000));
		GSF_LE_SET_GUINT32 (buf +  40,
				    (guint32) GO_PT_TO_CM(height*1000));
		/* buf + 44: size of metafile (point) */
		/* unit for points is EMU = 1/360000 cm = 1/12700 pt
		   - 360 times finer than for rect. */
		GSF_LE_SET_GUINT32 (buf +  44,
				    (guint32) GO_PT_TO_EMU (width));
		GSF_LE_SET_GUINT32 (buf +  48,
				    (guint32) GO_PT_TO_EMU (height));
		/* buf + 52: compressed length */
		GSF_LE_SET_GUINT32 (buf +  52, blip->bytes.len);
		/* buf + 56: = 0 if compressed, 0xfe if not */
		/* buf + 57: = 0xfe - filter none */
		GSF_LE_SET_GUINT8 (buf +  57, 0xfe);
		ms_biff_put_var_write (bp, buf, sizeof buf);

		/* Write image data */
		excel_write_image_bytes (bp, &blip->bytes);
	}
}

static void 
excel_write_raster_blip (ExcelWriteState *ewb, BlipInf *blip, BlipType *bt)
{
	BiffPut	 *bp = ewb->bp;

	if (bp->version >= MS_BIFF_V8) {
		guint8 buf [RASTER_BLIP_HDR_LEN];
		
		memset (buf, 0, sizeof buf);
		memcpy (buf, bt->blip_tag, sizeof bt->blip_tag);
		GSF_LE_SET_GUINT16 (buf + 2, 0xf018 + bt->type);
		GSF_LE_SET_GUINT32 (buf + 4, 
				    blip->bytes.len + BLIP_ID_LEN + 1);
		memcpy(buf + 8, blip->id, sizeof blip->id);
		GSF_LE_SET_GUINT8 (buf + 24, 0xff);
		ms_biff_put_var_write (bp, buf, sizeof buf);

		/* Write image data */
		excel_write_image_bytes (bp, &blip->bytes);
	}
}

static BlipType bliptypes[] = 
{
	{"emf",  2, {0x40, 0x3d}, excel_write_vector_blip},
	{"wmf",  3, {0x60, 0x21}, excel_write_vector_blip},
	{"pict", 4, {0x20, 0x54}, excel_write_vector_blip},
	{"jpeg", 5, {0xa0, 0x46}, excel_write_raster_blip},
	{"png",  6, {0,    0x6e}, excel_write_raster_blip},
	{"dib",  7, {0x80, 0x7a}, excel_write_raster_blip}
};

static BlipType *
get_bliptype (const char *type)
{
	int n = sizeof bliptypes / sizeof bliptypes[0];
	int i;

	for (i = 0; i < n; i++)
		if (strcmp (type, bliptypes[i].type_name) == 0)
			return &bliptypes[i];
	return NULL;
}
static void 
excel_write_blip (ExcelWriteState *ewb, BlipInf *blip)
{
	BiffPut	 *bp = ewb->bp;
	BlipType *bt;

	if (bp->version >= MS_BIFF_V8) {
		static guint8 const header_obj_v8[] = {
/* BSE header  */ 0x2, 0, 7, 0xf0, 0, 0, 0, 0  /* fill in bliptype, length */
		};
		guint8 buf [44];
		guint8 win_type, mac_type;

		memset (buf, 0, sizeof buf);
		memcpy (buf, header_obj_v8, sizeof header_obj_v8);
		GSF_LE_SET_GUINT32 (buf +  4, 
				    blip->bytes.len + blip->header_len - 8);
		bt = get_bliptype (blip->type);
		if (!bt)
			return;

		win_type = mac_type = bt->type;
		if (bt->type == 4)
			win_type = 2;
		else if (bt->type == 2 || bt->type == 3)
			mac_type = 4;
		GSF_LE_SET_GUINT8  (buf, (bt->type << 4) + 2);
		GSF_LE_SET_GUINT8  (buf +  8, win_type);
		GSF_LE_SET_GUINT8  (buf +  9, mac_type);
		
		/* id (checksum) */
		mdfour(blip->id, blip->bytes.data, blip->bytes.len);
		memcpy(buf + 10, blip->id, sizeof blip->id);
		/* size */
		GSF_LE_SET_GUINT32  (buf +  28, 
				     blip->bytes.len + blip->header_len - 44);
		/* refcount */
		GSF_LE_SET_GUINT32  (buf +  32, 1);
		ms_biff_put_var_write (bp, buf, sizeof buf);
		
		bt->handler (ewb, blip, bt);
	}
}

/* FIXME: Store repeats only once. */
static void
excel_write_blips (ExcelWriteState *ewb, guint32 bliplen)
{
	BiffPut	*bp = ewb->bp;

	if (bp->version >= MS_BIFF_V8) {
		static guint8 const header_obj_v8[] = {
/* BStore header */ 0x1f, 0, 1, 0xf0, 0, 0, 0, 0  /* fill in #blips, length */
		};
		guint8 buf [sizeof header_obj_v8];
		GSList  *b;
		ExcelWriteSheet const *s;
		guint i, nblips;

		for (i = 0, nblips = 0; i < ewb->sheets->len; i++) {
			s = g_ptr_array_index (ewb->sheets, i);
			for (b = s->blips; b != NULL; b = b->next)
				if (b->data)
					nblips++;
		}
		
		memcpy (buf, header_obj_v8, sizeof header_obj_v8);
		GSF_LE_SET_GUINT8 (buf, (nblips << 4 | 0xf));
		GSF_LE_SET_GUINT32 (buf +  4, bliplen);
		ms_biff_put_var_write (bp, buf, sizeof header_obj_v8);

		for (i = 0; i < ewb->sheets->len; i++) {
			s = g_ptr_array_index (ewb->sheets, i);
			for (b = s->blips; b != NULL; b = b->next)
				if (b->data)
					excel_write_blip (ewb, 
							  (BlipInf *) b->data);
		}
	}
}

static void
excel_write_workbook (ExcelWriteState *ewb)
{
	BiffPut		*bp = ewb->bp;
	ExcelWriteSheet	*s = NULL;
	guint8 *data;
	unsigned i, n;

	ewb->streamPos = excel_write_BOF (ewb->bp, MS_BIFF_TYPE_Workbook);

	if (bp->version >= MS_BIFF_V8)
		ms_biff_put_2byte (ewb->bp, BIFF_INTERFACEHDR, bp->codepage);
	else
		ms_biff_put_empty (ewb->bp, BIFF_INTERFACEHDR);

	data = ms_biff_put_len_next (bp, BIFF_MMS, 2);
	GSF_LE_SET_GUINT16(data, 0);
	ms_biff_put_commit (bp);

	if (bp->version < MS_BIFF_V8) {
		ms_biff_put_empty (ewb->bp, BIFF_TOOLBARHDR);
		ms_biff_put_empty (ewb->bp, BIFF_TOOLBAREND);
	}

	ms_biff_put_empty (ewb->bp, BIFF_INTERFACEEND);

	excel_write_WRITEACCESS (ewb->bp);

	ms_biff_put_2byte (ewb->bp, BIFF_CODEPAGE, bp->codepage);
	if (bp->version >= MS_BIFF_V8) {
		ms_biff_put_2byte (ewb->bp, BIFF_DSF, ewb->double_stream_file ? 1 : 0);
		ms_biff_put_empty (ewb->bp, BIFF_XL9FILE);

		n = ewb->sheets->len;
		data = ms_biff_put_len_next (bp, BIFF_TABID, n * 2);
		for (i = 0; i < n; i++)
			GSF_LE_SET_GUINT16 (data + i*2, i + 1);
		ms_biff_put_commit (bp);

		if (ewb->export_macros) {
			ms_biff_put_empty (ewb->bp, BIFF_OBPROJ);
			excel_write_CODENAME (ewb, G_OBJECT (ewb->gnum_wb));
		}
	}

	ms_biff_put_2byte (ewb->bp, BIFF_FNGROUPCOUNT, 0x0e);

	if (bp->version < MS_BIFF_V8) {
		/* write externsheets for every sheet in the workbook
		 * to make our lives easier */
		excel_write_externsheets_v7 (ewb);

		/* assign indicies to the names before we export */
		ewb->tmp_counter = 0;
		excel_write_names (ewb);
	}

	ms_biff_put_2byte (ewb->bp, BIFF_WINDOWPROTECT, 0);
	ms_biff_put_2byte (ewb->bp, BIFF_PROTECT, 0);
	ms_biff_put_2byte (ewb->bp, BIFF_PASSWORD, 0);

	if (bp->version >= MS_BIFF_V8) {
		ms_biff_put_2byte (ewb->bp, BIFF_PROT4REV, 0);
		ms_biff_put_2byte (ewb->bp, BIFF_PROT4REVPASS, 0);
	}

	WORKBOOK_FOREACH_VIEW (ewb->gnum_wb, view,
		excel_write_WINDOW1 (bp, view););

	ms_biff_put_2byte (ewb->bp, BIFF_BACKUP, 0);
	ms_biff_put_2byte (ewb->bp, BIFF_HIDEOBJ, 0);
	ms_biff_put_2byte (ewb->bp, BIFF_1904,
		workbook_date_conv (ewb->gnum_wb)->use_1904 ? 1 : 0);
	ms_biff_put_2byte (ewb->bp, BIFF_PRECISION, 0x0001);
	ms_biff_put_2byte (ewb->bp, BIFF_REFRESHALL, 0);
	ms_biff_put_2byte (ewb->bp, BIFF_BOOKBOOL, 0);

	excel_write_FONTs (bp, ewb);
	excel_write_FORMATs (ewb);
	excel_write_XFs (ewb);

	if (bp->version >= MS_BIFF_V8)
		ms_biff_put_2byte (ewb->bp, BIFF_USESELFS, 0x01);
	write_palette (bp, ewb);

	for (i = 0; i < ewb->sheets->len; i++) {
		s = g_ptr_array_index (ewb->sheets, i);
	        s->boundsheetPos = excel_write_BOUNDSHEET (bp, s->gnum_sheet);
	}

	if (bp->version >= MS_BIFF_V8) {
		unsigned max_obj_id, num_objs;
		guint32 bliplen = 0;

		excel_write_COUNTRY (bp);

		excel_write_externsheets_v8 (ewb);

		ewb->tmp_counter = 0;
		excel_write_names (ewb);

		/* If there are any objects in the workbook add a header */
		num_objs = max_obj_id = 0;
		for (i = 0; i < ewb->sheets->len; i++) {
			GSList *b;

			s = g_ptr_array_index (ewb->sheets, i);
			if (s->num_objs > 0) {
				ewb->num_obj_groups++;
				max_obj_id = 0x400 * (ewb->num_obj_groups) | s->num_objs;
				num_objs += s->num_objs + 1;
			}
			for (b = s->blips; b; b = b->next)
				if (b->data) {
					BlipInf *bi = (BlipInf *) b->data;
					bliplen += (bi->header_len 
						    + bi->bytes.len);
				}
		}

		if (ewb->num_obj_groups > 0) {
			static guint8 const header[] = {
/* DggContainer */	0xf, 0, 0, 0xf0,	0, 0, 0, 0, /* fill in length */
/* Dgg */		  0, 0, 6, 0xf0,	0, 0, 0, 0, /* fill in length */
			};
			static guint8 const footer[] = {
/* OPT */		0x33, 0,  0xb, 0xf0,	0x12, 0, 0, 0,
				0xbf, 0,    8,  0, 8, 0, /* bool fFitTextToShape 191	= 0x00080008; */
				0x81, 1, 0x41,  0, 0, 8, /* colour fillColor 385	= 0x08000041; */
				0xc0, 1, 0x40,  0, 0, 8, /* colour lineColor 448	= 0x08000040; */
/* SplitMenuColors */	0x40, 0, 0x1e, 0xf1,	0x10, 0, 0, 0,
				0x0d, 0, 0, 8, 0x0c, 0, 0, 0x08,
				0x17, 0, 0, 8, 0xf7, 0, 0, 0x10
			};

			guint8 buf[16];

			ms_biff_put_var_next (bp, BIFF_MS_O_DRAWING_GROUP);
			memcpy (buf, header, sizeof header);
			
			GSF_LE_SET_GUINT32 (buf+ 4, 
					    (0x4a + ewb->num_obj_groups * 8 + 
					     ((bliplen > 0) ? (bliplen + 8) : 0)));
			GSF_LE_SET_GUINT32 (buf+12, 
					    (0x10 + ewb->num_obj_groups * 8));
			ms_biff_put_var_write (bp, buf, sizeof header);

			GSF_LE_SET_GUINT32 (buf+  0, (max_obj_id+1)); /* max_spid */
			GSF_LE_SET_GUINT32 (buf+  4, (ewb->num_obj_groups+1)); /* num_id_clust */
			GSF_LE_SET_GUINT32 (buf+  8, num_objs);	/* (c) */ /* num_shapes_saved */
			GSF_LE_SET_GUINT32 (buf+ 12, ewb->num_obj_groups); /* num_drawings_saved */
			ms_biff_put_var_write (bp, buf, 4*4);

			ewb->cur_obj_group = 0;
			for (i = 0; i < ewb->sheets->len; i++) {
				s = g_ptr_array_index (ewb->sheets, i);
				if (s->num_objs > 0) {
					ewb->cur_obj_group++;
					GSF_LE_SET_GUINT32 (buf+0, ewb->cur_obj_group);	/* dgid - DG owning the SPIDs in this cluster */
					GSF_LE_SET_GUINT32 (buf+4, s->num_objs+1); /* cspidCur - number of SPIDs used so far */
					ms_biff_put_var_write (bp, buf, 8);
				}
			}
			ewb->cur_obj_group = 0;

			if (bliplen > 0)
				excel_write_blips (ewb, bliplen);

			ms_biff_put_var_write (bp, footer, sizeof footer);
			ms_biff_put_commit (bp);
		}

		excel_write_SST (ewb);
	}

	ms_biff_put_empty (ewb->bp, BIFF_EOF);

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
		excel_write_workbook (ewb);
		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	} else
		gnm_cmd_context_error_export (GNM_CMD_CONTEXT (ewb->io_context),
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
		excel_write_workbook (ewb);
		ms_biff_put_destroy (ewb->bp);
		ewb->bp = NULL;
	} else
		gnm_cmd_context_error_export (GNM_CMD_CONTEXT (ewb->io_context),
			_("Couldn't open stream 'Workbook' for writing\n"));
}

/****************************************************************************/

static void
cb_check_names (gpointer key, GnmNamedExpr *nexpr, ExcelWriteState *ewb)
{
	if (nexpr->active && !nexpr->is_placeholder)
		excel_write_prep_expr (ewb, nexpr->expr);
}

static void
extract_gog_object_style (ExcelWriteState *ewb, GogObject *obj)
{
	GSList *ptr = obj->children;

	if (IS_GOG_STYLED_OBJECT (obj)) {
		GogStyle const *style = GOG_STYLED_OBJECT (obj)->style;
		if (style->interesting_fields & GOG_STYLE_OUTLINE)
			put_color_bgr (ewb, go_color_to_bgr (style->outline.color));
		else if (style->interesting_fields & GOG_STYLE_LINE)
			put_color_bgr (ewb, go_color_to_bgr (style->line.color));
		if (style->interesting_fields & GOG_STYLE_FILL)
			switch (style->fill.type) {
			default :
			case GOG_FILL_STYLE_NONE :
			case GOG_FILL_STYLE_IMAGE :
				break;
			case GOG_FILL_STYLE_PATTERN :
				put_color_bgr (ewb, go_color_to_bgr (style->fill.pattern.fore));
				put_color_bgr (ewb, go_color_to_bgr (style->fill.pattern.back));
				break;
			case GOG_FILL_STYLE_GRADIENT :
				put_color_bgr (ewb, go_color_to_bgr (style->fill.pattern.fore));
			}
		if (style->interesting_fields & GOG_STYLE_MARKER) {
				put_color_bgr (ewb, go_color_to_bgr (go_marker_get_outline_color (style->marker.mark)));
				put_color_bgr (ewb, go_color_to_bgr (go_marker_get_fill_color (style->marker.mark)));
		}

		if (style->interesting_fields & GOG_STYLE_FONT) {
		}
	}
	if (IS_GOG_AXIS (obj)) {
		char *fmt_str;
		g_object_get (G_OBJECT (obj), "assigned-format-string-XL", &fmt_str, NULL);
		if (fmt_str != NULL) {
			GnmFormat *fmt = style_format_new_XL (fmt_str, FALSE);
			if (!style_format_is_general (fmt))
				two_way_table_put (ewb->formats.two_way_table,
						   (gpointer)fmt, TRUE,
						   (AfterPutFunc) after_put_format,
						   "Found unique format %d - 0x%x\n");
			else
				style_format_unref (fmt);
		}
		g_free (fmt_str);
	}

	for ( ; ptr != NULL ; ptr = ptr->next)
		extract_gog_object_style (ewb, ptr->data);
}

/* extract markup for text objects. Has to happen early, so that the font 
 * gets saved */
static void
extract_txomarkup (ExcelWriteState *ewb, SheetObject *so)
{
	PangoAttrList *markup;
	GArray *txo;

	g_object_get (G_OBJECT (so), "markup", &markup, NULL);
	if (!markup) return;

	txo = txomarkup_new (ewb, markup, ewb->xf.default_style);
	/* It isn't a cell, but that doesn't matter here */
	g_hash_table_insert (ewb->cell_markup, (gpointer)so, txo);
	
}

static void cb_g_array_free (GArray *array) { g_array_free (array, TRUE); }
ExcelWriteState *
excel_write_state_new (IOContext *context, WorkbookView const *gwb_view,
		       gboolean biff7, gboolean biff8)
{
	ExcelWriteState *ewb = g_new (ExcelWriteState, 1);
	ExcelWriteSheet *esheet;
	Sheet		*sheet;
	GSList		*objs, *ptr;
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
	ewb->cell_markup  = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) cb_g_array_free);
	ewb->double_stream_file = biff7 && biff8;
	ewb->num_obj_groups = ewb->cur_obj_group = ewb->cur_blip = 0;

	ewb->fonts.two_way_table = two_way_table_new (
		excel_font_hash, excel_font_equal, 0,
		(GDestroyNotify) excel_font_free);
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

		if (sheet->sheet_type != GNM_SHEET_DATA)
			continue;

		if (esheet->validations != NULL)
			excel_write_prep_validations (esheet); /* validation */
		if (sheet->filters != NULL)
			excel_write_prep_sheet (ewb, sheet);	/* filters */
		objs = sheet_objects_get (sheet,
			NULL, SHEET_OBJECT_GRAPH_TYPE);
		for (ptr = objs ; ptr != NULL ; ptr = ptr->next)
			extract_gog_object_style (ewb,
				(GogObject *)sheet_object_graph_get_gog (ptr->data));
		g_slist_free (objs);
		for (ptr = esheet->textboxes ; ptr != NULL ; ptr = ptr->next)
			extract_txomarkup (ewb, ptr->data);
	}

	if (biff8) {
		ewb->sst.strings  = g_hash_table_new (g_direct_hash, g_direct_equal);
		ewb->sst.indicies = g_ptr_array_new ();
	} else {
		ewb->sst.strings  = NULL;
		ewb->sst.indicies = NULL;
	}
	pre_pass (ewb);

	return ewb;
}

void
excel_write_state_free (ExcelWriteState *ewb)
{
	unsigned i;

	if (ewb->fonts.two_way_table != NULL) {
		two_way_table_free (ewb->fonts.two_way_table);
		ewb->fonts.two_way_table = NULL;
	}
	formats_free (ewb);
	palette_free (ewb);
	xf_free  (ewb);

	for (i = 0; i < ewb->sheets->len; i++)
		excel_sheet_free (g_ptr_array_index (ewb->sheets, i));

	g_ptr_array_free (ewb->sheets, TRUE);
	g_hash_table_destroy (ewb->names);
	g_ptr_array_free (ewb->externnames, TRUE);
	g_hash_table_destroy (ewb->function_map);
	g_hash_table_destroy (ewb->sheet_pairs);
	g_hash_table_destroy (ewb->cell_markup);

	if (ewb->sst.strings != NULL) {
		g_hash_table_destroy (ewb->sst.strings);
		g_ptr_array_free (ewb->sst.indicies, TRUE);
	}

	g_free (ewb);
}

