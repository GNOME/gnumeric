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
#include "gnumeric-pane.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "commands.h"

#include <gal/util/e-util.h>

static GnomeCanvasItemClass *item_bar_parent_class;

enum {
	ARG_0,
	ARG_GNUMERIC_SHEET,
	ARG_IS_COL_HEADER
};

struct _ItemBar {
	GnomeCanvasItem  canvas_item;

	GnumericSheet   *gsheet;
	GdkGC           *gc, *lines, *shade; /* Draw gc */
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
	StyleFont	*normal_font, *bold_font;
	GtkWidget       *tip;			/* Tip for scrolling */
	gboolean	 dragging : 1;
	gboolean	 is_col_header : 1;
	gboolean	 has_resize_guides : 1;
	int		 indent, cell_width, cell_height;
	int		 start_selection;	/* Where selection started */
	int		 colrow_being_resized;
	int		 colrow_resize_size;
	int		 resize_start_pos;
};

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemBarClass;

static int
ib_compute_pixels_from_indent (Sheet const *sheet, gboolean const is_cols)
{
	double const scale =
		sheet->last_zoom_factor_used *
		application_display_dpi_get (is_cols) / 72.;
	int const indent = is_cols
		? sheet->cols.max_outline_level
		: sheet->rows.max_outline_level;
	if (!sheet->display_outlines || indent <= 0)
		return 0;
	return (int)(5 + indent * 14 * scale + 0.5);
}

static void
ib_fonts_unref (ItemBar *ib)
{
	if (ib->normal_font != NULL) {
		style_font_unref (ib->normal_font);
		ib->normal_font = NULL;
	}

	if (ib->bold_font != NULL) {
		style_font_unref (ib->bold_font);
		ib->bold_font = NULL;
	}
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
	Sheet const *sheet = ((SheetControl *) ib->gsheet->scg)->sheet;
	double const zoom_factor = sheet->last_zoom_factor_used;
	double const res  = application_dpi_to_pixels ();

	/* ref before unref */
	StyleFont * const normal_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, FALSE, FALSE);
	StyleFont * const bold_font =
		style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
				       res*zoom_factor, TRUE, FALSE);

	/* Now that we have the new fonts unref the old ones */
	ib_fonts_unref (ib);

	/* And finish up by assigning the new fonts. */
	ib->normal_font = normal_font;
	ib->bold_font = bold_font;

	/* Use the size of the bold header font to size the free dimensions No
	 * need to zoom, the size of the font takes that into consideration.
	 * 2 pixels above and below
	 */
	ib->cell_height = 2 + 2 + style_font_get_height (bold_font);

	/* 5 pixels left and right plus the width of the widest string I can think of */
	ib->cell_width = 5 + 5 + gdk_string_width (
		style_font_gdk_font (bold_font), "88888");
	ib->indent = ib->is_col_header
		? ib_compute_pixels_from_indent (sheet, TRUE)
		: ib_compute_pixels_from_indent (sheet, FALSE);

	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (ib));

	return ib->indent +
		(ib->is_col_header ? ib->cell_height : ib->cell_width);
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

static void
item_bar_realize (GnomeCanvasItem *item)
{
	ItemBar *ib;
	GdkWindow *window;
	GdkGC *gc;

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->realize)(item);

	ib = ITEM_BAR (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Configure our gc */
	ib->gc = gc = gdk_gc_new (window);
	{
		GtkWidget *w = gtk_button_new ();
		GtkStyle *style;
		gtk_widget_ensure_style (w);

		style = gtk_widget_get_style (w);
		gdk_gc_set_foreground (ib->gc, &style->text[GTK_STATE_NORMAL]);

		ib->shade = gdk_gc_ref (style->dark_gc[GTK_STATE_NORMAL]);
		gtk_widget_destroy (w);
	}
	ib->lines = gdk_gc_new (window);
	gdk_gc_copy (ib->lines, gc);
	gdk_gc_set_line_attributes (ib->lines, 2, GDK_LINE_SOLID,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	ib->normal_cursor = gdk_cursor_new (GDK_ARROW);
	if (ib->is_col_header)
		ib->change_cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
	else
		ib->change_cursor = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	item_bar_calc_size (ib);
}

static void
item_bar_unrealize (GnomeCanvasItem *item)
{
	ItemBar *ib = ITEM_BAR (item);

	gdk_gc_unref (ib->gc);
	gdk_gc_unref (ib->lines);
	gdk_gc_unref (ib->shade);
	gdk_cursor_destroy (ib->change_cursor);
	gdk_cursor_destroy (ib->normal_cursor);

	if (GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_bar_parent_class)->unrealize)(item);
}

static void
ib_draw_cell (ItemBar const * const ib,
	      GdkDrawable *drawable, ColRowSelectionType const type,
	      char const * const str, GdkRectangle * rect)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (ib)->canvas);
	GdkFont *font;
	GdkGC *gc;
	int len, texth, shadow;

	switch (type){
	default:
	case COL_ROW_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		font = style_font_gdk_font (ib->normal_font);
		break;

	case COL_ROW_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->dark_gc [GTK_STATE_PRELIGHT];
		font = style_font_gdk_font (ib->bold_font);
		break;

	case COL_ROW_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		gc = canvas->style->dark_gc [GTK_STATE_NORMAL];
		font = style_font_gdk_font (ib->bold_font);
		break;
	}

	len = gdk_string_width (font, str);
	texth = font->ascent + font->descent;

	gdk_draw_rectangle (drawable, gc, TRUE,
			    rect->x + 1, rect->y + 1, rect->width-2, rect->height-2);
	gtk_draw_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
			 rect->x, rect->y, rect->width, rect->height);
	gdk_gc_set_clip_rectangle (ib->gc, rect);
	gdk_draw_string (drawable, font, ib->gc,
			 rect->x + (rect->width - len) / 2,
			 rect->y + (rect->height - texth) / 2 + font->ascent + 1,
			 str);
}

static void
item_bar_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemBar const         *ib = ITEM_BAR (item);
	GnumericSheet const   *gsheet = ib->gsheet;
	SheetControlGUI const *scg    = gsheet->scg;
	Sheet const           *sheet  = ((SheetControl *) scg)->sheet;
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (item)->canvas);
	ColRowInfo const *cri;
	int pixels;
	gboolean prev_visible;
	int prev_level;
	GdkRectangle rect;
	gboolean has_object = scg->new_object != NULL || scg->current_object != NULL;

	if (ib->is_col_header) {
		int const inc = (sheet->cols.max_outline_level > 0)
			? (ib->indent - 2) / sheet->cols.max_outline_level
			: 0;
		int const base_pos = .2 * inc - y;
		int const len = (inc > 4) ? 4 : inc;

		/* See comment above for explaination of the extra 1 pixel */
		int total = 1 + gsheet->col_offset.first - x;
		int col = gsheet->col.first;

		rect.y = ib->indent - y;
		rect.height = ib->cell_height;

		gdk_draw_line (drawable, ib->shade,
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
			if (col != -1 && ib->colrow_being_resized == col)
			/* || selection_contains_colrow (sheet, col, TRUE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					int level, i = 0, pos = base_pos;
					int left = total - pixels;

					rect.x = total - pixels;
					rect.width = pixels;
					ib_draw_cell (ib, drawable,
						       has_object ? COL_ROW_NO_SELECTION
						       : sheet_col_selection_type (sheet, col),
						       col_name (col), &rect);

					if (len > 0) {
						for (level = cri->outline_level; i++ < level ; pos += inc) {
							if (i > prev_level)
								gdk_draw_line (drawable, ib->lines,
									       left+1, pos,
									       left+1, pos+len);
							else
								left--; /* line loses 1 pixel */
							gdk_draw_line (drawable, ib->lines,
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
									gdk_draw_line (drawable, ib->lines,
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
								gdk_draw_line (drawable, ib->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
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
			? (ib->indent - 2) / sheet->rows.max_outline_level
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

		rect.x = ib->indent - x;
		rect.width = ib->cell_width;

		gdk_draw_line (drawable, ib->shade,
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
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_row_get_info (sheet, row);
			if (row != -1 && ib->colrow_being_resized == row)
			/* || selection_contains_colrow (sheet, row, FALSE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				total += pixels;
				if (total >= 0) {
					int level, i = 0, pos = base_pos;
					int top = total - pixels;

					rect.y = top;
					rect.height = pixels;
					ib_draw_cell (ib, drawable,
						       has_object ? COL_ROW_NO_SELECTION
						       : sheet_row_selection_type (sheet, row),
						       row_name (row), &rect);

					if (len > 0) {
						for (level = cri->outline_level; i++ < level ; pos += inc) {
							if (i > prev_level)
								gdk_draw_line (drawable, ib->lines,
									       pos,     top+1,
									       pos+len, top+1);
							else
								top--; /* line loses 1 pixel */
							gdk_draw_line (drawable, ib->lines,
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
									gdk_draw_line (drawable, ib->lines,
										       left+size/2, top+3,
										       left+size/2, top+size-4);
									if (!cri->is_collapsed)
										g_warning ("expected collapsed %s", row_name (row));
								}
								gdk_draw_line (drawable, ib->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
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
is_pointer_on_division (ItemBar const *ib, int pos, int *the_total, int *the_element)
{
	Sheet *sheet = ((SheetControl *) ib->gsheet->scg)->sheet;
	ColRowInfo *cri;
	int i, total = 0;

	for (i = 0; total < pos; i++) {
		if (ib->is_col_header) {
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
			if (!wbcg_rangesel_possible (ib->gsheet->scg->wbcg) &&
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
ib_set_cursor (ItemBar *ib, int x, int y)
{
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (ib)->canvas);
	GdkCursor *cursor = ib->normal_cursor;
	int major, minor;

	/* We might be invoked before we are realized */
	if (!canvas->window)
		return;

	if (!wbcg_edit_has_guru (ib->gsheet->scg->wbcg)) {
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
colrow_tip_setlabel (ItemBar *ib, gboolean const is_cols, int size_pixels)
{
	if (ib->tip) {
		char *buffer;
		double const scale = 72. / application_display_dpi_get (!is_cols);
		if (is_cols)
			buffer = g_strdup_printf (_("Width: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		else
			buffer = g_strdup_printf (_("Height: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		gtk_label_set_text (GTK_LABEL (ib->tip), buffer);
		g_free(buffer);
	}
}

static void
item_bar_end_resize (ItemBar *ib, int new_size)
{
	if (new_size != 0 && ib->colrow_being_resized >= 0)
		scg_colrow_size_set (ib->gsheet->scg,
				     ib->is_col_header,
				     ib->colrow_being_resized, new_size);
	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	scg_colrow_resize_end (ib->gsheet->scg);

	if (ib->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ib->tip));
		ib->tip = NULL;
	}
}

static gboolean
cb_extend_selection (GnumericSheet *gsheet,
		     int col, int row, gpointer user_data)
{
	ItemBar * const ib = user_data;
	gboolean const is_cols = ib->is_col_header;
	scg_colrow_select (gsheet->scg,
			   is_cols, is_cols ? col : row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (ItemBar const *ib, int element, int pixel)
{
	SheetControl *sc = (SheetControl *) ib->gsheet->scg;
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
	GnomeCanvas	* const canvas = item->canvas;
	ItemBar		* const ib = ITEM_BAR (item);
	GnumericSheet	* const gsheet = ib->gsheet;
	SheetControlGUI	* const scg = gsheet->scg;
	SheetControl	* const sc = (SheetControl *) gsheet->scg;
	Sheet		* const sheet = sc->sheet;
	WorkbookControlGUI * const wbcg = scg->wbcg;
	gboolean const is_cols = ib->is_col_header;
	int pos, other_pos, start, element, x, y;

	/* NOTE :
	 * No need to map coordinates since we do the zooming of the item bars manually
	 * there is no transform needed.
	 */
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		gnome_canvas_w2c (canvas, e->crossing.x, e->crossing.y, &x, &y);
		ib_set_cursor (ib, x, y);
		break;

	case GDK_MOTION_NOTIFY:
		gnome_canvas_w2c (canvas, e->motion.x, e->motion.y, &x, &y);

		/* Do col/row resizing or incremental marking */
		if (ib->colrow_being_resized != -1) {
			int new_size;
			if (!ib->has_resize_guides) {
				ib->has_resize_guides = TRUE;
				scg_colrow_resize_start	(ib->gsheet->scg,
							 ib->is_col_header,
							 ib->colrow_being_resized);

				gnome_canvas_item_grab (item,
							GDK_POINTER_MOTION_MASK |
							GDK_BUTTON_RELEASE_MASK,
							ib->change_cursor,
							e->motion.time);
			}

			pos = (is_cols) ? x : y;
			new_size = pos - ib->resize_start_pos;
			cri = sheet_colrow_get_info (sheet,
				ib->colrow_being_resized, is_cols);

			g_return_val_if_fail (cri != NULL, TRUE);

			/* Ensure we always have enough room for the margins */
			if (new_size < (cri->margin_a + cri->margin_b))
				new_size = cri->margin_a + cri->margin_b;

			ib->colrow_resize_size = new_size;
			colrow_tip_setlabel (ib, is_cols, new_size);
			scg_colrow_resize_move (scg, is_cols, pos);

			/* Redraw the ItemBar to show nice incremental progress */
			gnome_canvas_request_redraw (canvas, 0, 0, INT_MAX/2, INT_MAX/2);

		} else if (ib->start_selection != -1) {
			if (wbcg_edit_has_guru (wbcg) &&
			    !wbcg_edit_entry_redirect_p (wbcg))
				break;

			gnumeric_sheet_handle_motion (ib->gsheet,
				canvas, &e->motion,
				GNM_SLIDE_AT_COLROW_BOUND |
					is_cols ? GNM_SLIDE_X : GNM_SLIDE_Y,
				cb_extend_selection, ib);
		} else
			ib_set_cursor (ib, x, y);
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
		cri = is_pointer_on_division (ib, pos, &start, &element);
		if (element < 0)
			return FALSE;
		if (wbcg_edit_has_guru (wbcg))
			cri = NULL;
		else if (other_pos < ib->indent)
			return outline_button_press (ib, element, other_pos);

		if (e->button.button == 3) {
			if (wbcg_edit_has_guru (wbcg))
				return TRUE;
			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!selection_contains_colrow (sheet, element, is_cols))
				scg_colrow_select (scg, is_cols,
						   element, e->button.state);

			scg_context_menu (scg, &e->button, is_cols, !is_cols);
		} else if (cri != NULL) {
			/*
			 * Record the important bits.
			 *
			 * By setting colrow_being_resized to a non -1 value,
			 * we know that we are being resized (used in the
			 * other event handlers).
			 */
			ib->colrow_being_resized = element;
			ib->resize_start_pos = start - cri->size_pixels;
			ib->colrow_resize_size = cri->size_pixels;

			if (ib->tip == NULL) {
				ib->tip = gnumeric_create_tooltip ();
				colrow_tip_setlabel (ib, is_cols, ib->colrow_resize_size);
				gnumeric_position_tooltip (ib->tip, is_cols);
				gtk_widget_show_all (gtk_widget_get_toplevel (ib->tip));
			}
		} else {
			if (wbcg_edit_has_guru (wbcg) &&
			    !wbcg_edit_entry_redirect_p (wbcg))
				break;

			ib->start_selection = element;
			gnumeric_sheet_slide_init (gsheet);
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						ib->normal_cursor,
						e->button.time);

			scg_colrow_select (scg, is_cols,
					   element, e->button.state);
		}
		break;

	case GDK_2BUTTON_PRESS:
	{
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (e->button.button != 3)
			item_bar_end_resize (ib, -1);
		break;
	}

	case GDK_BUTTON_RELEASE:
	{
		gboolean needs_ungrab = FALSE;

		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		gnumeric_sheet_slide_stop (ib->gsheet);

		if (ib->start_selection >= 0) {
			needs_ungrab = TRUE;
			ib->start_selection = -1;
		}
		if (ib->colrow_being_resized >= 0) {
			if (ib->has_resize_guides) {
				needs_ungrab = TRUE;
				item_bar_end_resize (ib, ib->colrow_resize_size);
			} else
				/*
				 * No need to resize, nothing changed.
				 * This will handle the case of a double click.
				 */
				item_bar_end_resize (ib, 0);
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

static void
item_bar_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemBar *ib;

	item = GNOME_CANVAS_ITEM (o);
	ib = ITEM_BAR (o);

	switch (arg_id){
	case ARG_GNUMERIC_SHEET:
		ib->gsheet = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_IS_COL_HEADER:
		ib->is_col_header = GTK_VALUE_BOOL (*arg);
		break;
	}
	item_bar_update (item, NULL, NULL, 0);
}

static void
item_bar_finalize (GtkObject *object)
{
	ItemBar *bar = ITEM_BAR (object);

	ib_fonts_unref (bar);

	if (bar->tip) {
		gtk_object_unref (GTK_OBJECT (bar->tip));
		bar->tip = NULL;
	}

	if (GTK_OBJECT_CLASS (item_bar_parent_class)->finalize)
		(*GTK_OBJECT_CLASS (item_bar_parent_class)->finalize)(object);
}

static void
item_bar_init (ItemBar *ib)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (ib);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	ib->dragging = FALSE;
	ib->is_col_header = FALSE;
	ib->cell_width = ib->cell_height = 1;
	ib->indent = 0;
	ib->start_selection = -1;

	ib->normal_font = NULL;
	ib->bold_font = NULL;
	ib->tip = NULL;

	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
}

static void
item_bar_class_init (ItemBarClass *item_bar_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_bar_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_bar_class;
	item_class = (GnomeCanvasItemClass *) item_bar_class;

	gtk_object_add_arg_type ("ItemBar::GnumericSheet", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GNUMERIC_SHEET);
	gtk_object_add_arg_type ("ItemBar::IsColHeader", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_IS_COL_HEADER);

	item_class->update      = item_bar_update;
	item_class->realize     = item_bar_realize;
	item_class->unrealize   = item_bar_unrealize;
	item_class->draw        = item_bar_draw;
	item_class->point       = item_bar_point;
	item_class->translate   = item_bar_translate;
	item_class->event       = item_bar_event;
	object_class->finalize  = item_bar_finalize;
	object_class->set_arg   = item_bar_set_arg;
}

E_MAKE_TYPE (item_bar, "ItemBar", ItemBar,
	     item_bar_class_init, item_bar_init,
	     GNOME_TYPE_CANVAS_ITEM);
