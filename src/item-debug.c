#include <gnumeric-config.h>
#include "gnumeric.h"
#include "item-debug.h"

#include <gtk/gtk.h>

void
item_debug_cross (GdkDrawable *drawable, GdkGC *gc, int x1, int y1, int x2, int y2)
{
	gdk_draw_line (drawable, gc, x1, y1, x2, y2);
	gdk_draw_line (drawable, gc, x1, y2, x2, y1);
}

