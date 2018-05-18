/*
 * gnm-dashed-canvas-line.c : A canvas line with support for dash styles.
 *
 * Author:
 *  Jody Goldberg (jody@gnome.org)
 *
 *  (C) 1999-2002 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <widgets/gnm-dashed-canvas-line.h>

#include <gsf/gsf-impl-utils.h>
#include <math.h>

static GocItemClass *parent_class;

static void gnm_dashed_canvas_line_draw (GocItem const *item, cairo_t *cr);

static GnmDashedCanvasLineClass *gnm_dashed_canvas_line_class;

static void
line_draw (GocItem const *item, GnmStyleBorderType const i, cairo_t *cr)
{
	GocLine *line = GOC_LINE (item);
	double sign = (goc_canvas_get_direction (item->canvas) == GOC_DIRECTION_RTL)? -1: 1;
	double endx = (line->endx - line->startx) * sign, endy = line->endy - line->starty;
	double hoffs, voffs = ceil (go_styled_object_get_style (GO_STYLED_OBJECT (item))->line.width);
	if (line->startx == line->endx && line->starty == line->endy)
		return;
	if (voffs <= 0.)
		voffs = 1.;
	hoffs = ((int) voffs & 1)? .5: 0.;
	voffs = (line->starty == line->endy)? hoffs: 0.;
	if (line->startx != line->endx)
	                hoffs = 0.;
	cairo_save (cr);
	goc_group_cairo_transform (item->parent, cr, hoffs + (int) line->startx, voffs + (int) line->starty);
	if ((endx != 0. || endy!= 0.) && go_styled_object_set_cairo_line (GO_STYLED_OBJECT (item), cr)) {
		gnm_style_border_set_dash (i, cr);
		/* try to avoid horizontal and vertical lines between two pixels */
		cairo_move_to (cr, 0., 0.);
		endx = (endx > 0.)? ceil (endx): floor (endx);
		endy = (endy > 0.)? ceil (endy): floor (endy);
		cairo_line_to (cr, endx, endy);
		cairo_stroke (cr);
	}
	cairo_restore (cr);
}

/*
 * Draw a double line
 * NOTE: We only support a single straight line segment here.
 *
 * This function is called when drawing to the preview canvas in the cell
 * format dialog - not called when drawing the grid to the screen. It isn't
 * time critical, so we use similar triangles. This is slower than table
 * lookup, which is used in border.c, but correct for all angles of slope.
 *
 *                  y axis
 *                  /
 *                 /
 *                /
 *               /
 *    ~         /
 *       A +---/----------------------------------
 *         |  /B~
 *         | /       ~
 *         |/             ~
 * (x0,y0) *************************************** (x1,y1)
 *             ~                                /
 *                 ~                           /
 *                      ~                     /
 *         ------------------~---------------/----
 *                                ~         /
 *                                     ~   /
 *                                   (x1,y0)~
 *                                               ~ x axis
 *
 * In this diagram, the lines we are going to draw are horizontal, the
 * coordinate axes are slanted.
 * The starred line connects the endpoint coordinates as passed in. We
 * actually draw the two dashed lines shown on either side. The triangle
 * (x0,y0) - (x1,y0) - (x1,y1) is similar to the triangle (x0,y0) - B - A.
 * Note that B is supposed to be on the line of ~ through A parallell to the
 * x axis - that's hard to show clearly in ASCII art.
 * AB is the x offset from x0 of the start of the upper dashed line.
 * (x0,y0) - B is the y offset from y0 of the start of this line.
 */
static void
double_line_draw (GocItem const *item, GnmStyleBorderType const i, cairo_t *cr)
{
	double coords[4];
	double length, xdiff, ydiff, xoffs, yoffs;

	GocLine *line = GOC_LINE (item);

	coords[0] = line->startx;
	coords[1] = line->starty;
	coords[2] = line->endx;
	coords[3] = line->endy;
	xdiff = coords[2] - coords[0];
	ydiff = coords[3] - coords[1];

	length = hypot (xdiff, ydiff);
	yoffs = xdiff/length;
	xoffs = -ydiff/length;

	line->startx = coords[0] + xoffs;
	line->starty = coords[1] + yoffs;
	line->endx = coords[2] + xoffs;
	line->endy = coords[3] + yoffs;
	line_draw (item, i, cr);

	line->startx = coords[0] - xoffs;
	line->starty = coords[1] - yoffs;
	line->endx = coords[2] - xoffs;
	line->endy = coords[3] - yoffs;
	line_draw (item, i, cr);

	line->startx = coords[0];
	line->starty = coords[1];
	line->endx = coords[2];
	line->endy = coords[3];
}

static void
gnm_dashed_canvas_line_draw (GocItem const *item, cairo_t *cr)
{
	GnmDashedCanvasLine *line = GNM_DASHED_CANVAS_LINE (item);

	if (line->dash_style_index == GNM_STYLE_BORDER_DOUBLE)
		double_line_draw (item, line->dash_style_index, cr);
	else {
		line_draw (item, line->dash_style_index, cr);
	}
}

static void
gnm_dashed_canvas_line_update_bounds (GocItem *item)
{
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (item));
	double saved_width = style->line.width;
	style->line.width = 10.;	/* larger than any border */
	parent_class->update_bounds (item);
	style->line.width = saved_width;
}

static void
gnm_dashed_canvas_line_class_init (GnmDashedCanvasLineClass *klass)
{
	GocItemClass *item_class;

	parent_class = g_type_class_peek_parent (klass);
	gnm_dashed_canvas_line_class = klass;

	item_class = (GocItemClass *) klass;

	item_class->draw = gnm_dashed_canvas_line_draw;
	item_class->update_bounds = gnm_dashed_canvas_line_update_bounds;
}

static void
gnm_dashed_canvas_line_init (GnmDashedCanvasLine *line)
{
	line->dash_style_index = GNM_STYLE_BORDER_THIN;
}

GSF_CLASS (GnmDashedCanvasLine, gnm_dashed_canvas_line,
	   gnm_dashed_canvas_line_class_init,
	   gnm_dashed_canvas_line_init, GOC_TYPE_LINE)

void
gnm_dashed_canvas_line_set_dash_index (GnmDashedCanvasLine *line,
				       GnmStyleBorderType const indx)
{
	gint const width = gnm_style_border_get_width (indx);
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (line));
	line->dash_style_index = indx;
	style->line.width = width;

	goc_item_invalidate (GOC_ITEM (line));
}
