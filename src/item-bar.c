/*
 * Implements the resizable guides for columns and rows
 * in the Gnumeric Spreadsheet.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-bar.h"
#include "item-debug.h"
#include "utils.h"
#include "gnumeric-util.h"
#include "selection.h"

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
	ARG_SHEET_VIEW,
	ARG_ORIENTATION,
	ARG_FIRST_ELEMENT
};

static void
item_bar_destroy (GtkObject *object)
{
	ItemBar *bar;

	bar = ITEM_BAR (object);

	if (bar->tip)
		gtk_object_unref (GTK_OBJECT (bar->tip));

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

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)(item);

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

	/* Reference the bold font */
	style_font_ref (gnumeric_default_bold_font);
}

static void
item_bar_unrealize (GnomeCanvasItem *item)
{
	ItemBar *item_bar = ITEM_BAR (item);

	style_font_unref (gnumeric_default_bold_font);
	gdk_gc_unref (item_bar->gc);
	gdk_cursor_destroy (item_bar->change_cursor);
	gdk_cursor_destroy (item_bar->normal_cursor);

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)(item);
}

static void
item_bar_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	if (GNOME_CANVAS_ITEM_CLASS(item_bar_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_bar_parent_class)->update)(item, affine, clip_path, flags);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;
	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

static const char *
get_row_name (int n)
{
	static char x [4 * sizeof (int)];

	g_assert (n >= 0 && n < SHEET_MAX_ROWS);

	sprintf (x, "%d", n + 1);
	return x;
}

static void
bar_draw_cell (ItemBar *item_bar, GdkDrawable *drawable, ItemBarSelectionType type,
	       const char *str, int x1, int y1, int x2, int y2)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);
	GdkFont *font = canvas->style->font;
	GdkGC *gc;
	int len, texth, shadow;

	switch (type){
	default:
	case ITEM_BAR_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		break;

	case ITEM_BAR_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		font = style_font_gdk_font (gnumeric_default_bold_font);
		break;

	case ITEM_BAR_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		gc = canvas->style->dark_gc [GTK_STATE_NORMAL];
		font = style_font_gdk_font (gnumeric_default_bold_font);
		break;
	}

	len = gdk_string_width (font, str);
	texth = font->ascent + font->descent;

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
	Sheet   *sheet = item_bar->sheet_view->sheet;
	ColRowInfo *cri;
	int element, total, pixels, limit;
	const char *str;

	element = item_bar->first_element;

	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
		limit = y + height;
	else
		limit = x + width;

	total = 0;

	do {
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){

			if (element >= SHEET_MAX_ROWS){
				GtkWidget *canvas = GTK_WIDGET (item->canvas);

				gtk_draw_shadow (canvas->style, drawable,
						 GTK_STATE_NORMAL, GTK_SHADOW_OUT,
						 x, y, width, height);
				return;
			}
			cri = sheet_row_get_info (sheet, element);
			if (item_bar->resize_pos == element)
				pixels = item_bar->resize_width;
			else
				pixels = cri->pixels;

			if (total + pixels >= y){
				str = get_row_name (element);
				bar_draw_cell (item_bar, drawable,
					       sheet_row_selection_type (sheet, element),
					       str, -x, 1 + total - y,
					       GTK_WIDGET (item->canvas)->allocation.width - x,
					       1 + total + pixels - y);
			}
		} else {
			if (element >= SHEET_MAX_COLS){
				GtkWidget *canvas = GTK_WIDGET (item->canvas);

				gtk_draw_shadow (canvas->style, drawable,
						 GTK_STATE_NORMAL, GTK_SHADOW_OUT,
						 x, y, width, height);
				return;
			}
			cri = sheet_col_get_info (sheet, element);
			if (item_bar->resize_pos == element)
				pixels = item_bar->resize_width;
			else
				pixels = cri->pixels;

			if (total + pixels >= x){
				str = col_name (element);
				bar_draw_cell (item_bar, drawable,
					       sheet_col_selection_type (sheet, element),
					       str, 1 + total - x, -y,
					       1 + total + pixels - x,
					       GTK_WIDGET (item->canvas)->allocation.height - y);
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
	Sheet *sheet;
	int i, total;

	total = 0;
	sheet = item_bar->sheet_view->sheet;

	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			cri = sheet_row_get_info (sheet, i);
		else
			cri = sheet_col_get_info (sheet, i);

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

	/* We might be invoked before we are realized */
	if (!canvas->window)
		return;

	if (is_pointer_on_division (item_bar, pos, NULL, NULL))
		gdk_window_set_cursor(canvas->window, item_bar->change_cursor);
	else
		gdk_window_set_cursor(canvas->window, item_bar->normal_cursor);
}

/*
 * Returns the GnomeCanvasPoints for a line at position in the
 * correct orientation.
 */
static GnomeCanvasPoints *
item_bar_get_line_points (GnomeCanvas *gcanvas, ItemBar *item_bar, int position)
{
	GnomeCanvasPoints *points;
	GtkWidget *canvas = GTK_WIDGET (item_bar->sheet_view->sheet_view);
	double x1, y1, x2, y2;

	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
		gnome_canvas_window_to_world
			(gcanvas, canvas->allocation.width, position,
			 &x2, &y2);
		x1 = 0.0;
		y1 = y2;
	} else {
		gnome_canvas_window_to_world
			(gcanvas, position, canvas->allocation.height,
			 &x2, &y2);
		x1 = x2;
		y1 = 0.0;
	}

	points = gnome_canvas_points_new (2);
	points->coords [0] = x1;
	points->coords [1] = y1;
	points->coords [2] = x2;
	points->coords [3] = y2;

	return points;
}

static void
item_bar_start_resize (ItemBar *item_bar, int pos)
{
	GnomeCanvasPoints *points;
	GnomeCanvasGroup *group;
	GnomeCanvasItem *item;
	GnumericSheet *gsheet;
	Sheet *sheet;
	int division_pos;
	GnomeCanvas *canvas;

	sheet = item_bar->sheet_view->sheet;
	gsheet = GNUMERIC_SHEET (item_bar->sheet_view->sheet_view);
	canvas = GNOME_CANVAS (gsheet);
	group = GNOME_CANVAS_GROUP (canvas->root);

	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
		division_pos = sheet_row_get_distance (
			sheet, gsheet->top_row, pos+1);
	else
		division_pos = sheet_col_get_distance (
			sheet, gsheet->top_col, pos+1);

	points = item_bar_get_line_points (canvas, item_bar, division_pos);

	item = gnome_canvas_item_new (
		group,
		gnome_canvas_line_get_type (),
		"points", points,
		"fill_color", "black",
		"width_pixels", 1,
		NULL);
	gnome_canvas_points_free (points);

	item_bar->resize_guide = GTK_OBJECT (item);
}

static int
get_col_from_pos (ItemBar *item_bar, int pos)
{
	ColRowInfo *cri;
	Sheet *sheet;
	int i, total;

	total = 0;
	sheet = item_bar->sheet_view->sheet;
	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL)
			cri = sheet_row_get_info (sheet, i);
		else
			cri = sheet_col_get_info (sheet, i);

		total += cri->pixels;
		if (total > pos)
			return i;
	}
	return i;
}

static void
colrow_tip_setlabel (ItemBar *item_bar, gboolean const is_vertical, int const size)
{
	if (item_bar->tip) {
		char buffer [20 + sizeof (long) * 4];
		if (is_vertical)
			snprintf (buffer, sizeof (buffer), _("Height: %.2f"), size *.75);
		else
			snprintf (buffer, sizeof (buffer), _("Width: %.2f"), size / 7.5);
		gtk_label_set_text (GTK_LABEL (item_bar->tip), buffer);
	}
}

static void
item_bar_end_resize (ItemBar *item_bar, int new_size)
{
	if (new_size > 0)
		gtk_signal_emit (GTK_OBJECT (item_bar),
				 item_bar_signals [SIZE_CHANGED],
				 item_bar->resize_pos,
				 new_size);
	if (item_bar->resize_guide) {
		gtk_object_destroy (item_bar->resize_guide);
		item_bar->resize_guide = NULL;
	}
	if (item_bar->tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (item_bar->tip));
		item_bar->tip = NULL;
	}
	item_bar->start_selection = -1;

	item_bar->resize_pos = -1;
}

#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_bar_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ColRowInfo *cri;
	GnomeCanvas *canvas = item->canvas;
	ItemBar *item_bar = ITEM_BAR (item);
	int pos, start, element, x, y;
	gboolean const resizing = ITEM_BAR_RESIZING (item_bar);
	gboolean const is_vertical = (item_bar->orientation == GTK_ORIENTATION_VERTICAL);

	switch (e->type){
	case GDK_ENTER_NOTIFY:
		convert (canvas, e->crossing.x, e->crossing.y, &x, &y);
		if (is_vertical)
			pos = y;
		else
			pos = x;
		set_cursor (item_bar, pos);
		break;

	case GDK_MOTION_NOTIFY:
		convert (canvas, e->motion.x, e->motion.y, &x, &y);
		if (is_vertical)
			pos = y;
		else
			pos = x;

		/* Do column resizing or incremental marking */
		if (resizing){
			GnomeCanvasPoints *points;
			GnomeCanvasItem *resize_guide;
			int npos;

			if (item_bar->resize_guide == NULL){
				item_bar_start_resize (item_bar, item_bar->resize_pos);
				gnome_canvas_item_grab (item,
							GDK_POINTER_MOTION_MASK |
							GDK_BUTTON_RELEASE_MASK,
							item_bar->change_cursor,
							e->button.time);
			}
			
			npos = pos - item_bar->resize_start_pos;
			if (npos <= 0)
				break;

			item_bar->resize_width = npos;

			colrow_tip_setlabel (item_bar, is_vertical, item_bar->resize_width);
			resize_guide = GNOME_CANVAS_ITEM (item_bar->resize_guide);

			points = item_bar_get_line_points (canvas, item_bar, pos);

			gnome_canvas_item_set (resize_guide, "points",  points, NULL);
			gnome_canvas_points_free (points);

			/* Redraw the ItemBar to show nice incremental progress */
			gnome_canvas_request_redraw (
				canvas, 0, 0, INT_MAX, INT_MAX);

		}
		else if (ITEM_BAR_IS_SELECTING (item_bar))
		{

			element = get_col_from_pos (item_bar, pos);

			gtk_signal_emit (
				GTK_OBJECT (item),
				item_bar_signals [SELECTION_CHANGED],
				element, 0);

			set_cursor (item_bar, pos);
		}
		else
			set_cursor (item_bar, pos);
		break;

	case GDK_BUTTON_PRESS:
		convert (canvas, e->button.x, e->button.y, &x, &y);
		if (is_vertical)
			pos = y;
		else
			pos = x;

		cri = is_pointer_on_division (item_bar, pos, &start, &element);

		if (is_vertical) {
			if (element > SHEET_MAX_ROWS-1)
				break;
		} else {
			if (element > SHEET_MAX_COLS-1)
				break;
		}

		if (cri){
			/*
			 * Record the important bits.
			 *
			 * By setting resize_pos to a non -1 value,
			 * we know that we are being resized (used in the
			 * other event handlers).
			 */
			item_bar->resize_pos = element;
			item_bar->resize_start_pos = start - cri->pixels;
			item_bar->resize_width = cri->pixels;

			if (item_bar->tip == NULL) {
				item_bar->tip = gnumeric_create_tooltip ();
				colrow_tip_setlabel (item_bar, is_vertical, item_bar->resize_width);
				gnumeric_position_tooltip (item_bar->tip, !is_vertical);
				gtk_widget_show_all (gtk_widget_get_toplevel (item_bar->tip));
			}
		} else if (e->button.button == 3){
			Sheet   *sheet = item_bar->sheet_view->sheet;

			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!selection_contains_colrow (sheet, element, !is_vertical))
				gtk_signal_emit (GTK_OBJECT (item),
						 item_bar_signals [SELECTION_CHANGED],
						 element, e->button.state | GDK_BUTTON1_MASK);

			if (is_vertical)
				item_grid_popup_menu (sheet, e, 0, element);
			else
				item_grid_popup_menu (sheet, e, element, 0);
		} else {
			item_bar->start_selection = element;
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						item_bar->normal_cursor,
						e->button.time);
			gtk_signal_emit (GTK_OBJECT (item),
					 item_bar_signals [SELECTION_CHANGED],
					 element, e->button.state | GDK_BUTTON1_MASK);
		}
		break;

	case GDK_2BUTTON_PRESS: {
		Sheet *sheet;
		int new_size;
		
		if (!resizing)
			break;

		sheet = item_bar->sheet_view->sheet;
		if (is_vertical)
			new_size = sheet_row_size_fit (sheet, item_bar->resize_pos);
		else
			new_size = sheet_col_size_fit (sheet, item_bar->resize_pos);

		item_bar_end_resize (item_bar, new_size);
		}
		break;
		
	case GDK_BUTTON_RELEASE:
		if (e->button.button == 3)
			break;

		if (item_bar->resize_pos >= 0) {
			gnome_canvas_item_ungrab (item, e->button.time);
			item_bar_end_resize (item_bar, item_bar->resize_width);
		}
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
	item_bar->tip = NULL;
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
	case ARG_SHEET_VIEW:
		item_bar->sheet_view = GTK_VALUE_POINTER (*arg);
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
	}
	item_bar_update (item, NULL, NULL, 0);
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

	gtk_object_add_arg_type ("ItemBar::SheetView", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_VIEW);
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
	item_class->update      = item_bar_update;
	item_class->realize     = item_bar_realize;
	item_class->unrealize   = item_bar_unrealize;
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

