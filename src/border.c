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
{ sizeof(dashed_pattern), dashed_pattern };

static unsigned char med_dashed_pattern[] = { 9, 3 };
static struct LineDotPattern med_dashed_line =
{ sizeof(med_dashed_pattern), med_dashed_pattern };

static unsigned char dotted_pattern[] = { 2, 2 };
static struct LineDotPattern dotted_line =
{ sizeof(dotted_pattern), dotted_pattern };

static unsigned char hair_pattern[] = { 1, 1 };
static struct LineDotPattern hair_line =
{ sizeof(hair_pattern), hair_pattern };

static unsigned char dash_dot_pattern[] = { 8, 3, 3, 3 };
static struct LineDotPattern dash_dot_line =
{ sizeof(dash_dot_pattern), dash_dot_pattern };

static unsigned char med_dash_dot_pattern[] = { 9, 3, 3, 3 };
static struct LineDotPattern med_dash_dot_line =
{ sizeof(med_dash_dot_pattern), med_dash_dot_pattern };

static unsigned char dash_dot_dot_pattern[] = { 3, 3, 9, 3, 3, 3 };
static struct LineDotPattern dash_dot_dot_line =
{ sizeof(dash_dot_dot_pattern), dash_dot_dot_pattern };

static unsigned char med_dash_dot_dot_pattern[] = { 3, 3, 3, 3, 9, 3 };
static struct LineDotPattern med_dash_dot_dot_line =
{ sizeof(med_dash_dot_dot_pattern), med_dash_dot_dot_pattern };

static unsigned char slant_pattern[] = { 11, 1, 5, 1 };
static struct LineDotPattern slant_line =
{ sizeof(slant_pattern), slant_pattern };

struct {
	gint const		          	width;
	gint const			offset;
	struct LineDotPattern const * const	pattern;
} static style_border_data[] = {
 	/* 0x1 : BORDER_THIN */			{ 0, 0, NULL },
 	/* 0x2 : BORDER_MEDIUM */		{ 2, 0, NULL },
 	/* 0x3 : BORDER_DASHED */		{ 0, 0, &dashed_line },
 	/* 0x4 : BORDER_DOTTED */		{ 0, 0, &dotted_line },
 	/* 0x5 : BORDER_THICK */		{ 3, 0, NULL },
 	/* 0x6 : BORDER_DOUBLE */		{ 3, 0, NULL },/* How to clear middle line ?? */
 	/* 0x7 : BORDER_HAIR */			{ 0, 0, &hair_line },
	/* 0x8 : BORDER_MEDIUM_DASH */		{ 2, 9, &med_dashed_line },
	/* 0x9 : BORDER_DASH_DOT */		{ 0, 0, &dash_dot_line },
	/* 0xa : BORDER_MEDIUM_DASH_DOT */	{ 2, 17,&med_dash_dot_line },
	/* 0xb : BORDER_DASH_DOT_DOT */		{ 0, 0, &dash_dot_dot_line },
	/* 0xc : BORDER_MEDIUM_DASH_DOT_DOT */	{ 2, 21,&med_dash_dot_dot_line },
	/* 0xd : BORDER_SLANTED_DASH_DOT */	{ 2, 6, &slant_line },/* How to slant */
};

static GHashTable *style_border_hash = NULL;

static gint
border_equal (gconstpointer v1, gconstpointer v2)
{
	MStyleBorder const *k1 = (MStyleBorder const *) v1;
	MStyleBorder const *k2 = (MStyleBorder const *) v2;

	return	(k1->color == k2->color) && 
		(k1->line_type == k2->line_type) &&
		(k1->is_vertical == k1->is_vertical);
}

static guint
border_hash (gconstpointer v)
{
	MStyleBorder const *b = (MStyleBorder const *) v;

	/* Quick kludge */
 	return (((unsigned)b->color) ^ (b->line_type | (b->is_vertical << 5)));
}

#if 0
	g_hash_table_destroy (style_border_hash);
	style_border_hash = NULL;
#endif

MStyleBorder *
border_fetch (StyleBorderType const	 line_type,
	      StyleColor 		*color,
	      MStyleElementType const	 orientation)
{
	MStyleBorder *border;
	MStyleBorder key;

	key.line_type = line_type;
	key.color = color;
	/* TODO : Will need to expand this when we add diagonals */
	key.is_vertical = (orientation == MSTYLE_BORDER_LEFT ||
			   orientation == MSTYLE_BORDER_RIGHT);

	if (style_border_hash == NULL) {
		style_border_hash = g_hash_table_new (border_hash, border_equal);
	} else {
		border = g_hash_table_lookup (style_border_hash, &key);

		if (border != NULL) {
			++border->ref_count;
			return border;
		}
	}

	border = g_new0 (MStyleBorder, 1);
	*border = key;
	g_hash_table_insert (style_border_hash, border, border);
	border->ref_count = 1;
	border->gc = NULL;

	return border;
}

GdkGC *
border_get_gc (MStyleBorder * border, GdkWindow * window)
{
	g_return_val_if_fail (border != NULL, NULL);

	if (border->gc == NULL) {
		GdkLineStyle style = GDK_LINE_SOLID;
		int i = border->line_type - 1;

		g_return_val_if_fail (border->line_type <= BORDER_NONE, NULL);
		g_return_val_if_fail (border->line_type >= BORDER_MAX, NULL);

		border->gc = gdk_gc_new (window);

		if (style_border_data[i].pattern != NULL)
			style = GDK_LINE_DOUBLE_DASH;
		gdk_gc_set_line_attributes (border->gc,
					    style_border_data[i].width,
					    style,
					    GDK_CAP_BUTT, GDK_JOIN_MITER);

		if (style_border_data[i].pattern != NULL) {
			struct LineDotPattern const * const pat =
				style_border_data[i].pattern;
			gdk_gc_set_dashes (border->gc, style_border_data[i].offset,
					   pat->pattern, pat->elements);
		}
		/* The background should never be drawn */
		gdk_gc_set_background (border->gc, &gs_white);
		gdk_gc_set_foreground (border->gc, &border->color->color);
	}

	return border->gc;
}

void
border_ref (MStyleBorder *border)
{
	g_return_if_fail (border != NULL);
	++border->ref_count;
}

void
border_unref (MStyleBorder *border)
{
	g_return_if_fail (border != NULL);
	g_return_if_fail (border->ref_count > 0);

	border->ref_count--;
	if (border->ref_count != 0)
		return;

	if (border->gc !=NULL)
		gdk_gc_unref (border->gc);

	g_hash_table_remove (style_border_hash, border);
	g_free (border);
}
