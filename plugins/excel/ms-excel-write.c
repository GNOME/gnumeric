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

#define EXCEL_DEBUG 1

typedef struct _XF       XF;
typedef struct _FONTS    FONTS;
typedef struct _SHEET    SHEET;
typedef struct _FORMATS  FORMATS;
typedef struct _PALETTE  PALETTE;
typedef struct _WORKBOOK WORKBOOK;

struct _SHEET {
	WORKBOOK *wb;
	Sheet    *gnum_sheet;
	guint32   streamPos;
	guint32   boundsheetPos;
	guint32   maxx;
	guint32   maxy;
};

struct _WORKBOOK {
	Workbook      *gnum_wb;
	GPtrArray     *sheets;
	eBiff_version  ver;
	PALETTE       *pal;
	FONTS         *fonts;
	FORMATS       *formats;
};

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
static int
biff_put_text (BIFF_PUT *bp, char *txt, eBiff_version ver, gboolean write_len)
{
#define BLK_LEN 16

	guint8 data[BLK_LEN];
	guint32 chunks, pos, lpc, lp, len, ans;

	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (txt, 0);

	len = strlen (txt);
/*	printf ("Write '%s' len = %d\n", txt, len); */
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
			g_return_val_if_fail (len<256, 0);
			BIFF_SET_GUINT8(data, len);
			ms_biff_put_var_write (bp, data, 1);
			ans = len + 1;
		} else
			ans = len;
	}

	/* An attempt at efficiency */
	chunks = len/BLK_LEN;
	pos    = 0;
	for (lpc=0;lpc<chunks;lpc++) {
		for (lp=0;lp<BLK_LEN;lp++,pos++)
			data[lp] = txt[pos];
/*		data[BLK_LEN] = '\0';
		printf ("Writing chunk '%s'\n", data); */
		ms_biff_put_var_write (bp, data, BLK_LEN);
	}
	len = len-pos;
	if (len > 0) {
	        for (lp=0;lp<len;lp++,pos++)
			data[lp] = txt[pos];
/*		data[lp] = '\0';
		printf ("Writing chunk '%s'\n", data); */
		ms_biff_put_var_write (bp, data, lp);
	}
/* ans is the length but do we need it ? */
#undef BLK_LEN
	return ans;
}

/**
 * See S59D5D.HTM
 **/
static void
biff_bof_write (BIFF_PUT *bp, eBiff_version ver,
		eBiff_filetype type)
{
	guint   len;
	guint8 *data;

	if (ver >= eBiffV8)
		len = 16;
	else
		len = 8;

	data = ms_biff_put_len_next (bp, 0, len);

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

	/* Magic version numbers: build date etc. */
	switch (ver) {
	case eBiffV8:
		BIFF_SET_GUINT16 (data+4, 0x0dbb);
		BIFF_SET_GUINT16 (data+6, 0x07cc);
		g_assert (len > 11);
		/* Quandry: can we tell the truth about our history */
		BIFF_SET_GUINT32 (data+ 8, 0x00000004);
		BIFF_SET_GUINT16 (data+12, 0x06000908); /* ? */
		break;
	case eBiffV7:
	case eBiffV5:
		BIFF_SET_GUINT16 (data+4, 0x096c);
		BIFF_SET_GUINT16 (data+6, 0x07c9);
		break;
	default:
		printf ("FIXME: I need some magic numbers\n");
		BIFF_SET_GUINT16 (data+4, 0x0);
		BIFF_SET_GUINT16 (data+6, 0x0);
		break;
	}
	ms_biff_put_commit (bp);
}

static void
biff_eof_write (BIFF_PUT *bp)
{
	ms_biff_put_len_next (bp, BIFF_EOF, 0);
	ms_biff_put_commit (bp);
}

static void
write_magic_interface (BIFF_PUT *bp, eBiff_version ver)
{
	guint8 *data;
	if (ver >= eBiffV7) {
		ms_biff_put_len_next (bp, BIFF_INTERFACEHDR, 0);
		ms_biff_put_commit (bp);
		data = ms_biff_put_len_next (bp, BIFF_MMS, 2);
		BIFF_SET_GUINT16(data, 0);
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
write_constants (BIFF_PUT *bp, eBiff_version ver)
{
	guint8 *data;

	/* See: S59E1A.HTM */
	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
	biff_put_text (bp, "the free software foundation   ", ver, TRUE);
	ms_biff_put_commit (bp);

	/* See: S59D66.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CODEPAGE, 2);
	BIFF_SET_GUINT16 (data, 0x04e4); /* ANSI */
	ms_biff_put_commit (bp);

	/* See: S59D8A.HTM */
	if (ver >= eBiffV7) {
		data = ms_biff_put_len_next (bp, BIFF_FNGROUPCOUNT, 2);
		BIFF_SET_GUINT16 (data, 0x0e);
		ms_biff_put_commit (bp);
	}
}

static void
write_externsheets (BIFF_PUT *bp, WORKBOOK *wb, SHEET *ignore)
{
	guint32 num_sheets = wb->sheets->len;
	guint8 *data;
	int lp;

	if (wb->ver > eBiffV7) {
		printf ("Need some cunning BiffV8 stuff ?\n");
		return;
	}

	if (num_sheets == 1) /* Not enough sheets for extern records */
		return;

	if (ignore) /* Strangely needed */
		num_sheets--;

	g_assert (num_sheets < 0xffff);

	data = ms_biff_put_len_next (bp, BIFF_EXTERNCOUNT, 2);
	BIFF_SET_GUINT16(data, num_sheets);
	ms_biff_put_commit (bp);

	for (lp=0;lp<num_sheets;lp++) {
		SHEET *sheet = g_ptr_array_index (wb->sheets, lp);
		gint len = strlen (sheet->gnum_sheet->name);
		guint8 data[8];

		if (sheet == ignore) continue;
		
		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		BIFF_SET_GUINT8(data, len);
		BIFF_SET_GUINT8(data+1, 3); /* Magic */
		ms_biff_put_var_write (bp, data, 2);
		biff_put_text (bp, sheet->gnum_sheet->name, wb->ver, FALSE);
		ms_biff_put_commit (bp);
	}
}

static void
write_bits (BIFF_PUT *bp, eBiff_version ver)
{
	guint8 *data;

	/* See: S59E19.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WINDOWPROTECT, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DD1.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PROTECT, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCC.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PASSWORD, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59E17.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WINDOW1, 18);
	BIFF_SET_GUINT16 (data+  0, 0x00f0);
	BIFF_SET_GUINT16 (data+  2, 0x0078);
	BIFF_SET_GUINT16 (data+  4, 0x378c);
	BIFF_SET_GUINT16 (data+  6, 0x2238);
	BIFF_SET_GUINT16 (data+  8, 0x0038); /* various flags */
	BIFF_SET_GUINT16 (data+ 10, 0x0000); /* selected tab */
	BIFF_SET_GUINT16 (data+ 12, 0x0000); /* displayed tab */
	BIFF_SET_GUINT16 (data+ 14, 0x0001);
	BIFF_SET_GUINT16 (data+ 16, 0x0258);
	ms_biff_put_commit (bp);

	if (ver >= eBiffV8) {
		/* See: S59DCA.HTM */
		data = ms_biff_put_len_next (bp, BIFF_PANE, 2);
		BIFF_SET_GUINT16 (data, 0x0);
		ms_biff_put_commit (bp);
	}

	/* See: S59D5B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BACKUP, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D95.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HIDEOBJ, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D54.HTM */
	data = ms_biff_put_len_next (bp, BIFF_1904, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59DCE.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRECISION, 2);
	BIFF_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD8.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFRESHALL, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D5E.HTM */
	data = ms_biff_put_len_next (bp, BIFF_BOOKBOOL, 2);
	BIFF_SET_GUINT16 (data, 0x0);
	ms_biff_put_commit (bp);
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

	ms_biff_put_commit (bp);
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

struct _PALETTE {
	GHashTable *col_to_idx;
};

#define PALETTE_WHITE 0
#define PALETTE_BLACK 1

static PALETTE *
write_palette (BIFF_PUT *bp, WORKBOOK *wb)
{
	PALETTE *pal = g_new (PALETTE, 1);
	guint8  r,g,b, data[8];
	guint32 num, i;
	pal->col_to_idx = g_hash_table_new (g_direct_hash,
					    g_direct_equal);
	/* FIXME: should scan styles for colors and write intelligently. */
	ms_biff_put_var_next (bp, BIFF_PALETTE);
	
	BIFF_SET_GUINT16 (data, EXCEL_DEF_PAL_LEN); /* Entries */

	ms_biff_put_var_write (bp, data, 2);
	for (i=0;i<EXCEL_DEF_PAL_LEN;i++) {
		r = excel_default_palette[i].r;
		g = excel_default_palette[i].g;
		b = excel_default_palette[i].b;
		num = (b<<16) + (g<<8) + (r<<0);
		BIFF_SET_GUINT32 (data, num);
		ms_biff_put_var_write (bp, data, 4);
	}

	ms_biff_put_commit (bp);

	return pal;
}

/* To be used ... */
static guint32
palette_lookup (PALETTE *pal, StyleColor *col)
{
	return PALETTE_WHITE;
}

static void
palette_free (PALETTE *pal)
{
	if (pal) {
		/* Leak need to free indexes too */
		g_hash_table_destroy (pal->col_to_idx);
		g_free (pal);
	}
}

#define FONT_MAGIC 0

struct _FONTS {
	GHashTable *StyleFont_to_idx;
};

/* See S59D8C.HTM */
static FONTS *
write_fonts (BIFF_PUT *bp, WORKBOOK *wb)
{
	FONTS *fonts = g_new (FONTS, 1);
	guint8 data[64];
	int lp;
	
	for (lp=0;lp<4;lp++) { /* FIXME: Magic minimum fonts */
		fonts->StyleFont_to_idx = g_hash_table_new (g_direct_hash,
							    g_direct_equal);
		/* Kludge for now ... */
		ms_biff_put_var_next (bp, BIFF_FONT);
		
		BIFF_SET_GUINT16(data + 0, 12*20); /* 12 point */
		BIFF_SET_GUINT16(data + 2, (0<<1) + (0<<3)); /* italic, struck */
/*		BIFF_SET_GUINT16(data + 4, PALETTE_BLACK); */
		BIFF_SET_GUINT16(data + 4, 0x7fff); /* Magic ! */

		if (1)
			BIFF_SET_GUINT16(data + 6, 0x190); /* Normal boldness */
		else
			BIFF_SET_GUINT16(data + 6, 0x2bc); /* Bold */

		BIFF_SET_GUINT16(data + 8, 0); /* 0: Normal, 1; Super, 2: Sub script*/
		BIFF_SET_GUINT16(data +10, 0); /* No underline */
		BIFF_SET_GUINT16(data +12, 0); /* seems OK. */
		BIFF_SET_GUINT8 (data +13, 0);
		ms_biff_put_var_write (bp, data, 14);
		
		biff_put_text (bp, "Arial", eBiffV7, TRUE);
		
		ms_biff_put_commit (bp);
	}

	return fonts;
}

static guint32
fonts_get_index (FONTS *fonts, StyleFont *sf)
{
	return FONT_MAGIC;
}

static void
fonts_free (FONTS *fonts)
{
	if (fonts) {
		g_free (fonts->StyleFont_to_idx);
		g_free (fonts);
	}
}

#define FORMAT_MAGIC 0

struct _FORMATS {
	GHashTable *StyleFormat_to_idx;
};

/* See S59D8E.HTM */
static FORMATS *
write_formats (BIFF_PUT *bp, WORKBOOK *wb)
{
	FORMATS *formats = g_new (FORMATS, 1);
	guint8 data[64];
	int lp;
	
	for (lp=0;lp<8;lp++) { /* FIXME: Magic minimum formats */
		formats->StyleFormat_to_idx = g_hash_table_new (g_direct_hash,
								g_direct_equal);
		/* Kludge for now ... */
		ms_biff_put_var_next (bp, BIFF_FORMAT);
		
		BIFF_SET_GUINT16 (data, 0);
		ms_biff_put_var_write (bp, data, 2);

		biff_put_text (bp, "0", eBiffV7, TRUE);
		
		ms_biff_put_commit (bp);
	}

	return formats;
}

static guint32
formats_get_index (FORMATS *formats, StyleFormat *sf)
{
	return FORMAT_MAGIC;
}

static void
formats_free (FORMATS *formats)
{
	if (formats) {
		g_free (formats->StyleFormat_to_idx);
		g_free (formats);
	}
}

#define XF_MAGIC 15

/* See S59E1E.HTM */
static void
write_xf_record (BIFF_PUT *bp, Style *style, eBiff_version ver)
{
	guint8 data[256];
	int lp;

	for (lp=0;lp<250;lp++)
		data[lp] = 0;

	if (ver >= eBiffV7)
		ms_biff_put_var_next (bp, BIFF_XF);
	else
		ms_biff_put_var_next (bp, BIFF_XF_OLD);

	if (ver >= eBiffV8) {
		BIFF_SET_GUINT16(data+0, fonts_get_index (0, 0));
		BIFF_SET_GUINT16(data+2, formats_get_index (0, 0));
		BIFF_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		BIFF_SET_GUINT16(data+0, fonts_get_index (0, 0));
		BIFF_SET_GUINT16(data+2, formats_get_index (0, 0));
		BIFF_SET_GUINT16(data+4, 0xfff5); /* FIXME: Magic */
		BIFF_SET_GUINT16(data+6, 0xf420);
		BIFF_SET_GUINT16(data+8, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

struct _XF {
	GHashTable *Style_to_idx;
};

static XF *
write_xf (BIFF_PUT *bp, WORKBOOK *wb)
{
	int lp;
	guint32 style_magic[6] = { 0xff038010, 0xff068011, 0xff048012, 0xff078013,
				   0xff008000, 0xff058014 };

	/* FIXME: Scan through all the Styles... */
	XF *xf = g_new (XF, 1);
	xf->Style_to_idx = g_hash_table_new (g_direct_hash,
					    g_direct_equal);
	/* Need at least 16 apparently */
	for (lp=0;lp<21;lp++)
		write_xf_record (bp, NULL, wb->ver);

	/* See: S59DEA.HTM */
	for (lp=0;lp<6;lp++) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_STYLE, 4);
		BIFF_SET_GUINT32 (data, style_magic[lp]); /* cop out */
		ms_biff_put_commit (bp);
	}
	return xf;
}

static guint32
xf_lookup (XF *xf, Style *style)
{
	/* Fixme: Hash table lookup */
	return XF_MAGIC;
}

static void
xf_free (XF *xf)
{
	if (xf) {
		/* Another leak */
		g_hash_table_destroy (xf->Style_to_idx);
		g_free (xf);
	}
}

static void
write_value (BIFF_PUT *bp, Value *v, eBiff_version ver,
	     guint32 col, guint32 row, guint16 xf)
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
		ms_biff_put_commit (bp);
		break;
	}
	case VALUE_FLOAT:
	{
		if (ver >= eBiffV7) { /* See: S59DAC.HTM */
			guint8 *data =ms_biff_put_len_next (bp, BIFF_NUMBER, 14);
			EX_SETROW(data, row);
			EX_SETCOL(data, col);
			EX_SETXF (data, xf);
			BIFF_SETDOUBLE (data + 6, v->v.v_float);
			ms_biff_put_commit (bp);
		} else { /* Nasty RK thing S59DDA.HTM */
			guint8 data[16];

			ms_biff_put_var_next   (bp, BIFF_RK);
			BIFF_SETDOUBLE (data+6-4, v->v.v_float);
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

		/* See: S59DDC.HTM */
		ms_biff_put_var_next   (bp, BIFF_LABEL);
		EX_SETXF (data, xf);
		EX_SETCOL(data, col);
		EX_SETROW(data, row);
		EX_SETSTRLEN (data, strlen(v->v.str->str));
		ms_biff_put_var_write  (bp, data, 8);
		biff_put_text (bp, v->v.str->str, eBiffV7, FALSE);
		ms_biff_put_commit (bp);
		break;
	}
	default:
		printf ("Unhandled value type %d\n", v->type);
		break;
	}
}

static void
write_cell (BIFF_PUT *bp, SHEET *sheet, Cell *cell)
{
	g_return_if_fail (bp);
	g_return_if_fail (cell);

	if (cell->parsed_node)
#if EXCEL_DEBUG > 1
		printf ("FIXME: Skipping function\n");
#else
;
#endif
	else if (cell->value)
		write_value (bp, cell->value, sheet->wb->ver,
			     cell->col->pos, cell->row->pos, XF_MAGIC);
}

#define MAGIC_BLANK_XF 0

static void
write_mulblank (BIFF_PUT *bp, SHEET *sheet, guint32 end_col, guint32 row, guint32 run)
{
	g_return_if_fail (bp);
	g_return_if_fail (run);
	g_return_if_fail (sheet);

	if (run == 1) {
		guint8 *data;
		data = ms_biff_put_len_next (bp, BIFF_BLANK, 6);
		EX_SETXF (data, MAGIC_BLANK_XF);
		EX_SETCOL(data, end_col);
		EX_SETROW(data, row);
		ms_biff_put_commit (bp);
	} else { /* S59DA7.HTM */
		BYTE   *ptr;
		guint32 len = 4 + 2*run + 2;
		guint8 *data;
	
		data = ms_biff_put_len_next (bp, BIFF_MULBLANK, len);

		EX_SETCOL (data, end_col-run);
		EX_SETROW (data, row);
		BIFF_SET_GUINT16 (data + len - 2, end_col);
		ptr = data + 4;
		while (run > 0) {
			BIFF_SET_GUINT16 (ptr, MAGIC_BLANK_XF);
			ptr+=2;
			run--;
		}
	}
}

static void
write_sheet_bools (BIFF_PUT *bp, SHEET *sheet)
{
	guint8 *data;
	eBiff_version ver = sheet->wb->ver;

	/* See: S59D63.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCMODE, 2);
	BIFF_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D62.HTM */
	data = ms_biff_put_len_next (bp, BIFF_CALCCOUNT, 2);
	BIFF_SET_GUINT16 (data, 0x0064);
	ms_biff_put_commit (bp);

	/* See: S59DD7.HTM */
	data = ms_biff_put_len_next (bp, BIFF_REFMODE, 2);
	BIFF_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D9C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_ITERATION, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D75.HTM, FIXME: find what number this really is! */
	data = ms_biff_put_len_next (bp, BIFF_DELTA, 8);
	BIFF_SET_GUINT32 (data,   0xd2f1a9fc);
	BIFF_SET_GUINT32 (data+4, 0x3f50624d);
	ms_biff_put_commit (bp);

	/* See: S59DDD.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SAVERECALC, 2);
	BIFF_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59DD0.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTHEADERS, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59DCF.HTM */
	data = ms_biff_put_len_next (bp, BIFF_PRINTGRIDLINES, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59D91.HTM */
	data = ms_biff_put_len_next (bp, BIFF_GRIDSET, 2);
	BIFF_SET_GUINT16 (data, 0x0001);
	ms_biff_put_commit (bp);

	/* See: S59D92.HTM ( Gutters ) */
	data = ms_biff_put_len_next (bp, BIFF_GUTS, 8);
	BIFF_SET_GUINT32 (data,   0x0);
	BIFF_SET_GUINT32 (data+4, 0x0);
	ms_biff_put_commit (bp);

	/* See: S59D72.HTM */
	data = ms_biff_put_len_next (bp, BIFF_DEFAULTROWHEIGHT, 4);
	BIFF_SET_GUINT32 (data, 0x00ff0000);
	ms_biff_put_commit (bp);

	/* See: S59D6B.HTM */
	data = ms_biff_put_len_next (bp, BIFF_COUNTRY, 4);
	BIFF_SET_GUINT32 (data, 0x002c0001); /* Made in the UK */
	ms_biff_put_commit (bp);

	/* See: S59E1C.HTM */
	data = ms_biff_put_len_next (bp, BIFF_WSBOOL, 2);
	BIFF_SET_GUINT16 (data, 0x04c1);
	ms_biff_put_commit (bp);

	/* See: S59D94.HTM */
	ms_biff_put_var_next (bp, BIFF_HEADER);
	biff_put_text (bp, "&A", eBiffV7, TRUE);
	ms_biff_put_commit (bp);

	/* See: S59D8D.HTM */
	ms_biff_put_var_next (bp, BIFF_FOOTER);
	biff_put_text (bp, "&P", eBiffV7, TRUE);
	ms_biff_put_commit (bp);

	/* See: S59D93.HTM */
	data = ms_biff_put_len_next (bp, BIFF_HCENTER, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59E15.HTM */
	data = ms_biff_put_len_next (bp, BIFF_VCENTER, 2);
	BIFF_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59DE3.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SETUP, 34);
	BIFF_SET_GUINT32 (data +  0, 0x002f0000);
	BIFF_SET_GUINT32 (data +  4, 0x00010001);
	BIFF_SET_GUINT32 (data +  8, 0x00440001);
	BIFF_SET_GUINT32 (data + 12, 0x000000a0);
	BIFF_SET_GUINT32 (data + 16, 0x00000000);
	BIFF_SET_GUINT32 (data + 20, 0x3fe00000);
	BIFF_SET_GUINT32 (data + 24, 0x00000000);
	BIFF_SET_GUINT32 (data + 28, 0x3fe00000);
	BIFF_SET_GUINT16 (data + 32, 0x04e4);
	ms_biff_put_commit (bp);

	write_externsheets (bp, sheet->wb, sheet);

	/* See: S59D73.HTM */
	data = ms_biff_put_len_next (bp, BIFF_DEFCOLWIDTH, 2);
	BIFF_SET_GUINT16 (data, 0x0008);
	ms_biff_put_commit (bp);

	/* See: S59D76.HTM */
	if (sheet->wb->ver >= eBiffV8) {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 14);
		BIFF_SET_GUINT32 (data +  0, 0);
		BIFF_SET_GUINT32 (data +  4, sheet->maxy-1);
		BIFF_SET_GUINT16 (data +  8, 0);
		BIFF_SET_GUINT16 (data + 10, sheet->maxx);
		BIFF_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 10);
		BIFF_SET_GUINT16 (data +  0, 0);
		BIFF_SET_GUINT16 (data +  2, sheet->maxy-1);
		BIFF_SET_GUINT16 (data +  4, 0);
		BIFF_SET_GUINT16 (data +  6, sheet->maxx);
		BIFF_SET_GUINT16 (data +  8, 0x0000);
	}
	ms_biff_put_commit (bp);

	/* See: S59D67.HTM */
	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 11);
	BIFF_SET_GUINT16 (data+ 0, 0x00);   /* 1st  col formatted */
	BIFF_SET_GUINT16 (data+ 2, sheet->maxx);   /* last col formatted */
	BIFF_SET_GUINT16 (data+ 4, 0x0b9b); /* width */
	BIFF_SET_GUINT16 (data+ 6, 0x0f);   /* XF index */
	BIFF_SET_GUINT16 (data+ 8, 0x00);   /* options */
	BIFF_SET_GUINT8  (data+10, 0x00);   /* zero */
	ms_biff_put_commit (bp);
}

static void
write_sheet_tail (BIFF_PUT *bp, SHEET *sheet)
{
	guint8 *data;
	eBiff_version ver = sheet->wb->ver;

	if (ver <= eBiffV7) {
		/* See: S59E18.HTM */
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2, 10);
		BIFF_SET_GUINT32 (data +  0, 0x000006b6);
		BIFF_SET_GUINT32 (data +  4, 0x0);
		BIFF_SET_GUINT16 (data +  8, 0x0);
		ms_biff_put_commit (bp);
	} else {
		printf ("FIXME: need magic window2 numbers\n");
	}
	
	/* See: S59DE2.HTM */
	data = ms_biff_put_len_next (bp, BIFF_SELECTION, 15);
	BIFF_SET_GUINT32 (data +  0, 0x00000003);
	BIFF_SET_GUINT32 (data +  4, 0x01000000);
	BIFF_SET_GUINT32 (data +  8, 0x0);
	BIFF_SET_GUINT16 (data + 12, 0x0);
	BIFF_SET_GUINT8  (data + 14, 0x0);
	ms_biff_put_commit (bp);
}

/* See: S59DDB.HTM */
static ms_ole_pos_t
write_rowinfo (BIFF_PUT *bp, guint32 row, guint32 width)
{
	guint8 *data;
	ms_ole_pos_t pos = bp->streamPos;

	data = ms_biff_put_len_next (bp, BIFF_ROW, 16);
	BIFF_SET_GUINT16 (data +  0, row);    /* Row number */
	BIFF_SET_GUINT16 (data +  2, 0);      /* first def. col */
	BIFF_SET_GUINT16 (data +  4, width);  /* last  def. col */
	BIFF_SET_GUINT16 (data +  6, 0xff);   /* height */
	BIFF_SET_GUINT16 (data +  8, 0x00);   /* undocumented */
	BIFF_SET_GUINT16 (data + 10, 0x00);   /* reserved */
	BIFF_SET_GUINT16 (data + 12, 0x0100); /* options */
	BIFF_SET_GUINT16 (data + 14, 0x000f); /* magic so far */
	ms_biff_put_commit (bp);

	return pos;
}

static void
write_db_cell (BIFF_PUT *bp, ms_ole_pos_t start)
{
	/* See: 'Finding records in BIFF files': S59E28.HTM */
	/* See: 'DBCELL': S59D6D.HTM */
	
	ms_ole_pos_t pos = bp->streamPos;
	guint8 *data = ms_biff_put_len_next (bp, BIFF_DBCELL, 6);

	BIFF_SET_GUINT32 (data    , pos - start);
	BIFF_SET_GUINT16 (data + 4, 0); /* Only 1 row starts at the beggining */

	ms_biff_put_commit (bp);
}

static void
write_sheet (BIFF_PUT *bp, SHEET *sheet)
{
	guint32 x, y, maxx, maxy;

	sheet->streamPos = bp->streamPos;
	biff_bof_write (bp, sheet->wb->ver, eBiffTWorksheet);

	write_sheet_bools (bp, sheet);

/* FIXME: INDEX, UG! see S59D99.HTM */
/* Finding cell records in Biff files see: S59E28.HTM */
	maxx = sheet->maxx;
	maxy = sheet->maxy;
#if EXCEL_DEBUG > 0
	printf ("Saving sheet '%s' geom (%d, %d)\n", sheet->gnum_sheet->name,
		maxx, maxy);
#endif
	for (y=0;y<maxy;y++) {
		guint32 run_size = 0;
		ms_ole_pos_t start;

		start = write_rowinfo (bp, y, maxx);

		for (x=0;x<maxx;x++) {
			Cell *cell = sheet_cell_get (sheet->gnum_sheet, x, y);
			if (!cell)
				run_size++;
			else if (run_size) {
				write_mulblank (bp, sheet, x, y, run_size);
				run_size = 0;
			} else
				write_cell (bp, sheet, cell);
		}
		if (run_size)
			write_mulblank (bp, sheet, x, y, run_size);

		write_db_cell (bp, start);
	}
	write_sheet_tail (bp, sheet);

	biff_eof_write (bp);
}

static void
new_sheet (WORKBOOK *wb, Sheet *value)
{
	SHEET     *sheet = g_new (SHEET, 1);

	g_return_if_fail (value);
	g_return_if_fail (wb);

	sheet->gnum_sheet = value;
	sheet->streamPos  = 0x0deadbee;
	sheet->wb         = wb;
	sheet->maxx       = sheet->gnum_sheet->max_col_used+1;
	sheet->maxy       = sheet->gnum_sheet->max_row_used+1;

	printf ("Workbook  %d %p\n", wb->ver, wb->gnum_wb);
	g_ptr_array_add (wb->sheets, sheet);
}

static void
write_workbook (BIFF_PUT *bp, Workbook *gwb, eBiff_version ver)
{
	WORKBOOK *wb = g_new (WORKBOOK, 1);
	SHEET    *s  = 0;
	int       lp;
	GList    *sheets;

	wb->ver      = ver;
	wb->gnum_wb  = gwb;
	wb->sheets   = g_ptr_array_new ();
	
	sheets = workbook_sheets (gwb);
	while (sheets) {
		new_sheet (wb, sheets->data);
		sheets = g_list_next (sheets);
	}

	/* Workbook */
	biff_bof_write (bp, ver, eBiffTWorkbook);

	write_magic_interface (bp, ver);
	write_constants       (bp, ver);
	write_externsheets    (bp, wb, NULL);
	write_bits            (bp, ver);

	wb->fonts   = write_fonts (bp, wb);
	wb->formats = write_formats (bp, wb);
	write_xf (bp, wb);
	wb->pal     = write_palette (bp, wb);

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

	fonts_free (wb->fonts);
	palette_free (wb->pal);

	g_list_free (sheets);
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
	while (bp->pos->size < 0x1000) {
		ms_biff_put_len_next   (bp, 0, 0);
		ms_biff_put_commit (bp);
	}

	ms_biff_put_destroy    (bp);

	ms_ole_stream_close (str);
	ms_ole_directory_destroy (dir);
	return 1;
}

