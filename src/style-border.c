/*
 * border.c: Managing cell borders
 *
 * Author:
 *  Jody Goldberg (jgoldberg@home.org)
 *
 *  (C) 1999 Jody Goldberg
 */
#include <config.h>
#include "border.h"
#include "color.h"

struct LineDotPattern {
	gint const			elements;
	unsigned char * const	pattern;
};

static unsigned char dashed_pattern[] = { 3, 1 };
static struct LineDotPattern dashed_line =
{ sizeof (dashed_pattern), dashed_pattern };

static unsigned char med_dashed_pattern[] = { 9, 3 };
static struct LineDotPattern med_dashed_line =
{ sizeof (med_dashed_pattern), med_dashed_pattern };

static unsigned char dotted_pattern[] = { 2, 2 };
static struct LineDotPattern dotted_line =
{ sizeof (dotted_pattern), dotted_pattern };

static unsigned char hair_pattern[] = { 1, 1 };
static struct LineDotPattern hair_line =
{ sizeof (hair_pattern), hair_pattern };

static unsigned char dash_dot_pattern[] = { 8, 3, 3, 3 };
static struct LineDotPattern dash_dot_line =
{ sizeof (dash_dot_pattern), dash_dot_pattern };

static unsigned char med_dash_dot_pattern[] = { 9, 3, 3, 3 };
static struct LineDotPattern med_dash_dot_line =
{ sizeof (med_dash_dot_pattern), med_dash_dot_pattern };

static unsigned char dash_dot_dot_pattern[] = { 3, 3, 9, 3, 3, 3 };
static struct LineDotPattern dash_dot_dot_line =
{ sizeof (dash_dot_dot_pattern), dash_dot_dot_pattern };

static unsigned char med_dash_dot_dot_pattern[] = { 3, 3, 3, 3, 9, 3 };
static struct LineDotPattern med_dash_dot_dot_line =
{ sizeof (med_dash_dot_dot_pattern), med_dash_dot_dot_pattern };

static unsigned char slant_pattern[] = { 11, 1, 5, 1 };
static struct LineDotPattern slant_line =
{ sizeof (slant_pattern), slant_pattern };

struct {
	gint const		          	width;
	gint const			offset;
	struct LineDotPattern const * const	pattern;
} static style_border_data[] = {
 	/* 0x1 : STYLE_BORDER_THIN */			{ 0, 0, NULL },
 	/* 0x2 : STYLE_BORDER_MEDIUM */		{ 2, 0, NULL },
 	/* 0x3 : STYLE_BORDER_DASHED */		{ 0, 0, &dashed_line },
 	/* 0x4 : STYLE_BORDER_DOTTED */		{ 0, 0, &dotted_line },
 	/* 0x5 : STYLE_BORDER_THICK */		{ 3, 0, NULL },
 	/* 0x6 : STYLE_BORDER_DOUBLE */		{ 3, 0, NULL },/* How to clear middle line ?? */
 	/* 0x7 : STYLE_BORDER_HAIR */			{ 0, 0, &hair_line },
	/* 0x8 : STYLE_BORDER_MEDIUM_DASH */		{ 2, 9, &med_dashed_line },
	/* 0x9 : STYLE_BORDER_DASH_DOT */		{ 0, 0, &dash_dot_line },
	/* 0xa : STYLE_BORDER_MEDIUM_DASH_DOT */	{ 2, 17,&med_dash_dot_line },
	/* 0xb : STYLE_BORDER_DASH_DOT_DOT */		{ 0, 0, &dash_dot_dot_line },
	/* 0xc : STYLE_BORDER_MEDIUM_DASH_DOT_DOT */	{ 2, 21,&med_dash_dot_dot_line },
	/* 0xd : STYLE_BORDER_SLANTED_DASH_DOT */	{ 2, 6, &slant_line },/* How to slant */
	/* 0xe : STYLE_BORDER_INCONSISTENT */		{ 3, 0, &hair_line },
};

static GHashTable *border_hash = NULL;

static gint
style_border_equal (gconstpointer v1, gconstpointer v2)
{
	MStyleBorder const *k1 = (MStyleBorder const *) v1;
	MStyleBorder const *k2 = (MStyleBorder const *) v2;

	return	(k1->color == k2->color) && 
		(k1->line_type == k2->line_type);
}

static guint
style_border_hash (gconstpointer v)
{
	MStyleBorder const *b = (MStyleBorder const *) v;

	/* Quick kludge */
 	return (((unsigned)b->color) ^ b->line_type);
}

#if 0
	g_hash_table_destroy (border_hash);
	border_hash = NULL;
#endif

MStyleBorder *
style_border_none (void)
{
	static MStyleBorder * none = NULL;
	if (none == NULL) {
		none = g_new0 (MStyleBorder, 1);
		none->line_type = STYLE_BORDER_NONE;
		none->color = style_color_new (0,0,0);
		none->ref_count = 1;
	}

	g_return_val_if_fail (none != NULL, NULL);

	return none;
}

MStyleBorder *
style_border_fetch (StyleBorderType const	 line_type,
		    StyleColor 			*color,
		    StyleBorderOrientation	 orientation)
{
	MStyleBorder *border;
	MStyleBorder key;

	g_return_val_if_fail (line_type >= STYLE_BORDER_NONE, 0);
	g_return_val_if_fail (line_type < STYLE_BORDER_MAX, 0);

	if (line_type == STYLE_BORDER_NONE)
		return style_border_ref (style_border_none ());

	g_return_val_if_fail (color != NULL, NULL);
	key.line_type = line_type;
	key.color = color;

	if (border_hash) {
		border = g_hash_table_lookup (border_hash, &key);
		if (border != NULL)
			return style_border_ref (border);
	} else
		border_hash = g_hash_table_new (style_border_hash,
						style_border_equal);

	border = g_new0 (MStyleBorder, 1);
	*border = key;
	g_hash_table_insert (border_hash, border, border);
	border->ref_count = 1;
	border->gc = NULL;

	return border;
}

gint
style_border_get_width (StyleBorderType const line_type)
{
	g_return_val_if_fail (line_type >= STYLE_BORDER_NONE, 0);
	g_return_val_if_fail (line_type < STYLE_BORDER_MAX, 0);

	if (line_type == STYLE_BORDER_NONE)
		return 0;

	return style_border_data[line_type-1].width;
}

void
style_border_set_gc_dash (GdkGC *gc, StyleBorderType const line_type)
{
	GdkLineStyle style = GDK_LINE_SOLID;
	int i;

	g_return_if_fail (gc != NULL);
	g_return_if_fail (line_type >= STYLE_BORDER_NONE);
	g_return_if_fail (line_type < STYLE_BORDER_MAX);

	if (line_type == STYLE_BORDER_NONE)
		return;

	i = line_type - 1;

	if (style_border_data[i].pattern != NULL)
		style = GDK_LINE_DOUBLE_DASH;

	/* FIXME FIXME FIXME :
	 * We will want to Adjust the join styles eventually to get
	 * corners to render nicely */
	gdk_gc_set_line_attributes (gc,
				    style_border_data[i].width,
				    style,
				    GDK_CAP_BUTT, GDK_JOIN_MITER);

	if (style_border_data[i].pattern != NULL) {
		struct LineDotPattern const * const pat =
			style_border_data[i].pattern;
		gdk_gc_set_dashes (gc, style_border_data[i].offset,
				   pat->pattern, pat->elements);
	}

	/* The background should never be drawn */
	gdk_gc_set_background (gc, &gs_white);
}

GdkGC *
style_border_get_gc (MStyleBorder *border, GdkWindow *window)
{
	g_return_val_if_fail (border != NULL, NULL);

	if (border->gc == NULL) {
		border->gc = gdk_gc_new (window);
		style_border_set_gc_dash (border->gc, border->line_type);
		gdk_gc_set_foreground (border->gc, &border->color->color);
	}

	return border->gc;
}

MStyleBorder *
style_border_ref (MStyleBorder *border)
{
	/* NULL is ok */
	if (border != NULL)
		++border->ref_count;
	return border;
}

void
style_border_unref (MStyleBorder *border)
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

	if (border->gc) {
		gdk_gc_unref (border->gc);
		border->gc = NULL;
	}

	g_hash_table_remove (border_hash, border);

	g_free (border);
}

void
style_border_draw (GdkDrawable * drawable, const MStyleBorder* border,
		   int x1, int y1, int x2, int y2)
{
	if (border != NULL && border->line_type != STYLE_BORDER_NONE)
		gdk_draw_line (drawable,
			       style_border_get_gc ((MStyleBorder *)border,
						    drawable),
			       x1, y1, x2, y2);
}

StyleBorderOrientation
style_border_get_orientation (MStyleElementType type)
{
	switch (type) {
	case MSTYLE_BORDER_LEFT:
	case MSTYLE_BORDER_RIGHT:
		return STYLE_BORDER_VERTICAL;
	case MSTYLE_BORDER_DIAGONAL:
	case MSTYLE_BORDER_REV_DIAGONAL:
		return STYLE_BORDER_DIAGONAL;
	case MSTYLE_BORDER_TOP:
	case MSTYLE_BORDER_BOTTOM:
	default:
		return STYLE_BORDER_HORIZONTAL;
	}
}
