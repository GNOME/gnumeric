/*
 * border.c: Managing cell borders
 *
 * Author:
 *  Jody Goldberg (jgoldberg@home.org)
 *
 *  (C) 1999, 2000 Jody Goldberg
 */
#include <config.h>
#include "border.h"
#include "color.h"

struct LineDotPattern {
	gint const elements;
	const unsigned char * const	pattern;
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
	gint const		          	width;
	gint const			offset;
	struct LineDotPattern const * const	pattern;
} static style_border_data[] = {
 	/* 0x1 : STYLE_BORDER_THIN */			{ 0, 0, NULL },
 	/* 0x2 : STYLE_BORDER_MEDIUM */		{ 2, 0, NULL },
 	/* 0x3 : STYLE_BORDER_DASHED */		{ 0, 0, &dashed_line },
 	/* 0x4 : STYLE_BORDER_DOTTED */		{ 0, 0, &dotted_line },
 	/* 0x5 : STYLE_BORDER_THICK */		{ 3, 0, NULL },
 	/* 0x6 : STYLE_BORDER_DOUBLE */		{ 0, 0, NULL },
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
		style = GDK_LINE_ON_OFF_DASH;

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

static void
style_border_set_pc_dash (StyleBorderType const line_type,
			  GnomePrintContext *context)
{
	GdkLineStyle style = GDK_LINE_SOLID;
	int i;

	g_return_if_fail (context != NULL);
	g_return_if_fail (line_type >= STYLE_BORDER_NONE);
	g_return_if_fail (line_type < STYLE_BORDER_MAX);

	if (line_type == STYLE_BORDER_NONE)
		return;

	i = line_type - 1;

	if (style_border_data[i].pattern != NULL)
		style = GDK_LINE_ON_OFF_DASH;

#if 0
	/* FIXME FIXME FIXME :
	 * We will want to Adjust the join styles eventually to get
	 * corners to render nicely */
	gdk_gc_set_line_attributes (gc,
				    style_border_data[i].width,
				    style,
				    GDK_CAP_BUTT, GDK_JOIN_MITER);
#endif
	gnome_print_setlinewidth (context, style_border_data[i].width);

	if (style_border_data[i].pattern != NULL) {
		struct LineDotPattern const * const pat =
			style_border_data[i].pattern;
		gnome_print_setdash (context, pat->elements,
				     pat->pattern_d, style_border_data[i].offset);
	}
}

static void
style_border_set_pc (MStyleBorder const * const border, GnomePrintContext *context)
{
	style_border_set_pc_dash (border->line_type, context);
	gnome_print_setrgbcolor (context,
				 border->color->red   / (double) 0xffff,
				 border->color->green / (double) 0xffff,
				 border->color->blue  / (double) 0xffff);
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
style_border_draw (MStyleBorder const * const border, MStyleElementType const t,
		   GdkDrawable * const drawable,
		   int x1, int y1, int x2, int y2,
		   MStyleBorder const * const extend_begin,
		   MStyleBorder const * const extend_end)
{
	if (border != NULL && border->line_type != STYLE_BORDER_NONE) {
		/* Cast away const */
		GdkGC *gc = style_border_get_gc ((MStyleBorder *)border, drawable);

		/* This is WRONG.  FIXME FIXME FIXME
		 * when we are finished converting to drawing only top & left
		 * then rework the state table.
		 */
		if (border->line_type == STYLE_BORDER_DOUBLE) {
			static int const offsets[][2][4] = {
			    { { 0,-1,0,-1}, { 0,1,0,1} }, /* TOP */
			    { { 0,-1,0,-1}, { 0,1,0,1} }, /* BOTTOM */
			    { { -1,0,-1,0}, { 1,0,1,0} }, /* LEFT */
			    { { -1,0,-1,0}, { 1,0,1,0} }, /* RIGHT */
			    { { 0,1,-1,0}, { 1,0,0,-1} }, /* DIAGONAL */
			    { { 0,-1,-1,0 },{ 1,0,0,1} }, /* REV_DIAGONAL */
			};
			static int const extension_begin[][2][2] = {
			    { { -1, 0 }, { 1, 0 } }, /* TOP */
			    { {  1, 0 }, { -1, 0 } }, /* BOTTOM */
			    { { 0, -1 }, { 0, 1} }, /* LEFT */
			    { { 0, 1 }, { 0, -1} }, /* RIGHT */
			    { { 1, 1}, { 1, 1 } }, /* DIAGONAL */
			    { { -1, -1}, { -1, -1} }, /* REV_DIAGONAL */
			};

			int const i = t-MSTYLE_BORDER_TOP;
			int const * const o = (int *)&(offsets[i]);
			int x = x1+o[0], y = y1+o[1];

			if (extend_begin != NULL &&
			    extend_begin->line_type != STYLE_BORDER_NONE) {
				x += extension_begin[i][0][0];
				y += extension_begin[i][0][1];
			}

			gdk_draw_line (drawable, gc, x, y, x2+o[2], y2+o[3]);
			x1 += o[4]; y1 += o[5]; x2 += o[6]; y2 += o[7];

			if (extend_begin != NULL &&
			    extend_begin->line_type != STYLE_BORDER_NONE) {
				x1 += extension_begin[i][1][0];
				y1 += extension_begin[i][1][1];
			}
		}
		gdk_draw_line (drawable, gc, x1, y1, x2, y2);
	}
}

void
style_border_print (MStyleBorder const * const border, MStyleElementType const t,
		    GnomePrintContext *context,
		    double x1, double y1, double x2, double y2,
		    MStyleBorder const * const extend_begin,
		    MStyleBorder const * const extend_end)
{
	if (border != NULL && border->line_type != STYLE_BORDER_NONE) {

		gnome_print_gsave (context);

		style_border_set_pc (border, context);

		/* This is WRONG.  FIXME FIXME FIXME
		 * when we are finished converting to drawing only top & left
		 * then rework the state table.
		 */
		if (border->line_type == STYLE_BORDER_DOUBLE) {
			static int const offsets[][2][4] = {
			    { { 0,-1,0,-1}, { 0,1,0,1} }, /* TOP */
			    { { 0,-1,0,-1}, { 0,1,0,1} }, /* BOTTOM */
			    { { -1,0,-1,0}, { 1,0,1,0} }, /* LEFT */
			    { { -1,0,-1,0}, { 1,0,1,0} }, /* RIGHT */
			    { { 0,1,-1,0}, { 1,0,0,-1} }, /* DIAGONAL */
			    { { 0,-1,-1,0 },{ 1,0,0,1} }, /* REV_DIAGONAL */
			};
			static int const extension_begin[][2][2] = {
			    { { -1, 0 }, { 1, 0 } }, /* TOP */
			    { {  1, 0 }, { -1, 0 } }, /* BOTTOM */
			    { { 0, -1 }, { 0, 1} }, /* LEFT */
			    { { 0, 1 }, { 0, -1} }, /* RIGHT */
			    { { 1, 1}, { 1, 1 } }, /* DIAGONAL */
			    { { -1, -1}, { -1, -1} }, /* REV_DIAGONAL */
			};

			int const i = t-MSTYLE_BORDER_TOP;
			int const * const o = (int *)&(offsets[i]);
			double x = x1+o[0], y = y1-o[1];

			if (extend_begin != NULL &&
			    extend_begin->line_type != STYLE_BORDER_NONE) {
				x += extension_begin[i][0][0];
				y -= extension_begin[i][0][1];
			}

			gnome_print_moveto (context, x, y);
			gnome_print_lineto (context, x2+o[2], y2-o[3]);
			gnome_print_stroke (context);
			x1 += o[4]; y1 -= o[5]; x2 += o[6]; y2 -= o[7];

			if (extend_begin != NULL &&
			    extend_begin->line_type != STYLE_BORDER_NONE) {
				x1 += extension_begin[i][1][0];
				y1 -= extension_begin[i][1][1];
			}
		}
		gnome_print_moveto (context, x1, y1);
		gnome_print_lineto (context, x2, y2);
		gnome_print_stroke (context);

		gnome_print_grestore (context);
	}
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
