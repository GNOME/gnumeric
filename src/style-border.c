/* vim: set sw=8: */

/*
 * border.c: Managing drawing and printing cell borders
 *
 * Copyright (C) 1999, 2000, 2001 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "style-border.h"
#include "style-color.h"
#include "style.h"
#include "sheet-style.h"
#include "sheet.h"

struct LineDotPattern {
	gint const		elements;
	unsigned char * const	pattern;
	double * const		pattern_d;
};

static unsigned char dashed_pattern[] = { 3, 1 };
static double dashed_pattern_d[] = { 3., 1. };
static struct LineDotPattern dashed_line =
{ sizeof (dashed_pattern), dashed_pattern, dashed_pattern_d };

static unsigned char med_dashed_pattern[] = { 9, 3 };
static double med_dashed_pattern_d[] = { 9., 3. };
static struct LineDotPattern med_dashed_line =
{ sizeof (med_dashed_pattern), med_dashed_pattern, med_dashed_pattern_d };

static unsigned char dotted_pattern[] = { 2, 2 };
static double dotted_pattern_d[] = { 2., 2. };
static struct LineDotPattern dotted_line =
{ sizeof (dotted_pattern), dotted_pattern, dotted_pattern_d };

static unsigned char hair_pattern[] = { 1, 1 };
static double hair_pattern_d[] = { 1., 1. };
static struct LineDotPattern hair_line =
{ sizeof (hair_pattern), hair_pattern, hair_pattern_d };

static unsigned char dash_dot_pattern[] = { 8, 3, 3, 3 };
static double dash_dot_pattern_d[] = { 8., 3., 3., 3. };
static struct LineDotPattern dash_dot_line =
{ sizeof (dash_dot_pattern), dash_dot_pattern, dash_dot_pattern_d };

static unsigned char med_dash_dot_pattern[] = { 9, 3, 3, 3 };
static double med_dash_dot_pattern_d[] = { 9., 3., 3., 3. };
static struct LineDotPattern med_dash_dot_line =
{ sizeof (med_dash_dot_pattern), med_dash_dot_pattern, med_dash_dot_pattern_d };

static unsigned char dash_dot_dot_pattern[] = { 3, 3, 9, 3, 3, 3 };
static double dash_dot_dot_pattern_d[] = { 3., 3., 9., 3., 3., 3. };
static struct LineDotPattern dash_dot_dot_line =
{ sizeof (dash_dot_dot_pattern), dash_dot_dot_pattern, dash_dot_dot_pattern_d };

static unsigned char med_dash_dot_dot_pattern[] = { 3, 3, 3, 3, 9, 3 };
static double med_dash_dot_dot_pattern_d[] = { 3., 3., 3., 3., 9., 3. };
static struct LineDotPattern med_dash_dot_dot_line =
{ sizeof (med_dash_dot_dot_pattern), med_dash_dot_dot_pattern, med_dash_dot_dot_pattern_d };

static unsigned char slant_pattern[] = { 11, 1, 5, 1 };
static double slant_pattern_d[] = { 11., 1., 5., 1. };
static struct LineDotPattern slant_line =
{ sizeof (slant_pattern), slant_pattern, slant_pattern_d };

struct {
	gint width;
	gint offset;
	struct LineDotPattern const * pattern;
} static const style_border_data[] = {
 	/* 0x0 : STYLE_BORDER_NONE */			{ 0, 0, NULL },
 	/* 0x1 : STYLE_BORDER_THIN */			{ 0, 0, NULL },
 	/* 0x2 : STYLE_BORDER_MEDIUM */			{ 2, 0, NULL },
 	/* 0x3 : STYLE_BORDER_DASHED */			{ 1, 0, &dashed_line },
 	/* 0x4 : STYLE_BORDER_DOTTED */			{ 1, 0, &dotted_line },
 	/* 0x5 : STYLE_BORDER_THICK */			{ 3, 0, NULL },
 	/* 0x6 : STYLE_BORDER_DOUBLE */			{ 0, 0, NULL },
 	/* 0x7 : STYLE_BORDER_HAIR */			{ 1, 0, &hair_line },
	/* 0x8 : STYLE_BORDER_MEDIUM_DASH */		{ 2, 9, &med_dashed_line },
	/* 0x9 : STYLE_BORDER_DASH_DOT */		{ 1, 0, &dash_dot_line },
	/* 0xa : STYLE_BORDER_MEDIUM_DASH_DOT */	{ 2, 17,&med_dash_dot_line },
	/* 0xb : STYLE_BORDER_DASH_DOT_DOT */		{ 1, 0, &dash_dot_dot_line },
	/* 0xc : STYLE_BORDER_MEDIUM_DASH_DOT_DOT */	{ 2, 21,&med_dash_dot_dot_line },
	/* 0xd : STYLE_BORDER_SLANTED_DASH_DOT */	{ 2, 6, &slant_line },/* How to slant */
	/* 0xe : STYLE_BORDER_INCONSISTENT */		{ 3, 0, &hair_line },
};

static GHashTable *border_hash = NULL;

static gint
style_border_equal (gconstpointer v1, gconstpointer v2)
{
	StyleBorder const *k1 = (StyleBorder const *) v1;
	StyleBorder const *k2 = (StyleBorder const *) v2;

	/*
	 * ->color is a pointer, but the comparison is safe because
	 * all colours are cached, see style_color_new.
	 */
	return	(k1->color == k2->color) &&
		(k1->line_type == k2->line_type);
}

static guint
style_border_hash (gconstpointer v)
{
	StyleBorder const *b = (StyleBorder const *) v;

	/*
	 * HACK ALERT!
	 *
	 * ->color is a pointer, but the comparison is safe because
	 * all colours are cached, see style_color_new.
	 *
	 * We assume that casting a pointer to (unsigned) does something
	 * useful.  That's probably ok.
	 */
 	return (((unsigned)b->color) ^ b->line_type);
}

#if 0
	g_hash_table_destroy (border_hash);
	border_hash = NULL;
#endif

StyleBorder *
style_border_none (void)
{
	static StyleBorder * none = NULL;
	if (none == NULL) {
		none = g_new0 (StyleBorder, 1);
		none->line_type = STYLE_BORDER_NONE;
		none->color = style_color_grid ();
		none->begin_margin = none->end_margin = none->width = 0;
		none->ref_count = 1;
	}

	g_return_val_if_fail (none != NULL, NULL);

	return none;
}

/**
 * style_border_fetch :
 *
 * @line_type : dash style
 * @color : colour
 * @orientation : Not currently used.
 *
 * Fetches a StyleBorder from the cache, creating one if necessary.  Absorbs
 * the colour reference.  In the future we may have different dash styles for
 * the same pattern depending on whether this is a horizontal or vertical line.
 */
StyleBorder *
style_border_fetch (StyleBorderType const	 line_type,
		    StyleColor 			*color,
		    StyleBorderOrientation	 orientation)
{
	StyleBorder *border;
	StyleBorder key;

	g_return_val_if_fail (line_type >= STYLE_BORDER_NONE, 0);
	g_return_val_if_fail (line_type < STYLE_BORDER_MAX, 0);

	if (line_type == STYLE_BORDER_NONE) {
		if (color)
			style_color_unref (color);
		return style_border_ref (style_border_none ());
	}

	g_return_val_if_fail (color != NULL, NULL);
	key.line_type = line_type;
	key.color = color;

	if (border_hash) {
		border = g_hash_table_lookup (border_hash, &key);
		if (border != NULL) {
			if (color)
				style_color_unref (color);
			return style_border_ref (border);
		}
	} else
		border_hash = g_hash_table_new (style_border_hash,
						style_border_equal);

	border = g_new0 (StyleBorder, 1);
	*border = key;
	g_hash_table_insert (border_hash, border, border);
	border->ref_count = 1;
	border->gc = NULL;
	border->width = style_border_get_width (line_type);
	if (border->line_type == STYLE_BORDER_DOUBLE) {
		border->begin_margin = 1;
		border->end_margin = 1;
	} else {
		border->begin_margin = (border->width) > 1 ? 1 : 0;
		border->end_margin = (border->width) > 2 ? 1 : 0;
	}

	return border;
}

gboolean
style_border_visible_in_blank (StyleBorder const *border)
{
	g_return_val_if_fail (border != NULL, FALSE);

	return border->line_type != STYLE_BORDER_NONE;
}

gint
style_border_get_width (StyleBorderType const line_type)
{
	g_return_val_if_fail (line_type >= STYLE_BORDER_NONE, 0);
	g_return_val_if_fail (line_type < STYLE_BORDER_MAX, 0);

	if (line_type == STYLE_BORDER_NONE)
		return 0;

	return style_border_data [line_type].width;
}

StyleBorderOrientation
style_border_get_orientation (StyleBorderLocation type)
{
	switch (type) {
	case STYLE_BORDER_LEFT:
	case STYLE_BORDER_RIGHT:
		return STYLE_BORDER_VERTICAL;
	case STYLE_BORDER_DIAG:
	case STYLE_BORDER_REV_DIAG:
		return STYLE_BORDER_DIAGONAL;
	case STYLE_BORDER_TOP:
	case STYLE_BORDER_BOTTOM:
	default:
		return STYLE_BORDER_HORIZONTAL;
	}
}

void
style_border_set_gc_dash (GdkGC *gc, StyleBorderType const i)
{
	GdkLineStyle style = GDK_LINE_SOLID;

	g_return_if_fail (gc != NULL);
	g_return_if_fail (i >= STYLE_BORDER_NONE);
	g_return_if_fail (i < STYLE_BORDER_MAX);

	if (style_border_data[i].pattern != NULL)
		style = GDK_LINE_ON_OFF_DASH;

	/* NOTE : Tricky.  We Use CAP_NOT_LAST because with butt lines
	 * of width > 0 seem to exclude the far point (under Xfree86-4).
	 * The Docs for X11R6 say that NotLast will give the same behavior for
	 * lines of width 0.  Strangely the R5 docs say this for 0 AND 1.
	 */
	gdk_gc_set_line_attributes (gc, style_border_data[i].width, style,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	if (style_border_data[i].pattern != NULL) {
		struct LineDotPattern const * const pat =
			style_border_data[i].pattern;

		gdk_gc_set_dashes (gc, style_border_data[i].offset,
				   pat->pattern, pat->elements);
	}

	/* The background should never be drawn */
	gdk_gc_set_background (gc, &gs_white);
}

static inline GdkGC *
style_border_get_gc (StyleBorder const *border, GdkWindow *window)
{
	if (border == NULL)
		return NULL;

	if (border->gc == NULL) {
		((StyleBorder *)border)->gc = gdk_gc_new (window);
		style_border_set_gc_dash (border->gc, border->line_type);
		gdk_gc_set_foreground (border->gc, &border->color->color);
	}

	return border->gc;
}

static void
style_border_set_pc_dash (StyleBorderType const i,
			  GnomePrintContext *context)
{
	GdkLineStyle style = GDK_LINE_SOLID;
	int w;

	g_return_if_fail (context != NULL);
	g_return_if_fail (i >= STYLE_BORDER_NONE);
	g_return_if_fail (i < STYLE_BORDER_MAX);

	if (i == STYLE_BORDER_NONE)
		return;

	if (style_border_data[i].pattern != NULL)
		style = GDK_LINE_ON_OFF_DASH;

	w = style_border_data[i].width;
	if (w == 0)
		w = 1;
	gnome_print_setlinewidth (context, w);

	if (style_border_data[i].pattern != NULL) {
		struct LineDotPattern const * const pat =
			style_border_data[i].pattern;
		gnome_print_setdash (context, pat->elements,
				     pat->pattern_d, style_border_data[i].offset);
	}
}

static inline gboolean
style_border_set_pc (StyleBorder const * const border,
		     GnomePrintContext *context)
{
	if (border == NULL)
		return FALSE;

	gnome_print_gsave (context);
	style_border_set_pc_dash (border->line_type, context);
	gnome_print_setrgbcolor (context,
				 border->color->red   / (double) 0xffff,
				 border->color->green / (double) 0xffff,
				 border->color->blue  / (double) 0xffff);
	return TRUE;
}

StyleBorder *
style_border_ref (StyleBorder *border)
{
	/* NULL is ok */
	if (border != NULL)
		++border->ref_count;
	return border;
}

void
style_border_unref (StyleBorder *border)
{
	if (border == NULL)
		return;

	g_return_if_fail (border->ref_count > 0);

	border->ref_count--;
	if (border->ref_count != 0)
		return;

	/* Just to be on the safe side.
	 * We are allowed to deref the border_none,
	 * but not to free it.
	 */
	g_return_if_fail (border != style_border_none ());

	/* Remove here, before we mess with the hashed fields.  */
	g_hash_table_remove (border_hash, border);

	if (border->color) {
		style_color_unref (border->color);
		border->color = NULL;
	}

	if (border->gc) {
		gdk_gc_unref (border->gc);
		border->gc = NULL;
	}

	g_free (border);
}

static gboolean
style_border_hmargins (StyleBorder const * const * prev_vert,
		       StyleRow const *sr, int col, 
		       int offsets [2][2])
{
	StyleBorder const *border = sr->top [col];
	StyleBorder const *t0 = prev_vert [col];
	StyleBorder const *t1 = prev_vert [col+1];
	StyleBorder const *b0 = sr->vertical [col];
	StyleBorder const *b1 = sr->vertical [col+1];

	if (border->line_type == STYLE_BORDER_DOUBLE) {
		/* pull inwards or outwards */
		if (!style_border_is_blank (t0)) {
			if (t0->line_type == STYLE_BORDER_DOUBLE)
				offsets [1][0] =  t0->end_margin;
			else
				offsets [1][0] = -t0->begin_margin;
		} else if (!style_border_is_blank (b0))
			offsets [1][0] = -b0->begin_margin;
		else
			offsets [1][0] = 0;

		if (!style_border_is_blank (t1)) {
			if (t1->line_type == STYLE_BORDER_DOUBLE)
				offsets [1][1] = -t1->begin_margin;
			else
				offsets [1][1] =  t1->end_margin;
		} else if (!style_border_is_blank (b1))
			offsets [1][1] =  b1->end_margin;
		else
			offsets [1][1] = 0;

		if (!style_border_is_blank (b0)) {
			if (b0->line_type == STYLE_BORDER_DOUBLE)
				offsets [0][0] =  b0->end_margin;
			else
				offsets [0][0]= -b0->begin_margin;
		} else if (!style_border_is_blank (t0))
			offsets [0][0]= -t0->begin_margin;
		else
			offsets [0][0]= 0;

		if (!style_border_is_blank (b1)) {
			if (b1->line_type == STYLE_BORDER_DOUBLE)
				offsets [0][1] = -b1->begin_margin;
			else
				offsets [0][1] =  b1->end_margin;
		} else if (!style_border_is_blank (t1))
			offsets [0][1] =  t1->end_margin;
		else
			offsets [0][1] = 0;
		return TRUE;
	}

	offsets [0][0] = offsets [0][1] = 0;
	if (border->line_type == STYLE_BORDER_NONE) {
		/* No need to check for show grid.  That is done when the
		 * borders are loaded.  Do not over write background patterns
		 */
		if (!style_border_is_blank (b0))
			offsets [0][0] = 1 + b0->end_margin;
		else if (!style_border_is_blank (t0))
			offsets [0][0] = 1 + t0->end_margin;
		else if (sr->top [col-1] == NULL)
			offsets [0][0] = 1;

		if (!style_border_is_blank (b1))
			offsets [0][1] = -1 - b1->begin_margin;
		else if (!style_border_is_blank (t1))
			offsets [0][1] = -1 - t1->begin_margin;
		else if (sr->top [col+1] == NULL)
			offsets [0][1] = -1;
	} else {
		/* pull outwards */
		if (style_border_is_blank (sr->top [col-1])) {
			int offset = 0;
			if (!style_border_is_blank (b0))
				offset = b0->begin_margin;
			if (!style_border_is_blank (t0)) {
				int tmp = t0->begin_margin;
				if (offset < tmp)
					offset = tmp;
			}
			offsets [0][0] = -offset;
		}

		if (style_border_is_blank (sr->top [col+1])) {
			int offset = 0;
			if (!style_border_is_blank (b1))
				offset = b1->end_margin;
			if (!style_border_is_blank (t1)) {
				int tmp = t1->end_margin;
				if (offset < tmp)
					offset = tmp;
			}
			offsets [0][1] = offset;
		}
	}
	return FALSE;
}

static gboolean
style_border_vmargins (StyleBorder const * const * prev_vert,
		       StyleRow const *sr, int col, 
		       int offsets [2][2])
{
	StyleBorder const *border = sr->vertical [col];
	StyleBorder const *l0 = sr->top [col-1];
	StyleBorder const *r0 = sr->top [col];
	StyleBorder const *l1 = sr->bottom [col-1];
	StyleBorder const *r1 = sr->bottom [col];

	if (border->line_type == STYLE_BORDER_DOUBLE) {
		/* pull inwards or outwards */
		if (!style_border_is_blank (l0))
			offsets [1][0] =  l0->end_margin;
		else if (!style_border_is_blank (r0))
			offsets [1][0] = -r0->begin_margin;
		else
			offsets [1][0] = 0;

		if (!style_border_is_blank (l1))
			offsets [1][1] = -l1->begin_margin;
		else if (!style_border_is_blank (r1))
			offsets [1][1] =  r1->end_margin;
		else
			offsets [1][1] = 0;

		if (!style_border_is_blank (r0))
			offsets [0][0] = r0->end_margin;
		else if (!style_border_is_blank (l0))
			offsets [0][0] = -l0->begin_margin;
		else
			offsets [0][0] = 0;

		if (!style_border_is_blank (r1))
			offsets [0][1] = -r1->begin_margin;
		else if (!style_border_is_blank (l1))
			offsets [0][1] =  l1->end_margin;
		else
			offsets [0][1] = 0;
		return TRUE;
	} 

	offsets [0][0] = offsets [0][1] = 0;
	if (border->line_type == STYLE_BORDER_NONE) {
		/* No need to check for show grid.  That is done when the
		 * borders are loaded.
		 */
		if (!style_border_is_blank (r0))
			offsets [0][0] = 1 + r0->end_margin;
		else if (!style_border_is_blank (l0))
			offsets [0][0] = 1 + l0->end_margin;
		/* Do not over write background patterns */
		else if (prev_vert [col] == NULL)
			offsets [0][0] = 1;

		if (!style_border_is_blank (r1))
			offsets [0][1] = -1 - r1->begin_margin;
		else if (!style_border_is_blank (l1))
			offsets [0][1] = -1 - l1->begin_margin;
		/* Do not over write background patterns */
		else if (sr->vertical [col] == NULL)
			offsets [0][1] = -1;
	} else {
		/* pull inwards */
		int offset = 0;
		if (!style_border_is_blank (r0))
			offset = 1 + r0->end_margin;
		if (!style_border_is_blank (l0)) {
			int tmp = 1 + l0->end_margin;
			if (offset < tmp)
				offset = tmp;
		}
		offsets [0][0] = offset;

		offset = 0;
		if (!style_border_is_blank (r1))
			offset = 1 + r1->begin_margin;
		if (!style_border_is_blank (l1)) {
			int tmp = 1 + l1->begin_margin;
			if (offset < tmp)
				offset = tmp;
		}
		offsets [0][1] = -offset;
	}
	return FALSE;
}

/**
 * style_borders_row_draw :
 *
 * TODO : This is not the final resting place for this.
 * It will move into the gui layer eventually.
 */
void
style_borders_row_draw (StyleBorder const * const * prev_vert,
			StyleRow const *sr,
			GdkDrawable * const drawable,
			int x, int y1, int y2,
			int *colwidths, gboolean draw_vertical)
{
	int o[2][2];
	int col, next_x = x;
	GdkGC *gc;

	for (col = sr->start_col; col <= sr->end_col ; col++, x = next_x) {

		if (colwidths[col] == -1)
			continue;
		next_x = x + colwidths[col];

		gc = style_border_get_gc (sr->top [col], drawable);
		if (gc != NULL) {
			int y = y1;
			if (style_border_hmargins (prev_vert, sr, col, o)) {
				gdk_draw_line (drawable, gc, x + o[1][0], y1-1,
					       next_x + o[1][1] + 1, y1-1);
				++y;
			}

			/* See note in style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x + o[0][0], y,
				       next_x + o[0][1] + 1, y);
		}

		if (!draw_vertical)
			continue;

		gc = style_border_get_gc (sr->vertical [col], drawable);
		if (gc != NULL) {
			int x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				gdk_draw_line (drawable, gc, x-1, y1 + o[1][0],
					       x-1, y2 + o[1][1] + 1);
				++x1;
			}
			/* See note in style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x1, y1 + o[0][0],
				       x1, y2 + o[0][1] + 1);
		}
	}
	if (draw_vertical) {
		gc = style_border_get_gc (sr->vertical [col], drawable);
		if (gc != NULL) {
			int x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				gdk_draw_line (drawable, gc, x-1, y1 + o[1][0],
					       x-1, y2 + o[1][1] + 1);
				++x1;
			}
			/* See note in style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x, y1 + o[0][0],
				       x1, y2 + o[0][1] + 1);
		}
	}
}

void
style_border_draw_diag (MStyle const *style,
			GdkDrawable *drawable,
			int x1, int y1, int x2, int y2)
{
	StyleBorder const *diag;
	GdkGC *gc;

	diag = mstyle_get_border (style, MSTYLE_BORDER_REV_DIAGONAL);
	if (diag != NULL && diag->line_type != STYLE_BORDER_NONE) {
		int a = 0;
		gc = style_border_get_gc (diag, drawable);
		if (diag->line_type == STYLE_BORDER_DOUBLE) {
			gdk_draw_line (drawable, gc, x1, y1+2, x2-2, y2);
			a = 2;
		}
		gdk_draw_line (drawable, gc, x1+a, y1, x2, y2-a);
	}

	diag = mstyle_get_border (style, MSTYLE_BORDER_DIAGONAL);
	if (diag != NULL && diag->line_type != STYLE_BORDER_NONE) {
		int a = 0;
		gc = style_border_get_gc (diag, drawable);
		if (diag->line_type == STYLE_BORDER_DOUBLE) {
			gdk_draw_line (drawable, gc, x1, y2-2, x2-2, y1);
			a = 2;
		}
		gdk_draw_line (drawable, gc, x1+a, y2, x2, y1+a);
	}
}

static inline void
print_hline (GnomePrintContext *context,
	     float x1, float x2, float y, int width)
{
	if (width == 0 || width % 2)
		y -= .5;

	/* exclude far pixel to match gdk */
	gnome_print_moveto (context, x1, y);
	gnome_print_lineto (context, x2, y);
	gnome_print_stroke (context);
}

static inline void
print_vline (GnomePrintContext *context,
	     float x, float y1, float y2, int width)
{
	if (width == 0 || width % 2)
		x += .5;

	/* exclude far pixel to match gdk */
	gnome_print_moveto (context, x, y1);
	gnome_print_lineto (context, x, y2);
	gnome_print_stroke (context);
}

void
style_borders_row_print (StyleBorder const * const * prev_vert,
			 StyleRow const *sr,
			 GnomePrintContext *context,
			 float x, float y1, float y2,
			 Sheet const *sheet, gboolean draw_vertical)
{
	int o[2][2], col;
	float next_x = x;
	StyleBorder const *border;

	for (col = sr->start_col; col <= sr->end_col ; col++, x = next_x) {
		/* TODO : make this sheet agnostic.  Pass in an array of
		 * widths and a flag for whether or not to draw grids.
		 */
		ColRowInfo const *cri = sheet_col_get_info (sheet, col);
		if (!cri->visible)
			continue;
		next_x = x + cri->size_pts;

		border = sr->top [col];
		if (style_border_set_pc (border, context)) {
			float y = y1;
			if (style_border_hmargins (prev_vert, sr, col, o)) {
				print_hline (context, x + o[1][0],
					     next_x + o[1][1] + 1., y1+1., border->width);
				--y;
			}

			print_hline (context, x + o[0][0],
				     next_x + o[0][1] + 1., y, border->width);
			gnome_print_grestore (context);
		}

		if (!draw_vertical)
			continue;
		border = sr->vertical [col];
		if (style_border_set_pc (border, context)) {
			float x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				print_vline (context, x-1., y1 - o[1][0],
					     y2 - o[1][1] - 1., border->width);
				++x1;
			}
			print_vline (context, x1, y1 - o[0][0],
				     y2 - o[0][1] - 1., border->width);
			gnome_print_grestore (context);
		}
	}
	if (draw_vertical) {
		border = sr->vertical [col];
		if (style_border_set_pc (border, context)) {
			float x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				print_vline (context, x-1., y1 - o[1][0] - 1.,
					    y2 - o[1][1], border->width);
				++x1;
			}
			/* See note in style_border_set_gc_dash about +1 */
			print_vline (context, x, y1 - o[0][0],
				     y2 - o[0][1] - 1, border->width);
			gnome_print_grestore (context);
		}
	}
}
