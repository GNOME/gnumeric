#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-bar.h"
#include "item-debug.h"

/* The signals we emit */
enum {
	ITEM_BAR_TEST,
	ITEM_BAR_LAST_SIGNAL
};
static guint item_bar_signals [ITEM_BAR_LAST_SIGNAL] = { 0 };

static GnomeCanvasItem *item_bar_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET,
	ARG_ORIENTATION,
	ARG_FIRST_ELEMENT
};

static void
item_bar_destroy (GtkObject *object)
{
	ItemBar *bar;

	bar = ITEM_BAR (object);

	if (GTK_OBJECT_CLASS (item_bar_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_bar_parent_class)->destroy)(object);
}

static void
item_bar_realize (GnomeCanvasItem *item)
{
	ItemBar *item_bar;
	GdkWindow *window;
	GdkGC *gc;
	
	item_bar = ITEM_BAR (item);
	window = GTK_WIDGET (item->canvas)->window;
	
	/* Configure our gc */
	item_bar->gc = gc = gdk_gc_new (window);

	item_bar->normal_cursor = gdk_cursor_new (GDK_ARROW);
	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
		item_bar->change_cursor = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	else
		item_bar->change_cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
}

static void
item_bar_unrealize (GnomeCanvasItem *item)
{
	ItemBar *item_bar = ITEM_BAR (item);
	
	gdk_gc_unref (item_bar->gc);
	gdk_cursor_destroy (item_bar->change_cursor);
	gdk_cursor_destroy (item_bar->normal_cursor);
}

static void
item_bar_reconfigure (GnomeCanvasItem *item)
{
	GnomeCanvas *canvas = item->canvas;
	ItemBar *item_bar = ITEM_BAR (item);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;
	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

static char *
get_row_name (int n)
{
	static char x [32];

	sprintf (x, "%d", n);
	return x;
}

static char *
get_col_name (int n)
{
	static char x [3];

	g_assert (n < 256);
	
	if (n < 'z'-'a'){
		x [0] = n + 'A';
		x [1] = 0;
	} else {
		x [0] = (n / ('z'-'a')) + 'A';
		x [1] = (n % ('z'-'a')) + 'A';
		x [2] = 0;
	}
	return x;
}

static void
bar_draw_cell (ItemBar *item_bar, GdkDrawable *drawable, char *str, int x1, int y1, int x2, int y2)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);
	GdkFont *font = canvas->style->font;
	int len, texth;
	
	len = gdk_string_width (font, str);
	texth = gdk_string_height (font, str);

	gtk_draw_shadow (canvas->style, drawable, GTK_STATE_NORMAL, GTK_SHADOW_OUT, 
			 x1, y1, x2-x1, y2-y1);
	gdk_draw_string (drawable, font, item_bar->gc, x1 + ((x2 - x1)-len)/2,
			 y2 - font->descent,
			 str);
			 
}

static void
item_bar_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemBar *item_bar = ITEM_BAR (item);
	Sheet   *sheet = item_bar->sheet;
	ColInfo *ci;
	RowInfo *ri;
	int element, total, pixels, limit;
	char *str;
	
	element = item_bar->first_element;

	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
		limit = y + height;
	else
		limit = x + width;
	
	total = 0;
	do {
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
			ri = sheet_get_row_info (sheet, element);
			pixels = ri->height;
			if (total+pixels >= y){
				str = get_row_name (element);
				bar_draw_cell (item_bar, drawable, str,
						  -x, 1 + total - y,
						  item->canvas->width - x,
						  1 + total + pixels - y);
			}
		} else {
			ci = sheet_get_col_info (sheet, element);
			pixels = ci->width;
			if (total+pixels >= x){
				str = get_col_name (element);
				bar_draw_cell (item_bar, drawable, str, 
						  1 + total - x, -y,
						  1 + total + pixels - x,
						  item->canvas->height - y);
			}
		}
		
		total += pixels;
		element++;
	} while (total < limit);
}

static double
item_bar_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
item_bar_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_bar_translate %g, %g\n", dx, dy);
}

static int
is_pointer_on_division (ItemBar *item_bar, int pos)
{
	ColInfo *ci;
	RowInfo *ri;
	int i, total, pixels;
	
	total = 0;
	
	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
			ri = sheet_get_row_info (item_bar->sheet, i);
			pixels = ri->height;
		} else {
			ci = sheet_get_col_info (item_bar->sheet, i);
			pixels = ci->width;
		}
		total += pixels;

		if ((total - 4 < pos) && (pos < total + 4))
			return 1;
	}
	return 0;
}

static void
set_cursor (ItemBar *item_bar, int pos)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);

	if (is_pointer_on_division (item_bar, pos))
		gdk_window_set_cursor(canvas->window, item_bar->change_cursor);
	else
		gdk_window_set_cursor(canvas->window, item_bar->normal_cursor);
}

static gint
item_bar_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemBar *item_bar = ITEM_BAR (item);
	int pos;
	
	switch (event->type){
	case GDK_ENTER_NOTIFY:
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			pos = event->crossing.y;
		else
			pos = event->crossing.x;
		set_cursor (item_bar, pos);
		break;
		
	case GDK_MOTION_NOTIFY:
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			pos = event->motion.y;
		else
			pos = event->motion.x;
		set_cursor (item_bar, pos);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

/*
 * Instance initialization
 */
static void
item_bar_init (ItemBar *item_bar)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_bar);
	
	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;
	
	item_bar->first_element = 0;
	item_bar->orientation = GTK_ORIENTATION_VERTICAL;
}

static void
item_bar_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemBar *item_bar;
	int v;
	
	item = GNOME_CANVAS_ITEM (o);
	item_bar = ITEM_BAR (o);
	
	switch (arg_id){
	case ARG_SHEET:
		item_bar->sheet = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_ORIENTATION:
		item_bar->orientation = GTK_VALUE_INT (*arg);
		break;
	case ARG_FIRST_ELEMENT:
		v = GTK_VALUE_INT (*arg);
		if (item_bar->first_element != v){
			item_bar->first_element = v;
			g_warning ("ARG_FIRST_ELEMENT: do scroll\n");
		}
		break;
	default:
	}
	item_bar_reconfigure (item);
}

/*
 * ItemBar class initialization
 */
static void
item_bar_class_init (ItemBarClass *item_bar_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_bar_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_bar_class;
	item_class = (GnomeCanvasItemClass *) item_bar_class;

	gtk_object_add_arg_type ("ItemBar::Sheet", GTK_TYPE_POINTER, 
				 GTK_ARG_WRITABLE, ARG_SHEET);
	gtk_object_add_arg_type ("ItemBar::Orientation", GTK_TYPE_INT, 
				 GTK_ARG_WRITABLE, ARG_ORIENTATION);
	gtk_object_add_arg_type ("ItemBar::First", GTK_TYPE_INT, 
				 GTK_ARG_WRITABLE, ARG_FIRST_ELEMENT);
	
	object_class->set_arg = item_bar_set_arg;

	/* GnomeCanvasItem method overrides */
	item_class->realize     = item_bar_realize;
	item_class->unrealize   = item_bar_unrealize;
	item_class->reconfigure = item_bar_reconfigure;
	item_class->draw        = item_bar_draw;
	item_class->point       = item_bar_point;
	item_class->translate   = item_bar_translate;
	item_class->event       = item_bar_event;
}

GtkType
item_bar_get_type (void)
{
	static GtkType item_bar_type = 0;

	if (!item_bar_type) {
		GtkTypeInfo item_bar_info = {
			"ItemBar",
			sizeof (ItemBar),
			sizeof (ItemBarClass),
			(GtkClassInitFunc) item_bar_class_init,
			(GtkObjectInitFunc) item_bar_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_bar_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_bar_info);
	}

	return item_bar_type;
}
