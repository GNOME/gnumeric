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
#include "parse-util.h"
#include "gnumeric-util.h"
#include "selection.h"
#include "workbook-cmd-format.h"
#include "application.h"
#include "style.h"

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

#define ITEM_BAR_RESIZING(x) (ITEM_BAR(x)->resize_pos != -1)

static void
item_bar_fonts_unref (ItemBar *item_bar)
{
	if (item_bar->normal_font != NULL) {
		style_font_unref (item_bar->normal_font);
		item_bar->normal_font = NULL;
	}

	if (item_bar->bold_font != NULL) {
		style_font_unref (item_bar->bold_font);
		item_bar->bold_font = NULL;
	}
}

static void
item_bar_destroy (GtkObject *object)
{
	ItemBar *bar;

	bar = ITEM_BAR (object);

	item_bar_fonts_unref (bar);

	if (bar->tip)
		gtk_object_unref (GTK_OBJECT (bar->tip));

	if (GTK_OBJECT_CLASS (item_bar_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_bar_parent_class)->destroy)(object);
}

/*
 * Scale the item-bar heading fonts by the pixels_per_unit of
 * th associated sheet.
 */
void
item_bar_fonts_init (ItemBar *item_bar)
{
	double const zoom_factor =
		item_bar->sheet_view->sheet->last_zoom_factor_used;
	double const res  = application_dpi_to_pixels ();
	StyleFont * const normal_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, FALSE, FALSE);
	StyleFont * const bold_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, TRUE, FALSE);

	/* Now that we have the new fonts unref the old ones */
	item_bar_fonts_unref (item_bar);

	/* And finish up by assigning the new fonts. */
	item_bar->normal_font = normal_font;
	item_bar->bold_font = bold_font;
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

	item_bar_fonts_init (item_bar);
}

static void
item_bar_unrealize (GnomeCanvasItem *item)
{
	ItemBar *item_bar = ITEM_BAR (item);

	gdk_gc_unref (item_bar->gc);
	gdk_cursor_destroy (item_bar->change_cursor);
	gdk_cursor_destroy (item_bar->normal_cursor);
	item_bar_fonts_unref (item_bar);

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)(item);
}

static void
item_bar_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->update)(item, affine, clip_path, flags);

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
bar_draw_cell (ItemBar const * const item_bar,
	       GdkDrawable *drawable, ColRowSelectionType const type,
	       char const * const str, GdkRectangle * rect)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item_bar)->canvas);
	GdkFont *font;
	GdkGC *gc;
	int len, texth, shadow;

	switch (type){
	default:
	case COL_ROW_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		font = style_font_gdk_font (item_bar->normal_font);
		break;

	case COL_ROW_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->dark_gc [GTK_STATE_PRELIGHT];
		font = style_font_gdk_font (item_bar->bold_font);
		break;

	case COL_ROW_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		gc = canvas->style->dark_gc [GTK_STATE_NORMAL];
		font = style_font_gdk_font (item_bar->bold_font);
		break;
	}

	len = gdk_string_width (font, str);
	texth = font->ascent + font->descent;

	gdk_gc_set_clip_rectangle (gc, rect);
	gdk_draw_rectangle (drawable, gc, TRUE, rect->x + 1, rect->y + 1, rect->width-2, rect->height-2);
	gtk_draw_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
			 rect->x, rect->y, rect->width, rect->height);
	gdk_draw_string (drawable, font, item_bar->gc,
			 rect->x + (rect->width - len) / 2,
			 rect->y + (rect->height - texth) / 2 + font->ascent + 1,
			 str);
}

static void
item_bar_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemBar const * const item_bar = ITEM_BAR (item);
	Sheet   const * const sheet = item_bar->sheet_view->sheet;
	GnumericSheet const * const gsheet = GNUMERIC_SHEET (item_bar->sheet_view->canvas);
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item)->canvas);
	int pixels;
	GdkRectangle rect;

	if (item_bar->orientation == GTK_ORIENTATION_VERTICAL) {
		/* Include a 1 pixel buffer.
		 * To avoid overlaping the cells the shared pixel belongs to the cell above.
		 * This has the nice property that the bottom dark line of the
		 * shadow aligns with the grid lines.
		 * Unfortunately it also implies a 1 pixel empty space at the
		 * top of the bar.  Which we are forced to fill in with
		 * something.  For now I draw a black line there to be
		 * compatible with the default colour used on the bottom of the
		 * cell shadows.
		 */
		int total = 1 + gsheet->row_offset.first - y;
		int element = gsheet->row.first;

		rect.x = -x;
		rect.width = canvas->allocation.width;

		/* FIXME : How to avoid hard coding this color ?
		 * We need the color that will be drawn as the bottom bevel
		 * for the button shadow
		 */
		gdk_draw_line (drawable, canvas->style->black_gc,
			       rect.x, total-1,
			       rect.x + rect.width, total-1);
		do {
			ColRowInfo const *cri;
			if (element >= SHEET_MAX_ROWS)
				return;

			cri = sheet_row_get_info (sheet, element);
			pixels = (item_bar->resize_pos != element)
			    ? cri->size_pixels
			    : item_bar->resize_width;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					char const * const str = get_row_name (element);
					rect.y = total - pixels;
					rect.height = pixels;
					bar_draw_cell (item_bar, drawable,
						       sheet_row_selection_type (sheet, element),
						       str, &rect);
				}
			}
			++element;
		} while (total < height);
	} else {
		/* See comment above for explaination of the extra 1 pixel */
		int total = 1 + gsheet->col_offset.first - x;
		int element = gsheet->col.first;

		rect.y = -y;
		rect.height = canvas->allocation.height;

		/* FIXME : How to avoid hard coding this color ?
		 * We need the color that will be drawn as the right bevel
		 * for the button shadow
		 */
		gdk_draw_line (drawable, canvas->style->black_gc,
			       total-1, rect.y,
			       total-1, rect.y + rect.height);

		do {
			ColRowInfo const *cri;
			if (element >= SHEET_MAX_COLS)
				return;

			cri = sheet_col_get_info (sheet, element);
			pixels = (item_bar->resize_pos != element)
			    ? cri->size_pixels
			    : item_bar->resize_width;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					rect.x = total - pixels;
					rect.width = pixels;
					bar_draw_cell (item_bar, drawable,
						       sheet_col_selection_type (sheet, element),
						       col_name (element), &rect);
				}
			}

			++element;
		} while (total < width);
	}
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
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
			if (i >= SHEET_MAX_ROWS)
				return NULL;
			cri = sheet_row_get_info (sheet, i);
		} else {
			if (i >= SHEET_MAX_COLS)
				return NULL;
			cri = sheet_col_get_info (sheet, i);
		}

		if (cri->visible) {
			total += cri->size_pixels;
			if ((total - 4 < pos) && (pos < total + 4)) {
				if (the_total)
					*the_total = total;
				if (the_element)
					*the_element = i;

				return cri;
			}
		}

		if (total > pos) {
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
		gdk_window_set_cursor (canvas->window, item_bar->change_cursor);
	else
		gdk_window_set_cursor (canvas->window, item_bar->normal_cursor);
}

static void
item_bar_start_resize (ItemBar *bar)
{
	Sheet const * const sheet = bar->sheet_view->sheet;
#if 0
	/*
	 * handle the zoom from the item-grid canvas, the resolution scaling is
	 * handled elsewhere
	 */
	double const res  = application_display_dpi_get (bar->orientation ==
							 GTK_ORIENTATION_VERTICAL);
#endif
	double const zoom = sheet->last_zoom_factor_used; /* * res / 72.; */
	GnumericSheet const * const gsheet = GNUMERIC_SHEET (bar->sheet_view->canvas);
	GnomeCanvas const * const canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasGroup * const group = GNOME_CANVAS_GROUP (canvas->root);
	GnomeCanvasPoints * const points =
	    bar->resize_points = gnome_canvas_points_new (2);
	GnomeCanvasItem * item =
	    gnome_canvas_item_new ( group,
				    gnome_canvas_line_get_type (),
				    "fill_color", "black",
				    "width_pixels", 1,
				    NULL);
	bar->resize_guide = GTK_OBJECT (item);

	/* NOTE : Set the position of the stationary line here.
	 * Set the guide line later based on the motion coordinates.
	 */
	if (bar->orientation == GTK_ORIENTATION_VERTICAL) {
		double const y =
		    sheet_row_get_distance_pixels (sheet, 0, bar->resize_pos) / zoom;
		points->coords [0] =
		    sheet_col_get_distance_pixels (sheet, 0, gsheet->col.first) / zoom;
		points->coords [1] = y;
		points->coords [2] =
		    sheet_col_get_distance_pixels (sheet, 0, gsheet->col.last_visible+1) / zoom;
		points->coords [3] = y;
	} else {
		double const x =
		    sheet_col_get_distance_pixels (sheet, 0, bar->resize_pos) / zoom;
		points->coords [0] = x;
		points->coords [1] =
		    sheet_row_get_distance_pixels (sheet, 0, gsheet->row.first) / zoom;
		points->coords [2] = x;
		points->coords [3] =
		    sheet_row_get_distance_pixels (sheet, 0, gsheet->row.last_visible+1) / zoom;
	}

	item = gnome_canvas_item_new ( group,
				       gnome_canvas_line_get_type (),
				       "points", points,
				       "fill_color", "black",
				       "width_pixels", 1,
				       NULL);
	bar->resize_start = GTK_OBJECT (item);
}

static int
get_element_from_pixel (ItemBar *item_bar, int pos)
{
	ColRowInfo *cri;
	Sheet *sheet;
	int i, total;

	total = 0;
	sheet = item_bar->sheet_view->sheet;
	for (i = item_bar->first_element; total < pos; i++){
		if (item_bar->orientation == GTK_ORIENTATION_VERTICAL){
			if (i >= SHEET_MAX_ROWS)
				return i-1;
			cri = sheet_row_get_info (sheet, i);
		} else {
			if (i >= SHEET_MAX_COLS)
				return i-1;
			cri = sheet_col_get_info (sheet, i);
		}

		if (cri->visible) {
			total += cri->size_pixels;
			if (total > pos)
				return i;
		}
	}
	return i;
}

static void
colrow_tip_setlabel (ItemBar *item_bar, gboolean const is_vertical, int size_pixels)
{
	if (item_bar->tip) {
		char buffer [20 + sizeof (long) * 4];
		double const scale = 72. / application_display_dpi_get (is_vertical);
		if (is_vertical)
			snprintf (buffer, sizeof (buffer), _("Height: %.2f pts (%d pixels)"),
				  scale*size_pixels, size_pixels);
		else
			snprintf (buffer, sizeof (buffer), _("Width: %.2f pts (%d pixels)"),
				  scale*size_pixels, size_pixels);
		gtk_label_set_text (GTK_LABEL (item_bar->tip), buffer);
	}
}

static void
item_bar_end_resize (ItemBar *item_bar, int new_size)
{
	if (new_size != 0)
		gtk_signal_emit (GTK_OBJECT (item_bar),
				 item_bar_signals [SIZE_CHANGED],
				 item_bar->resize_pos,
				 new_size);

	if (item_bar->resize_points) {
		gnome_canvas_points_free (item_bar->resize_points);
		item_bar->resize_points = NULL;
	}
	if (item_bar->resize_guide) {
		gtk_object_destroy (item_bar->resize_start);
		item_bar->resize_start = NULL;
		gtk_object_destroy (item_bar->resize_guide);
		item_bar->resize_guide = NULL;
	}
	if (item_bar->tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (item_bar->tip));
		item_bar->tip = NULL;
	}

	item_bar->resize_pos = -1;
}

static gboolean
cb_extend_selection (SheetView *sheet_view, int col, int row, gpointer user_data)
{
	ItemBar * const item_bar = user_data;
	gboolean const is_vertical = (item_bar->orientation == GTK_ORIENTATION_VERTICAL);
	gtk_signal_emit (GTK_OBJECT (item_bar),
			 item_bar_signals [SELECTION_CHANGED],
			 is_vertical ? row : col, 0);
	return TRUE;
}

static gint
item_bar_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ColRowInfo *cri;
	GnomeCanvas * const canvas = item->canvas;
	ItemBar * const item_bar = ITEM_BAR (item);
	Sheet   * const sheet = item_bar->sheet_view->sheet;
	GnumericSheet * const gsheet = GNUMERIC_SHEET (item_bar->sheet_view->canvas);
	gboolean const is_vertical = (item_bar->orientation == GTK_ORIENTATION_VERTICAL);
#if 0
	/*
	 * handle the zoom from the item-grid canvas, the resolution scaling is
	 * handled elsewhere
	 */
	double const res  = application_display_dpi_get (is_vertical);
#endif
	double const zoom = sheet->last_zoom_factor_used; /* * res / 72.; */
	int pos, start, element;

	/* NOTE :
	 * No need to map coordinates since we do the zooming of the item bars manually
	 * there is no transform needed.
	 */
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		if (is_vertical)
			pos = e->crossing.y;
		else
			pos = e->crossing.x;
		set_cursor (item_bar, pos);
		break;

	case GDK_MOTION_NOTIFY:
		if (is_vertical)
			pos = e->motion.y;
		else
			pos = e->motion.x;

		/* Do col/row resizing or incremental marking */
		if (item_bar->resize_pos != -1) {
			GnomeCanvasItem *resize_guide;
			GnomeCanvasPoints *points;
			int npos;

			if (item_bar->resize_guide == NULL) {
				item_bar_start_resize (item_bar);
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
			points = item_bar->resize_points;

			if (is_vertical)
				points->coords [1] = points->coords [3] = pos / zoom;
			else
				points->coords [0] = points->coords [2] = pos / zoom;

			gnome_canvas_item_set (resize_guide, "points",  points, NULL);

			/* Redraw the ItemBar to show nice incremental progress */
			gnome_canvas_request_redraw (
				canvas, 0, 0, INT_MAX, INT_MAX);

		} else if (ITEM_BAR_IS_SELECTING (item_bar)) {
			int x, y, left, top, width, height, col, row;
			element = get_element_from_pixel (item_bar, pos);

			gtk_signal_emit (
				GTK_OBJECT (item),
				item_bar_signals [SELECTION_CHANGED],
				element, 0);

			gnome_canvas_w2c (canvas, e->motion.x, e->motion.y, &x, &y);
			gnome_canvas_get_scroll_offsets (canvas, &left, &top);

			width = GTK_WIDGET (canvas)->allocation.width;
			height = GTK_WIDGET (canvas)->allocation.height;

			col = gnumeric_sheet_find_col (gsheet, x, NULL);
			row = gnumeric_sheet_find_row (gsheet, y, NULL);

			if (x < left || y < top || x >= left + width || y >= top + height) {
				int dx = 0, dy = 0;

				if (is_vertical) {
					if (y < top)
						dy = y - top;
					else if (y >= top + height)
						dy = y - height - top;
				} else {
					if (x < left)
						dx = x - left;
					else if (x >= left + width)
						dx = x - width - left;
				}

				sheet_view_start_sliding (item_bar->sheet_view,
							  &cb_extend_selection, item_bar,
							  col, row, dx, dy);
			} else
				sheet_view_stop_sliding (item_bar->sheet_view);

			set_cursor (item_bar, pos);
		} else
			set_cursor (item_bar, pos);
		break;

	case GDK_BUTTON_PRESS:
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (is_vertical)
			pos = e->button.y;
		else
			pos = e->button.x;

		cri = is_pointer_on_division (item_bar, pos, &start, &element);

		if (is_vertical) {
			if (element > SHEET_MAX_ROWS-1)
				break;
		} else {
			if (element > SHEET_MAX_COLS-1)
				break;
		}

		if (e->button.button == 3) {
			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!selection_contains_colrow (sheet, element, !is_vertical))
				gtk_signal_emit (GTK_OBJECT (item),
						 item_bar_signals [SELECTION_CHANGED],
						 element, e->button.state | GDK_BUTTON1_MASK);

			item_grid_popup_menu (item_bar->sheet_view, &e->button,
					      !is_vertical, is_vertical);
		} else if (cri) {
			/*
			 * Record the important bits.
			 *
			 * By setting resize_pos to a non -1 value,
			 * we know that we are being resized (used in the
			 * other event handlers).
			 */
			item_bar->resize_pos = element;
			item_bar->resize_start_pos = start - cri->size_pixels;
			item_bar->resize_width = cri->size_pixels;

			if (item_bar->tip == NULL) {
				item_bar->tip = gnumeric_create_tooltip ();
				colrow_tip_setlabel (item_bar, is_vertical, item_bar->resize_width);
				gnumeric_position_tooltip (item_bar->tip, !is_vertical);
				gtk_widget_show_all (gtk_widget_get_toplevel (item_bar->tip));
			}
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

	case GDK_2BUTTON_PRESS:
	{
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (e->button.button == 3)
			break;

		item_bar_end_resize (item_bar, -1);
		break;
	}
		
	case GDK_BUTTON_RELEASE:
	{
		gboolean needs_ungrab = FALSE;

		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		sheet_view_stop_sliding (item_bar->sheet_view);

		if (item_bar->start_selection >= 0) {
			needs_ungrab = TRUE;
			item_bar->start_selection = -1;
		}
		if (item_bar->resize_pos >= 0) {
			if (item_bar->resize_guide != NULL) {
				needs_ungrab = TRUE;
				item_bar_end_resize (item_bar, item_bar->resize_width);
			} else
				/*
				 * No need to resize, nothing changed.
				 * This will handle the case of a double click.
				 */
				item_bar_end_resize (item_bar, 0);
		}
		if (needs_ungrab)
			gnome_canvas_item_ungrab (item, e->button.time);
		break;
	}

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
	item_bar->normal_font = NULL;
	item_bar->bold_font = NULL;
	item_bar->resize_guide = NULL;
	item_bar->resize_start = NULL;
	item_bar->resize_points = NULL;
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

	item_bar_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_bar_class;
	item_class = (GnomeCanvasItemClass *) item_bar_class;

	gtk_object_add_arg_type ("ItemBar::SheetView", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_VIEW);
	gtk_object_add_arg_type ("ItemBar::Orientation", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_ORIENTATION);

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

