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

#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <gsf/gsf-impl-utils.h>

#define GNUMERIC_ITEM "ACETATE"
#include "item-debug.h"

#define MARGIN	10

typedef FooCanvasRect		ItemAcetate;
typedef FooCanvasRectClass	ItemAcetateClass;

static double
item_acetate_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		    FooCanvasItem **actual_item)
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
item_acetate_class_init (FooCanvasItemClass *item_class)
{
	item_class->point = item_acetate_point;
}

GSF_CLASS (ItemAcetate, item_acetate,
	   item_acetate_class_init, NULL,
	   FOO_TYPE_CANVAS_RECT);
