/*
 * pattern-selector.c:  A widget that displays the Gnumeric patterns. 
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "pattern-selector.h"

#define PIXS_PER_SQUARE 25
#define BORDER 3

void
pattern_selector_select (PatternSelector *ps, int pattern)
{
	int x, y;
	
	g_return_if_fail (ps != NULL);
	g_return_if_fail (IS_PATTERN_SELECTOR (ps));
	g_return_if_fail (pattern >= 0 && pattern < GNUMERIC_SHEET_PATTERNS);

	x = pattern % 6;
	y = pattern / 6;
	
	if (ps->selector == NULL){
		ps->selector = gnome_canvas_item_new (
			GNOME_CANVAS_GROUP (GNOME_CANVAS (ps)->root),
			gnome_canvas_rect_get_type (),
			"x1", 0.0, "y1", 0.0, "x2", 0.0, "y2", 0.0,
			"outline_color", "black",
			"width_pixels",  3,
			NULL);
	}
	ps->selected_item = pattern;

	gnome_canvas_item_set (
		ps->selector,
		"x1",   (double) x * PIXS_PER_SQUARE + BORDER,
		"y1",   (double) y * PIXS_PER_SQUARE + BORDER,
		"x2",   (double) (x+1) * PIXS_PER_SQUARE - BORDER,
		"y2",   (double) (y+1) * PIXS_PER_SQUARE - BORDER,
		NULL);
}

GtkWidget *
pattern_selector_new (int initial_pattern)
{
	PatternSelector *ps;
	GnomeCanvas *canvas;
		
	ps = gtk_type_new (pattern_selector_get_type ());
	canvas = GNOME_CANVAS (ps);
	
	gtk_widget_set_usize (GTK_WIDGET (canvas),
			      6*PIXS_PER_SQUARE, 3*PIXS_PER_SQUARE);
	gnome_canvas_set_scroll_region (canvas, 0, 0,
					6*PIXS_PER_SQUARE, 3*PIXS_PER_SQUARE);
	gnome_canvas_set_pixels_per_unit (canvas, 1.0);

	pattern_selector_select (ps, initial_pattern);
		
	return GTK_WIDGET (ps);
}

static int
click_on_pattern (GnomeCanvasItem *item, GdkEvent *event, void *num)
{
	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	{
		int pattern = GPOINTER_TO_INT (num);
		PatternSelector *ps = PATTERN_SELECTOR (item->canvas);
		
		pattern_selector_select (ps, pattern);
	}
	return TRUE;
}

static void
pattern_selector_init (PatternSelector *pattern_selector)
{
	GnomeCanvas *canvas = GNOME_CANVAS (pattern_selector);
	GnomeCanvasGroup *group;
	int i;
	
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);

	group = GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (pattern_selector)));
	
	for (i = 0; i < GNUMERIC_SHEET_PATTERNS; i++){
		GdkBitmap *stipple;
		GnomeCanvasRE *item;
		int x, y;

		x = i % 6;
		y = i / 6;

		/*
		 * TODO TODO TODO
		 * 1) How to make the background of the stipples white ?
		 * 2) How to not have the border impinge upon the patterns
		 */
		stipple = gdk_bitmap_create_from_data (
			NULL, gnumeric_sheet_patterns [i].pattern, 8, 8);
		item = GNOME_CANVAS_RE (gnome_canvas_item_new (
			group,
			gnome_canvas_rect_get_type (),
			"x1",           (double) x * PIXS_PER_SQUARE + BORDER, 
			"y1",           (double) y * PIXS_PER_SQUARE + BORDER, 
			"x2",           (double) (x + 1) * PIXS_PER_SQUARE - BORDER,
			"y2",           (double) (y + 1) * PIXS_PER_SQUARE - BORDER,
			"fill_color",   "black",
			"width_pixels", (int) 0,
			"fill_stipple",  stipple,
			NULL));

		gdk_bitmap_unref (stipple);
		gtk_signal_connect (GTK_OBJECT (item), "event",
				    GTK_SIGNAL_FUNC (click_on_pattern),
				    GINT_TO_POINTER (i));
	}
}

GtkType
pattern_selector_get_type (void)
{
	static GtkType pattern_selector_type = 0;

	if (!pattern_selector_type){
		GtkTypeInfo pattern_selector_info = {
			"PatternSelector",
			sizeof (PatternSelector),
			sizeof (PatternSelectorClass),
			(GtkClassInitFunc) NULL,
			(GtkObjectInitFunc) pattern_selector_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		pattern_selector_type = gtk_type_unique (gnome_canvas_get_type (), &pattern_selector_info);
	}

	return pattern_selector_type;
}

