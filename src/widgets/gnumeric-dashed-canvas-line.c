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

static void
gnumeric_dashed_canvas_line_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
				  int x, int y, int width, int height)
{
	style_border_set_gc_dash (GNOME_CANVAS_LINE (item)->gc,
				  GNUMERIC_DASHED_CANVAS_LINE (item)->dash_style_index);
	gnumeric_dashed_canvas_line_class->
	    real_draw (item, drawable, x, y, width, height);
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
