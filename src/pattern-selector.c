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

#define PIXS_PER_SQUARE 30
#define BORDER 4

static GnomeCanvasClass *parent_class;

void
pattern_selector_select (PatternSelector *ps, int pattern)
{
	int x, y;
	
	g_return_if_fail (ps != NULL);
	g_return_if_fail (IS_PATTERN_SELECTOR (ps));
	g_return_if_fail (pattern >= 0 && pattern < GNUMERIC_SHEET_PATTERNS);

	x = pattern % 7;
	y = pattern / 7;
	
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
	
	gnome_canvas_set_size (canvas, 7*PIXS_PER_SQUARE, 2*PIXS_PER_SQUARE);
	gnome_canvas_set_scroll_region (canvas, 0, 0, 7*PIXS_PER_SQUARE, 2*PIXS_PER_SQUARE);
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
pattern_selector_realize (GtkWidget *widget)
{
	PatternSelector *ps = PATTERN_SELECTOR (widget);
	GdkWindow *window;
	GnomeCanvasGroup *group;
	int i;
	
	if (GTK_WIDGET_CLASS (parent_class)->realize)
		(*GTK_WIDGET_CLASS (parent_class)->realize)(widget);

	window = widget->window;
	group = GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (widget)));
	
	for (i = 0; i < GNUMERIC_SHEET_PATTERNS; i++){
		GnomeCanvasRE *item;
		int x, y;

		x = i % 7;
		y = i / 7;

		item = GNOME_CANVAS_RE (gnome_canvas_item_new (
			group,
			gnome_canvas_rect_get_type (),
			"x1",           (double) x * PIXS_PER_SQUARE + BORDER, 
			"y1",           (double) y * PIXS_PER_SQUARE + BORDER, 
			"x2",           (double) (x + 1) * PIXS_PER_SQUARE - BORDER,
			"y2",           (double) (y + 1) * PIXS_PER_SQUARE - BORDER,
			"fill_color",   "black",
			"width_pixels", (int) 1,
			"outline_color","black",
			NULL));

		ps->patterns [i] = gdk_bitmap_create_from_data (
			window, gnumeric_sheet_patterns [i].pattern, 8, 8);

		gdk_gc_set_stipple (item->fill_gc, ps->patterns [i]);
		gdk_gc_set_fill (item->fill_gc, GDK_STIPPLED);

		gtk_signal_connect (GTK_OBJECT (item), "event",
				    GTK_SIGNAL_FUNC (click_on_pattern), GINT_TO_POINTER (i));
	}
}

static void
pattern_selector_unrealize (GtkWidget *widget)
{
	PatternSelector *ps = PATTERN_SELECTOR (widget);
	int i;

	for (i = 0; i < GNUMERIC_SHEET_PATTERNS; i++)
		     gdk_pixmap_unref (ps->patterns [i]);
	     
	if (GTK_WIDGET_CLASS (parent_class)->unrealize)
		(*GTK_WIDGET_CLASS (parent_class)->unrealize)(widget);
}

static void
pattern_selector_class_init (PatternSelectorClass *class)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass *) class;

	parent_class = gtk_type_class (gnome_canvas_get_type());
	
	widget_class->realize = pattern_selector_realize;
	widget_class->unrealize = pattern_selector_unrealize;
}

static void
pattern_selector_init (PatternSelector *pattern_selector)
{
	GnomeCanvas *canvas = GNOME_CANVAS (pattern_selector);
	
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
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
			(GtkClassInitFunc) pattern_selector_class_init,
			(GtkObjectInitFunc) pattern_selector_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		pattern_selector_type = gtk_type_unique (gnome_canvas_get_type (), &pattern_selector_info);
	}

	return pattern_selector_type;
}

