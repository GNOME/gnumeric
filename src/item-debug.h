#if 0
#ifndef __GNUC__
#define __FUNCTION__ __FILE__
#endif
#define gnome_canvas_item_grab(a,b,c,d)	do {		\
	fprintf (stderr, "%s %d: grab "GNUMERIC_ITEM" %p\n",	\
		 __FUNCTION__, __LINE__, a);		\
	gnome_canvas_item_grab (a, b, c,d);		\
} while (0)
#define gnome_canvas_item_ungrab(a,b) do {		\
	fprintf (stderr, "%s %d: ungrab "GNUMERIC_ITEM" %p\n",	\
		 __FUNCTION__, __LINE__, a);		\
	gnome_canvas_item_ungrab (a, b);		\
} while (0)
#endif

void item_debug_cross (GdkDrawable *drawable, GdkGC *gc, int x1, int y1, int x2, int y2);
