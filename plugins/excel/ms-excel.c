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
#include <assert.h>
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

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula.h"
#include "ms-excel.h"
#include "ms-excel-biff.h"

/* Forward references */
static MS_EXCEL_SHEET *ms_excel_sheet_new (MS_EXCEL_WORKBOOK * wb, char *name) ;
static void ms_excel_workbook_attach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans) ;

/**
 * Generic 16 bit int index pointer functions.
 **/
static guint
biff_guint16_hash (const guint16 *d)
{ return *d ; }
static guint
biff_guint32_hash (const guint32 *d)
{ return *d ; }

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
 *  This function takes a length argument as Biff V7 has a byte length
 * ( seemingly ).
 *  FIXME: see S59D47.HTM for full description
 **/
char *
biff_get_text (BYTE * ptr, int length)
{
	int lp ;
	char *ans;
	BYTE header ;
	gboolean high_byte ;
	gboolean ext_str ;
	gboolean rich_str ;

	if (!length)
		return 0;

	ans = (char *) g_malloc (sizeof (char) * length + 2);

	header = BIFF_GETBYTE(ptr) ;
	/* I assume that this header is backwards compatible with raw ASCII */
	if (((header & 0xf0) == 0) &&
	    ((header & 0x02) == 0)) /* Its a proper Unicode header grbit byte */
	{
		high_byte = (header & 0x1) != 0 ;
		ext_str   = (header & 0x4) != 0 ;
		rich_str  = (header & 0x8) != 0 ;
		ptr++ ;
	}
	else /* Some assumptions: FIXME ? */
	{
		high_byte = 0 ;
		ext_str   = 0 ;
		rich_str  = 0 ;
	}

	/* A few friendly warnings */
	if (high_byte)
		printf ("FIXME: unicode support unimplemented: truncating\n") ;
	if (rich_str) /* The data for this appears after the string */
	{
		guint16 formatting_runs = BIFF_GETWORD(ptr) ;
		ptr+= 2 ;
		printf ("FIXME: rich string support unimplemented: discarding %d runs\n", formatting_runs) ;
	}
	if (ext_str) /* NB this data always comes after the rich_str data */
	{
		guint32 len_ext_rst = BIFF_GETLONG(ptr) ;
		printf ("FIXME: extended string support unimplemented: ignoring %d bytes\n", len_ext_rst) ;
		ptr+= 4 ;
	}

	for (lp = 0; lp < length; lp++)
	{
		ans [lp] = (char) *ptr ;
		ptr += high_byte ? 2 : 1;
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

char *
biff_get_error_text (guint8 err)
{
	char *buf ;
	switch (err)
	{
	case 0:  buf = "#NULL!" ;  break ;
	case 7:  buf = "#DIV/0!" ; break ;
	case 15: buf = "#VALUE!" ; break ;
	case 23: buf = "#REF!" ;   break ;
	case 29: buf = "#NAME?" ;  break ;
	case 36: buf = "#NUM!" ;   break ;
	case 42: buf = "#N/A" ;    break ;
	default:
		buf = "#UNKNOWN!" ;
	}
	return buf ;
}

/**
 * See S59D5D.HTM
 **/
static BIFF_BOF_DATA *
ms_biff_bof_data_new (BIFF_QUERY * q)
{
	BIFF_BOF_DATA *ans = g_new (BIFF_BOF_DATA, 1);

	if ((q->opcode & 0xff) == BIFF_BOF){
		assert (q->length >= 4);
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
				printf ("Complicated BIFF version %d\n",
					BIFF_GETWORD (q->data));
				dump (q->data, q->length);
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
		printf ("BOF %x, %d == %d, %d\n", q->opcode, q->length,
		  ans->version, ans->type);
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
		int strlen = BIFF_GETWORD (q->data + 6);
		ans->name = biff_get_text (q->data + 8, strlen);
	} else {
		int strlen = BIFF_GETBYTE (q->data + 6);

		ans->name = biff_get_text (q->data + 7, strlen);
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
	sheet = ms_excel_sheet_new (wb, ans->name);
	ms_excel_workbook_attach (wb, sheet);
}

static gboolean 
biff_boundsheet_data_destroy (gpointer key, BIFF_BOUNDSHEET_DATA *d, gpointer userdata)
{
	g_free (d->name) ;
	g_free (d) ;
	return 1 ;
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
	fd->italic = (data & 0x2) == 0x2;
	fd->struck_out = (data & 0x8) == 0x8;
	fd->color_idx = BIFF_GETWORD (q->data + 4);
	fd->boldness = BIFF_GETWORD (q->data + 6);
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
	fd->fontname = biff_get_text (q->data + 15, BIFF_GETBYTE (q->data + 14));
	/*
	 * dump (q->data, q->length); 
	 */
	printf ("Insert font '%s' size %d pts\n",
		fd->fontname, fd->height * 20);

        fd->index = g_hash_table_size (wb->font_data) ;
	if (fd->index >= 4) /* Wierd: for backwards compatibility */
		fd->index++ ;
	g_hash_table_insert (wb->font_data, &fd->index, fd) ;
}

static gboolean 
biff_font_data_destroy (gpointer key, BIFF_FONT_DATA *d, gpointer userdata)
{
	g_free (d->fontname) ;
	g_free (d) ;
	return 1 ;
}

static MS_EXCEL_PALETTE *
ms_excel_palette_new (BIFF_QUERY * q)
{
	int lp, len;
	MS_EXCEL_PALETTE *pal;

	pal = (MS_EXCEL_PALETTE *) g_malloc (sizeof (MS_EXCEL_PALETTE));
	len = BIFF_GETWORD (q->data);
	pal->length = len;
	pal->red = (int *) g_malloc (sizeof (int) * len);
	pal->green = (int *) g_malloc (sizeof (int) * len);
	pal->blue = (int *) g_malloc (sizeof (int) * len);

	printf ("New palette with %d entries\n", len);
	for (lp = 0; lp < len; lp++){
		LONG num = BIFF_GETLONG (q->data + 2 + lp * 4);

		pal->red[lp] = (num & 0x00ff0000) >> 16;
		pal->green[lp] = (num & 0x0000ff00) >> 8;
		pal->blue[lp] = (num & 0x000000ff) >> 0;
		printf ("Colour %d : (%d,%d,%d)\n", lp, pal->red[lp], pal->green[lp], pal->blue[lp]);
	}
	return pal;
}

static void
ms_excel_palette_destroy (MS_EXCEL_PALETTE * pal)
{
	g_free (pal->red);
	g_free (pal->green);
	g_free (pal->blue);
	g_free (pal);
}

typedef struct _BIFF_XF_DATA {
        guint16 index ;
	guint16 font_idx;
	WORD format_idx;
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
	BYTE border_color[4];	/*
				 * Array [StyleSide]
				 */
	StyleBorderType border_type[4];	/*
					 * Array [StyleSide]
					 */
	eBiff_border_orientation border_orientation;
	StyleBorderType border_linestyle;
	BYTE fill_pattern_idx;
	BYTE foregnd_col;
	BYTE backgnd_col;
} BIFF_XF_DATA;

static void
ms_excel_set_cell_colors (MS_EXCEL_SHEET * sheet, Cell * cell, BIFF_XF_DATA * xf)
{
	MS_EXCEL_PALETTE *p = sheet->wb->palette;
	int col;

	if (!p || !xf)
		return;

	col = xf->foregnd_col;
	if (col >= 0 && col < sheet->wb->palette->length){
		/*
		 * printf ("FG set to %d = (%d,%d,%d)\n", col, p->red[col], p->green[col], p->blue[col]); 
		 */
		cell_set_foreground (cell, p->red[col], p->green[col], p->blue[col]);
	} else
		printf ("FG col out of range %d\n", col);
	col = xf->backgnd_col;
	if (col >= 0 && col < sheet->wb->palette->length){
		/*
		 * printf ("BG set to %d = (%d,%d,%d)\n", col, p->red[col], p->green[col], p->blue[col]); 
		 */
		cell_set_background (cell, p->red[col], p->green[col], p->blue[col]);
	} else
		printf ("BG col out of range %d\n", col);
}

/**
 * Search for a font record from its index in the workbooks font table
 * NB. index 4 is omitted supposedly for backwards compatiblity
 **/
static void
ms_excel_set_cell_font (MS_EXCEL_SHEET * sheet, Cell * cell, BIFF_XF_DATA * xf)
{
	char font_size[16];	/*
				 * I know it may seem excessive. Time will say.  
				 */
	int i;
	BIFF_FONT_DATA *fd = g_hash_table_lookup (sheet->wb->font_data, &xf->font_idx) ;
	char *fname ;

	if (!fd)
	{
		printf ("Unknown fount idx %d\n", xf->font_idx);
		return ;
	}
	g_assert (fd->index != 4);

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
	cell_set_font (cell, (fname = font_change_component (cell->style->font->font_name, 1, fd->fontname)));
	if (fname) g_free (fname) ;
/*			printf ("FoNt [-]: %s\n", cell->style->font->font_name); */
	if (fd->italic){
   		cell_set_font (cell, (fname = font_get_italic_name (cell->style->font->font_name)));
		if (fname) g_free (fname) ;
/*				printf ("FoNt [i]: %s\n", cell->style->font->font_name); */
		cell->style->font->hint_is_italic = 1;
	}
	if (fd->boldness >= 0x2bc){
		cell_set_font (cell, (fname = font_get_bold_name (cell->style->font->font_name))); 
		if (fname) g_free (fname) ;
/*				printf ("FoNt [b]: %s\n", cell->style->font->font_name); */
		cell->style->font->hint_is_bold = 1;
	}
	/*
	 * What about underlining?  
	 */
	g_assert (snprintf (font_size, 16, "%d", fd->height / 2) != -1);
	cell_set_font (cell, (fname = font_change_component (cell->style->font->font_name, 7, font_size)));
	if (fname) g_free (fname) ;
}


static StyleColor *
get_style_color_from_idx (MS_EXCEL_SHEET *sheet, int idx)
{
 	MS_EXCEL_PALETTE *p = sheet->wb->palette;
	
 	if (idx >= 0 && p && idx < p->length)
 		return style_color_new (p->red[idx], p->green[idx],
 					p->blue[idx]);
 	else
 		return NULL;
}

void
ms_excel_set_cell_xf (MS_EXCEL_SHEET * sheet, Cell * cell, guint16 xfidx)
{
	GList *ptr;
	int cnt;
	BIFF_XF_DATA *xf;

	if (xfidx == 0){
/*		printf ("Normal cell formatting\n"); */
		return;
	}
	if (xfidx == 15){
/*		printf ("Default cell formatting\n"); */
    		return;
	}
	/*
	 * if (!cell->text) Crash if formatting and no text...
	 * cell_set_text_simple(cell, ""); 
	 * printf ("Looking for %d\n", xfidx); 
	 */
	
	xf = g_hash_table_lookup (sheet->wb->XF_cell_records, &xfidx) ;
	if (!xf)
	{
	        printf ("No XF record for %d out of %d found :-(\n",
			xfidx, g_hash_table_size (sheet->wb->XF_cell_records));
	        return;
	}
	if (xf->xftype != eBiffXCell)
	       printf ("FIXME: Error looking up XF\n") ;

	/*
	 * Well set it up then ! FIXME: hack ! 
	 */
	cell_set_alignment (cell, xf->halign, xf->valign, ORIENT_HORIZ, 1);
	ms_excel_set_cell_colors (sheet, cell, xf);
	ms_excel_set_cell_font (sheet, cell, xf);
	{
		int lp;
 		StyleColor *tmp[4];
 		for (lp=0;lp<4;lp++)
 			tmp[lp] = get_style_color_from_idx
 				(sheet, xf->border_color[lp]);
 		cell_set_border (cell, xf->border_type, tmp);
	}
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
		xf->foregnd_col = (data & 0x007f);
		xf->backgnd_col = (data & 0x3f80) >> 7;
	} else {
		data = BIFF_GETWORD (q->data + 8);
		xf->foregnd_col = (data & 0x007f);
		xf->backgnd_col = (data & 0x1f80) >> 7;

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

	if (xf->xftype == eBiffXCell)
	{
	        xf->index = 16 + 4 + g_hash_table_size (wb->XF_cell_records) ;
		/*	        printf ("Inserting into cell XF hash with : %d\n", xf->index) ; */
		g_hash_table_insert (wb->XF_cell_records, &xf->index, xf) ;
	}
 	else
	{
	        xf->index = 16 + 4 + g_hash_table_size (wb->XF_style_records) ;
		/*	        printf ("Inserting into style XF hash with : %d\n", xf->index) ; */
		g_hash_table_insert (wb->XF_style_records, &xf->index, xf) ;
	}
}

static gboolean 
biff_xf_data_destroy (gpointer key, BIFF_XF_DATA *d, gpointer userdata)
{
	g_free (d);
	return 1 ;
}

static MS_EXCEL_SHEET *
ms_excel_sheet_new (MS_EXCEL_WORKBOOK * wb, char *name)
{
	MS_EXCEL_SHEET *ans = (MS_EXCEL_SHEET *) g_malloc (sizeof (MS_EXCEL_SHEET));

	ans->gnum_sheet = sheet_new (wb->gnum_wb, name);
	ans->blank = 1 ;
	ans->wb = wb;
	ans->array_formulae = 0;
	return ans;
}

static void
ms_excel_sheet_set_version (MS_EXCEL_SHEET *sheet, eBiff_version ver)
{
	sheet->ver = ver ;
}

void
ms_excel_sheet_insert (MS_EXCEL_SHEET * sheet, int xfidx, int col, int row, char *text)
{
	Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);

	/* NB. cell_set_text _certainly_ strdups *text */
	sheet->blank = 0 ;
	cell_set_text (cell, text);
	ms_excel_set_cell_xf (sheet, cell, xfidx);
}

static void
ms_excel_sheet_set_index (MS_EXCEL_SHEET *ans, int idx)
{
	ans->index = idx ;
}

static void
ms_excel_sheet_destroy (MS_EXCEL_SHEET * sheet)
{
	GList *ptr = g_list_first (sheet->array_formulae);

	while (ptr){
		g_free (ptr->data);
		ptr = ptr->next;
	}
	g_list_free (sheet->array_formulae);

	sheet_destroy (sheet->gnum_sheet);
	
	g_free (sheet);
}

static MS_EXCEL_WORKBOOK *
ms_excel_workbook_new ()
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
	ans->excel_sheets = NULL;
	ans->XF_style_records = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						  (GCompareFunc)biff_guint16_equal) ;
	ans->XF_cell_records = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						 (GCompareFunc)biff_guint16_equal) ;
	ans->palette = NULL;
	ans->global_strings = NULL;
	ans->global_string_max = 0;
	return ans;
}

static void
ms_excel_workbook_attach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans)
{
	int    idx = 0 ;
	GList *list = wb->excel_sheets ;

	workbook_attach_sheet (wb->gnum_wb, ans->gnum_sheet);
	
	while (list)
	{
		idx++ ;
		list = list->next ;
	}
	ms_excel_sheet_set_index (ans, idx) ;
	wb->excel_sheets = g_list_append (wb->excel_sheets, ans) ;
}

static void
ms_excel_workbook_detach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans)
{
	int    idx = 0 ;
	GList *list = wb->excel_sheets ;

	if (ans->gnum_sheet)
		workbook_detach_sheet (wb->gnum_wb, ans->gnum_sheet);
	
	while (list)
		if (list->data == ans)
		{
			wb->excel_sheets = g_list_remove(wb->excel_sheets, list);
			return ;
		}
		else
			list = list->next ;
}

static MS_EXCEL_SHEET *
ms_excel_workbook_get_sheet (MS_EXCEL_WORKBOOK *wb, int idx)
{
	GList *list = wb->excel_sheets ;
	while (list)
	{
		MS_EXCEL_SHEET *sheet = list->data ;
		if (sheet->index == idx)
			return sheet ;
		list = list->next ;
	}
	return NULL ;
}

static void
ms_excel_workbook_destroy (MS_EXCEL_WORKBOOK * wb)
{
	g_hash_table_foreach_remove (wb->boundsheet_data_by_stream,
				     (GHRFunc)biff_boundsheet_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->boundsheet_data_by_index) ;
	g_hash_table_destroy (wb->boundsheet_data_by_stream) ;
	g_hash_table_foreach_remove (wb->XF_style_records,
				     (GHRFunc)biff_xf_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->XF_style_records) ;
	g_hash_table_foreach_remove (wb->XF_cell_records,
				     (GHRFunc)biff_xf_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->XF_cell_records) ;

	g_hash_table_foreach_remove (wb->font_data,
				     (GHRFunc)biff_font_data_destroy,
				     wb) ;
	g_hash_table_destroy (wb->font_data) ;

	if (wb->palette)
		ms_excel_palette_destroy (wb->palette);

	if (wb->extern_sheets)
		g_free (wb->extern_sheets) ;

	g_free (wb);
}

/**
 * Unpacks a MS Excel RK structure,
 * This needs to return / insert sensibly to keep precision / accelerate
 **/
static double
biff_get_rk (guint8 *ptr)
{
	LONG number;
	LONG tmp[2];
	char buf[65];
	double answer;
	enum eType {
		eIEEE = 0, eIEEEx10 = 1, eInt = 2, eIntx100 = 3
	} type;
	
	number = BIFF_GETLONG (ptr);
	type = (number & 0x3);
	switch (type){
	case eIEEE:
		tmp[0] = 0;
		tmp[1] = number & 0xfffffffc;
		answer = BIFF_GETDOUBLE (((BYTE *) tmp));
		break;
	case eIEEEx10:
		tmp[0] = 0;
		tmp[1] = number & 0xfffffffc;
		answer = BIFF_GETDOUBLE (((BYTE *) tmp));
		answer /= 100.0;
		break;
	case eInt:
		answer = (double) (number >> 2);
		break;
	case eIntx100:
		answer = ((double) (number >> 2)) / 100.0;
		break;
	default:
		printf ("You don't exist go away\n");
		answer = 0;
	}
	return answer ;
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
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), "");
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
				lastcol = BIFF_GETWORD (q->data + q->length - 2);	/*
											 * guess 
											 */
				printf ("Cells in row %d are blank starting at col %d until col %d (0x%x)\n",
				  row, col, lastcol, lastcol);
				/*
				 * if (lastcol<col)  What to do in this case ? 
				 * {
				 * printf ("Serious implentation documentation error\n");
				 * break;
				 * }
				 */
				incr = (lastcol > col) ? 1 : -1;
				/*
				 * g_assert (((lastcol-col)*incr+1)*2+6<=q->length); 
				 */
				while (col != lastcol){
					ms_excel_sheet_insert (sheet, BIFF_GETWORD (ptr), EX_GETCOL (q), EX_GETROW (q), "");
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
			printf ("Header '%s'\n", (str=biff_get_text (q->data+1,
								     BIFF_GETBYTE(q->data)))) ;
			g_free(str) ;
		}
 		break;
	}
 	case BIFF_FOOTER: /* FIXME : S59D8D */
	{
		char *str ;
		if (q->length)
		{
			printf ("Footer '%s'\n", (str=biff_get_text (q->data+1,
								     BIFF_GETBYTE(q->data)))) ;
			g_free(str) ;
		}
 		break;
	}
	case BIFF_RSTRING:
	{
		char *txt ;
		/*
		  printf ("Cell [%d, %d] = ", EX_GETCOL(q), EX_GETROW(q));
		  dump (q->data, q->length);
		*/
		printf ("Rstring\n") ;
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       (txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q))));
		g_free (txt) ;
		break;
	}
	case BIFF_NUMBER:
		{
			char buf[65];
			double num = BIFF_GETDOUBLE (q->data + 6);	/*
									 * FIXME GETDOUBLE is not endian independant 
									 */
/*			dump (q->data, q->length);
			snprintf (buf, 64, "NUM %f", num); */
			snprintf (buf, 64, "%f", num);
			ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), buf);
			break;
		}
	case BIFF_RK:		/*
				 * FIXME: S59DDA.HTM - test IEEE stuff on other endian platforms 
				 */
	{
		char buf[65];
		
/*		printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length);
		dump (q->data, q->length);*/
		sprintf (buf, "%f", biff_get_rk(q->data+6));
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), buf);
		break;
	}
	case BIFF_MULRK: /* S59DA8.HTM */
	{
		guint32 col, row, lastcol ;
		char buf[65] ;
		guint8 *ptr = q->data ;

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
			sprintf (buf, "%f", biff_get_rk(ptr+2)) ;
			ms_excel_sheet_insert(sheet, BIFF_GETWORD(ptr), col, row, buf) ;
			col++ ;
			ptr+= 6 ;
		}
		break ;
	}
	case BIFF_LABEL:
	{
		char *label ;
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       (label = biff_get_text (q->data + 8, EX_GETSTRLEN (q))));
		g_free (label) ;
		break;
	}
	case BIFF_ROW:		/*
				 * FIXME 
				 */
		/*
		 * printf ("Row %d formatting\n", EX_GETROW(q)); 
		 */
		break;
	case BIFF_ARRAY:	/*
				 * FIXME: S59D57.HTM 
				 */
		printf ("Array Formula\n") ;
	case BIFF_FORMULA:	/*
				 * FIXME: S59D8F.HTM 
				 */
		/*
		 * case BIFF_NAME FIXME: S59DA9.HTM 
		 */
		ms_excel_parse_formula (sheet, q, EX_GETCOL (q), EX_GETROW (q));
		break;
	case BIFF_LABELSST:
	{
		char *str;
		guint32 idx = BIFF_GETLONG (q->data + 6) ;
		
		if (idx >= sheet->wb->global_string_max)
		{
			printf ("string index 0x%x out of range\n", idx) ;
			break ;
		}
		
		str = sheet->wb->global_strings[idx] ;
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), str);
                break;
	}
	default:
		switch (q->opcode)
		{
		case BIFF_NAME: /* FIXME: S59DA9.HTM */
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

			g_assert (ixals==sheet_idx) ;
			ptr = q->data + 14 ;
			name = biff_get_text (ptr, name_len) ;
			ptr+= name_len + name_def_len ;
			menu_txt = biff_get_text (ptr, menu_txt_len) ;
			ptr+= menu_txt_len ;
			descr_txt = biff_get_text (ptr, descr_txt_len) ;
			ptr+= descr_txt_len ;
			help_txt = biff_get_text (ptr, help_txt_len) ;
			ptr+= help_txt_len ;
			status_txt = biff_get_text (ptr, status_txt_len) ;

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
			break ;
		}
		case BIFF_STRING: /* FIXME: S59DE9.HTM */
		{
			char *txt ;
			printf ("This cell evaluated to '%s': so what ? data:\n", (txt = biff_get_text (q->data + 2, BIFF_GETWORD(q->data)))) ;
			if (txt) g_free (txt) ;
			break ;
		}
		case BIFF_BOOLERR: /* S59D5F.HTM */
		{
			if (BIFF_GETBYTE(q->data + 7)) /* Error */
				ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q)
						       , EX_GETROW (q), 
						       biff_get_error_text (BIFF_GETBYTE(q->data + 6))) ;
			else /* Boolean */
			{
				if (BIFF_GETBYTE(q->data + 6))
					ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), "1") ; /* TRUE */
				else
					ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), "0") ; /* FALSE */
			}
			break;
		}
		default:
			printf ("Unrecognised opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
			break;
		}
	}
}

static void
ms_excel_read_sheet (MS_EXCEL_SHEET *sheet, BIFF_QUERY * q, MS_EXCEL_WORKBOOK * wb)
{
	LONG blankSheetPos = q->streamPos + q->length + 4;

	printf ("----------------- Sheet -------------\n");

	while (ms_biff_query_next (q)){
		switch (q->ls_op){
		case BIFF_EOF:
			if (q->streamPos == blankSheetPos || sheet->blank)
			{
				printf ("Blank sheet\n");
				ms_excel_workbook_detach (sheet->wb, sheet) ;
				ms_excel_sheet_destroy (sheet) ;
				return;
			}
			ms_excel_fixup_array_formulae (sheet);
			return;
			break;
		default:
			ms_excel_read_cell (q, sheet);
			break;
		}
	}
	ms_excel_workbook_detach (sheet->wb, sheet) ;
	ms_excel_sheet_destroy (sheet) ;
	printf ("Error, hit end without EOF\n");
	return;
}

char* 
biff_get_externsheet_name(MS_EXCEL_WORKBOOK *wb, guint16 idx, gboolean get_first)
{
	BIFF_EXTERNSHEET_DATA *bed ;
	BIFF_BOUNDSHEET_DATA *bsd ;
	guint16 index ;
	g_assert ( idx < wb->num_extern_sheets) ;

	bed = &wb->extern_sheets[idx] ;
	index = get_first ? bed->first_tab : bed->last_tab ;

	bsd = g_hash_table_lookup (wb->boundsheet_data_by_index, &index) ;
	if (!bsd) return 0 ;
	return bsd->name ;
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
		hit |= (strncasecmp (d->name, "book", 4) == 0);
		hit |= (strncasecmp (d->name, "workbook", 8) == 0);
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

Workbook *
ms_excelReadWorkbook (MS_OLE * file)
{
	MS_EXCEL_WORKBOOK *wb = NULL;
	xmlNodePtr child;

	if (1){ /* ? */
		MS_OLE_STREAM *stream;
		BIFF_QUERY *q;
		BIFF_BOF_DATA *ver = 0;
		int current_sheet = 0 ;
		
		/*
		 * Tabulate frequencies for testing 
		 */
		{
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
		}

		/*
		 * Find that book file 
		 */
		stream = find_workbook (file);
		q = ms_biff_query_new (stream);

		while (ms_biff_query_next (q))
		{
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

				if (ver->type == eBiffTWorkbook){
					wb = ms_excel_workbook_new ();
					wb->gnum_wb = workbook_new ();
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
				printf ("READ PALETTE\n");
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
				printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
				dump (q->data, q->length);
				break;
			case BIFF_XF_OLD:	/*
						 * FIXME: see S59E1E.HTM 
						 */
			case BIFF_XF:
				biff_xf_data_new (wb, q, ver->version) ;
				break;
			case BIFF_SST: /* see S59DE7.HTM */
			{
				int length, k ;
				char *temp ;
				wb->global_string_max = BIFF_GETLONG(q->data+4);
				wb->global_strings = g_new (char *, wb->global_string_max) ;

				temp = q->data + 8 ;
				for (k = 0; k < wb->global_string_max; k++)
				{
					length = BIFF_GETWORD (temp) ;
					wb->global_strings[k] = biff_get_text (temp+2, length) ;
					temp+= length + 3 ;
				}
				break;
			}
			case BIFF_EXTERNSHEET:
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
			}
			case BIFF_EXTERNCOUNT: /* see S59D7D.HTM */
				printf ("%d external references\n", BIFF_GETWORD(q->data)) ;
				break ;
			default:
				switch (q->opcode)
				{
				case BIFF_SUPBOOK: /* see S59DEC.HM, but this whole thing seems sketchy : always get 03 00 01 04 */
					printf ("SupBook:  %d tabs in workbook (FIXME!)\n", BIFF_GETWORD (q->data) ) ;
					dump (q->data, q->length);
					break ;
				default:
					printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
				        /*
					 * dump (q->data, q->length); 
					 */
					break;
				}
			}
		}
		ms_biff_query_destroy (q);
		if (ver)
			ms_biff_bof_data_destroy (ver);
		ms_ole_stream_close (stream);
	}
	if (wb)
	{
		workbook_recalc (wb->gnum_wb);
		return wb->gnum_wb;
	}
	return 0;
}
