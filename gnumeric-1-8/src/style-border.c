/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * border.c: Managing drawing and printing cell borders
 *
 * Copyright (C) 1999-2001 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "style-border.h"

#include "style-color.h"
#include "style.h"
#include "sheet-style.h"
#include "sheet.h"
#include <gdk/gdk.h>

struct LineDotPattern {
	const gint elements;
	const gint8 *const pattern;
	const double *const pattern_d;
};

static const gint8 dashed_pattern[] = { 3, 1 };
static const double dashed_pattern_d[] = { 3., 1. };
static const struct LineDotPattern dashed_line =
{ sizeof (dashed_pattern), dashed_pattern, dashed_pattern_d };

static const gint8 med_dashed_pattern[] = { 9, 3 };
static const double med_dashed_pattern_d[] = { 9., 3. };
static const struct LineDotPattern med_dashed_line =
{ sizeof (med_dashed_pattern), med_dashed_pattern, med_dashed_pattern_d };

static const gint8 dotted_pattern[] = { 2, 2 };
static const double dotted_pattern_d[] = { 2., 2. };
static const struct LineDotPattern dotted_line =
{ sizeof (dotted_pattern), dotted_pattern, dotted_pattern_d };

static const gint8 hair_pattern[] = { 1, 1 };
static const double hair_pattern_d[] = { 1., 1. };
static const struct LineDotPattern hair_line =
{ sizeof (hair_pattern), hair_pattern, hair_pattern_d };

static const gint8 dash_dot_pattern[] = { 8, 3, 3, 3 };
static const double dash_dot_pattern_d[] = { 8., 3., 3., 3. };
static const struct LineDotPattern dash_dot_line =
{ sizeof (dash_dot_pattern), dash_dot_pattern, dash_dot_pattern_d };

static const gint8 med_dash_dot_pattern[] = { 9, 3, 3, 3 };
static const double med_dash_dot_pattern_d[] = { 9., 3., 3., 3. };
static const struct LineDotPattern med_dash_dot_line =
{ sizeof (med_dash_dot_pattern), med_dash_dot_pattern, med_dash_dot_pattern_d };

static const gint8 dash_dot_dot_pattern[] = { 3, 3, 9, 3, 3, 3 };
static const double dash_dot_dot_pattern_d[] = { 3., 3., 9., 3., 3., 3. };
static const struct LineDotPattern dash_dot_dot_line =
{ sizeof (dash_dot_dot_pattern), dash_dot_dot_pattern, dash_dot_dot_pattern_d };

static const gint8 med_dash_dot_dot_pattern[] = { 3, 3, 3, 3, 9, 3 };
static const double med_dash_dot_dot_pattern_d[] = { 3., 3., 3., 3., 9., 3. };
static const struct LineDotPattern med_dash_dot_dot_line =
{ sizeof (med_dash_dot_dot_pattern), med_dash_dot_dot_pattern, med_dash_dot_dot_pattern_d };

static const gint8 slant_pattern[] = { 11, 1, 5, 1 };
static const double slant_pattern_d[] = { 11., 1., 5., 1. };
static const struct LineDotPattern slant_line =
{ sizeof (slant_pattern), slant_pattern, slant_pattern_d };

struct {
	gint width;
	gint offset;
	struct LineDotPattern const * pattern;
} static const style_border_data[] = {
	/* 0x0 : GNM_STYLE_BORDER_NONE */			{ 0, 0, NULL },
	/* 0x1 : GNM_STYLE_BORDER_THIN */			{ 0, 0, NULL },
	/* 0x2 : GNM_STYLE_BORDER_MEDIUM */			{ 2, 0, NULL },
	/* 0x3 : GNM_STYLE_BORDER_DASHED */			{ 1, 0, &dashed_line },
	/* 0x4 : GNM_STYLE_BORDER_DOTTED */			{ 1, 0, &dotted_line },
	/* 0x5 : GNM_STYLE_BORDER_THICK */			{ 3, 0, NULL },
	/* 0x6 : GNM_STYLE_BORDER_DOUBLE */			{ 0, 0, NULL },
	/* 0x7 : GNM_STYLE_BORDER_HAIR */			{ 1, 0, &hair_line },
	/* 0x8 : GNM_STYLE_BORDER_MEDIUM_DASH */		{ 2, 9, &med_dashed_line },
	/* 0x9 : GNM_STYLE_BORDER_DASH_DOT */		{ 1, 0, &dash_dot_line },
	/* 0xa : GNM_STYLE_BORDER_MEDIUM_DASH_DOT */	{ 2, 17,&med_dash_dot_line },
	/* 0xb : GNM_STYLE_BORDER_DASH_DOT_DOT */		{ 1, 0, &dash_dot_dot_line },
	/* 0xc : GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT */	{ 2, 21,&med_dash_dot_dot_line },
	/* 0xd : GNM_STYLE_BORDER_SLANTED_DASH_DOT */	{ 2, 6, &slant_line },/* How to slant */
	/* 0xe : GNM_STYLE_BORDER_INCONSISTENT */		{ 3, 0, &hair_line },
};

static GHashTable *border_hash = NULL;

static gint
style_border_equal (gconstpointer v1, gconstpointer v2)
{
	GnmBorder const *k1 = (GnmBorder const *) v1;
	GnmBorder const *k2 = (GnmBorder const *) v2;

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
	GnmBorder const *b = (GnmBorder const *) v;

	/*
	 * HACK ALERT!
	 *
	 * ->color is a pointer, but the comparison is safe because
	 * all colours are cached, see style_color_new.
	 *
	 */
	return (GPOINTER_TO_UINT(b->color) ^ b->line_type);
}

GnmBorder *
gnm_style_border_none (void)
{
	static GnmBorder * none = NULL;
	if (none == NULL) {
		none = g_new0 (GnmBorder, 1);
		none->line_type = GNM_STYLE_BORDER_NONE;
		none->color = style_color_grid ();
		none->begin_margin = none->end_margin = none->width = 0;
		none->ref_count = 1;
	}

	g_return_val_if_fail (none != NULL, NULL);

	return none;
}

/**
 * gnm_style_border_none_set_color:
 * @color :
 *
 * This function updates the color of gnm_style_border_none when the wanted grid
 * color is known. gnm_style_border_none tells how to render the grid. Because
 * the grid color may be different for different sheets, the functions which
 * render the grid call this function first.  The rule for selecting the
 * grid color, which is the same as in Excel, is: - if the auto pattern
 * color is default (which is black), the grid color is gray, as returned by
 * style_color_grid ().  - otherwise, the auto pattern color is used for the
 * grid.
 * NOTE : Absorbs a reference to @color.
 */
void
gnm_style_border_none_set_color (GnmColor *color)
{
	GnmBorder *none = gnm_style_border_none ();
	GnmColor *nc;

	if (color == none->color) {
		style_color_unref (color);
		return;
	}

	nc = none->color;
	none->color = color;
	style_color_unref (nc);

	if (none->gc) {
		gdk_gc_set_rgb_fg_color (none->gc, &none->color->gdk_color);
	}
}

/**
 * gnm_style_border_fetch :
 *
 * @line_type : dash style
 * @color : colour
 * @orientation : Not currently used.
 *
 * Fetches a GnmBorder from the cache, creating one if necessary.  Absorbs
 * the colour reference.  In the future we may have different dash styles for
 * the same pattern depending on whether this is a horizontal or vertical line.
 */
GnmBorder *
gnm_style_border_fetch (GnmStyleBorderType		 line_type,
			GnmColor			*color,
			GnmStyleBorderOrientation	 orientation)
{
	GnmBorder *border;
	GnmBorder key;

	if (line_type < GNM_STYLE_BORDER_NONE || line_type > GNM_STYLE_BORDER_MAX) {
		g_warning ("Invalid border type: %d", line_type);
		line_type = GNM_STYLE_BORDER_NONE;
	}

	if (line_type == GNM_STYLE_BORDER_NONE) {
		if (color)
			style_color_unref (color);
		return gnm_style_border_ref (gnm_style_border_none ());
	}

	g_return_val_if_fail (color != NULL, NULL);
	key.line_type = line_type;
	key.color = color;

	if (border_hash) {
		border = g_hash_table_lookup (border_hash, &key);
		if (border != NULL) {
			if (color)
				style_color_unref (color);
			return gnm_style_border_ref (border);
		}
	} else
		border_hash = g_hash_table_new (style_border_hash,
						style_border_equal);

	border = g_new0 (GnmBorder, 1);
	*border = key;
	g_hash_table_insert (border_hash, border, border);
	border->ref_count = 1;
	border->gc = NULL;
	border->gc_screen = NULL;
	border->width = gnm_style_border_get_width (line_type);
	if (border->line_type == GNM_STYLE_BORDER_DOUBLE) {
		border->begin_margin = 1;
		border->end_margin = 1;
	} else {
		border->begin_margin = (border->width) > 1 ? 1 : 0;
		border->end_margin = (border->width) > 2 ? 1 : 0;
	}

	return border;
}

gboolean
gnm_style_border_visible_in_blank (GnmBorder const *border)
{
	g_return_val_if_fail (border != NULL, FALSE);

	return border->line_type != GNM_STYLE_BORDER_NONE;
}

gint
gnm_style_border_get_width (GnmStyleBorderType const line_type)
{
	g_return_val_if_fail (line_type >= GNM_STYLE_BORDER_NONE, 0);
	g_return_val_if_fail (line_type < GNM_STYLE_BORDER_MAX, 0);

	if (line_type == GNM_STYLE_BORDER_NONE)
		return 0;

	return style_border_data [line_type].width;
}

GnmStyleBorderOrientation
gnm_style_border_get_orientation (GnmStyleBorderLocation type)
{
	switch (type) {
	case GNM_STYLE_BORDER_LEFT:
	case GNM_STYLE_BORDER_RIGHT:
		return GNM_STYLE_BORDER_VERTICAL;
	case GNM_STYLE_BORDER_DIAG:
	case GNM_STYLE_BORDER_REV_DIAG:
		return GNM_STYLE_BORDER_DIAGONAL;
	case GNM_STYLE_BORDER_TOP:
	case GNM_STYLE_BORDER_BOTTOM:
	default:
		return GNM_STYLE_BORDER_HORIZONTAL;
	}
}

void
gnm_style_border_set_gc_dash (GdkGC *gc, GnmStyleBorderType const i)
{
	GdkLineStyle style = GDK_LINE_SOLID;

	g_return_if_fail (gc != NULL);
	g_return_if_fail (i >= GNM_STYLE_BORDER_NONE);
	g_return_if_fail (i < GNM_STYLE_BORDER_MAX);

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
				   (gint8 *)pat->pattern, pat->elements);
	}

	/* The background should never be drawn */
	gdk_gc_set_rgb_bg_color (gc, &gs_white);
}

static inline GdkGC *
style_border_get_gc (GnmBorder const *border, GdkDrawable *drawable)
{
	GdkScreen *this_screen;
	if (border == NULL)
		return NULL;

	this_screen = gdk_drawable_get_screen (drawable);
	if (border->gc_screen != this_screen) {
		if (border->gc)
			g_object_unref (G_OBJECT (border->gc));
		if (border->gc_screen)
			g_object_unref (G_OBJECT (border->gc_screen));
		((GnmBorder *)border)->gc = gdk_gc_new (drawable);
		((GnmBorder *)border)->gc_screen = this_screen;
		g_object_ref (this_screen);
		gnm_style_border_set_gc_dash (border->gc, border->line_type);
		gdk_gc_set_rgb_fg_color (border->gc, &border->color->gdk_color);
	}

	return border->gc;
}


GnmBorder *
gnm_style_border_ref (GnmBorder *border)
{
	/* NULL is ok */
	if (border != NULL)
		++border->ref_count;
	return border;
}

void
gnm_style_border_unref (GnmBorder *border)
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
	g_return_if_fail (border != gnm_style_border_none ());

	/* Remove here, before we mess with the hashed fields.  */
	g_hash_table_remove (border_hash, border);

	if (border->color) {
		style_color_unref (border->color);
		border->color = NULL;
	}

	if (border->gc) {
		g_object_unref (G_OBJECT (border->gc));
		border->gc = NULL;
	}

	if (border->gc_screen) {
		g_object_unref (G_OBJECT (border->gc_screen));
		border->gc_screen = NULL;
	}

	g_free (border);
}

static gboolean
style_border_hmargins (GnmBorder const * const * prev_vert,
		       GnmStyleRow const *sr, int col,
		       int offsets [2][2], int dir)
{
	GnmBorder const *border = sr->top [col];
	GnmBorder const *t0 = prev_vert [col];
	GnmBorder const *t1 = prev_vert [col+1];
	GnmBorder const *b0 = sr->vertical [col];
	GnmBorder const *b1 = sr->vertical [col+1];

	if (border->line_type == GNM_STYLE_BORDER_DOUBLE) {
		/* pull inwards or outwards */
		if (!gnm_style_border_is_blank (t0)) {
			if (t0->line_type == GNM_STYLE_BORDER_DOUBLE)
				offsets [1][0] =  dir * t0->end_margin;
			else
				offsets [1][0] = -dir * t0->begin_margin;
		} else if (!gnm_style_border_is_blank (b0))
			offsets [1][0] = -dir * b0->begin_margin;
		else
			offsets [1][0] = 0;

		if (!gnm_style_border_is_blank (t1)) {
			if (t1->line_type == GNM_STYLE_BORDER_DOUBLE)
				offsets [1][1] = -dir * t1->begin_margin;
			else
				offsets [1][1] =  dir * t1->end_margin;
		} else if (!gnm_style_border_is_blank (b1))
			offsets [1][1] =  dir * b1->end_margin;
		else
			offsets [1][1] = 0;

		if (!gnm_style_border_is_blank (b0)) {
			if (b0->line_type == GNM_STYLE_BORDER_DOUBLE)
				offsets [0][0] =  dir * b0->end_margin;
			else
				offsets [0][0]= -dir * b0->begin_margin;
		} else if (!gnm_style_border_is_blank (t0))
			offsets [0][0]= -dir * t0->begin_margin;
		else
			offsets [0][0]= 0;

		if (!gnm_style_border_is_blank (b1)) {
			if (b1->line_type == GNM_STYLE_BORDER_DOUBLE)
				offsets [0][1] = -dir * b1->begin_margin;
			else
				offsets [0][1] =  dir * b1->end_margin;
		} else if (!gnm_style_border_is_blank (t1))
			offsets [0][1] =  dir * t1->end_margin;
		else
			offsets [0][1] = 0;
		return TRUE;
	}

	offsets [0][0] = offsets [0][1] = 0;
	if (border->line_type == GNM_STYLE_BORDER_NONE) {
		/* No need to check for show grid.  That is done when the
		 * borders are loaded.  Do not over write background patterns
		 */
		if (!gnm_style_border_is_blank (b0))
			offsets [0][0] = dir *(1 + b0->end_margin);
		else if (!gnm_style_border_is_blank (t0))
			offsets [0][0] = dir *(1 + t0->end_margin);
		else if (sr->top [col-1] == NULL)
			offsets [0][0] = dir;

		if (!gnm_style_border_is_blank (b1))
			offsets [0][1] = -dir * (1 - b1->begin_margin);
		else if (!gnm_style_border_is_blank (t1))
			offsets [0][1] = -dir * (1 - t1->begin_margin);
		else if (sr->top [col+1] == NULL)
			offsets [0][1] = -dir;
	} else {
		/* pull outwards */
		if (gnm_style_border_is_blank (sr->top [col-1])) {
			int offset = 0;
			if (!gnm_style_border_is_blank (b0))
				offset = b0->begin_margin;
			if (!gnm_style_border_is_blank (t0)) {
				int tmp = t0->begin_margin;
				if (offset < tmp)
					offset = tmp;
			}
			offsets [0][0] = -dir * offset;
		}

		if (gnm_style_border_is_blank (sr->top [col+1])) {
			int offset = 0;
			if (!gnm_style_border_is_blank (b1))
				offset = b1->end_margin;
			if (!gnm_style_border_is_blank (t1)) {
				int tmp = t1->end_margin;
				if (offset < tmp)
					offset = tmp;
			}
			offsets [0][1] = dir * offset;
		}
	}
	return FALSE;
}

static gboolean
style_border_vmargins (GnmBorder const * const * prev_vert,
		       GnmStyleRow const *sr, int col,
		       int offsets [2][2])
{
	GnmBorder const *border = sr->vertical [col];
	GnmBorder const *l0 = sr->top [col-1];
	GnmBorder const *r0 = sr->top [col];
	GnmBorder const *l1 = sr->bottom [col-1];
	GnmBorder const *r1 = sr->bottom [col];

	if (border->line_type == GNM_STYLE_BORDER_DOUBLE) {
		/* pull inwards or outwards */
		if (!gnm_style_border_is_blank (l0))
			offsets [1][0] =  l0->end_margin;
		else if (!gnm_style_border_is_blank (r0))
			offsets [1][0] = -r0->begin_margin;
		else
			offsets [1][0] = 0;

		if (!gnm_style_border_is_blank (l1))
			offsets [1][1] = -l1->begin_margin;
		else if (!gnm_style_border_is_blank (r1))
			offsets [1][1] =  r1->end_margin;
		else
			offsets [1][1] = 0;

		if (!gnm_style_border_is_blank (r0))
			offsets [0][0] = r0->end_margin;
		else if (!gnm_style_border_is_blank (l0))
			offsets [0][0] = -l0->begin_margin;
		else
			offsets [0][0] = 0;

		if (!gnm_style_border_is_blank (r1))
			offsets [0][1] = -r1->begin_margin;
		else if (!gnm_style_border_is_blank (l1))
			offsets [0][1] =  l1->end_margin;
		else
			offsets [0][1] = 0;
		return TRUE;
	}

	offsets [0][0] = offsets [0][1] = 0;
	if (border->line_type == GNM_STYLE_BORDER_NONE) {
		/* No need to check for show grid.  That is done when the
		 * borders are loaded.
		 */
		if (!gnm_style_border_is_blank (r0))
			offsets [0][0] = 1 + r0->end_margin;
		else if (!gnm_style_border_is_blank (l0))
			offsets [0][0] = 1 + l0->end_margin;
		/* Do not over write background patterns */
		else if (prev_vert [col] == NULL)
			offsets [0][0] = 1;

		if (!gnm_style_border_is_blank (r1))
			offsets [0][1] = -1 - r1->begin_margin;
		else if (!gnm_style_border_is_blank (l1))
			offsets [0][1] = -1 - l1->begin_margin;
		/* Do not over write background patterns */
		else if (sr->vertical [col] == NULL)
			offsets [0][1] = -1;
	} else {
		/* pull inwards */
		int offset = 0;
		if (!gnm_style_border_is_blank (r0))
			offset = 1 + r0->end_margin;
		if (!gnm_style_border_is_blank (l0)) {
			int tmp = 1 + l0->end_margin;
			if (offset < tmp)
				offset = tmp;
		}
		offsets [0][0] = offset;

		offset = 0;
		if (!gnm_style_border_is_blank (r1))
			offset = 1 + r1->begin_margin;
		if (!gnm_style_border_is_blank (l1)) {
			int tmp = 1 + l1->begin_margin;
			if (offset < tmp)
				offset = tmp;
		}
		offsets [0][1] = -offset;
	}
	return FALSE;
}

/**
 * gnm_style_borders_row_draw :
 *
 * TODO : This is not the final resting place for this.
 * It will move into the gui layer eventually.
 */
void
gnm_style_borders_row_draw (GnmBorder const * const * prev_vert,
			    GnmStyleRow const *sr,
			    GdkDrawable * const drawable,
			    int x, int y1, int y2,
			    int *colwidths,
			    gboolean draw_vertical, int dir)
{
	int o[2][2];
	int col, next_x = x;
	GdkGC *gc;

	for (col = sr->start_col; col <= sr->end_col ; col++, x = next_x) {

		if (colwidths[col] == -1)
			continue;
		next_x = x + dir * colwidths[col];

		gc = style_border_get_gc (sr->top [col], drawable);
		if (gc != NULL) {
			int y = y1;
			if (style_border_hmargins (prev_vert, sr, col, o, dir)) {
				gdk_draw_line (drawable, gc, x + o[1][0], y1-1,
					       next_x + o[1][1] + dir, y1-1);
				++y;
			}

			/* See note in gnm_style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x + o[0][0], y,
				       next_x + o[0][1] + dir, y);
		}

		if (!draw_vertical)
			continue;

		gc = style_border_get_gc (sr->vertical [col], drawable);
		if (gc != NULL) {
			int x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				gdk_draw_line (drawable, gc, x-dir, y1 + o[1][0],
					       x-dir, y2 + o[1][1] + 1);
				x1 += dir;
			}
			/* See note in gnm_style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x1, y1 + o[0][0],
				       x1, y2 + o[0][1] + 1);
		}
	}
	if (draw_vertical) {
		gc = style_border_get_gc (sr->vertical [col], drawable);
		if (gc != NULL) {
			int x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				gdk_draw_line (drawable, gc, x-dir, y1 + o[1][0],
					       x-dir, y2 + o[1][1] + 1);
				x1 += dir;
			}
			/* See note in gnm_style_border_set_gc_dash about +1 */
			gdk_draw_line (drawable, gc, x1, y1 + o[0][0],
				       x1, y2 + o[0][1] + 1);
		}
	}
}

void
gnm_style_border_draw_diag (GnmStyle const *style,
			    GdkDrawable *drawable,
			    int x1, int y1, int x2, int y2)
{
	GnmBorder const *diag;
	GdkGC *gc;

	diag = gnm_style_get_border (style, MSTYLE_BORDER_REV_DIAGONAL);
	if (diag != NULL && diag->line_type != GNM_STYLE_BORDER_NONE) {
		gc = style_border_get_gc (diag, drawable);
		if (diag->line_type == GNM_STYLE_BORDER_DOUBLE) {
			gdk_draw_line (drawable, gc, x1+1, y1+3, x2-3, y2-1);
			gdk_draw_line (drawable, gc, x1+3, y1+1, x2-1, y2-3);
		} else
			gdk_draw_line (drawable, gc, x1, y1, x2, y2);
	}

	diag = gnm_style_get_border (style, MSTYLE_BORDER_DIAGONAL);
	if (diag != NULL && diag->line_type != GNM_STYLE_BORDER_NONE) {
		gc = style_border_get_gc (diag, drawable);
		if (diag->line_type == GNM_STYLE_BORDER_DOUBLE) {
			gdk_draw_line (drawable, gc, x1+1, y2-3, x2-3, y1+1);
			gdk_draw_line (drawable, gc, x1+3, y2-1, x2-1, y1+3);
		} else
			gdk_draw_line (drawable, gc, x1, y2, x2, y1);
	}
}

static void
style_border_set_gtk_dash (GnmStyleBorderType const i,
			   cairo_t *context)
{
	GdkLineStyle style = GDK_LINE_SOLID;
	int w;

	g_return_if_fail (context != NULL);
	g_return_if_fail (i >= GNM_STYLE_BORDER_NONE);
	g_return_if_fail (i < GNM_STYLE_BORDER_MAX);

	if (style_border_data[i].pattern != NULL)
		style = GDK_LINE_ON_OFF_DASH;

	w = style_border_data[i].width;
	if (w == 0)
		w = 1;
	cairo_set_line_width (context,((double) w));

	if (style_border_data[i].pattern != NULL) {
		struct LineDotPattern const * const pat =
			style_border_data[i].pattern;
		cairo_set_dash (context, pat->pattern_d, pat->elements,
				style_border_data[i].offset);
	} else
		cairo_set_dash (context, NULL, 0, 0);
}

static inline gboolean
style_border_set_gtk (GnmBorder const * const border,
		      cairo_t *context)
{
	if (border == NULL)
		return FALSE;

	style_border_set_gtk_dash (border->line_type, context);
	cairo_set_source_rgb (context,
			      border->color->gdk_color.red   / (double) 0xffff,
			      border->color->gdk_color.green / (double) 0xffff,
			      border->color->gdk_color.blue  / (double) 0xffff);
	return TRUE;
}

static inline void
print_hline_gtk (cairo_t *context,
		 float x1, float x2, float y, int width)
{
	if (width == 0 || width % 2)
		y += .5;

	/* exclude far pixel to match gdk */
	cairo_move_to (context, x1, y);
	cairo_line_to (context, x2, y);
	cairo_stroke (context);
}

static inline void
print_vline_gtk (cairo_t *context,
		 float x, float y1, float y2, int width, int dir)
{
	if (width == 0 || width % 2)
		x += .5*dir;

	/* exclude far pixel to match gdk */
	cairo_move_to (context, x, y1);
	cairo_line_to (context, x, y2);
	cairo_stroke (context);
}

void
gnm_style_borders_row_print_gtk (GnmBorder const * const * prev_vert,
				 GnmStyleRow const *sr,
				 cairo_t *context,
				 float x, float y1, float y2,
				 Sheet const *sheet,
				 gboolean draw_vertical, int dir)
{
	int o[2][2], col;
	float next_x = x;
	GnmBorder const *border;

	cairo_save (context);

	for (col = sr->start_col; col <= sr->end_col ; col++, x = next_x) {
		/* TODO : make this sheet agnostic.  Pass in an array of
		 * widths and a flag for whether or not to draw grids.
		 */
		ColRowInfo const *cri = sheet_col_get_info (sheet, col);
		if (!cri->visible)
			continue;
		next_x = x + dir * cri->size_pts;

		border = sr->top [col];

		if (style_border_set_gtk (border, context)) {
			float y = y1;
			if (style_border_hmargins (prev_vert, sr, col, o, dir)) {
				print_hline_gtk (context, x + o[1][0],
						 next_x + o[1][1] + dir, y1-1.,
						 border->width);
				++y;
			}

			print_hline_gtk (context, x + o[0][0],
					 next_x + o[0][1] + dir, y, border->width);
		}

		if (!draw_vertical)
			continue;


		border = sr->vertical [col];
		if (style_border_set_gtk (border, context)) {
			float x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				print_vline_gtk (context, x-dir, y1 + o[1][0],
						 y2 + o[1][1] + 1., border->width, dir);
				x1 += dir;
			}
			print_vline_gtk (context, x1, y1 + o[0][0],
					 y2 + o[0][1] + 1., border->width, dir);
		}
	}
	if (draw_vertical) {
		border = sr->vertical [col];
		if (style_border_set_gtk (border, context)) {
			float x1 = x;
			if (style_border_vmargins (prev_vert, sr, col, o)) {
				print_vline_gtk (context, x-dir, y1 + o[1][0] + 1.,
						 y2 + o[1][1], border->width, dir);
				x1 += dir;
			}
			/* See note in gnm_style_border_set_gc_dash about +1 */
			print_vline_gtk (context, x1, y1 + o[0][0],
					 y2 + o[0][1] + 1, border->width, dir);
		}
	}

	cairo_restore (context);
}

void
gnm_style_border_print_diag_gtk (GnmStyle const *style,
			     cairo_t *context,
			     float x1, float y1, float x2, float y2)
{
	GnmBorder const *diag;


	cairo_save (context);

	diag = gnm_style_get_border (style, MSTYLE_BORDER_REV_DIAGONAL);
	if (diag != NULL && diag->line_type != GNM_STYLE_BORDER_NONE) {
		style_border_set_gtk (diag, context);
		if (diag->line_type == GNM_STYLE_BORDER_DOUBLE) {
			cairo_move_to (context, x1+1.5,  y1+3.);
			cairo_line_to (context, x2-2.,   y2- .5);
			cairo_stroke (context);
			cairo_move_to (context, x1+ 3.,  y1+1.5);
			cairo_line_to (context, x2-  .5, y2-2.);
		} else {
			cairo_move_to (context, x1+.5, y1+.5);
			cairo_line_to (context, x2+.5, y2+.5);
		}
		cairo_stroke (context);
	}

	diag = gnm_style_get_border (style, MSTYLE_BORDER_DIAGONAL);
	if (diag != NULL && diag->line_type != GNM_STYLE_BORDER_NONE) {
		style_border_set_gtk (diag, context);
		if (diag->line_type == GNM_STYLE_BORDER_DOUBLE) {
			cairo_move_to (context, x1+1.5, y2-2.);
			cairo_line_to (context, x2-2.,  y1+1.5);
			cairo_stroke (context);
			cairo_move_to (context, x1+3.,  y2- .5);
			cairo_line_to (context, x2- .5, y1+3.);
		} else {
			cairo_move_to (context, x1+.5, y2+.5);
			cairo_line_to (context, x2+.5, y1+.5);
		}
		cairo_stroke (context);
	}

	cairo_restore (context);
}
