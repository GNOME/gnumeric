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

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include <config.h>
#include <gnome.h>

#include "gnumeric.h"
#include "gnumeric-util.h"
#include "format.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"
#include "main.h"
#include "utils.h"
#include "print-info.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "excel.h"
#include "ms-excel-write.h"
#include "ms-excel-xf.h"
#include "ms-formula-write.h"

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
int
biff_put_text (BiffPut *bp, const char *txt, eBiff_version ver,
	       gboolean write_len, PutType how)
{
#define BLK_LEN 16

	guint8 data[BLK_LEN];
	guint32 lp, len, ans;

	gboolean sixteen_bit_len;
	gboolean unicode;
	guint32  off;

	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (txt, 0);

	ans = 0;
	len = strlen (txt);
/*	printf ("Write '%s' len = %d\n", txt, len); */

	if ((how == AS_PER_VER &&
	     ver >= eBiffV8) ||
	    how == SIXTEEN_BIT)
		sixteen_bit_len = TRUE;
	else
		sixteen_bit_len = FALSE; /* 8 bit */

	if (ver >= eBiffV8)
		unicode = TRUE;
	else
		unicode = FALSE;

	off = 0;
	if (write_len) {
		if (sixteen_bit_len) {
			MS_OLE_SET_GUINT16 (data, len);
			off = 2;
		} else {
			g_return_val_if_fail (len<256, 0);
			MS_OLE_SET_GUINT8  (data, len);
			off = 1;
		}
	}

	if (unicode) {
		MS_OLE_SET_GUINT8  (data + off, 0x0);
		off++;
	}
	ms_biff_put_var_write (bp, data, off);

/* You got it coming */
	for (lp = 0; lp < len; lp++) {
		MS_OLE_SET_GUINT16 (data, txt[lp]);
		ms_biff_put_var_write (bp, data, unicode?2:1);
	}
	return off + len*(unicode?2:1);

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
biff_bof_write (BiffPut *bp, eBiff_version ver,
		eBiff_filetype type)
{
	guint   len;
	guint8 *data;
	MsOlePos ans;

	if (ver >= eBiffV8)
		len = 16;
	else
		len = 8;

	data = ms_biff_put_len_next (bp, 0, len);

	ans = bp->streamPos;

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
		if (ver == eBiffV8 || /* as per the spec. */
		    (ver == eBiffV7 && type == eBiffTWorksheet)) /* Wierd hey */
			MS_OLE_SET_GUINT16 (data, 0x0600);
		else
			MS_OLE_SET_GUINT16 (data, 0x0500);
		break;
	default:
		g_warning ("Unknown version\n");
		break;
	}

	switch (type)
	{
	case eBiffTWorkbook:
		MS_OLE_SET_GUINT16 (data+2, 0x0005);
		break;
	case eBiffTVBModule:
		MS_OLE_SET_GUINT16 (data+2, 0x0006);
		break;
	case eBiffTWorksheet:
		MS_OLE_SET_GUINT16 (data+2, 0x0010);
		break;
	case eBiffTChart:
		MS_OLE_SET_GUINT16 (data+2, 0x0020);
		break;
	case eBiffTMacrosheet:
		MS_OLE_SET_GUINT16 (data+2, 0x0040);
		break;
	case eBiffTWorkspace:
		MS_OLE_SET_GUINT16 (data+2, 0x0100);
		break;
	default:
		g_warning ("Unknown type\n");
		break;
	}

	/* Magic version numbers: build date etc. */
	switch (ver) {
	case eBiffV8:
		MS_OLE_SET_GUINT16 (data+4, 0x0dbb);
		MS_OLE_SET_GUINT16 (data+6, 0x07cc);
		g_assert (len > 11);
		/* Quandry: can we tell the truth about our history */
		MS_OLE_SET_GUINT32 (data+ 8, 0x00000004);
		MS_OLE_SET_GUINT16 (data+12, 0x06000908); /* ? */
		break;
	case eBiffV7:
	case eBiffV5:
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
write_magic_interface (BiffPut *bp, eBiff_version ver)
{
	guint8 *data;
	if (ver >= eBiffV7) {
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

static void
write_externsheets (BiffPut *bp, ExcelWorkbook *wb, ExcelSheet *ignore)
{
	guint32 num_sheets = wb->sheets->len;
	guint8 *data;
	int lp;

	if (wb->ver > eBiffV7) {
		printf ("Need some cunning BiffV8 stuff ?\n");
		return;
	}

	if (ignore) /* Strangely needed */
		num_sheets--;

	g_assert (num_sheets < 0xffff);

	data = ms_biff_put_len_next (bp, BIFF_EXTERNCOUNT, 2);
	MS_OLE_SET_GUINT16(data, num_sheets);
	ms_biff_put_commit (bp);

	for (lp = 0; lp < num_sheets; lp++) {
		ExcelSheet *sheet = g_ptr_array_index (wb->sheets, lp);
		gint len = strlen (sheet->gnum_sheet->name);
		guint8 data[8];

		if (sheet == ignore) continue;

		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		MS_OLE_SET_GUINT8(data, len);
		MS_OLE_SET_GUINT8(data + 1, 3); /* Magic */
		ms_biff_put_var_write (bp, data, 2);
		biff_put_text (bp, sheet->gnum_sheet->name, wb->ver, FALSE, AS_PER_VER);
		ms_biff_put_commit (bp);
	}
}

static void
write_window1 (BiffPut *bp, eBiff_version ver)
{
	/* See: S59E17.HTM */
	guint8 *data = ms_biff_put_len_next (bp, BIFF_WINDOW1, 18);
	MS_OLE_SET_GUINT16 (data+  0, 0x0000);
	MS_OLE_SET_GUINT16 (data+  2, 0x0000);
	MS_OLE_SET_GUINT16 (data+  4, 0x2c4c);
	MS_OLE_SET_GUINT16 (data+  6, 0x198c);
	MS_OLE_SET_GUINT16 (data+  8, 0x0038); /* various flags */
	MS_OLE_SET_GUINT16 (data+ 10, 0x0000); /* selected tab */
	MS_OLE_SET_GUINT16 (data+ 12, 0x0000); /* displayed tab */
	MS_OLE_SET_GUINT16 (data+ 14, 0x0001);
	MS_OLE_SET_GUINT16 (data+ 16, 0x0258);
	ms_biff_put_commit (bp);
}

static void
write_bits (BiffPut *bp, ExcelWorkbook *wb, eBiff_version ver)
{
	guint8 *data;
	const char *fsf = "The Free Software Foundation";
	char pad [WRITEACCESS_LEN];

	/* See: S59E1A.HTM */
	g_assert (strlen (fsf) < WRITEACCESS_LEN);
	memset (pad, ' ', sizeof pad);
	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	biff_put_text (bp, fsf, ver, TRUE, AS_PER_VER);
	ms_biff_put_var_write (bp, pad, WRITEACCESS_LEN - strlen (fsf) - 1);
	ms_biff_put_commit (bp);

	/* See: S59D66.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CODEPAGE, 2);
	MS_OLE_SET_GUINT16 (data, 0x04e4); /* ANSI */
	ms_biff_put_commit (bp);

	if (ver >= eBiffV8) { /* See S59D78.HTM */
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

	if (ver >= eBiffV8) {
		/* See: S59DD2.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PROT4REV, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);

		/* See: S59DD3.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PROT4REVPASS, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
	}

	write_window1 (bp, ver);

	if (ver >= eBiffV8 && 0 /* if we have panes */) {
		/* See: S59DCA.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PANE, 2);
		MS_OLE_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
	}

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
		ExcelSheet *sheet = g_ptr_array_index (wb->sheets, lp);
		g_return_val_if_fail (sheet, 0);
		if (sheet->gnum_sheet == gnum_sheet)
			return lp;
	}
	g_warning ("No associated sheet for %p\n", gnum_sheet);
	return 0;
}

int
ms_excel_write_get_externsheet_idx (ExcelWorkbook *wb,
				    Sheet *sheeta,
				    Sheet *sheetb)
{
	g_warning ("Get Externsheet not implemented yet\n");
	return 0;
}

/**
 * Returns stream position of start.
 * See: S59D61.HTM
 **/
static guint32
biff_boundsheet_write_first (BiffPut *bp, eBiff_filetype type,
			     char *name, eBiff_version ver)
{
	guint32 pos;
	guint8 data[16];

	ms_biff_put_var_next (bp, BIFF_BOUNDSHEET);
	pos = bp->streamPos;

	MS_OLE_SET_GUINT32 (data, 0xdeadbeef); /* To be stream start pos */
	switch (type) {
	case eBiffTWorksheet:
		MS_OLE_SET_GUINT8 (data+4, 00);
		break;
	case eBiffTMacrosheet:
		MS_OLE_SET_GUINT8 (data+4, 01);
		break;
	case eBiffTChart:
		MS_OLE_SET_GUINT8 (data+4, 02);
		break;
	case eBiffTVBModule:
		MS_OLE_SET_GUINT8 (data+4, 06);
		break;
	default:
		g_warning ("Duff type\n");
		break;
	}
	MS_OLE_SET_GUINT8 (data+5, 0); /* Visible */
	ms_biff_put_var_write (bp, data, 6);

	biff_put_text (bp, name, ver, TRUE, AS_PER_VER);

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
style_color_to_int (const StyleColor *c)
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
#ifndef NO_DEBUG_EXCEL
	if (was_added) {
		if (ms_excel_write_debug > 2) {
			printf (tmpl, index, c);
		}
	}
#endif
}

/**
 * Put Excel default colors to palette table
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
		idx = 0;
	} else if (c == 0xffffff) {
		idx = 1;
	} else {
		idx = two_way_table_key_to_idx (twt, (gconstpointer) c);
		if (idx < EXCEL_DEF_PAL_LEN) {
			idx += 8;
		} else {
			idx = 0;
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
	gpointer pc = GUINT_TO_POINTER (style_color_to_int (c));
	gint idx;

	two_way_table_put (twt, pc, TRUE,
			   (AfterPutFunc) log_put_color,
			   "Found unique color %d - 0x%6.6x\n");

	idx = two_way_table_key_to_idx (twt, pc);
	if (idx >= 0 && idx < EXCEL_DEF_PAL_LEN) {
		wb->pal->entry_in_use [idx] = TRUE; /* Default entry in use */
	}
}

/**
 * Add colors in mstyle to palette
 **/
static void
put_colors (MStyle *st, gconstpointer dummy, ExcelWorkbook *wb)
{
	int i;
	const MStyleBorder * b;

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
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_write_debug > 2) {
					printf ("Custom color %d (0x%6.6x)"
						" moved to unused index %d\n",
						i, color, j);
				}
#endif
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
	static char buf[64];
	char* fstyle = "";

	if (sf->is_bold && sf->is_italic)
		fstyle = ", bold, italic";
	else if (sf->is_bold)
		fstyle = ", bold";
	else if (sf->is_italic)
		fstyle = ", italic";
	snprintf (buf, sizeof buf, "%s, %g %s",
		  sf->font_name, sf->size, fstyle);

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
	f->color = style_color_to_int (c);

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
		res = style_font_hash_func (font->style_font) ^ font->color;

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
			&& (fa->color == fb->color);
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
	int i;
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
after_put_font (ExcelFont *f, gboolean was_added, gint index, gpointer dummy)
{
	if (was_added) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 1) {
			printf ("Found unique font %d - %s\n",
			index, excel_font_to_string (f));
		}
#endif
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
	guint32 size  = sf->size * 20;
	guint16 grbit = 0;
	guint16 color = palette_get_index (wb, f->color);

	guint16 boldstyle = 0x190; /* Normal boldness */
	guint16 subsuper  = 0;   /* 0: Normal, 1; Super, 2: Sub script*/
	guint8  underline = 0;	 /* No underline */
	guint8  family    = 0;
	guint8  charset   = 0;	 /* Seems OK. */
	char    *font_name = sf->font_name;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Writing font %s, color idx %u\n",
			excel_font_to_string (f), color);
	}
#endif

	if (sf->is_italic)
		grbit |= 1 << 1;
	if (sf->is_bold)
		boldstyle = 0x2bc;

	ms_biff_put_var_next (bp, BIFF_FONT);
	MS_OLE_SET_GUINT16 (data + 0, size);
	MS_OLE_SET_GUINT16 (data + 2, grbit);
	MS_OLE_SET_GUINT16 (data + 4, color);
	MS_OLE_SET_GUINT16 (data + 6, boldstyle);
	MS_OLE_SET_GUINT16 (data + 8, subsuper);
	MS_OLE_SET_GUINT8  (data + 10, underline);
	MS_OLE_SET_GUINT8  (data + 11, family);
	MS_OLE_SET_GUINT8  (data + 12, charset);
	MS_OLE_SET_GUINT8  (data +13, 0);
	ms_biff_put_var_write (bp, data, 14);

	biff_put_text (bp, font_name, wb->ver, TRUE, EIGHT_BIT);

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
after_put_format (char *format, gboolean was_added, gint index,
		  const char *tmpl)
{
	if (was_added) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 2) {
			printf (tmpl, index, format);
		}
#endif
	} else {
		g_free (format);
	}
}

/**
 * Add built-in formats to format table
 **/
static void
formats_put_magic (ExcelWorkbook *wb)
{
	int i;
	char *fmt;

	for (i = 0; i < EXCEL_BUILTIN_FORMAT_LEN; i++) {
		fmt = excel_builtin_formats[i];
		if (!fmt || strlen (fmt) == 0)
			fmt = "General";
		two_way_table_put (wb->formats->two_way_table,
				   g_strdup (fmt),
				   FALSE, /* Not unique */
				   (AfterPutFunc) after_put_format,
				   "Magic format %d - %s\n");
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
		= two_way_table_new (g_str_hash, g_str_equal, 0);

	formats_put_magic (wb);
}


/**
 * Get a format, given index
 **/
static char *
formats_get_format (ExcelWorkbook *wb, gint idx)
{
	TwoWayTable *twt = wb->formats->two_way_table;

	return (char *) two_way_table_idx_to_key (twt, idx);
}

/**
 * Free format table
 **/
static void
formats_free (ExcelWorkbook *wb)
{
	TwoWayTable *twt;
	int i;
	char *format;

	if (wb && wb->formats) {
		twt = wb->formats->two_way_table;
		if (twt) {
			for (i = 0; i < twt->idx_to_key->len; i++) {
				format = formats_get_format (wb,
							     i + twt->base);
				g_free (format);
			}
			two_way_table_free (twt);
		}
		g_free (wb->formats);
		wb->formats = NULL;
	}
}

/**
 * Get index of a format
 **/
static gint
formats_get_index (ExcelWorkbook *wb, const char *format)
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
	StyleFormat *sf = mstyle_get_format (mstyle);

	two_way_table_put (wb->formats->two_way_table,
			   g_strdup (sf->format), TRUE,
			   (AfterPutFunc) after_put_format,
			   "Found unique format %d - %s\n");
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
	guint8 data[64];
	char *format = formats_get_format(wb, fidx);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Writing format 0x%x: %s\n", fidx, format);
	}
#endif
	/* Kludge for now ... */
	if (wb->ver >= eBiffV7)
		ms_biff_put_var_next (bp, (0x400|BIFF_FORMAT));
	else
		ms_biff_put_var_next (bp, BIFF_FORMAT);

	MS_OLE_SET_GUINT16 (data, fidx);
	ms_biff_put_var_write (bp, data, 2);

	biff_put_text (bp, format, eBiffV7, TRUE, AS_PER_VER);
	ms_biff_put_commit (bp);
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
	int nformats = twt->idx_to_key->len;
	int i;
	int   magic_num [] = { 5, 6, 7, 8, 0x2a, 0x29, 0x2c, 0x2b };

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
cell_used_map_new (ExcelSheet *sheet)
{
	long nwords;
	gpointer ptr = NULL;
	if (sheet->maxx > 0 && sheet->maxy> 0) {
		nwords = (sheet->maxx * sheet->maxy - 1) / 32 + 1;
		ptr = g_malloc0 (nwords * 4);
	}
	return ptr;
}

/**
 * Mark cell in use in bitmap
 **/
static void
cell_mark_used (ExcelSheet *sheet, int col, int row)
{
	long bit_ix = row * sheet->maxx + col;
	long word_ix = bit_ix / 32;
	int  rem     = bit_ix % 32;

	if (sheet && sheet->cell_used_map)
		*((guint32 *) sheet->cell_used_map + word_ix) |= 1 << rem;
}

/**
 * Return true if cell marked in use in bitmap
 **/
static gboolean
cell_is_used (const ExcelSheet *sheet, int col, int row)
{
	long bit_ix = row * sheet->maxx + col;
	long word_ix = bit_ix / 32;
	int  rem     = bit_ix % 32;
	gboolean ret = FALSE;

	if (sheet && sheet->cell_used_map)
		ret = 1 & *((guint32 *) sheet->cell_used_map + word_ix) >> rem;

	return ret;
}

/**
 * Get default MStyle of sheet
 *
 * FIXME: This works now. But only because the default style for a
 * sheet or workbook can't be changed. Unfortunately, there is no
 * proper API for accessing the default style of an existing sheet.
 **/
static MStyle *
get_default_mstyle ()
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
	int i;

	if (wb && wb->xf) {
		if (wb->xf->two_way_table) {
			twt = wb->xf->two_way_table;
			for (i = 0; i < twt->idx_to_key->len; i++) {
				st = xf_get_mstyle (wb, i + twt->base);
				mstyle_unref (st);
			}
			two_way_table_free (wb->xf->two_way_table);
		}
		g_free (wb->xf);
		wb->xf = NULL;
	}
}

/**
 * Callback called when putting MStyle to table. Print to debug log when
 * style is added. Free resources when it was already there.
 **/
static void
after_put_mstyle (MStyle *st, gboolean was_added, gint index, gpointer dummy)
{
	if (was_added) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 1) {
			printf ("Found unique mstyle %d\n", index);
			mstyle_dump (st);
		}
#endif
	} else {
		mstyle_unref (st);
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
excel_cell_get (ExcelSheet *sheet, int col, int row)
{
	return *(sheet->cells + row) + col;
}

/**
 * Add MStyle of cell to table if not already there. Cache some info.
 **/
static void
pre_cell (gconstpointer dummy, Cell *cell, ExcelSheet *sheet)
{
	ExcelCell *c;
	int col, row;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (sheet != NULL);

	col = cell->col->pos;
	row = cell->row->pos;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 3) {
		printf ("Pre cell %s\n", cell_name (col, row));
	}
#endif
	cell_mark_used (sheet, col, row);
	if (cell->parsed_node)
		ms_formula_build_pre_data (sheet, cell->parsed_node);

	/* Save cell pointer */
	c = excel_cell_get (sheet, col, row);
	c->gnum_cell = cell;
	c->xf = put_mstyle (sheet->wb, cell_get_mstyle (cell));
}

/**
 * Add MStyle of blank cell to table if not already there.
 **/
static void
pre_blank (ExcelSheet *sheet, int col, int row)
{
	ExcelCell *c = excel_cell_get (sheet, col, row);

	MStyle *st = sheet_style_compute (sheet->gnum_sheet,
				      col, row);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 3) {
		printf ("Pre blank %s\n", cell_name (col, row));
	}
#endif
	c->gnum_cell = NULL;
	c->xf = put_mstyle (sheet->wb, st);
}

/**
 * Add MStyles of all blank cells to table if not already there.
 **/
static void
pre_blanks (ExcelSheet *sheet)
{
	int row, col;

	for (row = 0; row < sheet->maxy; row++)
		for (col = 0; col < sheet->maxx; col++)
			if (!cell_is_used (sheet, col, row))
				pre_blank (sheet, col, row);
}

/**
 * Add all MStyles in workbook to table
 *
 * First, we invoke pre_cell on each cell in the cell hash. This
 * computes the style for each non-blank cell, and adds the style to
 * the table.
 *
 * We also need styles for blank cells, so we let pre_blanks scan all
 * blank cell positions, limited by maxx and maxy.
 *
 * To see what cells are blank, we use the cell_used_map, where
 * pre_cell sets a bit for each cell which is in use. This should be
 * somewhat faster than just visiting each cell postion in sequence.
 *
 * Another optimization we do: When writing to file, we need the cell
 * pointer and the XF style index for each cell. To avoid having to
 * locate the cell pointer and computing the style once more, we cache
 * the cell pointer and XF index in en ExcelCell in the cells table.
 **/
static void
gather_styles (ExcelWorkbook *wb)
{
	int i;

	for (i = 0; i < wb->sheets->len; i++) {
		ExcelSheet *s = g_ptr_array_index (wb->sheets, i);

		/* Gather style info from cells and blanks */
		g_hash_table_foreach (s->gnum_sheet->cell_hash,
				      (GHFunc) pre_cell, s);
		pre_blanks (s);
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
			      i < (sizeof(map_to_excel)/sizeof(int)), 0);

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
		ialign = eBiffHAGeneral;
		break;
	case HALIGN_LEFT:
		ialign = eBiffHALeft;
		break;
	case HALIGN_RIGHT:
		ialign = eBiffHARight;
		break;
	case HALIGN_CENTER:
		ialign = eBiffHACenter;
		break;
	case HALIGN_FILL:
		ialign = eBiffHAFill;
		break;
	case HALIGN_JUSTIFY:
		ialign = eBiffHAJustify;
		break;
	default:
		ialign = eBiffHAGeneral;
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
		ialign = eBiffVATop;
		break;
	case VALIGN_BOTTOM:
		ialign = eBiffVABottom;
		break;
	case VALIGN_CENTER:
		ialign = eBiffVACenter;
		break;
	case VALIGN_JUSTIFY:
		ialign = eBiffVAJustify;
		break;
	default:
		ialign = eBiffVATop;
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
		ior = eBiffOHoriz;
		break;
	case ORIENT_VERT_HORIZ_TEXT:
		ior = eBiffOVertHorizText;
		break;
	case ORIENT_VERT_VERT_TEXT:
		ior = eBiffOVertVertText;
		break;
	case ORIENT_VERT_VERT_TEXT2:
		ior = eBiffOVertVertText2;
		break;
	default:
		ior = eBiffOHoriz;
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
border_type_to_excel (StyleBorderType btype, eBiff_version ver)
{
	guint ibtype = btype;

	if (btype <= STYLE_BORDER_NONE)
		ibtype = STYLE_BORDER_NONE;

	if (ver <= eBiffV7) {
		if (btype > STYLE_BORDER_HAIR)
			ibtype = STYLE_BORDER_MEDIUM;
	}

	return ibtype;
}

/**
 * fixup_fill_colors
 * @xfd  XF data
 *
 * Do yucky stuff with fill foreground and background colors
 *
 * Solid fill patterns seem to reverse the meaning of foreground and
 * background
 *
 * FIXME:
 * Import side code does not flip colors if xfd->pat_foregnd_col == 0.
 *
 * This table shows import side behaviour when fill pattern is 1:
 *
 * bg(file) fg(file)    bg(internal) fg(internal)
 *  == 0     == 0         0            0
 *  == 0     != 0         fg(file)     0
 *  != 0     == 0         bg(file)     0
 *  != 0     != 0         fg(file)     bg(file)
 *
 * We can see from the table that bg(internal) is 0 only if fg(internal)
 * is also 0 .
 *
 * For export, the following two cases are obvious
 *  bg(internal)  fg(internal)     bg(file)  fg(file)
 *     == 0           == 0            0         0
 *     != 0           != 0        fg(internal)  bg(internal)
 * This one looks like we can choose, but testing with Excel showed that we
 * do have to reverse colors:
 *  bg(internal)  fg(internal)
 *     != 0           == 0
 *
 * Finally,  we have to do something special for bg(internal) == 0. In
 * this situation, I have seen Excel flip colors, but use 8 rather than 0
 * for black, i.e. fg(file) = 8, bg(file) = fg(internal). This is what
 * we'll do, although I don't know if Excel always does.
 *
 * This makes us compatible with our own import code.  The import
 * side test can't be correct, though. Excel displays fg(file) = 0,
 * bg(file) = 1 as black (=0) background. Gnumeric displays background
 * as white, since fg(file) = 0.  But I'll leave this ugliness in for
 * now.
 **/
static void
fixup_fill_colors (BiffXFData *xfd)
{
	guint8 c;

	if ((xfd->fill_pattern_idx == 1)
	    && ((xfd->pat_foregnd_col != PALETTE_BLACK)
		|| (xfd->pat_backgnd_col != PALETTE_BLACK))) {
		c = xfd->pat_backgnd_col;
		if (c == PALETTE_BLACK)
			c = PALETTE_ALSO_BLACK;
		xfd->pat_backgnd_col = xfd->pat_foregnd_col;
		xfd->pat_foregnd_col = c;
	}
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
		xfd->differences |= 1 << eBiffDFormatbit;
	if (xfd->font_idx != FONT_MAGIC)
		xfd->differences |= 1 << eBiffDFontbit;
	/* hmm. documentation doesn't say that alignment bit is
	   affected by vertical alignment, but it's a reasonable guess */
	if (xfd->halign != HALIGN_GENERAL || xfd->valign != VALIGN_TOP
	    || xfd->wrap)
		xfd->differences |= 1 << eBiffDAlignbit;
	for (i = 0; i < STYLE_ORIENT_MAX; i++) {
		/* Should we also test colors? */
		if (xfd->border_type[i] != BORDER_MAGIC) {
			xfd->differences |= 1 << eBiffDBorderbit;
			break;
		}
	}
	if (xfd->pat_foregnd_col != PALETTE_BLACK
	    || xfd->pat_backgnd_col != PALETTE_WHITE
	    || xfd->fill_pattern_idx != FILL_MAGIC)
		xfd->differences |= 1 << eBiffDFillbit;
	if (xfd->hidden || xfd->locked)
		xfd->differences |= 1 << eBiffDLockbit;
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

		printf ("Writing xf 0x%x : font 0x%x (%s), format 0x%x (%s)\n",
			idx, xfd->font_idx, excel_font_to_string (f),
			xfd->format_idx, xfd->style_format->format);
		printf (" hor align 0x%x, ver align 0x%x, wrap %s\n",
			xfd->halign, xfd->valign, xfd->wrap ? "on" : "off");
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
 * - hidden and locked - not yet in gnumeric.
 *
 * Apart from font, the style elements we retrieve do *not* need to be unrefed.
 *
 * FIXME:
 * It may be possible to recognize auto contrast for a few simple cases.
 **/
static void
build_xf_data (ExcelWorkbook *wb, BiffXFData *xfd, MStyle *st)
{
	ExcelFont *f;
	const MStyleBorder *b;
	int pat;
	StyleColor *pattern_color;
	StyleColor *back_color;
	guint pattern_pal_color;
	guint back_pal_color;
	guint c;
	int i;

	memset (xfd, 0, sizeof *xfd);

	xfd->parentstyle  = XF_MAGIC;
	xfd->mstyle       = st;
	f = excel_font_new (st);
	xfd->font_idx     = fonts_get_index (wb, f);
	excel_font_free (f);
	xfd->style_format = mstyle_get_format (st);
	xfd->format_idx   = formats_get_index (wb, xfd->style_format->format);

	/* Hidden and locked - we don't have those yet */
	xfd->hidden = eBiffHVisible;
	xfd->locked = eBiffLUnlocked;

	xfd->halign = mstyle_get_align_h (st);
	xfd->valign = mstyle_get_align_v (st);
	xfd->wrap   = mstyle_get_fit_in_cell (st);
	xfd->orientation = mstyle_get_orientation (st);

	/* Borders */
	for (i = STYLE_TOP; i < STYLE_ORIENT_MAX; i++) {
		xfd->border_type[i]  = STYLE_BORDER_NONE;
		xfd->border_color[i] = PALETTE_BLACK;
		b = mstyle_get_border (st, MSTYLE_BORDER_TOP + i);
		if (b) {
			xfd->border_type[i] = b->line_type;
			if (b->color) {
				c = style_color_to_int (b->color);
				xfd->border_color[i]
					= palette_get_index (wb, c);
			}

		}
	}

	pat = mstyle_get_pattern (st);
	xfd->fill_pattern_idx = (map_pattern_index_to_excel (pat));

	pattern_color = mstyle_get_color (st, MSTYLE_COLOR_PATTERN);
	back_color   = mstyle_get_color (st, MSTYLE_COLOR_BACK);
	if (pattern_color) {
		pattern_pal_color = style_color_to_int (pattern_color);
	} else {
		pattern_pal_color = PALETTE_BLACK;
	}
	if (back_color) {
		back_pal_color = style_color_to_int (back_color);
	} else {
		back_pal_color = PALETTE_WHITE;
	}
	xfd->pat_backgnd_col = palette_get_index (wb, back_pal_color);
	xfd->pat_foregnd_col = palette_get_index (wb, pattern_pal_color);

	/* Solid patterns seem to reverse the meaning */
	fixup_fill_colors (xfd);

	get_xf_differences (wb, xfd, wb->xf->default_style);
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
write_xf_magic_record (BiffPut *bp, eBiff_version ver, int idx)
{
	guint8 data[256];
	int lp;

	for (lp = 0; lp < 250; lp++)
		data[lp] = 0;

	if (ver >= eBiffV7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (ver >= eBiffV8) {
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

	for (lp = 0; lp < 250; lp++)
		data[lp] = 0;

	if (wb->ver >= eBiffV7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (wb->ver >= eBiffV8) {
		MS_OLE_SET_GUINT16 (data+0, xfd->font_idx);
		MS_OLE_SET_GUINT16 (data+2, xfd->format_idx);
		MS_OLE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		MS_OLE_SET_GUINT16 (data+0, xfd->font_idx);
		MS_OLE_SET_GUINT16 (data+2, xfd->format_idx);

		/* According to doc, 1 means locked, but it's 1 also for
		 * unlocked cells. Presumably, locking becomes effective when
		 * the locking bit in differences is also set */
		itmp = 0x0001;
		if (xfd->hidden != eBiffHVisible)
			itmp |= 1 << 1;
		if (xfd->locked != eBiffLUnlocked)
			itmp |= 1;
		itmp |= (xfd->parentstyle << 4) & 0xFFF0; /* Parent style */
		MS_OLE_SET_GUINT16(data+4, itmp);

		/* Horizontal alignment */
		itmp  = halign_to_excel (xfd->halign) & 0x7;
		if (xfd->wrap)	/* Wrapping */
			itmp |= 1 << 3;
		/* Vertical alignment */
		itmp |= (valign_to_excel (xfd->valign) << 4) & 0x70;
		itmp |= (orientation_to_excel (xfd->orientation) << 8)
			 & 0x300;
		itmp |= xfd->differences & 0xFC00; /* Difference bits */
		MS_OLE_SET_GUINT16(data+6, itmp);

		itmp = 1 << 13; /* fSxButton bit - apparently always set */
		/* Fill pattern foreground color */
		itmp |= xfd->pat_foregnd_col & 0x7f;
		/* Fill pattern background color */
		itmp |= (xfd->pat_backgnd_col << 7) & 0x1f80;
		MS_OLE_SET_GUINT16(data+8, itmp);

		itmp  = xfd->fill_pattern_idx & 0x3f;

		/* Borders */
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_BOTTOM],
					       wb->ver)
			  << 6) & 0x1c0;
		itmp |= (xfd->border_color[STYLE_BOTTOM] << 9) & 0xfe00;
		MS_OLE_SET_GUINT16(data+10, itmp);

		itmp  = border_type_to_excel (xfd->border_type[STYLE_TOP],
					      wb->ver)
			& 0x7;
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_LEFT],
					       wb->ver)
			 << 3) & 0x38;
		itmp |= (border_type_to_excel (xfd->border_type[STYLE_RIGHT],
					       wb->ver)
			 << 6) & 0x1c0;
		itmp |= (xfd->border_color[STYLE_TOP] << 9) & 0xfe00;
		MS_OLE_SET_GUINT16(data+12, itmp);

		itmp  = xfd->border_color[STYLE_LEFT] & 0x7f;
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
		guint8 *data = ms_biff_put_len_next (bp, BIFF_STYLE, 4);
		MS_OLE_SET_GUINT32 (data, style_magic[lp]); /* cop out */
		ms_biff_put_commit (bp);
	}

	/* See: S59E14.HTM */
	if (wb->ver >= eBiffV8) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_USESELFS, 2);
		MS_OLE_SET_GUINT16 (data, 0x1); /* we are language naturals */
		ms_biff_put_commit (bp);
	}
}

static void
write_names (BiffPut *bp, ExcelWorkbook *wb)
{
	Workbook* gwb = wb->gnum_wb;
	GList *names = gwb->names;
	ExcelSheet *sheet;

	g_return_if_fail (wb->ver <= eBiffV7);

	/* excel crashes if this isn't here and the names have Ref3Ds */
	if (names)
		write_externsheets (bp, wb, NULL);
	sheet = g_ptr_array_index (wb->sheets, 0);

	while (names) {
		guint8 data[20];
		guint32 len, name_len, i;

		ExprName    *expr_name = names->data;
		char *text;
		g_return_if_fail (expr_name != NULL);

		for (i = 0; i < 20; i++) data[i] = 0;

		text = expr_name->name->str;
		ms_biff_put_var_next (bp, BIFF_NAME);
		name_len = strlen (expr_name->name->str);
		MS_OLE_SET_GUINT8 (data + 3, name_len); /* name_len */

		/* This code will only work for eBiffV7. */
		ms_biff_put_var_write (bp, data, 14);
		biff_put_text (bp, text, wb->ver, FALSE, AS_PER_VER);
		ms_biff_put_var_seekto (bp, 14 + name_len);
		len = ms_excel_write_formula (bp, sheet,
					      expr_name->t.expr_tree,
					      0, 0);
		g_assert (len <= 0xffff);
		ms_biff_put_var_seekto (bp, 4);
		MS_OLE_SET_GUINT16 (data, len);
		ms_biff_put_var_write (bp, data, 2);
		ms_biff_put_commit (bp);

		g_ptr_array_add (wb->names, g_strdup(text));

		names = g_list_next(names);
	}
}

int
ms_excel_write_map_errcode (Value const * const v)
{
	char const * const mesg = v->v.error.mesg->str;
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
write_value (BiffPut *bp, Value *v, eBiff_version ver,
	     guint32 col, guint32 row, guint16 xf)
{
	switch (v->type) {

	case VALUE_EMPTY:
	{
	    guint8 *data = ms_biff_put_len_next (bp, (0x200 | BIFF_BLANK), 6);
	    EX_SETROW(data, row);
	    EX_SETCOL(data, col);
	    EX_SETXF (data, xf);
	    break;
	}
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
	{
		guint8 *data = ms_biff_put_len_next (bp, (0x200 | BIFF_BOOLERR), 8);
		EX_SETROW(data, row);
		EX_SETCOL(data, col);
		EX_SETXF (data, xf);
		if (v->type == VALUE_ERROR) {
			MS_OLE_SET_GUINT8 (data + 6, ms_excel_write_map_errcode (v));
			MS_OLE_SET_GUINT8 (data + 7, 1); /* Mark as a err */
		} else {
			MS_OLE_SET_GUINT8 (data + 6, v->v.v_bool ? 1 : 0);
			MS_OLE_SET_GUINT8 (data + 7, 0); /* Mark as a bool */
		}
		ms_biff_put_commit (bp);
		break;
	}
	case VALUE_INTEGER:
	{
		int_t vint = v->v.v_int;
		guint8 *data;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 3)
			printf ("Writing %d %d\n", vint, v->v.v_int);
#endif
		if (((vint<<2)>>2) != vint) { /* Chain to floating point then. */
			Value *vf = value_new_float (v->v.v_int);
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
	case VALUE_FLOAT:
	{
		float_t val = v->v.v_float;
		gboolean is_int = ((val - (int)val) == 0.0) &&
			(((((int)val)<<2)>>2) == ((int)val));

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 3)
			printf ("Writing %g is (%g %g) is int ? %d\n",
				val, 1.0*(int)val,
				1.0*(val - (int)val), is_int);
#endif

		/* FIXME : Add test for double with 2 digits of fraction
		 * and represent it as a mode 3 RK (val*100) construct */
		if (is_int) { /* not nice but functional */
			Value *vi = value_new_int (val);
			write_value (bp, vi, ver, col, row, xf);
			value_release (vi);
		} else if (ver >= eBiffV7) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (bp, (0x200 | BIFF_NUMBER), 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			BIFF_SETDOUBLE (data + 6, val);
			ms_biff_put_commit (bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (bp, (0x200 | BIFF_RK));
			BIFF_SETDOUBLE (data+6-4, val);
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
	{
		char data[16];
		g_return_if_fail (v->v.str->str);

		if (ver >= eBiffV8); /* Use SST stuff in fulness of time */

		/* See: S59DDC.HTM ( for RSTRING ) */
		/* See: S59D9D.HTM ( for LABEL ) */
		ms_biff_put_var_next   (bp, (0x200 | BIFF_LABEL));
		EX_SETXF (data, xf);
		EX_SETCOL(data, col);
		EX_SETROW(data, row);
		EX_SETSTRLEN (data, strlen(v->v.str->str));
		ms_biff_put_var_write  (bp, data, 8);
		biff_put_text (bp, v->v.str->str, eBiffV7, FALSE, AS_PER_VER);
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
 * @sheet sheet
 * @cell  cell
 * @xf    XF index
 *
 * Write formula to file
 **/
static void
write_formula (BiffPut *bp, ExcelSheet *sheet, const Cell *cell, gint16 xf)
{
	guint8   data[22];
	guint8   lendat[2];
	guint32  len;
	gboolean string_result = FALSE;
	gint     col, row;
	Value   *v;

	g_return_if_fail (bp);
	g_return_if_fail (cell);
	g_return_if_fail (sheet);
	g_return_if_fail (cell->parsed_node);
	g_return_if_fail (cell->value);

	col = cell->col->pos;
	row = cell->row->pos;
	v = cell->value;

	/* See: S59D8F.HTM */
	ms_biff_put_var_next (bp, BIFF_FORMULA);
	EX_SETROW (data, row);
	EX_SETCOL (data, col);
	EX_SETXF  (data, xf);
	switch (v->type) {
	case VALUE_INTEGER :
	case VALUE_FLOAT :
		BIFF_SETDOUBLE (data + 6, value_get_as_float (v));
		break;

	case VALUE_STRING :
		MS_OLE_SET_GUINT32 (data +  6, 0x00000000);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		string_result = TRUE;
		break;

	case VALUE_BOOLEAN :
		MS_OLE_SET_GUINT32 (data +  6,
				    (v->v.v_bool ? 0x100 : 0x0) | 0x00000001);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_ERROR :
		MS_OLE_SET_GUINT32 (data +  6,
				    0x00000002 | (ms_excel_write_map_errcode (v) << 8));
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	case VALUE_EMPTY :
		MS_OLE_SET_GUINT32 (data +  6, 0x00000003);
		MS_OLE_SET_GUINT32 (data + 10, 0xffff0000);
		break;

	default :
		g_warning ("Unhandled value->type (%d) in excel::write_formula", v->type);
	}

	MS_OLE_SET_GUINT16 (data + 14, 0x0); /* Always calc & on load */
	MS_OLE_SET_GUINT32 (data + 16, 0x0);
	MS_OLE_SET_GUINT16 (data + 20, 0x0);
	ms_biff_put_var_write (bp, data, 22);
	len = ms_excel_write_formula (bp, sheet, cell->parsed_node,
				      col, row);
	g_assert (len <= 0xffff);
	ms_biff_put_var_seekto (bp, 20);
	MS_OLE_SET_GUINT16 (lendat, len);
	ms_biff_put_var_write (bp, lendat, 2);

	ms_biff_put_commit (bp);

	if (string_result) {
		gchar *str;

		ms_biff_put_var_next (bp, 0x200|BIFF_STRING);
		str = value_get_as_string (v);
		biff_put_text (bp, str, eBiffV7, TRUE, SIXTEEN_BIT);
		g_free (str);
		ms_biff_put_commit (bp);
	}
}

/**
 * write_cell
 * @bp    biff buffer
 * @sheet sheet
 * @cell  cell
 *
 * Write cell to file
 **/
static void
write_cell (BiffPut *bp, ExcelSheet *sheet, const ExcelCell *cell)
{
	gint col, row;
	Cell *gnum_cell;

	g_return_if_fail (bp);
	g_return_if_fail (cell);
	g_return_if_fail (cell->gnum_cell);
	g_return_if_fail (sheet);

	gnum_cell = cell->gnum_cell;
	col = gnum_cell->col->pos;
	row = gnum_cell->row->pos;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 2) {
		ParsePosition tmp;
		printf ("Writing cell at %s '%s' = '%s', xf = 0x%x\n",
			cell_name (col, row),
			(gnum_cell->parsed_node ?
			 expr_decode_tree (gnum_cell->parsed_node,
					   parse_pos_init (&tmp,
							   sheet->wb->gnum_wb,
							   col, row)) :
			 "none"),
			(gnum_cell->value ?
			 value_get_as_string (gnum_cell->value) : "empty"),
			cell->xf);
	}
#endif
	if (gnum_cell->parsed_node)
		write_formula (bp, sheet, gnum_cell, cell->xf);
	else if (gnum_cell->value)
		write_value (bp, gnum_cell->value, sheet->wb->ver,
			     col, row, cell->xf);
}

/**
 * write_mulblank
 * @bp      BIFF buffer
 * @sheet   sheet
 * @end_col last blank column
 * @row     row
 * @xf_list list of XF indices - one per cell
 * @run     number of blank cells
 *
 * Write multiple blanks to file
 **/
static void
write_mulblank (BiffPut *bp, ExcelSheet *sheet, guint32 end_col, guint32 row,
		GList *xf_list, guint32 run)
{
	guint16 xf;
	g_return_if_fail (bp);
	g_return_if_fail (run);
	g_return_if_fail (sheet);

	xf = GPOINTER_TO_INT (xf_list->data);

	if (run == 1) {
		guint8 *data;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 2) {
			printf ("Writing blank at %s, xf = 0x%x\n",
				cell_name (end_col, row), xf);
		}
#endif

		data = ms_biff_put_len_next (bp, 0x200|BIFF_BLANK, 6);
		EX_SETXF (data, xf);
		EX_SETCOL(data, end_col);
		EX_SETROW(data, row);
		ms_biff_put_commit (bp);
	} else { /* S59DA7.HTM */
		guint8   *ptr;
		guint32 len = 4 + 2*run + 2;
		guint8 *data;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 2) {
			/* Strange looking code because the second
			 * cell_name call overwrites the result of the
			 * first */
			printf ("Writing multiple blanks %s",
				cell_name (end_col + 1 - run, row));
			printf (":%s\n", cell_name (end_col, row));
		}
#endif
		data = ms_biff_put_len_next (bp, BIFF_MULBLANK, len);

		EX_SETCOL (data, end_col + 1 - run);
		EX_SETROW (data, row);
		MS_OLE_SET_GUINT16 (data + len - 2, end_col);
		ptr = data + 4;
		for (;;) {
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_write_debug > 3) {
				printf (" xf(%s) = 0x%x",
					cell_name (end_col + 1 - run, row),
					xf);
			}
#endif
			MS_OLE_SET_GUINT16 (ptr, xf);
			ptr += 2;
			run--;
			if (!xf_list->next)
				break;
			xf_list = xf_list->next;
			xf = GPOINTER_TO_INT (xf_list->data);
		}
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 3) {
			printf ("\n");
		}
#endif
		ms_biff_put_commit (bp);
	}
}

/**
 * write_default_row_height
 * @bp  BIFF buffer
 * @sheet sheet
 *
 * Write default row height
 * See: S59D72.HTM
 */
static void
write_default_row_height (BiffPut *bp, ExcelSheet *sheet)
{
	guint8 *data;
	double def_height;
	guint16 options = 0x0;
	guint16 height;

	def_height = sheet_get_default_external_row_height (sheet->gnum_sheet);
	height = (guint16) (20. * def_height);
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Default row height 0x%x;\n", height);
	}
#endif
	data = ms_biff_put_len_next (bp, 0x200|BIFF_DEFAULTROWHEIGHT, 4);
	MS_OLE_SET_GUINT16 (data + 0, options);
	MS_OLE_SET_GUINT16 (data + 2, height);
	ms_biff_put_commit (bp);
}

static void
margin_write (BiffPut *bp, guint16 op, PrintUnit *pu)
{
	guint8 *data;
	double  margin;

	margin = unit_convert (pu->points, UNIT_POINTS, UNIT_INCH);

	data = ms_biff_put_len_next (bp, op, 8);
	BIFF_SETDOUBLE (data, margin);

	ms_biff_put_commit (bp);
}

/**
 * lookup_sheet_base_char_width_for_write
 * @sheet sheet
 *
 * Look up base character width
 */
static double
lookup_base_char_width_for_write (ExcelSheet *sheet)
{
	double res = EXCEL_DEFAULT_CHAR_WIDTH;
	ExcelFont *f = NULL;
	if (sheet && sheet->wb
	    && sheet->wb->xf && sheet->wb->xf->default_style) {
		f = excel_font_new (sheet->wb->xf->default_style);
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Font for column sizing: %s\n",
			f ? excel_font_to_string (f) : "none");
	}
#endif
	if (f) {
		gboolean do_log = (ms_excel_write_debug > 2);
		res = lookup_font_base_char_width (f->style_font, do_log);
		excel_font_free (f);
	}

	return res;
}

/**
 * get_base_char_width
 * @sheet	the Excel sheet
 *
 * Returns base character width for column sizing. Uses cached value
 * if font alrady measured. Otherwise measure font.
 *
 * Excel uses the character width of the font in the "Normal" style.
 * The char width is based on the font in the "Normal" style.
 * This style is actually common to all sheets in the
 * workbook, but I find it more robust to treat it as a sheet
 * attribute.
 *
 * FIXME: There is a function with this name both in ms-excel-read.c and
 * ms-excel-write.c. The only difference is lookup_base_char_width_for_read
 * vs. lookup_base_char_width_for_write. Pass the function as parameter?
 * May be not. I don't like clever code.
 */
static double
get_base_char_width (ExcelSheet *sheet)
{
	if (sheet->base_char_width <= 0)
		sheet->base_char_width
			= lookup_base_char_width_for_write (sheet);

	return sheet->base_char_width;
}

/**
 * write_default_col_width
 * @bp  BIFF buffer
 * @sheet sheet
 *
 * Write default column width
 * See: S59D73.HTM
 *
 * FIXME: Not yet roundtrip compatible. The problem is that the base
 * font when we export is the font in the default style. But this font
 * is hardcoded and is not changed when a worksheet is imported.
 */
static void
write_default_col_width (BiffPut *bp, ExcelSheet *sheet)
{
	guint8 *data;
	double def_width;
	double width_chars;
	guint16 width;

	def_width = sheet_get_default_external_col_width (sheet->gnum_sheet);
	width_chars = def_width / get_base_char_width (sheet);
	width = (guint16) (width_chars + .5);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Default column width %d characters\n", width);
	}
#endif

	data = ms_biff_put_len_next (bp, BIFF_DEFCOLWIDTH, 2);
	MS_OLE_SET_GUINT16 (data, width);
	ms_biff_put_commit (bp);
}

/**
 * write_colinfo
 * @bp   BIFF buffer
 * @col  column
 *
 * Write column info for a run of identical columns
 */
static void
write_colinfo (BiffPut *bp, ExcelCol *col)
{
	guint8 *data;
	double  width_chars = col->width / get_base_char_width (col->sheet);
	guint16 width = (guint16) (width_chars * 256.);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1) {
		printf ("Column Formatting from col %d to %d of width "
			"%f characters\n", col->first, col->last, width/256.0);
	}
#endif

	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 12);
	MS_OLE_SET_GUINT16 (data +  0, col->first); /* 1st  col formatted */
	MS_OLE_SET_GUINT16 (data +  2, col->last);  /* last col formatted */
	MS_OLE_SET_GUINT16 (data +  4, width);      /* width */
	MS_OLE_SET_GUINT16 (data +  6, col->xf);    /* XF index */
	MS_OLE_SET_GUINT16 (data +  8, 0x00);       /* options */
	MS_OLE_SET_GUINT16 (data + 10, 0x00);       /* magic */
	ms_biff_put_commit (bp);
}

/**
 * write_colinfos
 * @bp     BIFF buffer
 * @sheet  sheet
 *
 * Write column info for all columns
 * See: S59D72.HTM
 */
static void
write_colinfos (BiffPut *bp, ExcelSheet *sheet)
{
	ExcelCol col;
	int i;
	double width;

	col.first = 0;
	col.width = 0.0;
	col.sheet = sheet;

	/* FIXME: Find default style for column. Does it have to be common to
	 * all cells, or can a cell override? Do all cells have to be
	 * blank. */
	col.xf    = 0x0f;

	for (i = 0; i < sheet->maxx; i++) {
		width = sheet_col_get_external_width (sheet->gnum_sheet, i);
		if (width != col.width) {
			if (i > 0) {
				col.last = i - 1;
				write_colinfo (bp, &col);
			}
			col.width = width;
			col.first = i;
		}
	}
	col.last = sheet->maxx - 1;
	write_colinfo (bp, &col);
}

static void
write_sheet_bools (BiffPut *bp, ExcelSheet *sheet)
{
	guint8 *data;
	PrintInformation *pi;
	eBiff_version ver = sheet->wb->ver;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (sheet->gnum_sheet != NULL);
	g_return_if_fail (sheet->gnum_sheet->print_info != NULL);

	pi = sheet->gnum_sheet->print_info;

	/* See: S59D63.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCMODE, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D62.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCCOUNT, 2);
	MS_OLE_SET_GUINT16 (data, 0x0064);
	ms_biff_put_commit (bp);

	/* See: S59DD7.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFMODE, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D9C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_ITERATION, 2);
	MS_OLE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D75.HTM, FIXME: find what number this really is! */
	data = ms_biff_put_len_next (bp, BIFF_DELTA, 8);
	MS_OLE_SET_GUINT32 (data,   0xd2f1a9fc);
	MS_OLE_SET_GUINT32 (data+4, 0x3f50624d);
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
	MS_OLE_SET_GUINT16 (data, pi->print_line_divisions);
	ms_biff_put_commit (bp);

	/* See: S59D91.HTM */
	data = ms_biff_put_len_next (bp, BIFF_GRIDSET, 2);
	MS_OLE_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D92.HTM ( Gutters ) */
	data = ms_biff_put_len_next (bp, BIFF_GUTS, 8);
	MS_OLE_SET_GUINT32 (data,   0x0);
	MS_OLE_SET_GUINT32 (data+4, 0x0);
	ms_biff_put_commit (bp);

	write_default_row_height (bp, sheet); /* Default row height */

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
/*	biff_put_text (bp, "&A", eBiffV7, TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D8D.HTM */
	ms_biff_put_var_next (bp, BIFF_FOOTER);
/*	biff_put_text (bp, "&P", eBiffV7, TRUE); */
	ms_biff_put_commit (bp);

	/* See: S59D93.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HCENTER, 2);
	MS_OLE_SET_GUINT16 (data, pi->center_horizontally);
	ms_biff_put_commit (bp);

	/* See: S59E15.HTM */
	data = ms_biff_put_len_next (bp, BIFF_VCENTER, 2);
	MS_OLE_SET_GUINT16 (data, pi->center_vertically);
	ms_biff_put_commit (bp);

	if (ver >= eBiffV8) {
		margin_write (bp, BIFF_LEFT_MARGIN,   &pi->margins.left);
		margin_write (bp, BIFF_RIGHT_MARGIN,  &pi->margins.right);
		margin_write (bp, BIFF_TOP_MARGIN,    &pi->margins.top);
		margin_write (bp, BIFF_BOTTOM_MARGIN, &pi->margins.bottom);
	}

	/* See: S59DE3.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SETUP, 34);
	MS_OLE_SET_GUINT32 (data +  0, 0x002c0000);
	MS_OLE_SET_GUINT32 (data +  4, 0x00010001);
	MS_OLE_SET_GUINT32 (data +  8, 0x00440001);
	MS_OLE_SET_GUINT32 (data + 12, 0x0000002f);
	MS_OLE_SET_GUINT32 (data + 16, 0x00000000);
	MS_OLE_SET_GUINT32 (data + 20, 0x3fe00000);
	MS_OLE_SET_GUINT32 (data + 24, 0x00000000);
	MS_OLE_SET_GUINT32 (data + 28, 0x3fe00000);
	MS_OLE_SET_GUINT16 (data + 32, 0x04e4);
	ms_biff_put_commit (bp);

	write_externsheets (bp, sheet->wb, sheet);

	ms_formula_write_pre_data (bp, sheet, EXCEL_EXTERNNAME, ver);

	write_default_col_width (bp, sheet); /* Default column width */

	write_colinfos (bp, sheet); /* Column infos */

	/* See: S59D76.HTM */
	if (ver >= eBiffV8) {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 14);
		MS_OLE_SET_GUINT32 (data +  0, 0);
		MS_OLE_SET_GUINT32 (data +  4, sheet->maxy);
		MS_OLE_SET_GUINT16 (data +  8, 0);
		MS_OLE_SET_GUINT16 (data + 10, sheet->maxx);
		MS_OLE_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, (0x200 | BIFF_DIMENSIONS), 10);
		MS_OLE_SET_GUINT16 (data +  0, 0);
		MS_OLE_SET_GUINT16 (data +  2, sheet->maxy);
		MS_OLE_SET_GUINT16 (data +  4, 0);
		MS_OLE_SET_GUINT16 (data +  6, sheet->maxx);
		MS_OLE_SET_GUINT16 (data +  8, 0x0000);
	}
	ms_biff_put_commit (bp);
}

static void
write_sheet_tail (BiffPut *bp, ExcelSheet *sheet)
{
	guint8 *data;
	eBiff_version ver = sheet->wb->ver;

	write_window1 (bp, ver);
	/* See: S59E18.HTM */
	if (ver <= eBiffV7) {
		guint16 options = 0x2b6; /* Arabic ? */
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2, 10);

		if (sheet->gnum_sheet ==
		    workbook_get_current_sheet (sheet->wb->gnum_wb))
			options |= 0x400;

		MS_OLE_SET_GUINT16 (data +  0, options);
		MS_OLE_SET_GUINT16 (data +  2, 0x0);
		MS_OLE_SET_GUINT32 (data +  4, 0x0);
		MS_OLE_SET_GUINT16 (data +  8, 0x0);
		ms_biff_put_commit (bp);
	} else {
		guint16 options = 0x2b6;
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2, 18);

		if (sheet->gnum_sheet ==
		    workbook_get_current_sheet (sheet->wb->gnum_wb))
			options |= 0x400;

		MS_OLE_SET_GUINT16 (data +  0, options);
		MS_OLE_SET_GUINT16 (data +  2, 0x0);
		MS_OLE_SET_GUINT32 (data +  4, 0x0);
		MS_OLE_SET_GUINT16 (data +  8, 0x0);
		MS_OLE_SET_GUINT16 (data + 10, 0x1);
		MS_OLE_SET_GUINT32 (data + 12, 0x0);
		MS_OLE_SET_GUINT16 (data + 16, 0x0);
		ms_biff_put_commit (bp);
	}

	if (ver >= eBiffV8) {
		double zoom = sheet->gnum_sheet->last_zoom_factor_used;
		int    a = 1, b = 2, lp;

		for (lp = 0; lp < 10; lp++) {
			double d1, d2, d3;
			d1 = fabs ((a + 1.0 / b) - zoom);
			d2 = fabs ((a / b + 1.0) - zoom);
			d3 = fabs ((a + 1.0 / b + 1.0) - zoom);

			if ((fabs ((double)a/b) - zoom) < 0.005)
				break;

			if (d1 < d2 &&
			    d1 < d3) {
				a++;
			} else if (d2 < d1 &&
				   d2 < d3) {
				b++;
			} else {
				a++;
				b++;
			}
		}
		g_warning ("Untested: Zoom %f is %d/%d ( = %f)", zoom, a, b, (double)a/b);

		data = ms_biff_put_len_next (bp, BIFF_SCL, 4);
		MS_OLE_SET_GUINT16 (data + 0, a);
		MS_OLE_SET_GUINT16 (data + 2, b);
		ms_biff_put_commit (bp);
	}

	/* See: S59DE2.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SELECTION, 15);
	MS_OLE_SET_GUINT32 (data +  0, 0x00000103);
	MS_OLE_SET_GUINT32 (data +  4, 0x01000000);
	MS_OLE_SET_GUINT32 (data +  8, 0x01000100);
	MS_OLE_SET_GUINT16 (data + 12, 0x0);
	MS_OLE_SET_GUINT8  (data + 14, 0x0);
	ms_biff_put_commit (bp);

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
write_index (MsOleStream *s, ExcelSheet *sheet, MsOlePos pos)
{
	guint8  data[4];
	MsOlePos oldpos;
	int lp;

	g_return_if_fail (s);
	g_return_if_fail (sheet);

	oldpos = s->position;/* FIXME: tell function ? */
	if (sheet->wb->ver >= eBiffV8)
		s->lseek (s, pos+4+16, MsOleSeekSet);
	else
		s->lseek (s, pos+4+12, MsOleSeekSet);

	for (lp = 0; lp < sheet->dbcells->len; lp++) {
		MsOlePos pos = g_array_index (sheet->dbcells, MsOlePos, lp);
		MS_OLE_SET_GUINT32 (data, pos - sheet->wb->streamPos);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_write_debug > 2)
			printf ("Writing index record"
				" 0x%4.4x - 0x%4.4x = 0x%4.4x\n",
				pos, sheet->wb->streamPos,
				pos - sheet->wb->streamPos);
#endif
		s->write (s, data, 4);
	}

	s->lseek (s, oldpos, MsOleSeekSet);
}

/* See: S59DDB.HTM */
static MsOlePos
write_rowinfo (BiffPut *bp, ExcelSheet *sheet, guint32 row, guint32 width)
{
	guint8 *data;
	MsOlePos pos;

	double points = sheet_row_get_external_height (sheet->gnum_sheet, row);
	/* We don't worry about standard height. I haven't seen it
	 * indicated in any actual sheet. */
	guint16 height = (guint16) (20. * points);
	/* FIXME: Set option bit 7 if row has default style */
	guint16 options = 0x0100; /* Magic */
	/* FIXME: Find default style for row. Does it have to be common to
	 * all cells, or can a cell override? Do all cells have to be
	 * blank. */
	guint16 row_xf     = 0x000f; /* Magic */
	/* FIXME: set bit 12 of row_xf if thick border on top, bit 13 if thick
	 * border on bottom. */

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1)
		printf ("Row %d height 0x%x;\n", row+1, height);
#endif

	data = ms_biff_put_len_next (bp, (0x200 | BIFF_ROW), 16);
	pos = bp->streamPos;
	MS_OLE_SET_GUINT16 (data +  0, row);     /* Row number */
	MS_OLE_SET_GUINT16 (data +  2, 0);       /* first def. col */
	MS_OLE_SET_GUINT16 (data +  4, width);   /* last  def. col */
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
 * @sheet     sheet
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
write_db_cell (BiffPut *bp, ExcelSheet *sheet,
	       MsOlePos *ri_start, MsOlePos *rc_start, guint32 nrows)
{
	MsOlePos pos;
	guint32 i;
	guint16 offset;

	guint8 *data = ms_biff_put_len_next (bp, BIFF_DBCELL, 4 + nrows * 2);
	pos = bp->streamPos;

	MS_OLE_SET_GUINT32 (data, pos - ri_start [0]);
	offset = rc_start [0] - ri_start [1];
	for (i = 0 ; i < nrows; i++, offset = rc_start [i] - rc_start [i - 1])
		MS_OLE_SET_GUINT16 (data + 4 + i * 2, offset);

	ms_biff_put_commit (bp);

	g_array_append_val (sheet->dbcells, pos);
}

/**
 * write_block
 * @bp     BIFF buffer
 * @sheet  sheet
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
write_block (BiffPut *bp, ExcelSheet *sheet, guint32 begin, int nrows)
{
	guint32 maxx = sheet->maxx;
	guint32 end;
	guint32 x, y;
	MsOlePos  ri_start [2]; /* Row info start */
	MsOlePos *rc_start;	/* Row cells start */

	if (nrows > sheet->maxy - begin) /* Incomplete final block? */
		nrows = sheet->maxy - begin;
	end = begin + nrows - 1;

	ri_start [0] = write_rowinfo (bp, sheet, begin, maxx);
	ri_start [1] = bp->streamPos;
	for (y = begin + 1; y <= end; y++)
		(void) write_rowinfo (bp, sheet, y, maxx);

	rc_start = g_new0 (MsOlePos, nrows);
	for (y = begin; y <= end; y++) {
		guint32 run_size = 0;
		GList *xf_list = NULL;

		/* Save start pos of 1st cell in row */
		rc_start [y - begin] = bp->streamPos;
		for (x = 0; x < maxx; x++) {
			const ExcelCell *cell = excel_cell_get (sheet, x, y);
			if (!cell->gnum_cell) {
				xf_list = g_list_append
					(xf_list, GINT_TO_POINTER (cell->xf));
				run_size++;
			} else {
				if (run_size) {
					write_mulblank (bp, sheet, x - 1, y,
							xf_list, run_size);
					g_list_free (xf_list);
					xf_list = NULL;
					run_size = 0;
				}
				write_cell (bp, sheet, cell);
			}
		}
		if (run_size > 0 && run_size <= maxx) {
			write_mulblank (bp, sheet, x - 1, y,
					xf_list, run_size);
		}
		if (xf_list) {
			g_list_free (xf_list);
			xf_list = NULL;
		}
	}

	write_db_cell (bp, sheet, ri_start, rc_start, nrows);
	g_free (rc_start);

	return y - 1;
}

/* See: 'Finding records in BIFF files': S59E28.HTM */
/* and S59D99.HTM */
static void
write_sheet (BiffPut *bp, ExcelSheet *sheet)
{
	guint32 y, block_end;
	int rows_in_block = ROW_BLOCK_MAX_LEN;
	MsOlePos index_off;
	/* No. of blocks of rows. Only correct as long as all rows -
	   including empties - have row info records */
	guint32 nblocks = (sheet->maxy - 1) / rows_in_block + 1;

	sheet->streamPos = biff_bof_write (bp, sheet->wb->ver, eBiffTWorksheet);

	if (sheet->maxy > 16544)
		g_error ("Sheet seems impossibly big");

	if (sheet->wb->ver >= eBiffV8) {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 16);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT32 (data +  4, 0);
		MS_OLE_SET_GUINT32 (data +  8, sheet->maxy);
		MS_OLE_SET_GUINT32 (data + 12, 0);
	} else {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_INDEX,
						     nblocks * 4 + 12);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT16 (data + 4, 0);
		MS_OLE_SET_GUINT16 (data + 6, sheet->maxy);
		MS_OLE_SET_GUINT32 (data + 8, 0);
	}
	ms_biff_put_commit (bp);

	write_sheet_bools (bp, sheet);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 1)
		printf ("Saving sheet '%s' geom (%d, %d)\n",
			sheet->gnum_sheet->name, sheet->maxx, sheet->maxy);
#endif
	for (y = 0; y < sheet->maxy; y = block_end + 1)
		block_end = write_block (bp, sheet, y, rows_in_block);

	write_index (bp->pos, sheet, index_off);
	write_sheet_tail (bp, sheet);

	biff_eof_write (bp);
}

static void
new_sheet (ExcelWorkbook *wb, Sheet *value)
{
	ExcelSheet      *sheet = g_new (ExcelSheet, 1);
	Range           extent;
	ExcelCell       **p, **pmax;

	g_return_if_fail (value);
	g_return_if_fail (wb);

	sheet->gnum_sheet = value;
	sheet->streamPos  = 0x0deadbee;
	sheet->wb         = wb;
	extent            = sheet_get_extent (sheet->gnum_sheet);
	sheet->maxx       = extent.end.col + 1;
	sheet->maxy       = extent.end.row + 1;
	sheet->dbcells    = g_array_new (FALSE, FALSE, sizeof (MsOlePos));
	sheet->base_char_width = 0;

	g_ptr_array_add (wb->sheets, sheet);

	ms_formula_cache_init (sheet);
	sheet->cell_used_map = cell_used_map_new (sheet);
	
	sheet->cells = g_new (ExcelCell *, sheet->maxy);
	for (p = sheet->cells, pmax = p + sheet->maxy; p < pmax; p++)
		*p = g_new0 (ExcelCell, sheet->maxx);
}

static void
free_sheet (ExcelSheet *sheet)
{
	ExcelCell     **p, **pmax;

	if (sheet) {
		g_free (sheet->cell_used_map);
		for (p = sheet->cells, pmax = p + sheet->maxy; p < pmax; p++)
			g_free (*p);
		g_free (sheet->cells);
		g_array_free (sheet->dbcells, TRUE);
		ms_formula_cache_shutdown (sheet);
		g_free (sheet);
	}
}

/**
 * pre_pass
 * @wb: the workbook to scan
 *
 * Scans all the workbook items. Adds all styles, fonts, formats and
 * colors to tables. Resolves any referencing problems before they
 * occur, hence the records can be written in a linear order.
 *
 **/
static void
pre_pass (ExcelWorkbook *wb)
{
	/* The default style first */
	put_mstyle (wb, wb->xf->default_style);
	/* Its font and format */
	put_font (wb->xf->default_style, NULL, wb);
	put_format (wb->xf->default_style, NULL, wb);

	gather_styles (wb);	/* (and cache cells) */
	/* Gather Info from styles */
	gather_fonts (wb);
	gather_formats (wb);
	gather_palette (wb);
}

static void
write_workbook (BiffPut *bp, Workbook *gwb, eBiff_version ver)
{
	ExcelWorkbook *wb = g_new (ExcelWorkbook, 1);
	ExcelSheet    *s  = 0;
	int       lp;
	GList    *sheets;

	wb->ver      = ver;
	wb->gnum_wb  = gwb;
	wb->sheets   = g_ptr_array_new ();
	wb->names    = g_ptr_array_new ();
	fonts_init (wb);
	formats_init (wb);
	palette_init (wb);
	xf_init (wb);

	sheets = workbook_sheets (gwb);
	while (sheets) {
		new_sheet (wb, sheets->data);
		sheets = g_list_next (sheets);
	}

	pre_pass (wb);

	/* Workbook */
	wb->streamPos = biff_bof_write (bp, ver, eBiffTWorkbook);

	write_magic_interface (bp, ver);
/*	write_externsheets    (bp, wb, NULL); */
	write_bits            (bp, wb, ver);

	write_fonts (bp, wb);
	write_formats (bp, wb);
	write_xf (bp, wb);
	write_palette (bp, wb);

	for (lp = 0; lp < wb->sheets->len; lp++) {
		s = g_ptr_array_index (wb->sheets, lp);
	        s->boundsheetPos = biff_boundsheet_write_first
			(bp, eBiffTWorksheet,
			 s->gnum_sheet->name, wb->ver);

		ms_formula_write_pre_data (bp, s, EXCEL_NAME, wb->ver);
	}

	write_names(bp, wb);
	biff_eof_write (bp);
	/* End of Workbook */

	/* Sheets */
	for (lp = 0; lp < wb->sheets->len; lp++)
		write_sheet (bp, g_ptr_array_index (wb->sheets, lp));
	/* End of Sheets */

	/* Finalise Workbook stuff */
	for (lp = 0; lp < wb->sheets->len; lp++) {
		ExcelSheet *s = g_ptr_array_index (wb->sheets, lp);
		biff_boundsheet_write_last (bp->pos, s->boundsheetPos,
					    s->streamPos);
	}
	/* End Finalised workbook */

	/* Free various bits */
	fonts_free   (wb);
	formats_free (wb);
	palette_free (wb);
	xf_free  (wb);
	for (lp = 0; lp < wb->sheets->len; lp++) {
		ExcelSheet *s = g_ptr_array_index (wb->sheets, lp);
		free_sheet (s);
	}
	g_list_free (sheets);

	g_free (wb);
}

int
ms_excel_write_workbook (MsOle *file, Workbook *wb,
			 eBiff_version ver)
{
	MsOleErr     result;
	char        *strname;
	MsOleStream *str;
	BiffPut     *bp;

	g_return_val_if_fail (wb != NULL, 0);
	g_return_val_if_fail (file != NULL, 0);
	g_return_val_if_fail (ver >= eBiffV7, 0);

	if (!file || !wb) {
		printf ("Can't write Null pointers\n");
		return 0;
	}

	if (ver >= eBiffV8)
		strname = "Workbook";
	else
		strname = "Book";

	result = ms_ole_stream_open (&str, file, "/", strname, 'w');

	if (result != MS_OLE_ERR_OK) {
		printf ("Can't open stream for writing\n");
		return 0;
	}

	bp = ms_biff_put_new (str);

	write_workbook (bp, wb, ver);

	/* Kludge to make sure the file is a Big Block file */
	while (bp->pos->size < 0x1000) {
		ms_biff_put_len_next   (bp, 0, 0);
		ms_biff_put_commit (bp);
	}

	ms_biff_put_destroy (bp);

	ms_ole_stream_close (&str);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_write_debug > 0) {
		fflush (stdout);
	}
#endif

	return 1;
}
