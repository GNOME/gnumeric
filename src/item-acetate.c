/* vim: set sw=8: */
/*
 * Implements a clear canvas item with a border to catch events.
 *
 * Author:
 *     Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "item-acetate.h"

#define GNUMERIC_ITEM "ACETATE"
#include "item-debug.h"
#include "gnumeric-canvas.h"

#include <gal/util/e-util.h>

#define MARGIN	10

typedef struct {
	GnomeCanvasRect parent;
} ItemAcetate;
typedef struct {
	GnomeCanvasRectClass parent;
} ItemAcetateClass;

static double
item_acetate_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		    GnomeCanvasItem **actual_item)
{
	if (cx < (item->x1 - MARGIN) ||
	    cx > (item->x2 + MARGIN) ||
	    cy < (item->y1 - MARGIN) ||
	    cy > (item->y2 + MARGIN))
		return DBL_MAX;
	*actual_item = item;
	return 0.;
}

static void
item_acetate_class_init (GnomeCanvasItemClass *item_class)
{
	item_class->point = item_acetate_point;
}

E_MAKE_TYPE (item_acetate, "ItemAcetate", ItemAcetate,
	     item_acetate_class_init, NULL,
	     GNOME_TYPE_CANVAS_RECT);
