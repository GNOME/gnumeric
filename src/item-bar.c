/*
 * Implements the resizable guides for columns and rows
 * in the Gnumeric Spreadsheet.
 *
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-bar.h"
#include "item-debug.h"

/* Marshal forward declarations */
static void   item_bar_marshal      (GtkObject *,
				     GtkSignalFunc,
				     gpointer,
				     GtkArg *);
 
/* The signal signatures */
typedef void (*ItemBarSignal1) (GtkObject *, gint arg1, gpointer data);
typedef void (*ItemBarSignal2) (GtkObject *, gint arg1, gint arg2, gpointer data);

/* The signals we emit */
enum {
	SELECTION_CHANGED,
	SIZE_CHANGED,
	LAST_SIGNAL
};
static guint item_bar_signals [LAST_SIGNAL] = { 0 };

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
	GdkColor c;
	
	item_bar = ITEM_BAR (item);
	window = GTK_WIDGET (item->canvas)->window;
	
	/* Configure our gc */
	item_bar->gc = gc = gdk_gc_new (window);
	gnome_canvas_get_color (item->canvas, "black", &c);
	gdk_gc_set_foreground (item_bar->gc, &c);

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

	g_assert (n < 65536);

	sprintf (x, "%d", n + 1);
	return x;
}

static char *
get_col_name (int n)
{
	static char x [3];

	g_assert (n < 256);
	
	if (n <= 'z'-'a') {
		x [0] = n + 'A';
		x [1] = 0;
	} else {
		x [0] = (n / ('z'-'a'+1) - 1) + 'A';
		x [1] = (n % ('z'-'a'+1)) + 'A';
		x [2] = 0;
	}
	return x;
}

static void
bar_draw_cell (ItemBar *item_bar, GdkDrawable *drawable, ColRowInfo *info, char *str, int x1, int y1, int x2, int y2)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);
	GdkFont *font = canvas->style->font;
	GdkGC *gc;
	int len, texth, shadow;

	len = gdk_string_width (font, str);
	texth = font->ascent + font->descent;

	if (info->selected){
		shadow = GTK_SHADOW_IN;
		gc = canvas->style->dark_gc [GTK_STATE_NORMAL];
	} else {
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
	}

	gdk_draw_rectangle (drawable, gc, TRUE, x1 + 1, y1 + 1, x2-x1-2, y2-y1-2);
	gtk_draw_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow, 
			 x1, y1, x2-x1, y2-y1);
	gdk_draw_string (drawable, font, item_bar->gc,
			 x1 + ((x2 - x1) - len) / 2,
			 y1 + ((y2 - y1) - texth) / 2 + font->ascent,
			 str);
}

static void
item_bar_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemBar *item_bar = ITEM_BAR (item);
	Sheet   *sheet = item_bar->sheet;
	ColRowInfo *cri;
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
			cri = sheet_row_get_info (sheet, element);
			if (item_bar->resize_pos == element)
				pixels = item_bar->resize_width;
			else
				pixels = cri->pixels;
			
			if (total + pixels >= y){
				str = get_row_name (element);
				bar_draw_cell (item_bar, drawable, cri, str,
						  -x, 1 + total - y,
						  item->canvas->width - x,
						  1 + total + pixels - y);
			}
		} else {
			cri = sheet_col_get_info (sheet, element);
			if (item_bar->resize_pos == element)
				pixels = item_bar->resize_width;
			else
				pixels = cri->pixels;
			
			if (total + pixels >= x){
				str = get_col_name (element);
				bar_draw_cell (item_bar, drawable, cri, str, 
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

static ColRowInfo *
is_pointer_on_division (ItemBar *item_bar, int pos, int *the_total, int *the_element)
{
	ColRowInfo *cri;
	int i, total;
	
	total = 0;
	
	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			cri = sheet_row_get_info (item_bar->sheet, i);
		else
			cri = sheet_col_get_info (item_bar->sheet, i);

		total += cri->pixels;
		if ((total - 4 < pos) && (pos < total + 4)){
			if (the_total)
				*the_total = total;
			if (the_element)
				*the_element = i;

			return cri;
		}

		if (total > pos){
			if (the_element)
				*the_element = i;
			return NULL;
		}
	}
	return NULL;
}

static void
set_cursor (ItemBar *item_bar, int pos)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);

	if (is_pointer_on_division (item_bar, pos, NULL, NULL))
		gdk_window_set_cursor(canvas->window, item_bar->change_cursor);
	else
		gdk_window_set_cursor(canvas->window, item_bar->normal_cursor);
}

static void
item_bar_start_resize (ItemBar *item_bar, int pos)
{
	GnomeCanvas *canvas = GNOME_CANVAS (item_bar->sheet->sheet_view);
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	GnomeCanvasItem *item;
	GnomeCanvasPoints *points;
	
	double x1, x2, y1, y2;
	
	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
		x1 = 0.0;
		x2 = INT_MAX;
		y1 = GNOME_CANVAS_ITEM (item_bar)->y1 + pos;
		y2 = GNOME_CANVAS_ITEM (item_bar)->y1 + pos;
	}

	x1 = 0.0;
	x2 = 1000.0;
	y1 = 0.0;
	y2 = 1000.0;

	/* Add a guideline to the sheet canvas */
	points = gnome_canvas_points_new (2);
	points->coords[0] = x1;
	points->coords[1] = y1;
	points->coords[2] = x2;
	points->coords[3] = y2;
	item = gnome_canvas_item_new (group,
				      gnome_canvas_line_get_type (),
				      "points", points,
				      "fill_color", "black",
				      "width_pixels", 4,
				      NULL);
	gnome_canvas_points_free (points);

	item_bar->resize_guide = GTK_OBJECT (item);
}

static int
get_col_from_pos (ItemBar *item_bar, int pos)
{
	ColRowInfo *cri;
	int i, total;
	
	total = 0;
	
	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			cri = sheet_row_get_info (item_bar->sheet, i);
		else
			cri = sheet_col_get_info (item_bar->sheet, i);

		total += cri->pixels;
		if (total > pos)
			return i;
	}
	return i;
}

#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_bar_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ColRowInfo *cri;
	GnomeCanvas *canvas = item->canvas;
	ItemBar *item_bar = ITEM_BAR (item);
	int pos, start, ele, x, y;
	int resizing;

	resizing = ITEM_BAR_RESIZING (item_bar);
	
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		convert (canvas, e->crossing.x, e->crossing.y, &x, &y);
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			pos = y;
		else
			pos = x;
		set_cursor (item_bar, pos);
		break;
		
	case GDK_MOTION_NOTIFY:
		convert (canvas, e->motion.x, e->motion.y, &x, &y);
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			pos = y;
		else
			pos = x;

		/* Do column resizing or incremental marking */
		if (resizing){
			int npos;

			npos = pos - item_bar->resize_start_pos;
			if (npos > 0){
				item_bar->resize_width = npos;
				gnome_canvas_request_redraw (
					GNOME_CANVAS_ITEM(item_bar)->canvas,
					0, 0, INT_MAX, INT_MAX);
			}
		} else if (item_bar->emitting_selection){
			ele = get_col_from_pos (item_bar, pos);

			if (cri && !cri->selected){
				gtk_signal_emit (GTK_OBJECT (item),
						 item_bar_signals [SELECTION_CHANGED],
						 ele, FALSE);
			}
			set_cursor (item_bar, pos);
		} else
			set_cursor (item_bar, pos);
		break;

	case GDK_BUTTON_PRESS:
		convert (canvas, e->button.x, e->button.y, &x, &y);
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			pos = y;
		else
			pos = x;

		cri = is_pointer_on_division (item_bar, pos, &start, &ele);
		if (cri){
			/* Record the important bits */
			item_bar->resize_pos = ele;
			item_bar->resize_start_pos = start - cri->pixels;
			item_bar->resize_width = cri->pixels;

			item_bar_start_resize (item_bar, pos);
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
						item_bar->change_cursor,
						e->button.time);
		} else {
			item_bar->emitting_selection = ele;
			gtk_signal_emit (GTK_OBJECT (item),
					 item_bar_signals [SELECTION_CHANGED],
					 ele, TRUE);
		}
		break;

	case GDK_BUTTON_RELEASE:
		if (resizing){
			gtk_signal_emit (GTK_OBJECT (item),
					 item_bar_signals [SIZE_CHANGED],
					 item_bar->resize_pos,
					 item_bar->resize_width);
			item_bar->resize_pos = -1;
			gtk_object_destroy (item_bar->resize_guide);
			gnome_canvas_item_ungrab (item, e->button.time);
		}
		item_bar->emitting_selection = -1;
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
	item_bar->resize_pos = -1;
	item_bar->start_selection = -1;
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

	item_bar_signals [SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ItemBarClass, selection_changed),
				item_bar_marshal,
				GTK_TYPE_NONE,
				2,
				GTK_TYPE_INT, GTK_TYPE_INT);
	item_bar_signals [SIZE_CHANGED] =
		gtk_signal_new ("size_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ItemBarClass, size_changed),
				item_bar_marshal,
				GTK_TYPE_NONE,
				2,
				GTK_TYPE_INT,
				GTK_TYPE_INT);

	/* Register our signals */
	gtk_object_class_add_signals (object_class, item_bar_signals,
				      LAST_SIGNAL);
	
	/* Method overrides */
	object_class->destroy = item_bar_destroy;
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


/*
 * Marshaling routines for our signals
 */
static void
item_bar_marshal (GtkObject     *object,
		  GtkSignalFunc func,
		  gpointer      func_data,
		  GtkArg        *args)
{
	ItemBarSignal2 rfunc;
	
	rfunc = (ItemBarSignal2) func;
	(*rfunc) (object,
		  GTK_VALUE_INT (args [0]),
		  GTK_VALUE_INT (args [1]),
		  func_data);
}

