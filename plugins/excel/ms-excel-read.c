/**
 * ms-excel.c: MS Excel support for Gnumeric
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

#include "excel.h"
#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-obj.h"
#include "ms-escher.h"

#define EXCEL_DEBUG       0
#define EXCEL_DEBUG_COLOR 0

/* This many styles are reserved */
#define XF_MAGIC_OFFSET (16 + 4)

/* Forward references */
static MS_EXCEL_SHEET *ms_excel_sheet_new       (MS_EXCEL_WORKBOOK *wb,
						 char *name) ;
static void            ms_excel_workbook_attach (MS_EXCEL_WORKBOOK *wb,
						 MS_EXCEL_SHEET *ans) ;

/**
 * Generic 16 bit int index pointer functions.
 **/
static guint
biff_guint16_hash (const guint16 *d)
{ return *d*2 ; }
static guint
biff_guint32_hash (const guint32 *d)
{ return *d*2 ; }

static gint
biff_guint16_equal (const guint16 *a, const guint16 *b)
{
	if (*a==*b) return 1 ;
	return 0 ;
}
static gint
biff_guint32_equal (const guint32 *a, const guint32 *b)
{
	if (*a==*b) return 1 ;
	return 0 ;
}

/**
 * This returns whether there is a header byte
 * and sets various flags from it
 **/
static gboolean
biff_string_get_flags (BYTE *ptr,
		       gboolean *word_chars,
		       gboolean *extended,
		       gboolean *rich)
{
	BYTE header ;

	header = BIFF_GETBYTE(ptr) ;
	/* I assume that this header is backwards compatible with raw ASCII */
	if (((header & 0xf0) == 0) &&
	    ((header & 0x02) == 0)) /* Its a proper Unicode header grbit byte */
	{
		*word_chars = (header & 0x1) != 0 ;
		*extended   = (header & 0x4) != 0 ;
		*rich       = (header & 0x8) != 0 ;
		return TRUE ;
	}
	else /* Some assumptions: FIXME ? */
	{
		*word_chars = 0 ;
		*extended   = 0 ;
		*rich       = 0 ;
		return FALSE ;
	}
}

/**
 *  This function takes a length argument as Biff V7 has a byte length
 * ( seemingly ).
 * it returns the length in bytes of the string in byte_length
 * or nothing if this is NULL.
 *  FIXME: see S59D47.HTM for full description
 **/
char *
biff_get_text (BYTE *pos, guint32 length, guint32* byte_length)
{
	guint32 lp ;
	char *ans;
	BYTE *ptr ;
	guint32 byte_len ;
	gboolean header ;
	gboolean high_byte ;
	gboolean ext_str ;
	gboolean rich_str ;

	if (!byte_length)
		byte_length = &byte_len ;
	*byte_length = 0 ;

	if (!length)
		return 0 ;

	ans = (char *) g_new (char, length + 2);

	header = biff_string_get_flags (pos,
					&high_byte,
					&ext_str,
					&rich_str) ;
	if (header) {
		ptr = pos + 1 ;
		(*byte_length)++ ;
	}
	else
		ptr = pos ;

	/* A few friendly warnings */
	if (high_byte)
		printf ("FIXME: unicode support unimplemented: truncating\n") ;
	if (rich_str) /* The data for this appears after the string */
	{
		guint16 formatting_runs = BIFF_GETWORD(ptr) ;
		(*byte_length) += 2 ;
		printf ("FIXME: rich string support unimplemented: discarding %d runs\n", formatting_runs) ;
		(*byte_length) += formatting_runs*4 ; /* 4 bytes per */
		ptr+= 2 ;
	}
	if (ext_str) /* NB this data always comes after the rich_str data */
	{
		guint32 len_ext_rst = BIFF_GETLONG(ptr) ; /* A byte length */
		(*byte_length) += 4 + len_ext_rst ;
		ptr+= 4 ;
		printf ("FIXME: extended string support unimplemented: ignoring %d bytes\n", len_ext_rst) ;
	}
	(*byte_length) += length * (high_byte ? 2 : 1) ;
#if EXCEL_DEBUG > 4
		printf ("String len %d, byte length %d: %d %d %d:\n",
			length, (*byte_length), high_byte, rich_str, ext_str) ;
		dump (pos, *byte_length) ;
#endif

	for (lp = 0; lp < length; lp++) {
		guint16 c;
		if (high_byte) {
			c = BIFF_GETWORD(ptr);
			ptr+=2;
		} else {
			c = BIFF_GETBYTE(ptr);
			ptr+=1;
		}
		ans[lp] = (char)c;
	}
	ans[lp] = 0;
	return ans;
}

static char *
biff_get_global_string(MS_EXCEL_SHEET *sheet, int number)
{
	MS_EXCEL_WORKBOOK *wb = sheet->wb;
	
        if (number >= wb->global_string_max)
		return "Too Weird";
	
	return wb->global_strings[number] ;
}

const char *
biff_get_error_text (const guint8 err)
{
	char *buf ;
	switch (err)
	{
	case 0:  buf = gnumeric_err_NULL;  break ;
	case 7:  buf = gnumeric_err_DIV0; break ;
	case 15: buf = gnumeric_err_VALUE; break ;
	case 23: buf = gnumeric_err_REF;   break ;
	case 29: buf = gnumeric_err_NAME;  break ;
	case 36: buf = gnumeric_err_NUM;   break ;
	case 42: buf = gnumeric_err_NA;    break ;
	default:
		buf = _("#UNKNOWN!"); break ;
	}
	return buf ;
}

static BIFF_SHARED_FORMULA *
biff_shared_formula_new (guint16 col, guint16 row, BYTE *data,
			 guint32 data_len)
{
	BIFF_SHARED_FORMULA *sf = g_new (BIFF_SHARED_FORMULA, 1) ;
	sf->key.col = col ;
	sf->key.row = row ;
	sf->data = data ;
	sf->data_len = data_len ;
	return sf ;
}

static gboolean 
biff_shared_formula_destroy (gpointer key, BIFF_SHARED_FORMULA *sf, gpointer userdata)
{
	g_free (sf) ;
	return 1 ;
}

/* Shared formula hashing functions */
static guint
biff_shared_formula_hash (const BIFF_SHARED_FORMULA_KEY *d)
{ return (d->row<<16)+d->col ; }

static guint
biff_shared_formula_equal (const BIFF_SHARED_FORMULA_KEY *a,
			   const BIFF_SHARED_FORMULA_KEY *b)
{
	if (a->col == b->col &&
	    a->row == b->row) return 1 ;
	return 0 ;
}

/**
 * See S59D5D.HTM
 **/
static BIFF_BOF_DATA *
ms_biff_bof_data_new (BIFF_QUERY * q)
{
	BIFF_BOF_DATA *ans = g_new (BIFF_BOF_DATA, 1);

	if ((q->opcode & 0xff) == BIFF_BOF &&
	    (q->length >= 4)){
		/*
		 * Determine type from boff
		 */
		switch (q->opcode >> 8){
		case 0:
			ans->version = eBiffV2;
			break;
		case 2:
			ans->version = eBiffV3;
			break;
		case 4:
			ans->version = eBiffV4;
			break;
		case 8:	/*
			 * More complicated 
			 */
			{
#if EXCEL_DEBUG > 2
					printf ("Complicated BIFF version %d\n",
						BIFF_GETWORD (q->data));
					dump (q->data, q->length);
#endif			       
				switch (BIFF_GETWORD (q->data))
				{
				case 0x0600:
					ans->version = eBiffV8;
					break;
				case 0x500:
					ans->version = eBiffV7;		/*
									 * OR ebiff7 : FIXME ? ! 
									 */
					break;
				default:
					printf ("Unknown BIFF sub-number in BOF %x\n", q->opcode);
					ans->version = eBiffVUnknown;
				}
			}
			break;
		default:
			printf ("Unknown BIFF number in BOF %x\n", q->opcode);
			ans->version = eBiffVUnknown;
			printf ("Biff version %d\n", ans->version);
		}
		switch (BIFF_GETWORD (q->data + 2)){
		case 0x0005:
			ans->type = eBiffTWorkbook;
			break;
		case 0x0006:
			ans->type = eBiffTVBModule;
			break;
		case 0x0010:
			ans->type = eBiffTWorksheet;
			break;
		case 0x0020:
			ans->type = eBiffTChart;
			break;
		case 0x0040:
			ans->type = eBiffTMacrosheet;
			break;
		case 0x0100:
			ans->type = eBiffTWorkspace;
			break;
		default:
			ans->type = eBiffTUnknown;
			printf ("Unknown BIFF type in BOF %x\n", BIFF_GETWORD (q->data + 2));
			break;
		}
		/*
		 * Now store in the directory array: 
		 */
#if EXCEL_DEBUG > 2
			printf ("BOF %x, %d == %d, %d\n", q->opcode, q->length,
				ans->version, ans->type);
#endif
	} else {
		printf ("Not a BOF !\n");
		ans->version = eBiffVUnknown;
		ans->type = eBiffTUnknown;
	}
	return ans;
}

static void
ms_biff_bof_data_destroy (BIFF_BOF_DATA * data)
{
	g_free (data);
}

/**
 * See S59D61.HTM
 **/
static void
biff_boundsheet_data_new (MS_EXCEL_WORKBOOK *wb, BIFF_QUERY * q, eBiff_version ver)
{
	MS_EXCEL_SHEET *sheet ;
	BIFF_BOUNDSHEET_DATA *ans = g_new (BIFF_BOUNDSHEET_DATA, 1) ;

	if (ver != eBiffV5 &&	/*
				 * Testing seems to indicate that Biff5 is compatibile with Biff7 here. 
				 */
	    ver != eBiffV7 &&
	    ver != eBiffV8){
		printf ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		ver = eBiffV7;
	}
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

	/*
	 * printf ("Blocksheet : '%s', %d:%d offset %lx\n", ans->name, ans->type, ans->hidden, ans->streamStartPos); 
	 */
	ans->index = (guint16)g_hash_table_size (wb->boundsheet_data_by_index) ;
	g_hash_table_insert (wb->boundsheet_data_by_index,
			     &ans->index, ans) ;
	g_hash_table_insert (wb->boundsheet_data_by_stream, 
			     &ans->streamStartPos, ans) ;

	g_assert (ans->streamStartPos == BIFF_GETLONG (q->data)) ;
	ans->sheet = ms_excel_sheet_new (wb, ans->name);
	ms_excel_workbook_attach (wb, ans->sheet);
}

static gboolean 
biff_boundsheet_data_destroy (gpointer key, BIFF_BOUNDSHEET_DATA *d, gpointer userdata)
{
	g_free (d->name) ;
	g_free (d) ;
	return 1 ;
}

/**
 * Ug! FIXME
 **/
static char *
biff_nasty_font_check_function (char *name1, char *name2)
{
	if (gdk_font_load(name1))
	{
		if (name2)
			g_free(name2) ;
		return name1 ;
	}
	else
	{
		if (name1)
			g_free(name1) ;
		return name2 ;
	}
}

static StyleFont*
biff_font_data_get_style_font (BIFF_FONT_DATA *fd)
{
	char font_size[4*sizeof(int)]; /* I know it may seem excessive. Time will say. */
	int i;
	char *fname1, *fname2 ;
	StyleFont *ans ;

	if (!fd->fontname) {
#if EXCEL_DEBUG > 0
		printf ("Curious no font name on %d\n", fd->index);
#endif
		return style_font_new (gnumeric_default_font->font_name, 1);
	}
	
	/*
	 * FIXME: instead of just copying the windows font into the cell, we 
	 * should implement a font name mapping mechanism.
	 * In our first attempt to make it work, let's try to guess the 
	 * X font name from the windows name, by letting the first word 
	 * of the name be inserted in 0'th position of the X font name.  
	 */
	for (i = 0; fd->fontname[i] != '\0' && fd->fontname[i] != ' '; ++i)
		fd->fontname[i] = tolower (fd->fontname[i]);
	fd->fontname[i] = '\x0';
	
	fname1 = g_strdup (gnumeric_default_font->font_name);
	fname2 = font_change_component (gnumeric_default_font->font_name, 1, fd->fontname);
	fname1 = biff_nasty_font_check_function (fname2, fname1);

/*	printf ("FoNt [-]: %s\n", fname1) ; */
	if (fd->italic) {
		fname2 = font_get_italic_name (fname1);
/*			printf ("FoNt [i]: %s\n", fname2) ;  */
	}
	else
		fname2 = g_strdup (fname1) ;
	fname1 = biff_nasty_font_check_function (fname2, fname1) ;
	
	if (fd->boldness >= 0x2bc) {
		fname2 = font_get_bold_name (fname1) ;
/*			printf ("FoNt [b]: %s\n", fname1) ; */
	}
	else
		fname2 = g_strdup (fname1) ;
	fname1 = biff_nasty_font_check_function (fname2, fname1) ;
	/* What about underlining? */

	g_snprintf (font_size, 16, "%d", fd->height / 2);
	fname2 = font_change_component (fname1, 7, font_size) ;
	fname1 = biff_nasty_font_check_function (fname2, fname1) ;
	
	ans = style_font_new (fname1, 1) ;
	g_free (fname1) ;
	
	if (fd->italic)
		ans->hint_is_italic = 1 ;

	if (fd->boldness >= 0x2bc)
		ans->hint_is_bold = 1 ;

	return ans ;
}

/**
 * NB. 'fount' is the correct, and original _English_
 **/
static void
biff_font_data_new (MS_EXCEL_WORKBOOK *wb, BIFF_QUERY *q)
{
	BIFF_FONT_DATA *fd = g_new (BIFF_FONT_DATA, 1);
	WORD data;

	fd->height = BIFF_GETWORD (q->data + 0);
	data = BIFF_GETWORD (q->data + 2);
	fd->italic     = (data & 0x2) == 0x2;
	fd->struck_out = (data & 0x8) == 0x8;
	fd->color_idx  = BIFF_GETWORD (q->data + 4);
	fd->color_idx &= 0x7f; /* Undocumented but a good idea */
	fd->boldness   = BIFF_GETWORD (q->data + 6);
	data = BIFF_GETWORD (q->data + 8);
	switch (data){
	case 0:
		fd->script = eBiffFSNone;
		break;
	case 1:
		fd->script = eBiffFSSuper;
		break;
	case 2:
		fd->script = eBiffFSSub;
		break;
	default:
		printf ("Unknown script %d\n", data);
		break;
	}
	data = BIFF_GETWORD (q->data + 10);
	switch (data){
	case 0:
		fd->underline = eBiffFUNone;
		break;
	case 1:
		fd->underline = eBiffFUSingle;
		break;
	case 2:
		fd->underline = eBiffFUDouble;
		break;
	case 0x21:
		fd->underline = eBiffFUSingleAcc;
		break;
	case 0x22:
		fd->underline = eBiffFUDoubleAcc;
		break;
	}
	fd->fontname = biff_get_text (q->data + 15, BIFF_GETBYTE (q->data + 14), NULL);

#if EXCEL_DEBUG > 0
		printf ("Insert font '%s' size %d pts color %d\n",
			fd->fontname, fd->height / 20, fd->color_idx);
#endif
	fd->style_font = 0 ;
        fd->index = g_hash_table_size (wb->font_data) ;
	if (fd->index >= 4) /* Wierd: for backwards compatibility */
		fd->index++ ;
	g_hash_table_insert (wb->font_data, &fd->index, fd) ;
}

static gboolean 
biff_font_data_destroy (gpointer key, BIFF_FONT_DATA *fd, gpointer userdata)
{
	g_free (fd->fontname) ;
	if (fd->style_font)
		style_font_unref (fd->style_font) ;
	g_free (fd) ;
	return 1 ;
}

static StyleFormat *
biff_format_data_lookup (MS_EXCEL_WORKBOOK *wb, guint16 idx)
{
	char *low_formats[] =
	{
		"",
		"0",
		"0.00",
		"#,##0",
		"#,##0.00",
		"($#,##0_);($#,##0)",
		"($#,##0_);[Red]($#,##0)",
		"($#,##0.00_);($#,##0.00)",
		"($#,##0.00_);[Red]($#,##0.00)",
		"0%",
		"0.00%",
		"0.00E+00",
		"#",
		"#",
		"m/d/yy",
		"d-mmm-yy",
		"d-mmm",
		"mmm-yy",
		"h:mm",
		"h:mm:ss",
		"h:mm",
		"h:mm:ss",
		"m/d/yy"
	} ;
	char *high_formats[] =
	{
		"(#,##0_);(#,##0)",
		"(#,##0_);[Red](#,##0)",
		"(#,##0.00_);(#,##0.00)",
		"(#,##0.00_);[Red](#,##0.00)",
		"_(*",
		"_($*",
		"_(*",
		"_($*",
		"mm:ss",
		"[h]:mm:ss",
		"mm:ss.0",
		"##0.0E+0",
		"@"
	} ;
	char *ans ;
	if (idx <= 0x16)
		ans = low_formats[idx] ;
	else if (idx < 0x25)
	{
		printf ("Foreign undocumented format\n") ;
		ans = 0 ;
	}
	else if (idx <= 0x31)
		ans = high_formats[idx-0x25] ;
	else
	{
		BIFF_FORMAT_DATA *d = g_hash_table_lookup (wb->format_data,
							   &idx) ;
		if (!d)
		{
			printf ("Unknown format: 0x%x\n", idx) ;
			ans = 0 ;
		}
		else
			ans = d->name ;
	}
	if (ans)
		return style_format_new (ans) ;
	else
		return NULL ;
}

static gboolean 
biff_format_data_destroy (gpointer key, BIFF_FORMAT_DATA *d, gpointer userdata)
{
	g_free (d->name) ;
	g_free (d) ;
	return 1 ;
}

typedef struct {
	guint16 idx ;
	char *name ;
	guint8 *formula ;
	guint16 formula_len ;
} BIFF_NAME_DATA ;

/**
 * A copy of name is kept but
 * formula is g_malloc'd and copied
 **/
static void
biff_name_data_new (MS_EXCEL_WORKBOOK *wb, char *name, guint8 *formula, guint16 len)
{
	BIFF_NAME_DATA *bnd = g_new (BIFF_NAME_DATA, 1) ;
	bnd->idx = g_hash_table_size (wb->name_data) + 1 ;
	bnd->name = name ;
	if (formula) {
		bnd->formula = g_new (guint8, len) ;
		memcpy (bnd->formula, formula, len) ;
		bnd->formula_len = len ;
	} else {
		bnd->formula = 0 ;
		bnd->formula_len = 0 ;
	}
	g_hash_table_insert (wb->name_data, &bnd->idx, bnd) ;
/*	printf ("Inserting '%s' into externname table at (%d)\n", bnd->name, bnd->idx) ; */
}

char *
biff_name_data_get_name (MS_EXCEL_SHEET *sheet, guint16 idx)
{
	BIFF_NAME_DATA *ptr = g_hash_table_lookup (sheet->wb->name_data, &idx) ;
	if (ptr)
		return ptr->name ;
	else
		return 0 ;
}

static gboolean 
biff_name_data_destroy (gpointer key, BIFF_NAME_DATA *bnd, gpointer userdata)
{
	g_free (bnd->name) ;
	g_free (bnd->formula) ;
	g_free (bnd) ;
	return 1 ;
}

static MS_EXCEL_PALETTE *
ms_excel_default_palette ()
{
	static MS_EXCEL_PALETTE * pal = NULL;

	if (!pal)
	{
		static struct default_color
		{
			/* These were generated by creating a sheet and
			 * modifying the 1st color cell and saving.  This
			 * created a custom palette.  I then loaded the sheet
			 * into gnumeric and dumped the results.  Unfortunately
			 * there was a bug in the extraction that swapped the
			 * red and blue.  It is too much effort to retype this.
			 * So I'll leave it in this odd format for now.
			 */
			int b, g, r;
		} default_colors[56] = {
		    { 0,0,0 }, { 255,255,255 }, { 0,0,255 }, { 0,255,0 },
		    { 255,0,0 }, { 0,255,255 }, { 255,0,255 }, { 255,255,0},

		    { 0,0,128 }, { 0,128,0 }, { 128,0,0 }, { 0,128,128 },
		    { 128,0,128 }, { 128,128,0 }, { 192,192,192}, {128,128,128},

		    { 255,153,153}, {102,51,153}, {204,255,255}, {255,255,204 },
		    { 102,0,102 }, { 128,128,255 }, {204,102,0}, {255,204,204 },

		    { 128,0,0 }, { 255,0,255 }, { 0,255,255 }, { 255,255,0 },
		    { 128,0,128 }, { 0,0,128 }, { 128,128,0 }, { 255,0,0 },

		    {255,204,0}, {255,255,204}, {204,255,204}, { 153,255,255 },
		    {255,204,153}, {204,153,255}, {255,153,204}, {153,204,255},

		    {255,102,51}, {204,204,51}, {0,204,153}, {0,204,255},
		    {0,153,255}, {0,102,255}, {153,102,102}, {150,150,150},

		    {102,51,0}, {102,153,51}, {0,51,0}, {0,51,51},
		    {0,51,153}, {102,51,153}, {153,51,51}, {51,51,51}
		};

		int entries = sizeof(default_colors)/
		    sizeof(struct default_color);
#if EXCEL_DEBUG_COLOR > 1
		printf ("Creating default pallete\n");
#endif
		pal = (MS_EXCEL_PALETTE *) g_malloc (sizeof (MS_EXCEL_PALETTE));
		pal->length = entries;
		pal->red = g_new (int, entries) ;
		pal->green = g_new (int, entries) ;
		pal->blue = g_new (int, entries) ;
		pal->gnum_cols = g_new (StyleColor *, entries) ;

		while (--entries >= 0) {
			pal->red[entries]	= default_colors[entries].r;
			pal->green[entries]	= default_colors[entries].g;
			pal->blue[entries]	= default_colors[entries].b;
			pal->gnum_cols[entries] = NULL ;
		}
	}

	return pal;
}

/* See: S59DC9.HTM */
static MS_EXCEL_PALETTE *
ms_excel_palette_new (BIFF_QUERY * q)
{
	int lp, len;
	MS_EXCEL_PALETTE *pal;

	pal = (MS_EXCEL_PALETTE *) g_malloc (sizeof (MS_EXCEL_PALETTE));
	len = BIFF_GETWORD (q->data);
	pal->length = len;
	pal->red = g_new (int, len) ;
	pal->green = g_new (int, len) ;
	pal->blue = g_new (int, len) ;
	pal->gnum_cols = g_new (StyleColor *, len) ;

#if EXCEL_DEBUG_COLOR > 3
	printf ("New palette with %d entries\n", len);
#endif
	for (lp = 0; lp < len; lp++){
		LONG num = BIFF_GETLONG (q->data + 2 + lp * 4);

		/* NOTE the order of bytes is different from what one would
		 * expect */
		pal->blue[lp] = (num & 0x00ff0000) >> 16;
		pal->green[lp] = (num & 0x0000ff00) >> 8;
		pal->red[lp] = (num & 0x000000ff) >> 0;
#if EXCEL_DEBUG_COLOR > 2
		printf ("Colour %d : 0x%8x (%d,%d,%d)\n", lp,
			num, pal->red[lp], pal->green[lp], pal->blue[lp]);
#endif
		pal->gnum_cols[lp] = NULL ;
	}
	return pal;
}

static StyleColor *
ms_excel_palette_get (MS_EXCEL_PALETTE *pal, guint idx, StyleColor * contrast)
{
#if EXCEL_DEBUG_COLOR > 4
	printf ("Color Index %d\n", idx);
#endif

	/* NOTE : not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	0    = Black
	 *	8-63 = Palette index 0-55
	 *
	 *	127, 64 = contrast ??
	 *	65 = White ??
	 *
	 *	Standard combos are
	 *	    - fore=64,    back=65
	 *	    - fore=0-63,  back=64
	 *
	 *	 Rethink this when we understand to relationships between
	 *	 automatic colors.
	 */

	/* FIXME FIXME FIXME : this should now be an assert */
	if (!pal)
		return NULL ;

	if (idx == 0)
	{
		/* Just a guess but maybe this is constant black */
		static StyleColor * black = NULL;
		if (!black)
			black = style_color_new (0, 0, 0);
		return black;
	}
	else if (idx == 64 || idx ==127)
	{
		/* These seem to be some sort of automatic contract colors */
		if (contrast)
		{
			/* FIXME FIXME FIXME : This is a BIG guess */
			/* If the contrast colour closer to black or white based
			 * on this VERY loose metric.  There is more to do.
			 */
			int const guess =
			    contrast->color.red +
			    contrast->color.green +
			    contrast->color.blue;

#if EXCEL_DEBUG_COLOR > 1
			printf ("Contrast : %d", guess);
#endif
			if (guess <= (0x7fff  + 0x8000 + 0x7fff))
			{
#if EXCEL_DEBUG_COLOR > 1
				puts("White");
#endif
				return style_color_new (0xffff, 0xffff, 0xffff);
			}
#if EXCEL_DEBUG_COLOR > 1
			puts("Black");
#endif
		}
		return style_color_new (0, 0, 0);
	} else if (idx == 65)
	{
		/* FIXME FIXME FIXME */
		/* These seem to be some sort of automatic contract colors */
		return style_color_new (0xffff, 0xffff, 0xffff);
	}
	
	idx -= 8;
	if (idx < pal->length && idx >= 0)
	{
		if (pal->gnum_cols[idx] == NULL) {
			gushort r, g, b;
			/* scale 8 bit/color ->  16 bit/color by cloning */
			r = (pal->red[idx] << 8) | pal->red[idx];
			g = (pal->green[idx] << 8) | pal->green[idx];
			b = (pal->blue[idx] << 8) | pal->blue[idx];
#if EXCEL_DEBUG_COLOR > 1
			printf ("New color in slot %d : RGB= %d,%d,%d\n",
					r, g, b);
#endif
			pal->gnum_cols[idx] = style_color_new (r, g, b);
			g_return_val_if_fail (pal->gnum_cols[idx], NULL);
		}
		return pal->gnum_cols[idx];
	}
	{
		return NULL;
}
}

static void
ms_excel_palette_destroy (MS_EXCEL_PALETTE * pal)
{
	guint16 lp ;

	g_free (pal->red);
	g_free (pal->green);
	g_free (pal->blue);
	for (lp=0;lp<pal->length;lp++)
		if (pal->gnum_cols[lp])
			style_color_unref (pal->gnum_cols[lp]) ;
	g_free (pal);
}

typedef struct _BIFF_XF_DATA {
	guint16 font_idx;
	guint16 format_idx;
	StyleFormat *style_format ;
	eBiff_hidden hidden;
	eBiff_locked locked;
	eBiff_xftype xftype;	/*
				 * -- Very important field... 
				 */
	eBiff_format format;
	WORD parentstyle;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	eBiff_wrap wrap;
	BYTE rotation;
	eBiff_eastern eastern;
	guint8 border_color[4];	/*
				 * Array [StyleSide]
				 */
	StyleBorderType border_type[4];	/*
					 * Array [StyleSide]
					 */
	eBiff_border_orientation border_orientation;
	StyleBorderType border_linestyle;
	BYTE fill_pattern_idx;
	BYTE pat_foregnd_col;
	BYTE pat_backgnd_col;
} BIFF_XF_DATA;

static void
ms_excel_set_cell_colors (MS_EXCEL_SHEET * sheet, Cell * cell,
			  BIFF_XF_DATA * xf, StyleColor *basefore)
{
	MS_EXCEL_PALETTE *p = sheet->wb->palette;
	StyleColor *fore, *back;
	int col;

	if (!p || !xf) {
			printf ("Internal Error: No palette !\n");
		return;
	}

	/* Whack into cell.c(cell_draw):
	   printf ("Background %p (%d %d %d)\n", style->back_color, style->back_color->color.red,
	   style->back_color->color.green, style->back_color->color.blue) ; */

	if (!basefore) {
#if EXCEL_DEBUG_COLOR > 2
		printf ("Cell Color : '%s' : (%d, %d)\n",
			cell_name (cell->col->pos, cell->row->pos),
			xf->pat_foregnd_col, xf->pat_backgnd_col);
#endif
		fore = ms_excel_palette_get (sheet->wb->palette,
					     xf->pat_foregnd_col, NULL);
		back = ms_excel_palette_get (sheet->wb->palette,
					     xf->pat_backgnd_col, fore);
	} else {
		fore = basefore;
		back = ms_excel_palette_get (sheet->wb->palette,
					     xf->pat_foregnd_col, NULL);
#if EXCEL_DEBUG_COLOR > 2
		printf ("Cell Color : '%s' : (Fontcol, %d)\n",
				cell_name (cell->col->pos, cell->row->pos),
				xf->pat_foregnd_col);
#endif
	}
	if (fore && back) {
		if (fore == back)
#if EXCEL_DEBUG > 0
			printf ("FIXME: patterns need work\n")
#endif
				;
		else
			cell_set_color_from_style (cell, fore, back);
	}
	else
		printf ("Missing color\n");
}

/**
 * Search for a font record from its index in the workbooks font table
 * NB. index 4 is omitted supposedly for backwards compatiblity
 * Returns the font color if there is one.
 **/
static StyleColor *
ms_excel_set_cell_font (MS_EXCEL_SHEET * sheet, Cell * cell, BIFF_XF_DATA * xf)
{
	BIFF_FONT_DATA *fd = g_hash_table_lookup (sheet->wb->font_data, &xf->font_idx);
	StyleColor *col;

	if (!fd) {
		printf ("Unknown font idx %d\n", xf->font_idx);
		return NULL ;
	}
	g_assert (fd->index != 4);

	if (!fd->style_font)
		fd->style_font = biff_font_data_get_style_font (fd);

	if (fd->style_font)
		cell_set_font_from_style (cell, fd->style_font);
	else
		printf ("Duff StyleFont\n") ;
	return ms_excel_palette_get (sheet->wb->palette, fd->color_idx, NULL);
}

static void
ms_excel_set_cell_xf (MS_EXCEL_SHEET * sheet, Cell * cell, guint16 xfidx)
{
	BIFF_XF_DATA *xf;
	StyleColor *fore;
	GPtrArray *p;
	guint16 idx = xfidx - XF_MAGIC_OFFSET;

	if (!cell->value) {
#if EXCEL_DEBUG > 0
			printf ("FIXME: Error setting xf on cell\n");
#endif
		return;
	}

	if (xfidx == 0){
/*		printf ("Normal cell formatting\n"); */
		return;
	}
	if (xfidx == 15){
/*		printf ("Default cell formatting\n"); */
    		return;
	}

	p = sheet->wb->XF_cell_records;
	if (p && p->len > idx)
		xf = g_ptr_array_index (p, idx);
        else {
#if EXCEL_DEBUG > 0
	        printf ("FIXME: No XF record for %d out of %d found :-(\n",
			xfidx, p?p->len:-666);
#endif
	        return;
	}
	if (xf->xftype != eBiffXCell)
	       printf ("FIXME: Error looking up XF\n") ;

	/*
	 * Well set it up then ! FIXME: hack ! 
	 */
	cell_set_alignment (cell, xf->halign, xf->valign, ORIENT_HORIZ, 0);
	fore = ms_excel_set_cell_font (sheet, cell, xf);
	if (sheet->wb->palette)
	{
		int lp;
 		StyleColor *tmp[4];
 		for (lp=0;lp<4;lp++)
			tmp[lp] = ms_excel_palette_get (sheet->wb->palette,
							xf->border_color[lp],
							NULL);
		cell_set_border (cell, xf->border_type, tmp);
	}

	if (xf->format_idx>0)
	{
		if (xf->style_format)
			cell_set_format_from_style (cell, xf->style_format) ;
		else {
			xf->style_format = biff_format_data_lookup (sheet->wb, xf->format_idx) ;
			if (xf->style_format)
				cell_set_format_from_style (cell, xf->style_format) ;
		}
	}
	ms_excel_set_cell_colors (sheet, cell, xf, fore);
}

static StyleBorderType
biff_xf_map_border (int b)
{
	switch (b)
 	{
 	case 0: /* None */
 		return BORDER_NONE;
 	case 1: /* Thin */
 		return BORDER_THIN;
 	case 2: /* Medium */
 		return BORDER_MEDIUM;
 	case 3: /* Dashed */
 		return BORDER_DASHED;
 	case 4: /* Dotted */
 		return BORDER_DOTTED;
 	case 5: /* Thick */
 		return BORDER_THICK;
 	case 6: /* Double */
 		return BORDER_DOUBLE;
 	case 7: /* Hair */
 		return BORDER_HAIR;
 	case 8: /* Medium Dashed */
 		return BORDER_MEDIUM;
 	case 9: /* Dash Dot */
 		return BORDER_THIN;
 	case 10: /* Medium Dash Dot */
 		return BORDER_THIN;
 	case 11: /* Dash Dot Dot */
 		return BORDER_HAIR;
 	case 12: /* Medium Dash Dot Dot */
 		return BORDER_THIN;
 	case 13: /* Slanted Dash Dot*/
 		return BORDER_HAIR;
 	}
  	printf ("Unknown border style %d\n", b);
 	return BORDER_NONE;
}

/**
 * Parse the BIFF XF Data structure into a nice form, see S59E1E.HTM
 **/
static void
biff_xf_data_new (MS_EXCEL_WORKBOOK *wb, BIFF_QUERY * q, eBiff_version ver)
{
	BIFF_XF_DATA *xf = (BIFF_XF_DATA *) g_malloc (sizeof (BIFF_XF_DATA));
	LONG data, subdata;

	xf->font_idx = BIFF_GETWORD (q->data);
	xf->format_idx = BIFF_GETWORD (q->data + 2);
	xf->style_format = 0 ;

	data = BIFF_GETWORD (q->data + 4);
	xf->locked = (data & 0x0001) ? eBiffLLocked : eBiffLUnlocked;
	xf->hidden = (data & 0x0002) ? eBiffHHidden : eBiffHVisible;
	xf->xftype = (data & 0x0004) ? eBiffXStyle : eBiffXCell;
	xf->format = (data & 0x0008) ? eBiffFLotus : eBiffFMS;
	xf->parentstyle = (data >> 4);

	data = BIFF_GETWORD (q->data + 6);
	subdata = data & 0x0007;
	switch (subdata){
	case 0:
		xf->halign = HALIGN_GENERAL;
		break;
	case 1:
		xf->halign = HALIGN_LEFT;
		break;
	case 2:
		xf->halign = HALIGN_CENTER;
		break;
	case 3:
		xf->halign = HALIGN_RIGHT;
		break;
	case 4:
		xf->halign = HALIGN_FILL;
		break;
	case 5:
		xf->halign = HALIGN_JUSTIFY;
		break;
	case 6:
		xf->halign = HALIGN_JUSTIFY;
		/*
		 * xf->halign = HALIGN_CENTRE_ACROSS_SELECTION;
		 */
		break;
	default:
		xf->halign = HALIGN_JUSTIFY;
		printf ("Unknown halign %d\n", subdata);
		break;
	}
	xf->wrap = (data & 0x0008) ? eBiffWWrap : eBiffWNoWrap;
	subdata = (data & 0x0070) >> 4;
	switch (subdata){
	case 0:
		xf->valign = VALIGN_TOP;
		break;
	case 1:
		xf->valign = VALIGN_CENTER;
		break;
	case 2:
		xf->valign = VALIGN_BOTTOM;
		break;
	case 3:
		xf->valign = VALIGN_JUSTIFY;
		break;
	default:
		printf ("Unknown valign %d\n", subdata);
		break;
	}
	/*
	 * FIXME: ignored bit 0x0080 
	 */
	if (ver == eBiffV8)
		xf->rotation = (data >> 8);
	else {
		subdata = (data & 0x0300) >> 8;
		switch (subdata){
		case 0:
			xf->rotation = 0;
			break;
		case 1:
			xf->rotation = 255;	/*
						 * vertical letters no rotation   
						 */
			break;
		case 2:
			xf->rotation = 90;	/*
						 * 90deg anti-clock               
						 */
			break;
		case 3:
			xf->rotation = 180;	/*
						 * 90deg clock                    
						 */
			break;
		}
	}

	if (ver == eBiffV8){
		/*
		 * FIXME: Got bored and stop implementing everything, there is just too much ! 
		 */
		data = BIFF_GETWORD (q->data + 8);
		subdata = (data & 0x00C0) >> 10;
		switch (subdata){
		case 0:
			xf->eastern = eBiffEContext;
			break;
		case 1:
			xf->eastern = eBiffEleftToRight;
			break;
		case 2:
			xf->eastern = eBiffErightToLeft;
			break;
		default:
			printf ("Unknown location %d\n", subdata);
			break;
		}
	}
	if (ver == eBiffV8){	/*
				 * Very different 
				 */
		data = BIFF_GETWORD (q->data + 10);
		subdata = data;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;

		data = BIFF_GETWORD (q->data + 12);
		subdata = data;
		xf->border_color[STYLE_LEFT] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_RIGHT] = (subdata & 0x7f);
		subdata = (data & 0xc000) >> 14;
		switch (subdata){
		case 0:
			xf->border_orientation = eBiffBONone;
			break;
		case 1:
			xf->border_orientation = eBiffBODiagDown;
			break;
		case 2:
			xf->border_orientation = eBiffBODiagUp;
			break;
		case 3:
			xf->border_orientation = eBiffBODiagBoth;
			break;
		}

		data = BIFF_GETLONG (q->data + 14);
		subdata = data;
		xf->border_color[STYLE_TOP] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_BOTTOM] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_linestyle = biff_xf_map_border ((data & 0x01e00000) >> 21);
		xf->fill_pattern_idx = biff_xf_map_border ((data & 0xfc000000) >> 26);

		data = BIFF_GETWORD (q->data + 18);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x3f80) >> 7;
#if EXCEL_DEBUG_COLOR > 2
		printf("Color f=%x b=%x\n",
		       xf->pat_foregnd_col,
		       xf->pat_backgnd_col);
#endif
	} else { /* Biff 7 */
		data = BIFF_GETWORD (q->data + 8);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x1f80) >> 7;

		data = BIFF_GETWORD (q->data + 10);
		xf->fill_pattern_idx = data & 0x03f;
		/*
		 * Luckily this maps nicely onto the new set. 
		 */
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border ((data & 0x1c0) >> 6);
		xf->border_color[STYLE_BOTTOM] = (data & 0xfe00) >> 9;

		data = BIFF_GETWORD (q->data + 12);
		subdata = data;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_color[STYLE_TOP] = subdata;

		data = BIFF_GETWORD (q->data + 14);
		subdata = data;
		xf->border_color[STYLE_LEFT] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_RIGHT] = (subdata & 0x7f);
	}

	if (xf->xftype == eBiffXCell) {
		/*printf ("Inserting into Cell XF hash with : %d\n", wb->XF_cell_records->len) ; */
		g_ptr_array_add (wb->XF_cell_records, xf); 
	} else {
		/*printf ("Inserting into style XF hash with : %d\n", wb->XF_style_records->len) ; */
		g_ptr_array_add (wb->XF_style_records, xf); 
	}
#if EXCEL_DEBUG > 0
		printf ("XF : Fore %d, Back %d\n", xf->pat_foregnd_col, xf->pat_backgnd_col) ;
#endif
}

static gboolean 
biff_xf_data_destroy (BIFF_XF_DATA *xf)
{
	if (xf->style_format)
		style_format_unref (xf->style_format) ;
	g_free (xf);
	return 1 ;
}

static MS_EXCEL_SHEET *
ms_excel_sheet_new (MS_EXCEL_WORKBOOK * wb, char *name)
{
	MS_EXCEL_SHEET *ans = (MS_EXCEL_SHEET *) g_malloc (sizeof (MS_EXCEL_SHEET));

	ans->gnum_sheet = sheet_new (wb->gnum_wb, name);
	ans->blank = 1 ;
	ans->wb = wb;

	ans->shared_formulae = g_hash_table_new ((GHashFunc)biff_shared_formula_hash,
						 (GCompareFunc)biff_shared_formula_equal) ;

	return ans;
}

ExprTree *
ms_excel_sheet_shared_formula (MS_EXCEL_SHEET *sheet,
			       int shr_col, int shr_row,
			       int col, int row)
{
	BIFF_SHARED_FORMULA_KEY k ;
	BIFF_SHARED_FORMULA *sf ;
	k.col = shr_col ;
	k.row = shr_row ;
	sf = g_hash_table_lookup (sheet->shared_formulae, &k) ;
	if (sf)
		return ms_excel_parse_formula (sheet, sf->data,
					       col, row, 1,
					       sf->data_len) ;
#if EXCEL_DEBUG > 0
		printf ("Duff shared formula index %d %d\n", col, row) ;
#endif
	return NULL;
}

static void
ms_excel_sheet_set_version (MS_EXCEL_SHEET *sheet, eBiff_version ver)
{
	sheet->ver = ver ;
}

static void
ms_excel_sheet_insert (MS_EXCEL_SHEET * sheet, int xfidx,
		       int col, int row, const char *text)
{
	Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
	/* NB. cell_set_text _certainly_ strdups *text */
	if (text)
	{
		sheet->blank = 0 ;
		cell_set_text_simple (cell, text);
	}
	else
		cell_set_text_simple (cell, "") ;
	ms_excel_set_cell_xf (sheet, cell, xfidx);
}

static void
ms_excel_sheet_insert_form (MS_EXCEL_SHEET * sheet, int xfidx,
			    int col, int row, ExprTree *tr)
{
	Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
	/* NB. cell_set_text _certainly_ strdups *text */
	if (tr)
	{
		sheet->blank = 0 ;
		cell_set_formula_tree_simple (cell, tr);
	}
	else
		cell_set_text_simple (cell, "") ;
	ms_excel_set_cell_xf (sheet, cell, xfidx);
}

static void
ms_excel_sheet_insert_val (MS_EXCEL_SHEET * sheet, int xfidx,
			   int col, int row, Value *v)
{
	Cell *cell;
	g_return_if_fail (v);
	g_return_if_fail (sheet);
	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
	sheet->blank = 0 ;
	cell_set_value_simple (cell, v);
	ms_excel_set_cell_xf (sheet, cell, xfidx);
}

static void
ms_excel_sheet_set_comment (MS_EXCEL_SHEET * sheet, int col, int row, char *text)
{
	if (text)
	{
		Cell *cell = sheet_cell_get (sheet->gnum_sheet, col, row);
		if (!cell) {
			cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
			cell_set_text_simple (cell, "");
		}
		sheet->blank = 0 ;
		cell_set_comment (cell, text);
	}
}

static void
ms_excel_sheet_append_comment (MS_EXCEL_SHEET * sheet, int col, int row, char *text)
{
	if (text)
	{
		Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
		if (cell->comment && cell->comment->comment &&
		    cell->comment->comment->str) {
			char *txt = g_strconcat (cell->comment->comment->str, text, NULL);
			sheet->blank = 0 ;
			cell_set_comment (cell, txt);
			g_free (txt);
		}
	}
}

static void
ms_excel_sheet_destroy (MS_EXCEL_SHEET * sheet)
{
	g_hash_table_foreach_remove (sheet->shared_formulae,
				     (GHRFunc)biff_shared_formula_destroy,
				     sheet) ;
	g_hash_table_destroy (sheet->shared_formulae) ;
	sheet->shared_formulae = NULL ;

	if (sheet->gnum_sheet)
		sheet_destroy (sheet->gnum_sheet);
	sheet->gnum_sheet = NULL ;
	
	g_free (sheet);
}

static MS_EXCEL_WORKBOOK *
ms_excel_workbook_new (void)
{
	MS_EXCEL_WORKBOOK *ans = (MS_EXCEL_WORKBOOK *) g_malloc (sizeof (MS_EXCEL_WORKBOOK));

	ans->extern_sheets = NULL ;
	ans->gnum_wb = NULL;
	/* Boundsheet data hashed twice */
	ans->boundsheet_data_by_stream = g_hash_table_new ((GHashFunc)biff_guint32_hash,
							   (GCompareFunc)biff_guint32_equal) ;
	ans->boundsheet_data_by_index = g_hash_table_new ((GHashFunc)biff_guint16_hash,
							  (GCompareFunc)biff_guint16_equal) ;
	ans->font_data = g_hash_table_new ((GHashFunc)biff_guint16_hash,
					   (GCompareFunc)biff_guint16_equal) ;
	ans->excel_sheets     = g_ptr_array_new ();
	ans->XF_style_records = g_ptr_array_new ();
	ans->XF_cell_records  = g_ptr_array_new ();
	ans->format_data = g_hash_table_new ((GHashFunc)biff_guint16_hash,
					     (GCompareFunc)biff_guint16_equal) ;
	ans->name_data = g_hash_table_new ((GHashFunc)biff_guint16_hash,
					   (GCompareFunc)biff_guint16_equal) ;
	ans->palette = ms_excel_default_palette ();
	ans->global_strings = NULL;
	ans->global_string_max = 0;
	ans->read_drawing_group = 0;
	return ans;
}

static void
ms_excel_workbook_attach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans)
{
	g_return_if_fail (wb);
	g_return_if_fail (ans);

	workbook_attach_sheet (wb->gnum_wb, ans->gnum_sheet);
	g_ptr_array_add (wb->excel_sheets, ans);
}

static gboolean
ms_excel_workbook_detach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans)
{
	int    idx = 0 ;

	if (ans->gnum_sheet) {
		if (!workbook_detach_sheet (wb->gnum_wb, ans->gnum_sheet, FALSE))
			return FALSE;
	}
	for (idx=0;idx<wb->excel_sheets->len;idx++)
		if (g_ptr_array_index (wb->excel_sheets, idx) == ans) {
			g_ptr_array_index (wb->excel_sheets, idx) = NULL;
			return TRUE;
		}

	printf ("Sheet not in list of sheets !\n");
	return FALSE;
}

static MS_EXCEL_SHEET *
ms_excel_workbook_get_sheet (MS_EXCEL_WORKBOOK *wb, guint idx)
{
	if (idx < wb->excel_sheets->len)
		return g_ptr_array_index (wb->excel_sheets, idx);
	return NULL ;
}

static void
ms_excel_workbook_destroy (MS_EXCEL_WORKBOOK * wb)
{
	gint lp;
	
	g_hash_table_foreach_remove (wb->boundsheet_data_by_stream,
				     (GHRFunc)biff_boundsheet_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->boundsheet_data_by_index) ;
	g_hash_table_destroy (wb->boundsheet_data_by_stream) ;
	if (wb->XF_style_records)
		for (lp=0;lp<wb->XF_style_records->len;lp++)
			biff_xf_data_destroy (g_ptr_array_index (wb->XF_style_records, lp));
	g_ptr_array_free (wb->XF_style_records, TRUE);
	if (wb->XF_cell_records)
		for (lp=0;lp<wb->XF_cell_records->len;lp++)
			biff_xf_data_destroy (g_ptr_array_index (wb->XF_cell_records, lp));
	g_ptr_array_free (wb->XF_cell_records, TRUE);

	g_hash_table_foreach_remove (wb->font_data,
				     (GHRFunc)biff_font_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->font_data) ;

	g_hash_table_foreach_remove (wb->format_data,
				     (GHRFunc)biff_format_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->format_data) ;

	g_hash_table_foreach_remove (wb->name_data,
				     (GHRFunc)biff_name_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->name_data) ;

	if (wb->palette && wb->palette != ms_excel_default_palette ())
		ms_excel_palette_destroy (wb->palette);

	if (wb->extern_sheets)
		g_free (wb->extern_sheets) ;

	g_free (wb);
}

/**
 * Unpacks a MS Excel RK structure,
 **/
static Value *
biff_get_rk (guint8 *ptr)
{
	LONG number;
	Value *ans=NULL;
	guint8 tmp[8];
	int lp;
	double answer;
	enum eType {
		eIEEE = 0, eIEEEx100 = 1, eInt = 2, eIntx100 = 3
	} type;
	
	number = BIFF_GETLONG (ptr);
	type = (number & 0x3);
	switch (type){
	case eIEEE:
	case eIEEEx100:
		for (lp=0;lp<4;lp++) {
			tmp[lp+4]=(lp>0)?ptr[lp]:(ptr[lp]&0xfc);
			tmp[lp]=0;
		}

		answer = BIFF_GETDOUBLE(tmp);
		answer /= (type == eIEEEx100)?100.0:1.0;
		ans = value_new_float (answer);
		break;
	case eInt:
		ans = value_new_int ((number>>2));
		break;
	case eIntx100:
		if (number%100==0)
			ans = value_new_int ((number>>2)/100);
		else
			ans = value_new_float ((number>>2)/100.0);
		break;
	}
	return ans;
}

/* FIXME: S59DA9.HTM */
static void
ms_excel_read_name (BIFF_QUERY * q)
{
	guint16 flags = BIFF_GETWORD(q->data) ;
	guint16 fn_grp_idx ;
	guint8  kb_shortcut = BIFF_GETBYTE(q->data+2);
	guint8  name_len = BIFF_GETBYTE(q->data+3) ;
	guint16 name_def_len  = BIFF_GETWORD(q->data+4) ;
	guint8* name_def_data = q->data+14+name_def_len ;
	guint16 sheet_idx = BIFF_GETWORD(q->data+6) ;
	guint16 ixals = BIFF_GETWORD(q->data+8) ; /* dup */
	guint8  menu_txt_len = BIFF_GETBYTE(q->data+10) ;
	guint8  descr_txt_len = BIFF_GETBYTE(q->data+11) ;
	guint8  help_txt_len = BIFF_GETBYTE(q->data+12) ;
	guint8  status_txt_len = BIFF_GETBYTE(q->data+13) ;
	char *name, *menu_txt, *descr_txt, *help_txt, *status_txt ;
	guint8 *ptr ;

/*	g_assert (ixals==sheet_idx) ; */
	ptr = q->data + 14 ;
	if (name_len == 1 && *ptr <= 0x0c)
		/* FIXME FIXME FIXME */
		/* Be sure to new these when we actually use the result */
		switch(*ptr)
		{
		case 0x00 :	name = "Consolidate_Area"; break;
		case 0x01 :	name = "Auto_Open"; break;
		case 0x02 :	name = "Auto_Close"; break;
		case 0x03 :	name = "Extract"; break;
		case 0x04 :	name = "Database"; break;
		case 0x05 :	name = "Criteria"; break;
		case 0x06 :	name = "Print_Area"; break;
		case 0x07 :	name = "Print_Titles"; break;
		case 0x08 :	name = "Recorder"; break;
		case 0x09 :	name = "Data_Form"; break;
		case 0x0a :	name = "Auto_Activate"; break;
		case 0x0b :	name = "Auto_Deactivate"; break;
		case 0x0c :	name = "Sheet_Title"; break;
		default :
				name = "ERROR ERROR ERROR.  This is impossible";
		}
	else
		name = biff_get_text (ptr, name_len, NULL) ;
	ptr+= name_len + name_def_len ;
	menu_txt = biff_get_text (ptr, menu_txt_len, NULL) ;
	ptr+= menu_txt_len ;
	descr_txt = biff_get_text (ptr, descr_txt_len, NULL) ;
	ptr+= descr_txt_len ;
	help_txt = biff_get_text (ptr, help_txt_len, NULL) ;
	ptr+= help_txt_len ;
	status_txt = biff_get_text (ptr, status_txt_len, NULL) ;

	printf ("Name record : '%s', '%s', '%s', '%s', '%s'\n", name, menu_txt, descr_txt,
		help_txt, status_txt) ;
	dump (name_def_data, name_def_len) ;

	/* Unpack flags */
	fn_grp_idx = (flags&0xfc0)>>6 ;
	if ((flags&0x0001) != 0)
		printf (" Hidden") ;
	if ((flags&0x0002) != 0)
		printf (" Function") ;
	if ((flags&0x0004) != 0)
		printf (" VB-Proc") ;
	if ((flags&0x0008) != 0)
		printf (" Proc") ;
	if ((flags&0x0010) != 0)
		printf (" CalcExp") ;
	if ((flags&0x0020) != 0)
		printf (" BuiltIn") ;
	if ((flags&0x1000) != 0)
		printf (" BinData") ;
	printf ("\n") ;
}

/* FIXME: S59D7E.HTM */
static void
ms_excel_externname(BIFF_QUERY * q,
		    MS_EXCEL_WORKBOOK * wb,
		    eBiff_version version)
{
	char *externname ;
	if ( version >= eBiffV7) {
		guint16 options  = BIFF_GETWORD(q->data) ;
		guint8  namelen  = BIFF_GETBYTE(q->data+6) ;
		guint16 defnlen  = BIFF_GETWORD(q->data + 7 + namelen) ;
		char *definition = 0 ;
		
		externname = biff_get_text (q->data+7, namelen, NULL) ;
		if ((options & 0xffe0) != 0) {
			printf ("Duff externname\n") ; return ;
		}
		if ((options & 0x0001) != 0)
			printf ("fBuiltin\n") ;
		/* Copy the definition to storage to parse at run-time in the formula */
		biff_name_data_new (wb, externname, definition, defnlen) ;
	} else { /* Ancient Papyrus spec. */
		guint8 data[] = { 0x1c, 0x17 } ;
		printf ("Externname Data:\n") ;
		dump (q->data, q->length) ;
		externname = biff_get_text (q->data+1, BIFF_GETBYTE(q->data), NULL) ;
		biff_name_data_new (wb, externname, data, 2) ;
	}
	if (EXCEL_DEBUG>1)
	{
		printf ("Externname '%s'\n", externname) ;
		dump (q->data, q->length) ;
	}
}

/**
 * Parse the cell BIFF tag, and act on it as neccessary
 * NB. Microsoft Docs give offsets from start of biff record, subtract 4 their docs.
 **/
static void
ms_excel_read_cell (BIFF_QUERY * q, MS_EXCEL_SHEET * sheet)
{
	Cell *cell;

	switch (q->ls_op){
	case BIFF_BLANK:	/*
				 * FIXME: Not a good way of doing blanks ? 
				 */
		/*
		 * printf ("Cell [%d, %d] XF = %x\n", EX_GETCOL(q), EX_GETROW(q),
		 * EX_GETXF(q)); 
		 */
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), 0);
		break;
	case BIFF_MULBLANK:	/*
				 * S59DA7.HTM is extremely unclear, this is an educated guess 
				 */
		{
			if (q->opcode == BIFF_DV){
				printf ("Unimplemented DV: data validation criteria, FIXME\n");
				break;
			} else {
				int row, col, lastcol;
				int incr;
				BYTE *ptr;

				/*
				 * dump (ptr, q->length); 
				 */
				row = EX_GETROW (q);
				col = EX_GETCOL (q);
				ptr = (q->data + 4);
				lastcol = BIFF_GETWORD (q->data + q->length - 2);
/*				printf ("Cells in row %d are blank starting at col %d until col %d (0x%x)\n",
				row, col, lastcol, lastcol); */
				incr = (lastcol > col) ? 1 : -1;
				while (col != lastcol){
					ms_excel_sheet_insert (sheet, BIFF_GETWORD (ptr), EX_GETCOL (q), EX_GETROW (q), 0);
					col += incr;
					ptr += 2;
				}
			}
		}
		break;
 	case BIFF_HEADER: /* FIXME : S59D94 */
	{
		char *str ;
		if (q->length)
		{
#if EXCEL_DEBUG > 0
				printf ("Header '%s'\n", (str=biff_get_text (q->data+1,
									     BIFF_GETBYTE(q->data), NULL))) ;
				g_free(str) ;
#endif
		}
 		break;
	}
 	case BIFF_FOOTER: /* FIXME : S59D8D */
	{
		char *str ;
		if (q->length)
		{
#if EXCEL_DEBUG > 0
				printf ("Footer '%s'\n", (str=biff_get_text (q->data+1,
								     BIFF_GETBYTE(q->data), NULL))) ;
				g_free(str) ;
#endif
		}
 		break;
	}
	case BIFF_RSTRING: /* See: S59DDC.HTM */
	{
		char *txt ;
		/*
		  printf ("Cell [%d, %d] = ", EX_GETCOL(q), EX_GETROW(q));
		  dump (q->data, q->length);
		  printf ("Rstring\n") ;
		*/
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       (txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL)));
		g_free (txt) ;
		break;
	}
	case BIFF_DBCELL: /* S59D6D.HTM */
		/* Can be ignored on read side */
		break ;
	case BIFF_NUMBER: /* S59DAC.HTM */
	{
		Value *v = value_new_float (BIFF_GETDOUBLE (q->data + 6));
#if EXCEL_DEBUG > 0
		printf ("Read number %g\n", BIFF_GETDOUBLE (q->data + 6));
#endif
		ms_excel_sheet_insert_val (sheet, EX_GETXF (q), EX_GETCOL (q),
					   EX_GETROW (q), v);
		break;
	}
	case BIFF_COLINFO: /* FIXME: See: S59D67.HTM */
	{
		int firstcol, lastcol, lp ;
		guint16 cols_xf, options, width ;
		int hidden, collapsed, outlining ;
		firstcol = BIFF_GETWORD(q->data) ;
		lastcol  = BIFF_GETWORD(q->data+2) ;
		width    = BIFF_GETWORD(q->data+4) ;
		cols_xf  = BIFF_GETWORD(q->data+6) ;
		options  = BIFF_GETWORD(q->data+8) ;
		
		hidden    = (options & 0x0001) != 0 ;
		collapsed = (options & 0x1000) != 0 ;
		outlining = (options & 0x0700) >> 8 ;

		if (EXCEL_DEBUG>0 && BIFF_GETBYTE(q->data+10) != 0)
			printf ("Odd Colinfo\n") ;
		if (EXCEL_DEBUG>0)
			printf ("Column Formatting from col %d to %d of width %f characters\n",
				firstcol, lastcol, width/256.0) ;
		if (width>>8 == 0) {
			printf ("FIXME: Hidden columns need implementing\n") ;
			width=40.0 ;
		}
		for (lp=firstcol;lp<=lastcol;lp++)
			sheet_col_set_width (sheet->gnum_sheet, lp, width/25) ;
		break ;
	}
	case BIFF_RK: /* See: S59DDA.HTM */
	{
		Value *v = biff_get_rk(q->data+6);
#if EXCEL_DEBUG > 2
		printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length);
		dump (q->data, q->length);
#endif
		ms_excel_sheet_insert_val (sheet, EX_GETXF (q), EX_GETCOL (q),
					   EX_GETROW (q), v);
		break;
	}
	case BIFF_MULRK: /* S59DA8.HTM */
	{
		guint32 col, row, lastcol;
		guint8 *ptr = q->data;
		Value *v;

/*		printf ("MULRK\n") ;
		dump (q->data, q->length) ; */

		row = BIFF_GETWORD(q->data) ;
		col = BIFF_GETWORD(q->data+2) ;
		ptr+= 4 ;
		lastcol = BIFF_GETWORD(q->data+q->length-2) ;
/*		g_assert ((lastcol-firstcol)*6 == q->length-6 */
		g_assert (lastcol>=col) ;
		while (col<=lastcol)
		{ /* 2byte XF, 4 byte RK */
			v = biff_get_rk(ptr+2);
			ms_excel_sheet_insert_val (sheet, BIFF_GETWORD(ptr),
						   col, row, v) ;
			col++ ;
			ptr+= 6 ;
		}
		break ;
	}
	case BIFF_LABEL:
	{
		char *label ;
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       (label = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL)));
		g_free (label) ;
		break;
	}
	case BIFF_ROW: /* FIXME: See: S59DDB.HTM */
	{
		guint16 height = BIFF_GETWORD(q->data+6) ;
/*		printf ("Row %d formatting 0x%x\n", BIFF_GETWORD(q->data), height);  */
		if ((height&0x8000) == 0) /* Fudge Factor */
		{
/*			printf ("Row height : %f points\n", BIFF_GETWORD(q->data+6)/20.0) ;*/
			sheet_row_set_height (sheet->gnum_sheet, BIFF_GETWORD(q->data), BIFF_GETWORD(q->data+6)/15, TRUE) ;
		}
		break;
	}
	case BIFF_SHRFMLA: /* See: S59DE4.HTM */
	{
		int array_col_first, array_col_last ;
		int array_row_first, array_row_last ;
		BYTE *data ;
		guint16 data_len ;
		ExprTree *tr ;
		Cell *cell ;
		BIFF_SHARED_FORMULA *sf ;

		array_row_first = BIFF_GETWORD(q->data + 0) ;
		array_row_last  = BIFF_GETWORD(q->data + 2) ;
		array_col_first = BIFF_GETBYTE(q->data + 4) ;
		array_col_last  = BIFF_GETBYTE(q->data + 5) ;

		data = q->data + 10 ;
		data_len = BIFF_GETWORD(q->data + 8) ;

		/* Whack in the hash for later */
		
		sf = biff_shared_formula_new (array_col_first, array_row_first,
					      data, data_len) ;
		g_hash_table_insert (sheet->shared_formulae, &sf->key, sf) ;

		if (EXCEL_DEBUG>0)
			printf ("Shared formula of extent %d %d %d %d\n",
				array_col_first, array_row_first, array_col_last, array_row_last) ;
		tr = ms_excel_parse_formula (sheet, data,
					      array_col_first, array_row_first,
					      1, data_len) ;
		/* NB. This keeps the pre-set XF record */
		if (tr) {
			cell = sheet_cell_fetch (sheet->gnum_sheet,
						 array_col_first, array_row_first);
			if (cell)
				cell_set_formula_tree_simple (cell, tr);
			expr_tree_unref (tr);
		}
		break ;
	}
	case BIFF_ARRAY: /* See: S59D57.HTM */
	{
		int array_col_first, array_col_last ;
		int array_row_first, array_row_last ;
		int xlp, ylp ;
		BYTE *data ;
		int data_len ;

		array_row_first = BIFF_GETWORD(q->data + 0) ;
		array_row_last  = BIFF_GETWORD(q->data + 2) ;
		array_col_first = BIFF_GETBYTE(q->data + 4) ;
		array_col_last  = BIFF_GETBYTE(q->data + 5) ;

/*				int options  = BIFF_GETWORD(q->data + 6) ; not so useful */
		data = q->data + 14 ;
		data_len = BIFF_GETWORD(q->data + 12) ;
		printf ("Array Formula of extent %d %d %d %d\n",
			array_col_first, array_row_first, array_col_last, array_row_last) ;
		for (xlp=array_col_first;xlp<=array_col_last;xlp++)
			for (ylp=array_row_first;ylp<=array_row_last;ylp++)
			{
				ExprTree *tr = ms_excel_parse_formula (sheet, data,
								       xlp, ylp, 
								       0, data_len) ;
				/* NB. This keeps the pre-set XF record */
				if (tr) {
					Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, xlp, ylp);
					if (cell)
						cell_set_formula_tree_simple (cell, tr);
					expr_tree_unref (tr);
				}
			}
		break ;
	}
	case BIFF_FORMULA: /* See: S59D8F.HTM */
	{
		ExprTree *tr;
		if (q->length < 22 ||
		    q->length < 22 + BIFF_GETWORD(q->data+20)) {
			printf ("FIXME: serious formula error: "
				"supposed length 0x%x, real len 0x%x\n",
				BIFF_GETWORD(q->data+20), q->length);
			break;
		}
		tr = ms_excel_parse_formula (sheet, (q->data + 22),
					     EX_GETCOL (q), EX_GETROW (q),
					     0, BIFF_GETWORD(q->data+20));
		ms_excel_sheet_insert_form (sheet, EX_GETXF(q), EX_GETCOL(q), EX_GETROW(q), tr) ;
		expr_tree_unref (tr);
		break;
	}
	case BIFF_LABELSST:
	{
		char *str;
		guint32 idx = BIFF_GETLONG (q->data + 6) ;
		
		if (!sheet->wb->global_strings || idx >= sheet->wb->global_string_max)
			printf ("string index 0x%x out of range\n", idx) ;
		else
		{
			str = sheet->wb->global_strings[idx] ;
			ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), str);
		}
                break;
	}

	case BIFF_EXTERNNAME:
		ms_excel_externname(q, sheet->wb, sheet->ver);
		break ;

	default:
		switch (q->opcode)
		{
		case BIFF_NAME:
			ms_excel_read_name (q);
			break ;

		case BIFF_STRING: /* FIXME: S59DE9.HTM */
		{
			char *txt ;
			if (EXCEL_DEBUG>0)
			{
				printf ("This cell evaluated to '%s': so what ? data:\n",
					(txt = biff_get_text (q->data + 2, BIFF_GETWORD(q->data), NULL))) ;
				if (txt) g_free (txt) ;
			}
			break ;
		}
		case BIFF_BOOLERR: /* S59D5F.HTM */
		{
			if (BIFF_GETBYTE(q->data + 7)) /* Error */
				ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q),
						       EX_GETROW (q), 
						       biff_get_error_text (BIFF_GETBYTE(q->data + 6))) ;
			else /* Boolean */
			{
				char *bl = "0" ; /* FALSE */
				if (BIFF_GETBYTE(q->data + 6))
					bl = "1" ; /* TRUE */
				ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), bl) ;
			}
			break;
		}
		default:
			if (EXCEL_DEBUG>0)
				printf ("Unrecognised opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
			break;
		}
	}
}

static void
ms_excel_read_sheet (MS_EXCEL_SHEET *sheet, BIFF_QUERY * q, MS_EXCEL_WORKBOOK * wb)
{
	LONG blankSheetPos = q->streamPos + q->length + 4;

	if (EXCEL_DEBUG>0)
		printf ("----------------- '%s' -------------\n", sheet->gnum_sheet->name);
				
	while (ms_biff_query_next (q)){
		if (EXCEL_DEBUG>5)
			printf ("Opcode : 0x%x\n", q->opcode) ;
		switch (q->ls_op){
		case BIFF_EOF:
			if (q->streamPos == blankSheetPos || sheet->blank) {
				if (EXCEL_DEBUG>0)
					printf ("Blank sheet\n");
				if (ms_excel_workbook_detach (sheet->wb, sheet)) {
					ms_excel_sheet_destroy (sheet) ;
					sheet = NULL ;
				} else 
					printf ("Serious error detaching sheet '%s'\n",
						sheet->gnum_sheet->name);
			}
			return;
			break;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			ms_obj_read_obj (q);
			break;

		case BIFF_SELECTION: /* S59DE2.HTM */
		{
			int pane_number ;
			int act_row, act_col ;
			int num_refs ;
			guint8 *refs ;
			pane_number = BIFF_GETBYTE (q->data) ;
			act_row     = BIFF_GETWORD (q->data + 1) ;
			act_col     = BIFF_GETWORD (q->data + 3) ;
			num_refs    = BIFF_GETWORD (q->data + 7) ;
			refs        = q->data + 9 ;
/*			printf ("Cursor : %d %d\n", act_col, act_row) ; */
			if (pane_number != 3) {
				printf ("FIXME: no pane support\n") ;
				break ;
			}
			sheet_selection_reset_only (sheet->gnum_sheet) ;
			while (num_refs>0) {
				int start_row = BIFF_GETWORD(refs + 0) ;
				int start_col = BIFF_GETBYTE(refs + 4) ;
				int end_row   = BIFF_GETWORD(refs + 2) ;
				int end_col   = BIFF_GETBYTE(refs + 5) ;
/*				printf ("Ref %d = %d %d %d %d\n", num_refs, start_col, start_row, end_col, end_row) ; */
				sheet_selection_append_range (sheet->gnum_sheet, start_col, start_row,
							      start_col, start_row,
							      end_col, end_row) ;
				refs+=6 ;
				num_refs-- ;
			}
			sheet_cursor_set (sheet->gnum_sheet, act_col, act_row, act_col, act_row, act_col, act_row) ;
			break ;
		}
		case BIFF_MS_O_DRAWING: /* FIXME: See: ms-escher.c and S59DA4.HTM */
			if (gnumeric_debugging>0)
				ms_escher_hack_get_drawing (q);
			break;
		case BIFF_NOTE: /* See: S59DAB.HTM */
		{
			guint16 row = EX_GETROW(q);
			guint16 col = EX_GETCOL(q);
			if ( sheet->ver >= eBiffV8 ) {
				guint16 options = BIFF_GETWORD(q->data+4);
				guint16 obj_id  = BIFF_GETWORD(q->data+6);
				guint16 author_len = BIFF_GETWORD(q->data+8);
				char *author=biff_get_text(author_len%2?q->data+11:q->data+10,
							   author_len, NULL);
				int hidden;
				if (options&0xffd)
					printf ("FIXME: Error in options\n");
				hidden = (options&0x2)==0;
				if (EXCEL_DEBUG>0)
					printf ("Comment at %d,%d id %d options 0x%x hidden %d by '%s'\n",
						col, row, obj_id, options, hidden, author);
			} else {
				guint16 author_len = BIFF_GETWORD(q->data+4);
				char *text=biff_get_text(q->data+6, author_len, NULL);
				if (EXCEL_DEBUG>1)
					printf ("Comment at %d,%d '%s'\n", col, row, text);

				if (row==0xffff && col==0)
					ms_excel_sheet_append_comment (sheet, col, row, text);
				else
					ms_excel_sheet_set_comment (sheet, col, row, text);
			}
			break;
		}
		default:
			switch (q->opcode) {
			case BIFF_WINDOW2: /* FIXME: see S59E18.HTM */
			{
				int top_vis_row, left_vis_col ;
				guint16 options ;
				
				if (q->length<6) {
					printf ("Duff window data");
					break;
				}

				options      = BIFF_GETWORD(q->data + 0) ;
				top_vis_row  = BIFF_GETWORD(q->data + 2) ;
				left_vis_col = BIFF_GETWORD(q->data + 4) ;
				if (options & 0x0200)
					printf ("Sheet flag selected\n") ;
				if (options & 0x0400) {
					printf ("Sheet top in workbook\n") ;
					workbook_focus_sheet (sheet->gnum_sheet);
				}
				if (options & 0x0001)
					printf ("FIXME: Sheet display formulae\n") ;
				break ;
			}
			default:
				ms_excel_read_cell (q, sheet);
				break;
			}
		}
	}
	if (ms_excel_workbook_detach (sheet->wb, sheet))
		ms_excel_sheet_destroy (sheet) ;
	sheet = NULL ;
	printf ("Error, hit end without EOF\n");
	return;
}

Sheet *
biff_get_externsheet_name(MS_EXCEL_WORKBOOK *wb, guint16 idx, gboolean get_first)
{
	BIFF_EXTERNSHEET_DATA *bed ;
	BIFF_BOUNDSHEET_DATA *bsd ;
	guint16 index ;

	if (idx>=wb->num_extern_sheets)
		return NULL;

	bed = &wb->extern_sheets[idx] ;
	index = get_first ? bed->first_tab : bed->last_tab ;

	bsd = g_hash_table_lookup (wb->boundsheet_data_by_index, &index) ;
	if (!bsd)
		printf ("Duff sheet index %d\n", index);
	return bsd->sheet->gnum_sheet;
}

/**
 * Find a stream with the correct name
 **/
static MS_OLE_STREAM *
find_workbook (MS_OLE * ptr)
{				/*
				 * Find the right Stream ... John 4:13-14 
				 */
	MS_OLE_DIRECTORY *d = ms_ole_directory_new (ptr);
	
	/*
	 * The thing to seek; first the kingdom of God, then this: 
	 */
	while (ms_ole_directory_next (d))
	  {
	    if (d->type == MS_OLE_PPS_STREAM)
	      {
		int hit = 0;

		/*
		 * printf ("Checking '%s'\n", d->name); 
		 */
		hit |= (g_strncasecmp (d->name, "book", 4) == 0);
		hit |= (g_strncasecmp (d->name, "workbook", 8) == 0);
		if (hit) {
			MS_OLE_STREAM *stream ;
			printf ("Found Excel Stream : %s\n", d->name);
			stream = ms_ole_stream_open (d, 'r') ;
			ms_ole_directory_destroy (d) ;
			return stream ;
		}
	      }
	  }
	printf ("No Excel file found\n");
	ms_ole_directory_destroy (d) ;
	return 0;
}

/*
 * see S59DEC.HM,
 * but this whole thing seems sketchy.
 * always get 03 00 01 04
 */
static void
ms_excel_read_supporting_wb (BIFF_BOF_DATA *ver, BIFF_QUERY *q)
{
	char *	name;
	BYTE *  data;
	guint32 byte_length, slen = 0;
	WORD	numTabs = BIFF_GETWORD (q->data);
	int i;

	printf("Supporting workbook with %d Tabs\n", numTabs);
	data = q->data + 2;
	{
		BYTE encodeType = BIFF_GETBYTE(data);
		++data;
		printf("--> SUPBOOK VirtPath encoding = ");
		switch (encodeType)
		{
		case 0x00 : /* chEmpty */
			puts("chEmpty");
			break;
		case 0x01 : /* chEncode */
			puts("chEncode");
#if 0
			for (i = 0 ; i < 50 ; ++i)
				printf("%3d (%c)(%x)\n", i, data[i], data[i]);
#endif
			break;
		case 0x02 : /* chSelf */
			puts("chSelf");
			break;
		default :
			printf("Unknown/Unencoded ??(%x '%c') %d\n",
			       encodeType, encodeType, q->length);
		};
	}

#if 0
	for (data = q->data + 2; numTabs-- > 0 ; )
	{
		if (ver->version == eBiffV8) {
			slen = (guint32) BIFF_GETWORD (data);
			name = biff_get_text (data += 2, slen, &byte_length);
		} else {
			slen = (guint32) BIFF_GETBYTE (data);
			name = biff_get_text (data += 1, slen, &byte_length);
		}
		puts(name);
	}
#endif
}

/*
 * A utility routine which should be moved to ms-biff.c but currently depends
 * on EXCEL_DEBUG which is local to this file
 */
static void
ms_biff_unknown_code (BIFF_QUERY *q)
{
#if 0
	if (EXCEL_DEBUG>0 && EXCEL_DEBUG<=5)
#endif
		printf ("Unknown Opcode : 0x%x, length 0x%x\n",
			q->opcode, q->length);
	if (EXCEL_DEBUG>2)
		dump (q->data, q->length); 
}

Workbook *
ms_excel_read_workbook (MS_OLE * file)
{
	MS_EXCEL_WORKBOOK *wb = NULL;
	xmlNodePtr child;

	cell_deep_freeze_redraws ();

	if (1){ /* ? */
		MS_OLE_STREAM *stream;
		BIFF_QUERY *q;
		BIFF_BOF_DATA *ver = 0;
		int current_sheet = 0 ;
		
		/*
		 * Tabulate frequencies for testing 
		 */
/*		{
			int freq[256];
			int lp;

			printf ("--------- BIFF Usage Chart ----------\n");
			for (lp = 0; lp < 256; lp++)
				freq[lp] = 0;
			stream = find_workbook (file);
			q = ms_biff_query_new (stream);
			while (ms_biff_query_next (q))
				freq[q->ls_op]++;
			for (lp = 0; lp < 256; lp++)
				if (freq[lp] > 0)
					printf ("Opcode 0x%x : %d\n", lp, freq[lp]);
			printf ("--------- End  Usage Chart ----------\n");
			ms_biff_query_destroy (q);
			ms_ole_stream_close (stream);
			} */

		/*
		 * Find that book file 
		 */
		stream = find_workbook (file);
		q = ms_biff_query_new (stream);

		while (ms_biff_query_next (q))
		{
			if (EXCEL_DEBUG>5)
				printf ("Opcode : 0x%x\n", q->opcode) ;

			/* Catch Chart specific codes before primary */
			if (0x10 == q->ms_op)
			    if (ms_chart_biff_read (wb, q))
				continue;

			/* Catch Oddballs
			 * The heuristic seems to be that 'version 1' BIFF types
			 * are unique and not versioned.
			 */
			if (0x1 == q->ms_op)
			{
				switch (q->opcode)
				{
				case BIFF_DFS :
					printf ("Double Stream File : %s\n",
						(BIFF_GETWORD(q->data) == 1)
						? "Yes" : "No");
					break;

				case BIFF_TXO :
					break;

				case BIFF_REFRESHALL :
					break;

				case BIFF_USESELFS :
					break;

				case BIFF_TABID :
					break;

				case BIFF_PROT4REV :
					break;

				case BIFF_PROT4REVPASS :
					break;

				case BIFF_SUPBOOK:
					ms_excel_read_supporting_wb (ver, q);
					break;

				default:
					ms_biff_unknown_code (q);
				}
				continue;
			}

			switch (q->ls_op)
			{
			case BIFF_BOF:
			{
				/* The first BOF seems to be OK, the rest lie ? */
				eBiff_version vv = eBiffVUnknown;
				if (ver)
				{
					vv = ver->version;
					ms_biff_bof_data_destroy (ver);
				}
				ver = ms_biff_bof_data_new (q);
				if (vv != eBiffVUnknown)
					ver->version = vv;

				if (ver->type == eBiffTWorkbook) {
					wb = ms_excel_workbook_new ();
					wb->gnum_wb = workbook_new ();
					if (ver->version >= eBiffV8)
						printf ("Excel 97 +\n");
					else
						printf ("Excel 95 or older\n");
				} else if (ver->type == eBiffTWorksheet) {
					BIFF_BOUNDSHEET_DATA *bsh ;

					bsh = g_hash_table_lookup (wb->boundsheet_data_by_stream,
								   &q->streamPos) ;
					if (!bsh)
						printf ("Sheet offset in stream of %x not found in list\n", q->streamPos);
					else
					{
						MS_EXCEL_SHEET *sheet = ms_excel_workbook_get_sheet (wb, current_sheet) ;
						ms_excel_sheet_set_version (sheet, ver->version) ;
						ms_excel_read_sheet (sheet, q, wb) ;
						current_sheet++ ;
					}
				} else
					printf ("Unknown BOF\n");
			}
			break;
			case BIFF_EOF:
				printf ("End of worksheet spec.\n");
				break;
			case BIFF_BOUNDSHEET:
				biff_boundsheet_data_new (wb, q, ver->version);
				break;
			case BIFF_PALETTE:
				wb->palette = ms_excel_palette_new (q);
				break;
			case BIFF_FONT:	        /* see S59D8C.HTM */
				{
					BIFF_FONT_DATA *ptr;

/*					printf ("Read Font\n");
					dump (q->data, q->length); */
					biff_font_data_new (wb, q);
				}
				break;
			case BIFF_PRECISION:	/*
						 * FIXME: 
						 */
				if (EXCEL_DEBUG>0) {
					printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
					dump (q->data, q->length);
				}
				break;
			case BIFF_XF_OLD:	/*
						 * FIXME: see S59E1E.HTM 
						 */
			case BIFF_XF:
				biff_xf_data_new (wb, q, ver->version) ;
				break;
			case BIFF_SST: /* see S59DE7.HTM */
			{
				guint32 length, k, tot_len ;
				BYTE *tmp ;

				if (EXCEL_DEBUG>4) {
					printf ("SST\n") ;
					dump (q->data, q->length) ;
				}
				wb->global_string_max = BIFF_GETLONG(q->data+4);
				wb->global_strings = g_new (char *, wb->global_string_max) ;

				tmp = q->data + 8 ;
				tot_len = 8 ;
				for (k = 0; k < wb->global_string_max; k++)
				{
					guint32 byte_len ;
					length = BIFF_GETWORD (tmp) ;
					wb->global_strings[k] = biff_get_text (tmp+2, length, &byte_len) ;
					if (!wb->global_strings[k])
						printf ("Blank string in table at : %d\n", k) ;
					tmp += byte_len + 2 ;
					tot_len += byte_len + 2 ;
					if (tot_len > q->length) {
						/*
						  This means that somehow, the string table has been split
						  Perhaps it is too big for a single biff record, or
						  perhaps excel is just cussid. Either way a big pain.
						*/
						wb->global_string_max = k;
						printf ("FIXME: Serious SST overrun lost %d of %d strings!\n",
							wb->global_string_max - k, wb->global_string_max) ;
						printf ("Last string was '%s' 0x%x > 0x%x\n",
							wb->global_strings[k-1], tot_len, q->length);

						break ;
					}
				}
				break;
			}
			case BIFF_EXTSST: /* See: S59D84 */
				/* Can be safely ignored on read side */
				break;
			case BIFF_MS_O_DRAWING_GROUP: /* FIXME: See: S59DA5.HTM */
				if (gnumeric_debugging>0) {
					printf ("FIXME: MS Drawing Group\n");
					if (wb && wb->read_drawing_group==0) {
						ms_escher_hack_get_drawing (q);
						wb->read_drawing_group=1;
					} else /* Why should this be ? don't ask me */
						printf ("FIXME: bad docs\n");
				}
				break;
			case BIFF_EXTERNSHEET: /* See: S59D82.HTM */
			{
				if ( ver->version == eBiffV8 )
				{
					guint16 numXTI = BIFF_GETWORD(q->data) ;
					guint16 cnt ;
					
					wb->num_extern_sheets = numXTI ;
					/* printf ("ExternSheet (%d entries)\n", numXTI) ;
					   dump (q->data, q->length); */
					
					wb->extern_sheets = g_new (BIFF_EXTERNSHEET_DATA, numXTI+1) ;

					for (cnt=0; cnt < numXTI; cnt++)
					{
						wb->extern_sheets[cnt].sup_idx   =  BIFF_GETWORD(q->data + 2 + cnt*6 + 0) ;
						wb->extern_sheets[cnt].first_tab =  BIFF_GETWORD(q->data + 2 + cnt*6 + 2) ;
						wb->extern_sheets[cnt].last_tab  =  BIFF_GETWORD(q->data + 2 + cnt*6 + 4) ;
						/* printf ("SupBook : %d First sheet %d, Last sheet %d\n", BIFF_GETWORD(q->data + 2 + cnt*6 + 0),
						   BIFF_GETWORD(q->data + 2 + cnt*6 + 2), BIFF_GETWORD(q->data + 2 + cnt*6 + 4)) ; */
					}
					
				} else {
					printf ("ExternSheet : only BIFF8 supported so far...\n") ;
				}
				break ;
			}
			case BIFF_FORMAT: /* S59D8E.HTM */
			{
				BIFF_FORMAT_DATA *d = g_new(BIFF_FORMAT_DATA,1) ;
/*				printf ("Format data 0x%x %d\n", q->ms_op, ver->version) ;
				dump (q->data, q->length) ;*/
				if (ver->version == eBiffV7) /* Totaly guessed */
				{
					d->idx = BIFF_GETWORD(q->data) ;
					d->name = biff_get_text(q->data+3, BIFF_GETBYTE(q->data+2), NULL) ;
				}
				else if (ver->version == eBiffV8)
				{
					d->idx = BIFF_GETWORD(q->data) ;
					d->name = biff_get_text(q->data+4, BIFF_GETWORD(q->data+2), NULL) ;
				}
				else /* FIXME: mythical old papyrus spec. */
				{
					d->name = biff_get_text(q->data+1, BIFF_GETBYTE(q->data), NULL) ;
					d->idx = g_hash_table_size (wb->format_data) + 0x32 ;
				}
/*				printf ("Format data : %d == '%s'\n", d->idx, d->name) ; */
				g_hash_table_insert (wb->format_data, &d->idx, d) ;
			}
			case BIFF_EXTERNCOUNT: /* see S59D7D.HTM */
				if (EXCEL_DEBUG>0)
					printf ("%d external references\n", BIFF_GETWORD(q->data)) ;
				break ;

			case BIFF_CODEPAGE : /* DUPLICATE 42 */
				{
				    /* This seems to appear within a workbook */
				    char * page = NULL;
				    WORD codepage = BIFF_GETWORD (q->data);

				    switch(codepage)
				    {
				    case 0x01b5 :
					puts("CodePage = IBM PC (Multiplan)");
					break ;
				    case 0x8000 :
					puts("CodePage = Apple Macintosh");
					break;
				    case 0x04e4 :
					puts("CodePage = ANSI (Microsoft Windows)");
					break;
				    default :
					printf("CodePage = UNKNOWN(%hx)\n",
					       codepage);
				    };
				}
				break;

			case BIFF_PROTECT :
	     			break;

			case BIFF_PASSWORD :
	     			break;

			case (BIFF_NAME & 0xff) : /* Why here and not as 18 */
				ms_excel_read_name (q);
	     			break;

			case (BIFF_STYLE & 0xff) : /* Why here and not as 93 */
	     			break;

			case BIFF_WINDOWPROTECT :
	     			break;

			case BIFF_EXTERNNAME :
				ms_excel_externname(q, wb, ver->version);
	     			break;

			case BIFF_BACKUP :
				break;
			case BIFF_WRITEACCESS :
				break;
			case BIFF_HIDEOBJ :
				break;
			case BIFF_FNGROUPCOUNT :
				break;
			case BIFF_MMS :
				break;

			case BIFF_OBPROJ :
				puts("Visual Basic Project");
				break;

			case BIFF_BOOKBOOL :
				break;
			case BIFF_COUNTRY :
				break;

			case BIFF_INTERFACEHDR :
				break;

			case BIFF_INTERFACEEND :
				break;

			case BIFF_1904 : /* 0, NOT 1 */
				if (BIFF_GETWORD(q->data) == 1)
					printf ("Uses 1904 Date System\n");
				break;

			case BIFF_WINDOW1 : /* 0 NOT 1 */
				break;

			case (BIFF_WINDOW2 & 0xff) :
				break;

			case BIFF_SELECTION : /* 0, NOT 10 */
				break;

			case BIFF_DIMENSIONS :	/* 2, NOT 1,10 */
				break;

			case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
				ms_obj_read_obj (q);
				break;

			case BIFF_SCL :
				break;

			case BIFF_MS_O_DRAWING: /* FIXME: See: ms-escher.c and S59DA4.HTM */
				if (gnumeric_debugging>0)
					ms_escher_hack_get_drawing (q);
				break;

			default:
				ms_biff_unknown_code (q);
				break;
			}
			/* NO Code here unless you modify the handling
			 * of Chart and Odd Balls Above the switch
			 */
		}
		ms_biff_query_destroy (q);
		if (ver)
			ms_biff_bof_data_destroy (ver);
		ms_ole_stream_close (stream);
	}

	cell_deep_thaw_redraws ();
	
	if (wb)
	{
		workbook_recalc (wb->gnum_wb);
		return wb->gnum_wb;
	}
	return 0;
}
