#include <config.h>
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
static GnomeCanvasLineClass *parent_class;

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
	gnumeric_dashed_canvas_line_class = klass;
	parent_class = gtk_type_class (gnome_canvas_line_get_type ());
	klass->real_draw = parent_class->parent_class.draw;
	parent_class->parent_class.draw = &gnumeric_dashed_canvas_line_draw;
}

static void
gnumeric_dashed_canvas_line_init (GnumericDashedCanvasLine *line)
{
	line->dash_style_index = BORDER_THIN;
}

static void
gnumeric_dashed_canvas_line_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
				  int x, int y, int width, int height)
{
	gnumeric_dashed_canvas_line_class->
	    real_draw (item, drawable, x, y, width, height);
}

void
gnumeric_dashed_canvas_line_set_dash_index (GnumericDashedCanvasLine *line,
					    StyleBorderType const indx,
					    guint const rgba)
{
#if 0
	/* FIXME FIXME FIXME setting the width seems to negate any
	 * attempts to set the dash.
	 */
	gint const width = border_get_width (indx);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (line),
			       "width_pixels", width,
			       NULL);

#endif
	line->dash_style_index = indx;

	border_set_gc_dash (line->line.gc, indx);

	/* HACK HACK HACK
	 * FIXME FIXME FIXME
	 * Force a redraw by setting the colour
	 * How should this be done correctly ? */
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (line),
			       "fill_color_rgba", rgba,
			       NULL);
}
