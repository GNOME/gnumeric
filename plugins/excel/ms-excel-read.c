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

#define STRNPRINTF(ptr,n){ int xxxlp; printf ("'") ; for (xxxlp=0;xxxlp<(n);xxxlp++) printf ("%c", (ptr)[xxxlp]) ; printf ("'\n") ; }

/*
 * FIXME: This needs proper unicode support ! current support is a guess 
 */
static char *
biff_get_text (BYTE * ptr, int length)
{
	int lp, unicode;
	char *ans;
	BYTE *inb;

	if (!length)
		return 0;

	ans = (char *) g_malloc (sizeof (char) * length + 1);

	/*
	 * Magic unicode number 
	 */
	unicode = (ptr[0] == 0x1);
	inb = unicode ? ptr + 1 : ptr;

	for (lp = 0; lp < length; lp++){
		ans [lp] = (char) *inb;
		inb += unicode ? 2 : 1;
	}
	ans [lp] = 0;
	return ans;
}

static char *
biff_get_global_string(MS_EXCEL_SHEET *sheet, int number)
{
	char *temp;
	int length, k;
	
	MS_EXCEL_WORKBOOK *wb = sheet->wb;
	
        if (number >= wb->global_string_max)
		return "Too Weird";
	
	temp= wb->global_strings;
        for (k = 0; k < number; k++){
		length= BIFF_GETWORD (temp);
		temp+= length + 3;
        }
        length = BIFF_GETWORD(temp);
        return biff_get_text (temp+3, length);
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

static BIFF_BOUNDSHEET_DATA *
biff_boundsheet_data_new (BIFF_QUERY * q, eBiff_version ver)
{
	BIFF_BOUNDSHEET_DATA *ans = (BIFF_BOUNDSHEET_DATA *) g_malloc (sizeof (BIFF_BOUNDSHEET_DATA));

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
	if (ver == eBiffV8){
		int strlen = BIFF_GETWORD (q->data + 6);

		ans->name = biff_get_text (q->data + 8, strlen);
	} else {
		int strlen = BIFF_GETBYTE (q->data + 6);

		ans->name = biff_get_text (q->data + 7, strlen);
	}
	/*
	 * printf ("Blocksheet : '%s', %d:%d offset %lx\n", ans->name, ans->type, ans->hidden, ans->streamStartPos); 
	 */
	return ans;
}

static void
biff_boundsheet_data_destroy (BIFF_BOUNDSHEET_DATA * d)
{
	g_free (d->name);
	g_free (d);
}

/**
 * NB. 'fount' is the correct, and original _English_
 **/
static BIFF_FONT_DATA *
biff_font_data_new (BIFF_QUERY * q)
{
	BIFF_FONT_DATA *fd = (BIFF_FONT_DATA *) g_malloc (sizeof (BIFF_FONT_DATA));
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
	return fd;
}

static void
biff_font_data_destroy (BIFF_FONT_DATA * p)
{
	g_free (p->fontname);
	g_free (p);
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
	WORD font_idx;
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
	GList *ptr = g_list_first (sheet->wb->font_data);
	int idx = 0;

	g_assert (idx != 4);
	while (ptr){
		if (idx == 4)
			idx++;	/*
				 * Backwards compatibility 
				 */

		if (idx == xf->font_idx){
			BIFF_FONT_DATA *fd = ptr->data;

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
			cell_set_font (cell, font_change_component (cell->style->font->font_name, 1, fd->fontname));
/*			printf ("FoNt [-]: %s\n", cell->style->font->font_name); */
			if (fd->italic){
				cell_set_font (cell, font_get_italic_name (cell->style->font->font_name));
/*				printf ("FoNt [i]: %s\n", cell->style->font->font_name); */
				cell->style->font->hint_is_italic = 1;
			}
			if (fd->boldness == 0x2bc){
				cell_set_font (cell, font_get_bold_name (cell->style->font->font_name));
/*				printf ("FoNt [b]: %s\n", cell->style->font->font_name); */
				cell->style->font->hint_is_bold = 1;
			}
			/*
			 * What about underlining?  
			 */
			g_assert (snprintf (font_size, 16, "%d", fd->height / 2) != -1);
			cell_set_font (cell, font_change_component (cell->style->font->font_name, 7, font_size));

			return;
		}
		idx++;
		ptr = ptr->next;
	}
	printf ("Unknown fount idx %d\n", xf->font_idx);
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
ms_excel_set_cell_xf (MS_EXCEL_SHEET * sheet, Cell * cell, int xfidx)
{
	GList *ptr;
	int cnt;
	BIFF_XF_DATA *xf;
	static int cache_xfidx = 0;
	static BIFF_XF_DATA *cache_ptr   = 0;

	if (xfidx == 0){
		printf ("Normal cell formatting\n");
		return;
	}
	if (xfidx == 15){
		printf ("Default cell formatting\n");
		return;
	}
	xf = cache_ptr;

	if (cache_xfidx != xfidx || !cache_ptr)
	{
		cache_xfidx = xfidx;
		cache_ptr   = 0;
		/*
		 * if (!cell->text) Crash if formatting and no text...
		 * cell_set_text_simple(cell, ""); 
		 */
		ptr = g_list_first (sheet->wb->XF_records);
		/*
		 * printf ("Looking for %d\n", xfidx); 
		 */
		cnt = 16 + 4;		/*
					 * Magic number ... :-)  FIXME - dodgy 
					 */
		while (ptr){
			xf = ptr->data;
	
			if (xf->xftype != eBiffXCell){
				ptr = ptr->next;
				continue;
			}
			if (cnt == xfidx){
				cache_ptr = xf;
				break;
			}
			cnt++;
			ptr = ptr->next;
			if (!ptr)
			{
				printf ("No XF record for %d out of %d found :-(\n", xfidx, cnt);
				return;
			}
		}
	}

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
static BIFF_XF_DATA *
biff_xf_data_new (BIFF_QUERY * q, eBiff_version ver)
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
	return xf;
}

static void
biff_xf_data_destroy (BIFF_XF_DATA * d)
{
	g_free (d);
}

static MS_EXCEL_SHEET *
ms_excel_sheet_new (MS_EXCEL_WORKBOOK * wb, eBiff_version ver, char *name)
{
	MS_EXCEL_SHEET *ans = (MS_EXCEL_SHEET *) g_malloc (sizeof (MS_EXCEL_SHEET));

	ans->gnum_sheet = sheet_new (wb->gnum_wb, name);
	ans->wb = wb;
	ans->ver = ver;
	ans->array_formulae = 0;
	return ans;
}

void
ms_excel_sheet_insert (MS_EXCEL_SHEET * sheet, int xfidx, int col, int row, char *text)
{
	Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);

	cell_set_text (cell, text);
	ms_excel_set_cell_xf (sheet, cell, xfidx);
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

	ans->gnum_wb = NULL;
	ans->boundsheet_data = NULL;
	ans->font_data = NULL;
	ans->excel_sheets = NULL;
	ans->XF_records = NULL;
	ans->palette = NULL;
	ans->global_strings = NULL;
	ans->global_string_max = 0;
	return ans;
}

static void
ms_excel_workbook_attach (MS_EXCEL_WORKBOOK * wb, MS_EXCEL_SHEET * ans)
{
	workbook_attach_sheet (wb->gnum_wb, ans->gnum_sheet);
}

static void
ms_excel_workbook_destroy (MS_EXCEL_WORKBOOK * wb)
{
	GList *ptr = g_list_first (wb->boundsheet_data);

	while (ptr){
		BIFF_BOUNDSHEET_DATA *dat;

		dat = ptr->data;
		biff_boundsheet_data_destroy (dat);
		ptr = ptr->next;
	}
	g_list_free (wb->boundsheet_data);

	ptr = g_list_first (wb->XF_records);
	while (ptr){
		BIFF_XF_DATA *dat;

		dat = ptr->data;
		biff_xf_data_destroy (dat);
		ptr = ptr->next;
	}
	g_list_free (wb->XF_records);

	ptr = g_list_first (wb->font_data);
	while (ptr){
		BIFF_FONT_DATA *dat;

		dat = ptr->data;
		biff_font_data_destroy (dat);
		ptr = ptr->next;
	}
	g_list_free (wb->font_data);

	if (wb->palette)
		ms_excel_palette_destroy (wb->palette);

	g_free (wb);
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
				 * dump (q->data, q->length); 
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
 	case BIFF_HEADER: /* FIXME : S59D94 Microsoft failed to document this correctly  */
 		printf ("Header\n");
		if (q->length)
			dump (q->data+1, BIFF_GETBYTE(q->data+0));
 		break;
 	case BIFF_FOOTER: /* FIXME : S59D8D Microsoft failed to document this correctly  */
 		printf ("Footer\n");
		if (q->length)
			dump (q->data+1, BIFF_GETBYTE(q->data+0));
 		break;
	case BIFF_RSTRING:
		/*
		  printf ("Cell [%d, %d] = ", EX_GETCOL(q), EX_GETROW(q));
		  dump (q->data, q->length);
		  STRNPRINTF(q->data + 8, EX_GETSTRLEN(q)); 
		*/
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
		  biff_get_text (q->data + 8, EX_GETSTRLEN (q)));
		break;
	case BIFF_NUMBER:
		{
			char buf[65];
			double num = BIFF_GETDOUBLE (q->data + 6);	/*
									 * FIXME GETDOUBLE is not endian independant 
									 */
			printf ("A number : %f\n", num);
			dump (q->data, q->length);
			sprintf (buf, "%f", num);
			ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), buf);
			break;
		}
	case BIFF_RK:		/*
				 * FIXME: S59DDA.HTM - test IEEE stuff on other endian platforms 
				 */
		{
			LONG number;
			LONG tmp[2];
			char buf[65];
			double answer;
			enum eType {
				eIEEE = 0, eIEEEx10 = 1, eInt = 2, eIntx100 = 3
			} type;

			number = BIFF_GETLONG (q->data + 6);
			printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length);
			type = (number & 0x3);
			printf ("position [%d,%d] = %x ( type %d )\n", EX_GETCOL (q), EX_GETROW (q), number, type);
			switch (type){
			case eIEEE:
				dump (q->data, q->length);
				tmp[0] = 0;
				tmp[1] = number & 0xfffffffc;
				answer = BIFF_GETDOUBLE (((BYTE *) tmp));
				break;
			case eIEEEx10:
				dump (q->data, q->length);
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
			sprintf (buf, "%f", answer);
			ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), buf);
		}
		break;
	case BIFF_LABEL:
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       biff_get_text (q->data + 8, EX_GETSTRLEN (q)));
		break;
	case BIFF_ROW:		/*
				 * FIXME 
				 */
		/*
		 * printf ("Row %d formatting\n", EX_GETROW(q)); 
		 */
		break;
	case BIFF_FORMULA:	/*
				 * FIXME: S59D8F.HTM 
				 */
	case BIFF_ARRAY:	/*
				 * FIXME: S59D57.HTM 
				 */
		/*
		 * case BIFF_NAME FIXME: S59DA9.HTM 
		 */
		ms_excel_parse_formula (sheet, q, EX_GETCOL (q), EX_GETROW (q));
		break;

	case BIFF_STRING_REF: {
		char *str;

		str = biff_get_global_string (sheet, BIFF_GETLONG (q->data + 6));
					      
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), str);
		g_free (str);
                break;
	}
	
	default:
		printf ("Unrecognised opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
		break;
	}
}

static void
ms_excel_read_sheet (BIFF_QUERY * q, MS_EXCEL_WORKBOOK * wb,
  BIFF_BOUNDSHEET_DATA * bsh, eBiff_version ver)
{
	MS_EXCEL_SHEET *sheet = ms_excel_sheet_new (wb, ver, bsh->name);
	LONG blankSheetPos = q->streamPos + q->length + 4;

	printf ("----------------- Sheet -------------\n");

	while (ms_biff_query_next (q)){
		switch (q->ls_op){
		case BIFF_EOF:
			if (q->streamPos == blankSheetPos){
				printf ("Blank sheet '%s'\n", bsh->name);
				ms_excel_sheet_destroy (sheet);
				return;
			}
			ms_excel_fixup_array_formulae (sheet);
			ms_excel_workbook_attach (wb, sheet);
			return;
			break;
		default:
			ms_excel_read_cell (q, sheet);
			break;
		}
	}
	ms_excel_sheet_destroy (sheet);
	printf ("Error, hit end without EOF\n");
	return;
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
	 * We do not release the resources associated with d */
	g_warning ("Leaking memory here");
	
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
		if (hit){
		  printf ("Found Excel Stream : %s\n", d->name);
		  return ms_ole_stream_open (d, 'r');
		}
	      }
	  }
	printf ("No Excel file found\n");
	return 0;
}

Workbook *
ms_excelReadWorkbook (MS_OLE * file)
{
	MS_EXCEL_WORKBOOK *wb = NULL;
	xmlNodePtr child;

	if (1){
		MS_OLE_STREAM *stream;
		BIFF_QUERY *q;
		BIFF_BOF_DATA *ver = 0;

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

		while (ms_biff_query_next (q)){
			switch (q->ls_op){
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
				} else if (ver->type == eBiffTWorksheet){
					GList *which = g_list_first (wb->boundsheet_data);

					while (which){
						BIFF_BOUNDSHEET_DATA *bsh = which->data;

						if (bsh->streamStartPos == q->streamPos){
							ms_excel_read_sheet (q, wb, bsh, ver->version);
							break;
						}
						which = which->next;
					}
					if (!which)
						printf ("Sheet offset in stream of %x not found in list\n", q->streamPos);
				} else
					printf ("Unknown BOF\n");
			}
			break;
			case BIFF_EOF:
				printf ("End of worksheet spec.\n");
				break;
			case BIFF_BOUNDSHEET:
				{
					BIFF_BOUNDSHEET_DATA *dat = biff_boundsheet_data_new (q, ver->version);

					assert (dat);
					wb->boundsheet_data = g_list_append (wb->boundsheet_data, dat);
				}
				break;
			case BIFF_PALETTE:
				printf ("READ PALETTE\n");
				wb->palette = ms_excel_palette_new (q);
				break;
			case BIFF_FONT:	/*
						 * see S59D8C.HTM 
						 */
				{
					BIFF_FONT_DATA *ptr;

					printf ("Read Font\n");
					dump (q->data, q->length);
					ptr = biff_font_data_new (q);
					wb->font_data = g_list_append (wb->font_data, ptr);
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
				{
					BIFF_XF_DATA *ptr = biff_xf_data_new (q, ver->version);

					/*
					 * printf ("Extended format:\n");
					 * dump (q->data, q->length); 
					 */
					wb->XF_records = g_list_append (wb->XF_records, ptr);
				}
				break;
			case BIFF_STRINGS:
				wb->global_strings= g_malloc(q->length-8);
				memcpy(wb->global_strings, q->data+8, q->length-8);
				wb->global_string_max= BIFF_GETLONG(q->data+4);
				printf("There are apparently %d strings\n",
				       wb->global_string_max);
				dump(wb->global_strings, wb->global_string_max);
				break;
			default:
				printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length);
				/*
				 * dump (q->data, q->length); 
				 */
				break;
			}
		}
		ms_biff_query_destroy (q);
		if (ver)
			ms_biff_bof_data_destroy (ver);
		ms_ole_stream_close (stream);
	}
	if (wb){
		workbook_recalc (wb->gnum_wb);
		return wb->gnum_wb;
	}
	return 0;
}
