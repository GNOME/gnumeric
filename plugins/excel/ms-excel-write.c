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
#include <math.h>

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
#include "utils.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "excel.h"
#include "ms-excel-write.h"
#include "ms-formula-write.h"

#define EXCEL_DEBUG 0

/**
 *  This function writes simple strings...
 *  FIXME: see S59D47.HTM for full description
 *  it returns the length of the string.
 **/
int
biff_put_text (BiffPut *bp, char *txt, eBiff_version ver,
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
	for (lp=0; lp<len;lp++) {
		MS_OLE_SET_GUINT16 (data, txt[lp]);
		ms_biff_put_var_write (bp, data, unicode?2:1);
	}
	return off + len*(unicode?2:1);

	/* An attempt at efficiency */
/*	chunks = len/BLK_LEN;
	pos    = 0;
	for (lpc=0;lpc<chunks;lpc++) {
		for (lp=0;lp<BLK_LEN;lp++,pos++)
			data[lp] = txt[pos];
		data[BLK_LEN] = '\0';
		printf ("Writing chunk '%s'\n", data); 
		ms_biff_put_var_write (bp, data, BLK_LEN);
	}
	len = len-pos;
	if (len > 0) {
	        for (lp=0;lp<len;lp++,pos++)
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

	if (num_sheets == 1) /* Not enough sheets for extern records */
		return;

	if (ignore) /* Strangely needed */
		num_sheets--;

	g_assert (num_sheets < 0xffff);

	data = ms_biff_put_len_next (bp, BIFF_EXTERNCOUNT, 2);
	MS_OLE_SET_GUINT16(data, num_sheets);
	ms_biff_put_commit (bp);

	for (lp=0;lp<num_sheets;lp++) {
		ExcelSheet *sheet = g_ptr_array_index (wb->sheets, lp);
		gint len = strlen (sheet->gnum_sheet->name);
		guint8 data[8];

		if (sheet == ignore) continue;
		
		ms_biff_put_var_next (bp, BIFF_EXTERNSHEET);
		MS_OLE_SET_GUINT8(data, len);
		MS_OLE_SET_GUINT8(data+1, 3); /* Magic */
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

	/* See: S59E1A.HTM */
	ms_biff_put_var_next (bp, BIFF_WRITEACCESS);
/*	biff_put_text (bp, "the free software foundation   ", ver, TRUE); */
	biff_put_text (bp, "Michael Meeks", ver, TRUE, AS_PER_VER); /* For testing */
	ms_biff_put_var_write (bp, "                  "
			       "                "
			       "                "
			       "                "
			       "                "
			       "                  ", 16*6+2);
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
		data = ms_biff_put_len_next (bp, BIFF_TABID, len);
		for (lp = 0; lp < len; lp++) /* FIXME: ? */
			MS_OLE_SET_GUINT16 (data + lp*2, lp);
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

	if (ver >= eBiffV8) {
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
	for (lp=0;lp<wb->sheets->len;lp++) {
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

#define PALETTE_WHITE 0
#define PALETTE_BLACK 1

static Palette *
write_palette (BiffPut *bp, ExcelWorkbook *wb)
{
	Palette *pal = g_new (Palette, 1);
	guint8  r,g,b, data[8];
	guint32 num, i;
	pal->col_to_idx = g_hash_table_new (g_direct_hash,
					    g_direct_equal);
	/* FIXME: should scan styles for colors and write intelligently. */
	ms_biff_put_var_next (bp, BIFF_PALETTE);
	
	MS_OLE_SET_GUINT16 (data, EXCEL_DEF_PAL_LEN); /* Entries */

	ms_biff_put_var_write (bp, data, 2);
	for (i=0;i<EXCEL_DEF_PAL_LEN;i++) {
		r = excel_default_palette[i].r;
		g = excel_default_palette[i].g;
		b = excel_default_palette[i].b;
		num = (b<<16) + (g<<8) + (r<<0);
		MS_OLE_SET_GUINT32 (data, num);
		ms_biff_put_var_write (bp, data, 4);
	}

	ms_biff_put_commit (bp);

	return pal;
}

/* To be used ... */
static guint32
palette_lookup (Palette *pal, StyleColor *col)
{
	return PALETTE_WHITE;
}

static void
palette_free (Palette *pal)
{
	if (pal) {
		/* Leak need to free indexes too */
		g_hash_table_destroy (pal->col_to_idx);
		g_free (pal);
	}
}

#define FONT_MAGIC 0

/* See S59D8C.HTM */
static Fonts *
write_fonts (BiffPut *bp, ExcelWorkbook *wb)
{
	Fonts *fonts = g_new (Fonts, 1);
	guint8 data[64];
	int lp;
	
	for (lp=0;lp<4;lp++) { /* FIXME: Magic minimum fonts */
		fonts->StyleFont_to_idx = g_hash_table_new (g_direct_hash,
							    g_direct_equal);
		/* Kludge for now ... */
		ms_biff_put_var_next (bp, BIFF_FONT);
		
		MS_OLE_SET_GUINT16(data + 0, 10*20); /* 10 point */
		MS_OLE_SET_GUINT16(data + 2, (0<<1) + (0<<3)); /* italic, struck */
/*		MS_OLE_SET_GUINT16(data + 4, PALETTE_BLACK); */
		MS_OLE_SET_GUINT16(data + 4, 0x7fff); /* Magic ! */

		if (1)
			MS_OLE_SET_GUINT16(data + 6, 0x190); /* Normal boldness */
		else
			MS_OLE_SET_GUINT16(data + 6, 0x2bc); /* Bold */

		MS_OLE_SET_GUINT16(data + 8, 0); /* 0: Normal, 1; Super, 2: Sub script*/
		MS_OLE_SET_GUINT16(data +10, 0); /* No underline */
		MS_OLE_SET_GUINT16(data +12, 0); /* seems OK. */
		MS_OLE_SET_GUINT8 (data +13, 0);
		ms_biff_put_var_write (bp, data, 14);
		
		biff_put_text (bp, "Arial", wb->ver, TRUE, EIGHT_BIT);
		
		ms_biff_put_commit (bp);
	}

	return fonts;
}

static guint32
fonts_get_index (Fonts *fonts, StyleFont *sf)
{
	return FONT_MAGIC;
}

static void
fonts_free (Fonts *fonts)
{
	if (fonts) {
		g_free (fonts->StyleFont_to_idx);
		g_free (fonts);
	}
}

#define FORMAT_MAGIC 0

/* See S59D8E.HTM */
static Formats *
write_formats (BiffPut *bp, ExcelWorkbook *wb)
{
	Formats *formats = g_new (Formats, 1);
	int   magic_num[] = { 5, 6, 7, 8, 0x2a, 0x29, 0x2c, 0x2b };
	char *magic[] = {
		"\"\xa3\"#,##0;\\-\"\xa3\"#,##0",
		"\"\xa3\"#,##0;[Red]\\-\"\xa3\"#,##0",
		"\"\xa3\"#,##0.00;\\-\"\xa3\"#,##0.00",
		"\"\xa3\"#,##0.00;[Red]\\-\"\xa3\"#,##0.00",
		"_-\"\xa3\"*\x20#,##0_-;\\-\"\xa3\"*\x20#,##0_-;_-\"\xa3\"*\x20\"-\"_-;_-@_-",
		"_-*\x20#,##0_-;\\-*\x20#,##0_-;_-*\x20\"-\"_-;_-@_-",
		"_-\"\xa3\"*\x20#,##0.00_-;\\-\"\xa3\"*\x20#,##0.00_-;_-\"\xa3\"*\x20\"-\"??_-;_-@_-",
		"_-*\x20#,##0.00_-;\\-*\x20#,##0.00_-;_-*\x20\"-\"??_-;_-@_-",
	};
	guint8 data[64];
	int lp;
	
	for (lp=0;lp<8;lp++) { /* FIXME: Magic minimum formats */
		guint fidx = magic_num[lp];
		char *fmt;
		formats->StyleFormat_to_idx = g_hash_table_new (g_direct_hash,
								g_direct_equal);
		/* Kludge for now ... */
		if (wb->ver >= eBiffV7)
			ms_biff_put_var_next (bp, (0x400|BIFF_FORMAT));
		else
			ms_biff_put_var_next (bp, BIFF_FORMAT);
		
		g_assert (fidx < EXCEL_BUILTIN_FORMAT_LEN);
		g_assert (fidx >= 0);
		fmt = magic[lp]; /*excel_builtin_formats[fidx];*/

		MS_OLE_SET_GUINT16 (data, fidx);
		ms_biff_put_var_write (bp, data, 2);

		if (fmt)
			biff_put_text (bp, fmt, eBiffV7, TRUE, AS_PER_VER);
		else
			biff_put_text (bp, "", eBiffV7, TRUE, AS_PER_VER);
		
		ms_biff_put_commit (bp);
	}

	return formats;
}

static guint32
formats_get_index (Formats *formats, StyleFormat *sf)
{
	return FORMAT_MAGIC;
}

static void
formats_free (Formats *formats)
{
	if (formats) {
		g_free (formats->StyleFormat_to_idx);
		g_free (formats);
	}
}

#define XF_MAGIC 15

/* See S59E1E.HTM */
static void
write_xf_record (BiffPut *bp, Style *style, eBiff_version ver, int hack)
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
		MS_OLE_SET_GUINT16(data+0, fonts_get_index (0, 0));
		MS_OLE_SET_GUINT16(data+2, formats_get_index (0, 0));
		MS_OLE_SET_GUINT16(data+18, 0xc020); /* Color ! */
		ms_biff_put_var_write (bp, data, 24);
	} else {
		MS_OLE_SET_GUINT16(data+0, fonts_get_index (0, 0));
		MS_OLE_SET_GUINT16(data+2, formats_get_index (0, 0));
		MS_OLE_SET_GUINT16(data+4, 0xfff5); /* FIXME: Magic */
		MS_OLE_SET_GUINT16(data+6, 0xf420);
		MS_OLE_SET_GUINT16(data+8, 0x20c0); /* Color ! */

		if (hack == 1 || hack == 2)
			MS_OLE_SET_GUINT16 (data,1);
		if (hack == 3 || hack == 4)
			MS_OLE_SET_GUINT16 (data,2);
		if (hack == 15) {
			MS_OLE_SET_GUINT16 (data+4, 1);
			MS_OLE_SET_GUINT8  (data+7, 0x0);
		}
		if (hack == 16)
			MS_OLE_SET_GUINT32 (data, 0x002b0001); /* These turn up in the formats... */
		if (hack == 17)
			MS_OLE_SET_GUINT32 (data, 0x00290001);
		if (hack == 18)
			MS_OLE_SET_GUINT32 (data, 0x002c0001);
		if (hack == 19)
			MS_OLE_SET_GUINT32 (data, 0x002a0001);
		if (hack == 20)
			MS_OLE_SET_GUINT32 (data, 0x00090001);
		if (hack < 21 && hack > 15) /* Style bit ? */
			MS_OLE_SET_GUINT8  (data+7, 0xf8);
		if (hack == 0)
			MS_OLE_SET_GUINT8  (data+7, 0);			

		ms_biff_put_var_write (bp, data, 16);
	}
	ms_biff_put_commit (bp);
}

static XF *
write_xf (BiffPut *bp, ExcelWorkbook *wb)
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
		write_xf_record (bp, NULL, wb->ver,lp);

	/* See: S59DEA.HTM */
	for (lp=0;lp<6;lp++) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_STYLE, 4);
		MS_OLE_SET_GUINT32 (data, style_magic[lp]); /* cop out */
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

#if EXCEL_DEBUG > 0
		printf ("writing %d %d %d\n", vint, v->v.v_int);
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

#if EXCEL_DEBUG > 0
		printf ("writing %g is (%g %g) is int ? %d\n", val, 1.0*(int)val,
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

static void
write_formula (BiffPut *bp, ExcelSheet *sheet, Cell *cell)
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
	EX_SETXF  (data, XF_MAGIC);
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

static void
write_cell (BiffPut *bp, ExcelSheet *sheet, Cell *cell)
{
	gint col, row;
	g_return_if_fail (bp);
	g_return_if_fail (cell);
	
	col = cell->col->pos;
	row = cell->row->pos;

#if EXCEL_DEBUG > 2
	{
		ParsePosition tmp;
		printf ("Cell at %s '%s' = '%s'\n", cell_name (col, row),
			cell->parsed_node?expr_decode_tree (cell->parsed_node,
							    parse_pos_init (&tmp, sheet->wb->gnum_wb,
									    col, row)):"none",
			cell->value?value_get_as_string (cell->value):"empty");
	}
#endif
	if (cell->parsed_node)
		write_formula (bp, sheet, cell);
	else if (cell->value)
		write_value (bp, cell->value, sheet->wb->ver,
			     col, row, XF_MAGIC);
}

#define MAGIC_BLANK_XF 0

static void
write_mulblank (BiffPut *bp, ExcelSheet *sheet, guint32 end_col, guint32 row, guint32 run)
{
	g_return_if_fail (bp);
	g_return_if_fail (run);
	g_return_if_fail (sheet);

	if (run == 1) {
		guint8 *data;
		data = ms_biff_put_len_next (bp, 0x200|BIFF_BLANK, 6);
		EX_SETXF (data, MAGIC_BLANK_XF);
		EX_SETCOL(data, end_col);
		EX_SETROW(data, row);
		ms_biff_put_commit (bp);
	} else { /* S59DA7.HTM */
		guint8   *ptr;
		guint32 len = 4 + 2*run + 2;
		guint8 *data;
	
		data = ms_biff_put_len_next (bp, BIFF_MULBLANK, len);

		EX_SETCOL (data, end_col-run);
		EX_SETROW (data, row);
		MS_OLE_SET_GUINT16 (data + len - 2, end_col);
		ptr = data + 4;
		while (run > 0) {
			MS_OLE_SET_GUINT16 (ptr, MAGIC_BLANK_XF);
			ptr+=2;
			run--;
		}
	}
}

static void
write_sheet_bools (BiffPut *bp, ExcelSheet *sheet)
{
	guint8 *data;
/*	eBiff_version ver = sheet->wb->ver; */

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
	MS_OLE_SET_GUINT16 (data, 0x0000);
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

	/* See: S59D72.HTM */
	data = ms_biff_put_len_next (bp, 0x200|BIFF_DEFAULTROWHEIGHT, 4);
	MS_OLE_SET_GUINT32 (data, 0x00ff0000);
	ms_biff_put_commit (bp);

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
	MS_OLE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

	/* See: S59E15.HTM */
	data = ms_biff_put_len_next (bp, BIFF_VCENTER, 2);
	MS_OLE_SET_GUINT16 (data, 0x0000);
	ms_biff_put_commit (bp);

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

	/* See: S59D73.HTM */
	data = ms_biff_put_len_next (bp, BIFF_DEFCOLWIDTH, 2);
	MS_OLE_SET_GUINT16 (data, 0x0008);
	ms_biff_put_commit (bp);

	/* See: S59D67.HTM */
	data = ms_biff_put_len_next (bp, BIFF_COLINFO, 12);
	MS_OLE_SET_GUINT16 (data+ 0, 0x00);   /* 1st  col formatted */
	MS_OLE_SET_GUINT16 (data+ 2, sheet->maxx);   /* last col formatted */
	MS_OLE_SET_GUINT16 (data+ 4, 0x0c49); /* width */
	MS_OLE_SET_GUINT16 (data+ 6, 0x0f);   /* XF index */
	MS_OLE_SET_GUINT16 (data+ 8, 0x00);   /* options */
	MS_OLE_SET_GUINT16 (data+10, 0x00);   /* magic */
	ms_biff_put_commit (bp);

	/* See: S59D76.HTM */
	if (sheet->wb->ver >= eBiffV8) {
		data = ms_biff_put_len_next (bp, BIFF_DIMENSIONS, 14);
		MS_OLE_SET_GUINT32 (data +  0, 0);
		MS_OLE_SET_GUINT32 (data +  4, sheet->maxy-1);
		MS_OLE_SET_GUINT16 (data +  8, 0);
		MS_OLE_SET_GUINT16 (data + 10, sheet->maxx);
		MS_OLE_SET_GUINT16 (data + 12, 0x0000);
	} else {
		data = ms_biff_put_len_next (bp, (0x200 | BIFF_DIMENSIONS), 10);
		MS_OLE_SET_GUINT16 (data +  0, 0);
		MS_OLE_SET_GUINT16 (data +  2, sheet->maxy-1);
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
	if (ver <= eBiffV7) {
		/* See: S59E18.HTM */
		data = ms_biff_put_len_next (bp, BIFF_WINDOW2, 10);
		MS_OLE_SET_GUINT32 (data +  0, 0x000006b6);
		MS_OLE_SET_GUINT32 (data +  4, 0x0);
		MS_OLE_SET_GUINT16 (data +  8, 0x0);
		ms_biff_put_commit (bp);
	} else {
		printf ("FIXME: need magic window2 numbers\n");
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
		for (lp=0;lp<34;lp++)
			MS_OLE_SET_GUINT8 (data+lp, 0xff);
		MS_OLE_SET_GUINT32 (data, 0xfffd0020);
	}
	ms_biff_put_commit (bp);
*/
}

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

	g_assert (sheet->maxy >= sheet->dbcells->len);
	for (lp=0;lp<sheet->dbcells->len;lp++) {
		MsOlePos pos = g_array_index (sheet->dbcells, MsOlePos, lp);
		MS_OLE_SET_GUINT32 (data, pos - sheet->wb->streamPos);
#if EXCEL_DEBUG > 1
		printf ("writing index record 0x%4x - 0x%4x = 0x%4x\n",
			pos, sheet->wb->streamPos, pos - sheet->wb->streamPos);
#endif
		s->write (s, data, 4);
	}

	s->lseek (s, oldpos, MsOleSeekSet);
}

/* See: S59DDB.HTM */
static MsOlePos
write_rowinfo (BiffPut *bp, guint32 row, guint32 width)
{
	guint8 *data;
	MsOlePos pos;

	data = ms_biff_put_len_next (bp, (0x200 | BIFF_ROW), 16);
	pos = bp->streamPos;
	MS_OLE_SET_GUINT16 (data +  0, row);    /* Row number */
	MS_OLE_SET_GUINT16 (data +  2, 0);      /* first def. col */
	MS_OLE_SET_GUINT16 (data +  4, width);  /* last  def. col */
	MS_OLE_SET_GUINT16 (data +  6, 0xff);   /* height */
	MS_OLE_SET_GUINT16 (data +  8, 0x00);   /* undocumented */
	MS_OLE_SET_GUINT16 (data + 10, 0x00);   /* reserved */
	MS_OLE_SET_GUINT16 (data + 12, 0x0100); /* options */
	MS_OLE_SET_GUINT16 (data + 14, 0x000f); /* magic so far */
	ms_biff_put_commit (bp);

	return pos;
}

static void
write_db_cell (BiffPut *bp, ExcelSheet *sheet, MsOlePos start)
{
	/* See: 'Finding records in BIFF files': S59E28.HTM */
	/* See: 'DBCELL': S59D6D.HTM */
	
	MsOlePos pos;

	guint8 *data = ms_biff_put_len_next (bp, BIFF_DBCELL, 6);
	pos = bp->streamPos;

	MS_OLE_SET_GUINT32 (data    , pos - start);
	MS_OLE_SET_GUINT16 (data + 4, 0); /* Only 1 row starts at the beggining */

	ms_biff_put_commit (bp);

	g_array_append_val (sheet->dbcells, pos);
}

/* See: 'Finding records in BIFF files': S59E28.HTM */
/* and S59D99.HTM */
static void
write_sheet (BiffPut *bp, ExcelSheet *sheet)
{
	guint32 x, y, maxx, maxy;
	MsOlePos index_off;

	sheet->streamPos = biff_bof_write (bp, sheet->wb->ver, eBiffTWorksheet);

	if (sheet->maxy > 4096)
		g_error ("Sheet seems impossibly big");
	
	if (sheet->wb->ver >= eBiffV8) {
		guint8 *data = ms_biff_put_len_next (bp, BIFF_INDEX,
						     sheet->maxy*4 + 16);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT32 (data +  4, 0);
		MS_OLE_SET_GUINT32 (data +  8, sheet->maxy);
		MS_OLE_SET_GUINT32 (data + 12, 0);
	} else {
		guint8 *data = ms_biff_put_len_next (bp, 0x200|BIFF_INDEX,
						     sheet->maxy*4 + 12);
		index_off = bp->streamPos;
		MS_OLE_SET_GUINT32 (data, 0);
		MS_OLE_SET_GUINT16 (data + 4, 0);
		MS_OLE_SET_GUINT16 (data + 6, sheet->maxy);
		MS_OLE_SET_GUINT32 (data + 8, 0);
	}
	ms_biff_put_commit (bp);

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
		MsOlePos start;

		start = write_rowinfo (bp, y, maxx);

		for (x=0;x<maxx;x++) {
			Cell *cell = sheet_cell_get (sheet->gnum_sheet, x, y);
			if (!cell)
				run_size++;
			else {
				if (run_size)
					write_mulblank (bp, sheet, x, y, run_size);
				run_size = 0;
				write_cell (bp, sheet, cell);
			}
		}
		if (run_size > 0 && run_size < maxx)
			write_mulblank (bp, sheet, x, y, run_size);

		write_db_cell (bp, sheet, start);
	}

	write_index (bp->pos, sheet, index_off);
	write_sheet_tail (bp, sheet);

	biff_eof_write (bp);
}

static void
new_sheet (ExcelWorkbook *wb, Sheet *value)
{
	ExcelSheet     *sheet = g_new (ExcelSheet, 1);

	g_return_if_fail (value);
	g_return_if_fail (wb);

	sheet->gnum_sheet = value;
	sheet->streamPos  = 0x0deadbee;
	sheet->wb         = wb;
	sheet->maxx       = sheet->gnum_sheet->max_col_used+1;
	sheet->maxy       = sheet->gnum_sheet->max_row_used+1;
	sheet->dbcells    = g_array_new (FALSE, FALSE, sizeof (MsOlePos));

	printf ("Workbook  %d %p\n", wb->ver, wb->gnum_wb);
	g_ptr_array_add (wb->sheets, sheet);
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
	
	sheets = workbook_sheets (gwb);
	while (sheets) {
		new_sheet (wb, sheets->data);
		sheets = g_list_next (sheets);
	}

	/* Workbook */
	wb->streamPos = biff_bof_write (bp, ver, eBiffTWorkbook);

	write_magic_interface (bp, ver);
/*	write_externsheets    (bp, wb, NULL); */
	write_bits            (bp, wb, ver);

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
		ExcelSheet *s = g_ptr_array_index (wb->sheets, lp);
		biff_boundsheet_write_last (bp->pos, s->boundsheetPos,
					    s->streamPos);
	}
	/* End Finalised workbook */

	fonts_free (wb->fonts);
	palette_free (wb->pal);

	g_list_free (sheets);
}

int
ms_excel_write_workbook (MsOle *file, Workbook *wb,
			 eBiff_version ver)
{
	MsOleDirectory *dir;
	MsOleStream    *str;
	BiffPut *bp;
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
				       MsOlePPSStream);
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
