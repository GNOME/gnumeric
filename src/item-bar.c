/* vim: set sw=8: */
/*
 * A canvas item implementing row/col headers with support for outlining.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jgoldberg@home.com)
 */
#include <config.h>

#include "item-bar.h"
#define GNUMERIC_ITEM "BAR"
#include "item-debug.h"
#include "style.h"
#include "sheet.h"
#include "sheet-control-gui-priv.h"
#include "application.h"
#include "selection.h"
#include "gnumeric-sheet.h"
#include "workbook-edit.h"
#include "gnumeric-type-util.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "commands.h"

/* The signals we emit */
enum {
	LAST_SIGNAL
};
static guint item_bar_signals [LAST_SIGNAL + 1] = { 0 };

static GnomeCanvasItem *item_bar_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,
	ARG_IS_COL_HEADER,
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

static int
compute_pixels_from_indent (Sheet const *sheet, int indent, gboolean const horizontal)
{
	double const scale =
		sheet->last_zoom_factor_used *
		application_display_dpi_get (horizontal) / 72.;

	if (!sheet->display_outlines || indent <= 0)
		return 0;
	return (int)(5 + indent * 14 * scale + 0.5);
}

/**
 * item_bar_calc_size :
 *
 * Scale fonts and sizes by the pixels_per_unit of the associated sheet.
 *
 * returns : the size of the fixed dimension.
 */
int
item_bar_calc_size (ItemBar *ib)
{
	Sheet const *sheet = ((SheetControl *) ib->scg)->sheet;
	double const zoom_factor = sheet->last_zoom_factor_used;
	double const res  = application_dpi_to_pixels ();
	StyleFont * const normal_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, FALSE, FALSE);
	StyleFont * const bold_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, TRUE, FALSE);

	/* Now that we have the new fonts unref the old ones */
	item_bar_fonts_unref (ib);

	/* And finish up by assigning the new fonts. */
	ib->normal_font = normal_font;
	ib->bold_font = bold_font;

	/*
	* Use the size of the bold header font to size the free dimensions
	* No need to zoom, the size of the font takes that into consideration.
	*/
	/* 2 pixels above and below */
	ib->cell_height = 2 + 2 + style_font_get_height (bold_font);

	/* 5 pixels left and right plus the width of the widest string I can think of */
	ib->cell_width = 5 + 5 + gdk_string_width (
		style_font_gdk_font (bold_font), "88888");
	ib->indent = ib->is_col_header
		? compute_pixels_from_indent (sheet, sheet->cols.max_outline_level, TRUE)
		: compute_pixels_from_indent (sheet, sheet->rows.max_outline_level, FALSE);

	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (ib));

	return ib->indent +
		(ib->is_col_header ? ib->cell_height : ib->cell_width);
}

static void
item_bar_realize (GnomeCanvasItem *item)
{
	ItemBar *item_bar;
	GdkWindow *window;
	GdkGC *gc;

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)(item);

	item_bar = ITEM_BAR (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Configure our gc */
	item_bar->gc = gc = gdk_gc_new (window);
	{
		GtkWidget *w = gtk_button_new();
		GtkStyle *style;
		gtk_widget_ensure_style(w);

		style = gtk_widget_get_style (w);
		gdk_gc_set_foreground (item_bar->gc, &style->text[GTK_STATE_NORMAL]);

		item_bar->shade = gdk_gc_ref (style->dark_gc[GTK_STATE_NORMAL]);
		gtk_widget_destroy(w);		
	}
	item_bar->lines = gdk_gc_new (window);
	gdk_gc_copy (item_bar->lines, gc);
	gdk_gc_set_line_attributes (item_bar->lines, 2, GDK_LINE_SOLID,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	item_bar->normal_cursor = gdk_cursor_new (GDK_ARROW);
	if (item_bar->is_col_header)
		item_bar->change_cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
	else
		item_bar->change_cursor = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	item_bar_calc_size (item_bar);
}

static void
item_bar_unrealize (GnomeCanvasItem *item)
{
	ItemBar *item_bar = ITEM_BAR (item);

	gdk_gc_unref (item_bar->gc);
	gdk_gc_unref (item_bar->lines);
	gdk_gc_unref (item_bar->shade);
	gdk_cursor_destroy (item_bar->change_cursor);
	gdk_cursor_destroy (item_bar->normal_cursor);
	item_bar_fonts_unref (item_bar);

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)(item);
}

static void
item_bar_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemBar *ib = ITEM_BAR (item);

	item->x1 = 0;
	item->y1 = 0;
	if (ib->is_col_header) {
		item->x2 = INT_MAX;
		item->y2 = (ib->cell_height + ib->indent);
	} else {
		item->x2 = (ib->cell_width  + ib->indent);
		item->y2 = INT_MAX;
	}

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->update)(item, affine, clip_path, flags);
}

static const char *
get_row_name (int n)
{
	static char x [4 * sizeof (int)];

	g_return_val_if_fail (n >= 0 && n < SHEET_MAX_ROWS, "0");

	snprintf (x, sizeof (x)-1, "%d", n + 1);
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

	gdk_draw_rectangle (drawable, gc, TRUE,
			    rect->x + 1, rect->y + 1, rect->width-2, rect->height-2);
	gtk_draw_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
			 rect->x, rect->y, rect->width, rect->height);
	gdk_gc_set_clip_rectangle (item_bar->gc, rect);
	gdk_draw_string (drawable, font, item_bar->gc,
			 rect->x + (rect->width - len) / 2,
			 rect->y + (rect->height - texth) / 2 + font->ascent + 1,
			 str);
}

static void
item_bar_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemBar const         *item_bar = ITEM_BAR (item);
	SheetControlGUI const *scg = item_bar->scg;
	Sheet const           *sheet = ((SheetControl *) scg)->sheet;
	GnumericSheet const   *gsheet = GNUMERIC_SHEET (scg->canvas);
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item)->canvas);
	ColRowInfo const *cri;
	int pixels;
	gboolean prev_visible;
	int prev_level;
	GdkRectangle rect;
	gboolean has_object = scg->new_object != NULL || scg->current_object != NULL;

	if (item_bar->is_col_header) {
		int const inc = (sheet->cols.max_outline_level > 0)
			? (item_bar->indent - 2) / sheet->cols.max_outline_level
			: 0;
		int const base_pos = .2 * inc - y;
		int const len = (inc > 4) ? 4 : inc;

		/* See comment above for explaination of the extra 1 pixel */
		int total = 1 + gsheet->col_offset.first - x;
		int col = gsheet->col.first;

		rect.y = item_bar->indent - y;
		rect.height = item_bar->cell_height;

		gdk_draw_line (drawable, item_bar->shade,
			       total-1, rect.y,
			       total-1, rect.y + rect.height);

		if (col > 0) {
			cri = sheet_col_get_info (sheet, col-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (col >= SHEET_MAX_COLS)
				return;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_col_get_info (sheet, col);
			if (item_bar->resize_pos != -1 &&
			    ((item_bar->resize_pos == col)))
				/* || selection_contains_colrow (sheet, col, TRUE))) */
				pixels = item_bar->resize_width;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					int level, i = 0, pos = base_pos;
					int left = total - pixels;

					rect.x = total - pixels;
					rect.width = pixels;
					bar_draw_cell (item_bar, drawable,
						       has_object ? COL_ROW_NO_SELECTION
						       : sheet_col_selection_type (sheet, col),
						       col_name (col), &rect);

					if (len > 0) {
						for (level = cri->outline_level; i++ < level ; pos += inc) {
							if (i > prev_level)
								gdk_draw_line (drawable, item_bar->lines,
									       left+1, pos,
									       left+1, pos+len);
							else
								left--; /* line loses 1 pixel */
							gdk_draw_line (drawable, item_bar->lines,
								       left, pos,
								       total+1, pos);
						}

						if (!prev_visible || prev_level > level) {
							int safety = 0;
							int top = pos - base_pos - y;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							top += 2; /* inside cell's shadow */
							gtk_draw_shadow (canvas->style, drawable,
									 GTK_STATE_NORMAL,
									 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
									 left, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									left++;
									gdk_draw_line (drawable, item_bar->lines,
										       left+size/2, top+3,
										       left+size/2, top+size-4);
									/*
									 * This fails miserably if grouped cols/rows are present, why
									 * is this here? This is basically assuming that if col X is
									 * not visible than col X + 1 must be collapsed ?!?
									 */
									if (!cri->is_collapsed)
										g_warning ("expected collapsed %s", col_name (col));
								}
								gdk_draw_line (drawable, item_bar->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, item_bar->lines,
								       total-pixels/2, pos,
								       total-pixels/2, pos+len);
					}
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++col;
		} while (total < width);
	} else {
		int const inc = (sheet->rows.max_outline_level > 0)
			? (item_bar->indent - 2) / sheet->rows.max_outline_level
			: 0;
		int const base_pos = .2 * inc - x;
		int const len = (inc > 4) ? 4 : inc;

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
		int row = gsheet->row.first;

		rect.x = item_bar->indent - x;
		rect.width = item_bar->cell_width;

		gdk_draw_line (drawable, item_bar->shade,
			       rect.x, total-1,
			       rect.x + rect.width, total-1);
		if (row > 0) {
			cri = sheet_row_get_info (sheet, row-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (row >= SHEET_MAX_ROWS)
				return;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead of it move
			 */
			cri = sheet_row_get_info (sheet, row);
			if (item_bar->resize_pos != -1 &&
			    ((item_bar->resize_pos == row)))
			     /* || selection_contains_colrow (sheet, row, FALSE))) */
				pixels = item_bar->resize_width;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					char const * const str = get_row_name (row);
					int level, i = 0, pos = base_pos;
					int top = total - pixels;

					rect.y = top;
					rect.height = pixels;
					bar_draw_cell (item_bar, drawable,
						       has_object ? COL_ROW_NO_SELECTION
						       : sheet_row_selection_type (sheet, row),
						       str, &rect);

					if (len > 0) {
						for (level = cri->outline_level; i++ < level ; pos += inc) {
							if (i > prev_level)
								gdk_draw_line (drawable, item_bar->lines,
									       pos,     top+1,
									       pos+len, top+1);
							else
								top--; /* line loses 1 pixel */
							gdk_draw_line (drawable, item_bar->lines,
								       pos, top,
								       pos, total+1);
						}

						if (prev_level > level) {
							int safety = 0;
							int left = pos - base_pos - x;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							top += 2; /* inside cell's shadow */
							gtk_draw_shadow (canvas->style, drawable,
									 GTK_STATE_NORMAL,
									 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
									 left+safety, top, size, size);
							if (size > 9) {
								if (!prev_visible) {
									left++;
									top++;
									gdk_draw_line (drawable, item_bar->lines,
										       left+size/2, top+3,
										       left+size/2, top+size-4);
									if (!cri->is_collapsed)
										g_warning ("expected collapsed %d", row+1);
								}
								gdk_draw_line (drawable, item_bar->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, item_bar->lines,
								       pos,      total-pixels/2,
								       pos+len,  total-pixels/2);
					}
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++row;
		} while (total < height);
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

/**
 * is_pointer_on_division :
 *
 * NOTE : this could easily be optimized.  We need not start at 0 every time.
 *        We could potentially use the routines in gnumeric-sheet.
 *
 * return -1 if the event is outside the item boundary
 */
static ColRowInfo *
is_pointer_on_division (ItemBar const *item_bar, int pos, int *the_total, int *the_element)
{
	Sheet *sheet = ((SheetControl *) item_bar->scg)->sheet;
	ColRowInfo *cri;
	int i, total = 0;

	for (i = item_bar->first_element; total < pos; i++) {
		if (item_bar->is_col_header) {
			if (i >= SHEET_MAX_COLS) {
				if (the_element)
					*the_element = -1;
				return NULL;
			}
			cri = sheet_col_get_info (sheet, i);
		} else {
			if (i >= SHEET_MAX_ROWS) {
				if (the_element)
					*the_element = -1;
				return NULL;
			}
			cri = sheet_row_get_info (sheet, i);
		}

		if (cri->visible) {
			total += cri->size_pixels;

			/* TODO : This is more expensive than it needs to be.
			 * We should really set a flag (in scg ?) and adjust
			 * it as the cursor in the entry is moved.  The current
			 * approach recalculates the state every time.
			 */
			if (!wbcg_rangesel_possible (item_bar->scg->wbcg) &&
			    (total - 4 < pos) && (pos < total + 4)) {
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
	if (the_element)
		*the_element = -1;
	return NULL;
}

static void
set_cursor (ItemBar *ib, int x, int y)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (ib)->canvas);
	GdkCursor *cursor = ib->normal_cursor;
	int major, minor;

	/* We might be invoked before we are realized */
	if (!canvas->window)
		return;

	if (!wbcg_edit_has_guru (ib->scg->wbcg)) {
		if (ib->is_col_header) {
			major = x;
			minor = y;
		} else {
			major = y;
			minor = x;
		}

		if (minor >= ib->indent &&
		    is_pointer_on_division (ib, major, NULL, NULL) != NULL)
			cursor = ib->change_cursor;
	}
	gdk_window_set_cursor (canvas->window, cursor);
}

static void
item_bar_start_resize (ItemBar *ib)
{
	SheetControlGUI const * const scg = ib->scg;
	Sheet const * const sheet = ((SheetControl *) scg)->sheet;
	double const zoom = sheet->last_zoom_factor_used; /* * res / 72.; */
	GnumericSheet const * const gsheet = GNUMERIC_SHEET (scg->canvas);
	GnomeCanvas const * const canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasGroup * const group = GNOME_CANVAS_GROUP (canvas->root);
	GnomeCanvasPoints * const points =
		ib->resize_points = gnome_canvas_points_new (2);
	GnomeCanvasItem * item =
		gnome_canvas_item_new ( group,
					gnome_canvas_line_get_type (),
					"fill_color", "black",
					"width_pixels", 1,
					NULL);
	ib->resize_guide = GTK_OBJECT (item);

	/* NOTE : Set the position of the stationary line here.
	 * Set the guide line later based on the motion coordinates.
	 */
	if (ib->is_col_header) {
		double const x = scg_colrow_distance_get (scg, TRUE,
					0, ib->resize_pos) / zoom;
		points->coords [0] = x;
		points->coords [1] = scg_colrow_distance_get (scg, FALSE,
					0, gsheet->row.first) / zoom;
		points->coords [2] = x;
		points->coords [3] = scg_colrow_distance_get (scg, FALSE,
					0, gsheet->row.last_visible+1) / zoom;
	} else {
		double const y = scg_colrow_distance_get (scg, FALSE,
					0, ib->resize_pos) / zoom;
		points->coords [0] = scg_colrow_distance_get (scg, TRUE,
					0, gsheet->col.first) / zoom;
		points->coords [1] = y;
		points->coords [2] = scg_colrow_distance_get (scg, TRUE,
					0, gsheet->col.last_visible+1) / zoom;
		points->coords [3] = y;
	}

	item = gnome_canvas_item_new ( group,
				       gnome_canvas_line_get_type (),
				       "points", points,
				       "fill_color", "black",
				       "width_pixels", 1,
				       NULL);
	ib->resize_start = GTK_OBJECT (item);
}

static void
colrow_tip_setlabel (ItemBar *item_bar, gboolean const is_cols, int size_pixels)
{
	if (item_bar->tip) {
		char *buffer;
		double const scale = 72. / application_display_dpi_get (!is_cols);
		if (is_cols)
			buffer = g_strdup_printf (_("Width: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		else
			buffer = g_strdup_printf (_("Height: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		gtk_label_set_text (GTK_LABEL (item_bar->tip), buffer);
		g_free(buffer);
	}
}

static void
item_bar_end_resize (ItemBar *item_bar, int new_size)
{
	if (new_size != 0 && item_bar->resize_pos >= 0)
		scg_colrow_size_set (item_bar->scg,
				     item_bar->is_col_header,
				     item_bar->resize_pos, new_size);

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
cb_extend_selection (SheetControlGUI *scg, int col, int row, gpointer user_data)
{
	ItemBar * const item_bar = user_data;
	gboolean const is_cols = item_bar->is_col_header;
	scg_colrow_select (item_bar->scg, is_cols, is_cols ? col : row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (ItemBar const *ib, int element, int pixel)
{
	SheetControl *sc = (SheetControl *) ib->scg;
	Sheet * const sheet = sc->sheet;
	int inc, step;

	if (ib->is_col_header) {
		if (sheet->cols.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / sheet->cols.max_outline_level;
	} else if (sheet->rows.max_outline_level > 0)
		inc = (ib->indent - 2) / sheet->rows.max_outline_level;
	else
		return TRUE;

	step = pixel / inc;

	cmd_colrow_outline_change (sc->wbc, sheet,
				   ib->is_col_header, element, step);
	return TRUE;
}

static gint
item_bar_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ColRowInfo *cri;
	GnomeCanvas * const canvas = item->canvas;
	ItemBar * const item_bar = ITEM_BAR (item);
	SheetControl *sc = (SheetControl *) item_bar->scg;
	Sheet   * const sheet = sc->sheet;
	GnumericSheet * const gsheet = GNUMERIC_SHEET (item_bar->scg->canvas);
	WorkbookControlGUI * const wbcg = item_bar->scg->wbcg;
	gboolean const is_cols = item_bar->is_col_header;
	double const zoom = sheet->last_zoom_factor_used;
	int pos, other_pos, start, element, x, y;

	/* NOTE :
	 * No need to map coordinates since we do the zooming of the item bars manually
	 * there is no transform needed.
	 */
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		gnome_canvas_w2c (canvas, e->crossing.x, e->crossing.y, &x, &y);
		set_cursor (item_bar, x, y);
		break;

	case GDK_MOTION_NOTIFY:
		gnome_canvas_w2c (canvas, e->motion.x, e->motion.y, &x, &y);
		pos = (is_cols) ? x : y;

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
							e->motion.time);
			}

			npos = pos - item_bar->resize_start_pos;
			if (npos <= 0)
				break;

			item_bar->resize_width = npos;

			colrow_tip_setlabel (item_bar, is_cols, item_bar->resize_width);
			resize_guide = GNOME_CANVAS_ITEM (item_bar->resize_guide);
			points = item_bar->resize_points;

			if (is_cols)
				points->coords [0] = points->coords [2] = pos / zoom;
			else
				points->coords [1] = points->coords [3] = pos / zoom;

			gnome_canvas_item_set (resize_guide, "points",  points, NULL);

			/* Redraw the ItemBar to show nice incremental progress */
			gnome_canvas_request_redraw (canvas, 0, 0, INT_MAX/2, INT_MAX/2);

		} else if (item_bar->start_selection != -1) {
			int left, top, width, height, col, row;

			gnome_canvas_get_scroll_offsets (canvas, &left, &top);

			width = GTK_WIDGET (canvas)->allocation.width;
			height = GTK_WIDGET (canvas)->allocation.height;

			col = gnumeric_sheet_find_col (gsheet, x, NULL);
			row = gnumeric_sheet_find_row (gsheet, y, NULL);

			
			if (wbcg_edit_has_guru (wbcg) &&
			    !wbcg_edit_entry_redirect_p (wbcg))
				break;
			
			scg_colrow_select (item_bar->scg, is_cols,
					   is_cols ? col : row,
					   GDK_SHIFT_MASK);
			if (x < left || y < top || x >= left + width || y >= top + height) {
				int dx = 0, dy = 0;

				if (is_cols) {
					if (x < left)
						dx = x - left;
					else if (x >= left + width)
						dx = x - width - left;
				} else {
					if (y < top)
						dy = y - top;
					else if (y >= top + height)
						dy = y - height - top;
				}

				scg_start_sliding (item_bar->scg,
						   &cb_extend_selection, item_bar,
						   col, row, dx, dy);
			} else
				scg_stop_sliding (item_bar->scg);
		}
		set_cursor (item_bar, x, y);
		break;

	case GDK_BUTTON_PRESS:
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (!wbcg_edit_has_guru (wbcg))
			scg_mode_edit (sc);

		gnome_canvas_w2c (canvas, e->button.x, e->button.y, &x, &y);
		if (is_cols) {
			pos = x;
			other_pos = y;
		} else {
			pos = y;
			other_pos = x;
		}
		cri = is_pointer_on_division (item_bar, pos, &start, &element);
		if (element < 0)
			return FALSE;
		if (wbcg_edit_has_guru (wbcg))
			cri = NULL;
		else if (other_pos < item_bar->indent)
			return outline_button_press (item_bar, element, other_pos);

		if (e->button.button == 3) {
			if (wbcg_edit_has_guru (wbcg))
				return TRUE;
			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!selection_contains_colrow (sheet, element, is_cols))
				scg_colrow_select (item_bar->scg, is_cols,
						   element, e->button.state);

			scg_context_menu (item_bar->scg, &e->button,
					  is_cols, !is_cols);
		} else if (cri != NULL) {
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
				colrow_tip_setlabel (item_bar, is_cols, item_bar->resize_width);
				gnumeric_position_tooltip (item_bar->tip, is_cols);
				gtk_widget_show_all (gtk_widget_get_toplevel (item_bar->tip));
			}
		} else {
			if (wbcg_edit_has_guru (wbcg) &&
			    !wbcg_edit_entry_redirect_p (wbcg))
				break;
			
			item_bar->start_selection = element;
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						item_bar->normal_cursor,
						e->button.time);

			scg_colrow_select (item_bar->scg, is_cols,
					   element, e->button.state);
		}
		break;

	case GDK_2BUTTON_PRESS:
	{
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (e->button.button != 3)
			item_bar_end_resize (item_bar, -1);
		break;
	}

	case GDK_BUTTON_RELEASE:
	{
		gboolean needs_ungrab = FALSE;

		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		scg_stop_sliding (item_bar->scg);

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

	item_bar->dragging = FALSE;
	item_bar->is_col_header = FALSE;
	item_bar->first_element = 0;
	item_bar->cell_width = item_bar->cell_height = 1;
	item_bar->indent = 0;
	item_bar->start_selection = -1;

	item_bar->normal_font = NULL;
	item_bar->bold_font = NULL;
	item_bar->tip = NULL;

	item_bar->resize_pos = -1;
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
	case ARG_SHEET_CONTROL_GUI:
		item_bar->scg = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_IS_COL_HEADER:
		item_bar->is_col_header = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_FIRST_ELEMENT :
		v = GTK_VALUE_INT (*arg);
		if (item_bar->first_element != v)
			item_bar->first_element = v;
		break;
	}
	item_bar_update (item, NULL, NULL, 0);
}

typedef struct {
	GnomeCanvasItemClass parent_class;

	/* Signals emited */
	void (* selection_changed) (ItemBar *, int column, int modifiers);
	void (* size_changed)      (ItemBar *, int column, int new_width);
} ItemBarClass;

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

	gtk_object_add_arg_type ("ItemBar::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);
	gtk_object_add_arg_type ("ItemBar::IsColHeader", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_IS_COL_HEADER);
	gtk_object_add_arg_type ("ItemBar::FirstElement", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_FIRST_ELEMENT);

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

GNUMERIC_MAKE_TYPE (item_bar, "ItemBar", ItemBar,
		    item_bar_class_init, item_bar_init,
		    gnome_canvas_item_get_type ())
