/*
 * gnumeric-dashed-canvas-line.c : A canvas line with support for dash styles.
 *
 * Author:
 *  Jody Goldberg (jody@gnome.org)
 *
 *  (C) 1999, 2000 Jody Goldberg
 */
#include <config.h>
#include <math.h>
#include "gnumeric-dashed-canvas-line.h"

/*
 * A utility class to provide advanced dashed line support to the gnome-canvas-line.
 * We need not just the predefined dash styles, we need to be able to set the styles directly.
 */
static void gnumeric_dashed_canvas_line_class_init (GnumericDashedCanvasLineClass *klass);
static void gnumeric_dashed_canvas_line_init       (GnumericDashedCanvasLine      *line);

static void   gnumeric_dashed_canvas_line_draw     (GnomeCanvasItem *item, GdkDrawable *drawable,
						    int x, int y, int width, int height);

static GnumericDashedCanvasLineClass *gnumeric_dashed_canvas_line_class;

GtkType
gnumeric_dashed_canvas_line_get_type (void)
{
	static GtkType line_type = 0;

	if (!line_type) {
		GtkTypeInfo line_info = {
			"GnumericDashedCanvasLine",
			sizeof (GnumericDashedCanvasLine),
			sizeof (GnumericDashedCanvasLineClass),
			(GtkClassInitFunc) gnumeric_dashed_canvas_line_class_init,
			(GtkObjectInitFunc) gnumeric_dashed_canvas_line_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		line_type = gtk_type_unique (gnome_canvas_line_get_type (), &line_info);
	}

	return line_type;
}

static void
gnumeric_dashed_canvas_line_class_init (GnumericDashedCanvasLineClass *klass)
{
	GnomeCanvasItemClass *item_class;

	gnumeric_dashed_canvas_line_class = klass;

	item_class = (GnomeCanvasItemClass *) klass;

	klass->real_draw = item_class->draw;
	item_class->draw = &gnumeric_dashed_canvas_line_draw;
}

static void
gnumeric_dashed_canvas_line_init (GnumericDashedCanvasLine *line)
{
	line->dash_style_index = STYLE_BORDER_THIN;
}

static inline double
hypothenuse (double xlength, double ylength)
{
	/* A little optimisation for speed. Horizontal or vertical lines
	 * are a lot more common than slanted */
	if (xlength == 0)
		return fabs (ylength);
	else if (ylength == 0)
		return fabs (xlength);
	else
		return sqrt (xlength * xlength + ylength * ylength);

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
double_line_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		  int x, int y, int width, int height)
{
	double *coords;
	double length, xdiff, ydiff, xoffs, yoffs;
	double offsetcoords[4];

	GnumericDashedCanvasLine *line = GNUMERIC_DASHED_CANVAS_LINE (item);

	if (GNOME_CANVAS_LINE (line)->num_points != 2) {
		g_warning ("file %s: line %d: \n%s", __FILE__, __LINE__,
			   "GnumericDashedCanvasLine only supports a "
			   "single line segment.");

		line->dash_style_index = STYLE_BORDER_MEDIUM;
		gnumeric_dashed_canvas_line_draw
			(GNOME_CANVAS_ITEM (line), drawable,
			 x, y, width, height);
		return;
	}

	coords = GNOME_CANVAS_LINE (line)->coords;
	xdiff = coords[2] - coords[0];
	ydiff = coords[3] - coords[1];

	length = hypothenuse (xdiff, ydiff);
	yoffs = xdiff/length;
	xoffs = -ydiff/length;

	style_border_set_gc_dash (GNOME_CANVAS_LINE (item)->gc,
				  STYLE_BORDER_THIN);
	offsetcoords[0] = coords[0] + xoffs;
	offsetcoords[1] = coords[1] + yoffs;
	offsetcoords[2] = coords[2] + xoffs;
	offsetcoords[3] = coords[3] + yoffs;
	GNOME_CANVAS_LINE (line)->coords = offsetcoords;
	gnumeric_dashed_canvas_line_class->
		real_draw (item, drawable, x, y, width, height);

	offsetcoords[0] = coords[0] - xoffs;
	offsetcoords[1] = coords[1] - yoffs;
	offsetcoords[2] = coords[2] - xoffs;
	offsetcoords[3] = coords[3] - yoffs;
	gnumeric_dashed_canvas_line_class->
		real_draw (item, drawable, x, y, width, height);

	GNOME_CANVAS_LINE (line)->coords = coords;
}

static void
gnumeric_dashed_canvas_line_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
				  int x, int y, int width, int height)
{
	GnumericDashedCanvasLine *line = GNUMERIC_DASHED_CANVAS_LINE (item);

	if (line->dash_style_index == STYLE_BORDER_DOUBLE)
		double_line_draw (item, drawable, x, y, width, height);
	else {
 		style_border_set_gc_dash (GNOME_CANVAS_LINE (item)->gc,
					  line->dash_style_index);
		gnumeric_dashed_canvas_line_class->
			real_draw (item, drawable, x, y, width, height);
	}
}

void
gnumeric_dashed_canvas_line_set_dash_index (GnumericDashedCanvasLine *line,
					    StyleBorderType const indx)
{
	gint const width = style_border_get_width (indx);
	line->dash_style_index = indx;
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (line),
			       "width_pixels", width,
			       NULL);

	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (line));
}
