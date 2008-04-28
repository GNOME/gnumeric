/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * item-grid.c : A canvas item that is responsible for drawing gridlines and
 *     cell content.  One item per sheet displays all the cells.
 *
 * Authors:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 *
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "item-grid.h"

#include "gnm-pane-impl.h"
#include "wbc-gtk-impl.h"
#include "workbook-view.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet-object-impl.h"
#include "cell.h"
#include "cell-draw.h"
#include "cellspan.h"
#include "ranges.h"
#include "selection.h"
#include "parse-util.h"
#include "mstyle.h"
#include "style-conditions.h"
#include "gnm-style-impl.h"	/* cheesy */
#include "position.h"		/* to eval conditions */
#include "style-border.h"
#include "style-color.h"
#include "pattern.h"
#include "commands.h"
#include "hlink.h"
#include "gui-util.h"

#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkicontheme.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>
#include <string.h>
#define GNUMERIC_ITEM "GRID"
#include "item-debug.h"

#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

typedef enum {
	ITEM_GRID_NO_SELECTION,
	ITEM_GRID_SELECTING_CELL_RANGE,
	ITEM_GRID_SELECTING_FORMULA_RANGE
} ItemGridSelectionType;

struct _ItemGrid {
	FooCanvasItem canvas_item;

	SheetControlGUI *scg;

	ItemGridSelectionType selecting;

	struct {
		GdkGC      *fill;	/* Default background fill gc */
		GdkGC      *cell;	/* Color used for the cell */
		GdkGC      *empty;	/* GC used for drawing empty cells */
		GdkGC      *bound;	/* the dark line at the edge */
	} gc;

	GnmRange bound;

	/* information for the cursor motion handler */
	guint cursor_timer;
	double last_x, last_y;
	GnmHLink *cur_link; /* do not derference, just a pointer */
	GtkWidget *tip;
	guint tip_timer;

	GdkCursor *cursor_link, *cursor_cross;

	guint32 last_click_time;
};
typedef FooCanvasItemClass ItemGridClass;
static FooCanvasItemClass *parent_class;

enum {
	ITEM_GRID_PROP_0,
	ITEM_GRID_PROP_SHEET_CONTROL_GUI,
	ITEM_GRID_PROP_BOUND
};

static void
ig_clear_hlink_tip (ItemGrid *ig)
{
	if (ig->tip_timer != 0) {
		g_source_remove (ig->tip_timer);
		ig->tip_timer = 0;
	}

	if (ig->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ig->tip));
		ig->tip = NULL;
	}
}

static void
item_grid_finalize (GObject *object)
{
	ItemGrid *ig = ITEM_GRID (object);

	if (ig->cursor_timer != 0) {
		g_source_remove (ig->cursor_timer);
		ig->cursor_timer = 0;
	}
	ig_clear_hlink_tip (ig);
	ig->cur_link = NULL;

	(*G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static gint
cb_cursor_motion (ItemGrid *ig)
{
	Sheet const *sheet = scg_sheet (ig->scg);
	FooCanvas *canvas = ig->canvas_item.canvas;
	GnmPane *pane = GNM_PANE (canvas);
	int x, y;
	GdkCursor *cursor;
	GnmCellPos pos;
	GnmHLink *old_link;

	foo_canvas_w2c (canvas, ig->last_x, ig->last_y, &x, &y);
	pos.col = gnm_pane_find_col (pane, x, NULL);
	pos.row = gnm_pane_find_row (pane, y, NULL);

	old_link = ig->cur_link;
	ig->cur_link = sheet_hlink_find (sheet, &pos);
	cursor = (ig->cur_link != NULL) ? ig->cursor_link : ig->cursor_cross;
	if (pane->mouse_cursor != cursor) {
		gnm_pane_mouse_cursor_set (pane, cursor);
		scg_set_display_cursor (ig->scg);
	}

	if (ig->cursor_timer != 0) {
		g_source_remove (ig->cursor_timer);
		ig->cursor_timer = 0;
	}

	if (old_link != ig->cur_link && ig->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ig->tip));
		ig->tip = NULL;
	}
	return FALSE;
}

static void
item_grid_realize (FooCanvasItem *item)
{
	GdkWindow  *window;
	GdkDisplay *display;
	ItemGrid   *ig;

	if (parent_class->realize)
		(*parent_class->realize) (item);

	ig = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Configure the default grid gc */
	ig->gc.cell = gdk_gc_new (window);
	gdk_gc_set_fill (ig->gc.cell, GDK_SOLID);

	ig->gc.empty = gdk_gc_new (window);

	ig->gc.fill = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (ig->gc.fill,  &gs_white);
	gdk_gc_set_rgb_bg_color (ig->gc.fill,  &gs_light_gray);

	ig->gc.bound = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (ig->gc.bound, &gs_dark_gray);
	gdk_gc_set_line_attributes (ig->gc.bound, 3, GDK_LINE_SOLID,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	display = gtk_widget_get_display (GTK_WIDGET (item->canvas));
	ig->cursor_link  = gdk_cursor_new_for_display (display, GDK_HAND2);
	ig->cursor_cross = gdk_cursor_new_from_pixbuf (display,
			gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "cursor_cross", 32, 0, NULL),
			17, 17);
	cb_cursor_motion (ig);
}

static void
item_grid_unrealize (FooCanvasItem *item)
{
	ItemGrid *ig = ITEM_GRID (item);

	gdk_cursor_unref (ig->cursor_link);
	gdk_cursor_unref (ig->cursor_cross);

	g_object_unref (G_OBJECT (ig->gc.fill));	ig->gc.fill = NULL;
	g_object_unref (G_OBJECT (ig->gc.cell));	ig->gc.cell = NULL;
	g_object_unref (G_OBJECT (ig->gc.empty));	ig->gc.empty = NULL;
	g_object_unref (G_OBJECT (ig->gc.bound));	ig->gc.bound = NULL;

	if (parent_class->unrealize)
		(*parent_class->unrealize) (item);
}

static void
item_grid_update (FooCanvasItem *item, double i2w_dx, double i2w_dy, int flags)
{
	if (parent_class->update)
		(*parent_class->update) (item, i2w_dx, i2w_dy, flags);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX/2;
	item->y2 = INT_MAX/2;
}

static void
item_grid_draw_merged_range (GdkDrawable *drawable, ItemGrid *ig,
			     int start_x, int start_y,
			     GnmRange const *view, GnmRange const *range,
			     gboolean draw_selection)
{
	int l, r, t, b, last;
	GdkGC *gc = ig->gc.empty;
	SheetView const *sv = scg_view (ig->scg);
	Sheet const *sheet  = sv->sheet;
	GnmCell  const *cell   = sheet_cell_get (sheet, range->start.col, range->start.row);
	int const dir = sheet->text_is_rtl ? -1 : 1;

	/* load style from corner which may not be visible */
	GnmStyle const *style = sheet_style_get (sheet, range->start.col, range->start.row);
	gboolean const is_selected = draw_selection &&
		(sv->edit_pos.col != range->start.col ||
		 sv->edit_pos.row != range->start.row) &&
		sv_is_full_range_selected (sv, range);

	/* Get the coordinates of the visible region */
	l = r = start_x;
	if (view->start.col < range->start.col)
		l += dir * scg_colrow_distance_get (ig->scg, TRUE,
			view->start.col, range->start.col);
	if (range->end.col <= (last = view->end.col))
		last = range->end.col;
	r += dir * scg_colrow_distance_get (ig->scg, TRUE, view->start.col, last+1);

	t = b = start_y;
	if (view->start.row < range->start.row)
		t += scg_colrow_distance_get (ig->scg, FALSE,
			view->start.row, range->start.row);
	if (range->end.row <= (last = view->end.row))
		last = range->end.row;
	b += scg_colrow_distance_get (ig->scg, FALSE, view->start.row, last+1);

	if (l == r || t == b)
		return;

	if (style->conditions) {
		GnmEvalPos ep;
		int res;
		eval_pos_init (&ep, (Sheet *)sheet, range->start.col, range->start.row);
		if ((res = gnm_style_conditions_eval (style->conditions, &ep)) >= 0)
			style = g_ptr_array_index (style->cond_styles, res);
	}

	/* Check for background THEN selection */
	if (gnumeric_background_set_gc (style, gc,
		ig->canvas_item.canvas, is_selected) ||
	    is_selected) {
		/* Remember X excludes the far pixels */
		if (dir > 0)
			gdk_draw_rectangle (drawable, gc, TRUE, l, t, r-l+1, b-t+1);
		else
			gdk_draw_rectangle (drawable, gc, TRUE, r, t, l-r+1, b-t+1);
	}

	/* Expand the coords to include non-visible areas too.  The clipped
	 * region is only necessary when drawing the background */
	if (range->start.col < view->start.col)
		l -= dir * scg_colrow_distance_get (ig->scg, TRUE,
			range->start.col, view->start.col);
	if (view->end.col < range->end.col)
		r += dir * scg_colrow_distance_get (ig->scg, TRUE,
			view->end.col+1, range->end.col+1);
	if (range->start.row < view->start.row)
		t -= scg_colrow_distance_get (ig->scg, FALSE,
			range->start.row, view->start.row);
	if (view->end.row < range->end.row)
		b += scg_colrow_distance_get (ig->scg, FALSE,
			view->end.row+1, range->end.row+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;

		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *)ri, cell->pos.row, sheet);

		if (dir > 0)
			cell_draw (cell, ig->gc.cell, drawable,
				l, t, r - l, b - t, -1);
		else
			cell_draw (cell, ig->gc.cell, drawable,
				r, t, l - r, b - t, -1);
	}
	if (dir > 0)
		gnm_style_border_draw_diag (style, drawable, l, t, r, b);
	else
		gnm_style_border_draw_diag (style, drawable, r, t, l, b);
}

static void
item_grid_draw_background (GdkDrawable *drawable, ItemGrid *ig,
			   GnmStyle const *style,
			   int col, int row, int x, int y, int w, int h,
			   gboolean draw_selection)
{
	GdkGC           *gc = ig->gc.empty;
	SheetView const *sv = scg_view (ig->scg);
	gboolean const is_selected = draw_selection &&
		(sv->edit_pos.col != col || sv->edit_pos.row != row) &&
		sv_is_pos_selected (sv, col, row);
	gboolean const has_back =
		gnumeric_background_set_gc (style, gc,
					    ig->canvas_item.canvas,
					    is_selected);

#if DEBUG_SELECTION_PAINT
	if (is_selected) {
		g_printerr ("x = %d, w = %d\n", x, w+1);
	}
#endif
	if (has_back || is_selected)
		/* Fill the entire cell (API excludes far pixel) */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);

	gnm_style_border_draw_diag (style, drawable, x, y, x+w, y+h);
}

static gint
merged_col_cmp (GnmRange const *a, GnmRange const *b)
{
	return a->start.col - b->start.col;
}

static void
item_grid_draw (FooCanvasItem *item, GdkDrawable *drawable, GdkEventExpose *expose)
{
	gint width  = expose->area.width;
	gint height = expose->area.height;
	FooCanvas *canvas = item->canvas;
	GnmPane *pane = GNM_PANE (canvas);
	Sheet const *sheet = scg_sheet (pane->simple.scg);
	WBCGtk *wbcg = scg_wbcg (pane->simple.scg);
	GnmCell const * const edit_cell = wbcg->editing_cell;
	ItemGrid *ig = ITEM_GRID (item);
	ColRowInfo const *ri = NULL, *next_ri = NULL;
	int const dir = sheet->text_is_rtl ? -1 : 1;

	/* To ensure that far and near borders get drawn we pretend to draw +-2
	 * pixels around the target area which would include the surrounding
	 * borders if necessary */
	/* TODO : there is an opportunity to speed up the redraw loop by only
	 * painting the borders of the edges and not the content.
	 * However, that feels like more hassle that it is worth.  Look into this someday.
	 */
	int x, y, col, row, n, start_col, end_col, start_x, offset;
	int start_row = gnm_pane_find_row (pane, expose->area.y-2, &y);
	int end_row = gnm_pane_find_row (pane, expose->area.y+height+2, NULL);
	int const start_y = y;

	GnmStyleRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none =
		sheet->hide_grid ? NULL : gnm_style_border_none ();

	GnmRange     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	int *colwidths = NULL;

	gboolean const draw_selection =
		ig->scg->selected_objects == NULL &&
		ig->scg->new_object == NULL;

	if (dir < 0) {
		start_col = gnm_pane_find_col (pane, expose->area.x+width+2, &start_x);
		end_col   = gnm_pane_find_col (pane, expose->area.x-2, NULL);
	} else {
		start_col = gnm_pane_find_col (pane, expose->area.x-2, &start_x);
		end_col   = gnm_pane_find_col (pane, expose->area.x+width+2, NULL);
	}

	g_return_if_fail (start_col <= end_col);

#if 0
	g_printerr ("%s:", cell_coord_name (start_col, start_row));
	g_printerr ("%s <= %d vs %d", cell_coord_name(end_col, end_row), y, expose->area.y);
	g_printerr (" [%s]\n", cell_coord_name (ig->bound.end.col, ig->bound.end.row));

#endif

	/* clip to bounds */
	if (end_col > ig->bound.end.col)
		end_col = ig->bound.end.col;
	if (end_row > ig->bound.end.row)
		end_row = ig->bound.end.row;

	/* Skip any hidden cols/rows at the start */
	for (; start_col <= end_col ; ++start_col) {
		ri = sheet_col_get_info (sheet, start_col);
		if (ri->visible)
			break;
	}
	for (; start_row <= end_row ; ++start_row) {
		ri = sheet_row_get_info (sheet, start_row);
		if (ri->visible)
			break;
	}

	/* if everything is hidden no need to draw */
	if (end_col < ig->bound.start.col || start_col > ig->bound.end.col ||
	    end_row < ig->bound.start.row || start_row > ig->bound.end.row)
		return;

	/* Respan all rows that need it.  */
	for (row = start_row; row <= end_row; row++) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, row);
		if (ri->visible && ri->needs_respan)
			row_calc_spans ((ColRowInfo *)ri, row, sheet);
	}

	sheet_style_update_grid_color (sheet);

	/* Fill entire region with default background (even past far edge) */
	gdk_draw_rectangle (drawable, ig->gc.fill, TRUE,
		expose->area.x, expose->area.y, width, height);

	/* Get ordered list of merged regions */
	merged_active = merged_active_seen = merged_used = NULL;
	merged_unused = gnm_sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 *	- 6 arrays of n GnmBorder const *
	 *	- 2 arrays of n GnmStyle const *
	 *
	 * then alias the arrays for easy access so that array [col] is valid
	 * for all elements start_col-1 .. end_col+1 inclusive.
	 * Note that this means that in some cases array [-1] is legal.
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	style_row_init (&prev_vert, &sr, &next_sr, start_col, end_col,
			g_alloca (n * 8 * sizeof (gpointer)), sheet->hide_grid);

	/* load up the styles for the first row */
	next_sr.row = sr.row = row = start_row;
	sheet_style_get_row (sheet, &sr);

	/* Collect the column widths */
	colwidths = g_alloca (n * sizeof (int));
	colwidths -= start_col;
	for (col = start_col; col <= end_col; col++) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);
		colwidths[col] = ci->visible ? ci->size_pixels : -1;
	}

	x = start_x; /* make gcc happy in case there are no visible rows */
	for (y = start_y; row <= end_row; row = sr.row = next_sr.row, ri = next_ri) {
		/* Restore the set of ranges seen, but still active.
		 * Reinverting list to maintain the original order */
		g_return_if_fail (merged_active == NULL);

#if DEBUG_SELECTION_PAINT
		g_printerr ("row = %d (startcol = %d)\n", row, start_col);
#endif
		while (merged_active_seen != NULL) {
			GSList *tmp = merged_active_seen->next;
			merged_active_seen->next = merged_active;
			merged_active = merged_active_seen;
			merged_active_seen = tmp;
			MERGE_DEBUG (merged_active->data, " : seen -> active\n");
		}

		/* find the next visible row */
		while (1) {
			++next_sr.row;
			if (next_sr.row <= end_row) {
				next_ri = sheet_row_get_info (sheet, next_sr.row);
				if (next_ri->visible) {
					sheet_style_get_row (sheet, &next_sr);
					break;
				}
			} else {
				for (col = start_col ; col <= end_col; ++col)
					next_sr.vertical [col] =
					next_sr.bottom [col] = none;
				break;
			}
		}

		/* look for merges that start on this row, on the first painted row
		 * also check for merges that start above. */
		view.start.row = row;
		lag = &merged_unused;
		for (ptr = merged_unused; ptr != NULL; ) {
			GnmRange * const r = ptr->data;

			if (r->start.row <= row) {
				GSList *tmp = ptr;
				ptr = *lag = tmp->next;
				if (r->end.row < row) {
					tmp->next = merged_used;
					merged_used = tmp;
					MERGE_DEBUG (r, " : unused -> used\n");
				} else {
					ColRowInfo const *ci =
						sheet_col_get_info (sheet, r->start.col);
					g_slist_free_1 (tmp);
					merged_active = g_slist_insert_sorted (merged_active, r,
								(GCompareFunc)merged_col_cmp);
					MERGE_DEBUG (r, " : unused -> active\n");

					if (ci->visible)
						item_grid_draw_merged_range (drawable, ig,
							start_x, y, &view, r, draw_selection);
				}
			} else {
				lag = &(ptr->next);
				ptr = ptr->next;
			}
		}

		for (col = start_col, x = start_x; col <= end_col ; col++) {
			GnmStyle const *style;
			CellSpanInfo const *span;
			ColRowInfo const *ci = sheet_col_get_info (sheet, col);

#if DEBUG_SELECTION_PAINT
			g_printerr ("col [%d] = %d\n", col, x);
#endif
			if (!ci->visible) {
				if (merged_active != NULL) {
					GnmRange const *r = merged_active->data;
					if (r->end.col == col) {
						ptr = merged_active;
						merged_active = merged_active->next;
						if (r->end.row <= row) {
							ptr->next = merged_used;
							merged_used = ptr;
							MERGE_DEBUG (r, " : active2 -> used\n");
						} else {
							ptr->next = merged_active_seen;
							merged_active_seen = ptr;
							MERGE_DEBUG (r, " : active2 -> seen\n");
						}
					}
				}
				continue;
			}

			/* Skip any merged regions */
			if (merged_active != NULL) {
				GnmRange const *r = merged_active->data;
				if (r->start.col <= col) {
					gboolean clear_top, clear_bottom = FALSE;
					int i, first = r->start.col;
					int last  = r->end.col;

					ptr = merged_active;
					merged_active = merged_active->next;
					if (r->end.row <= row) {
						ptr->next = merged_used;
						merged_used = ptr;
						MERGE_DEBUG (r, " : active -> used\n");

						/* in case something managed the bottom of a merge */
						if (r->end.row < row)
							goto plain_draw;
					} else {
						ptr->next = merged_active_seen;
						merged_active_seen = ptr;
						MERGE_DEBUG (r, " : active -> seen\n");
						if (next_sr.row <= r->end.row)
							clear_bottom = TRUE;
					}

					x += dir * scg_colrow_distance_get (
						pane->simple.scg, TRUE, col, last+1);
					col = last;

					if (first < start_col) {
						first = start_col;
						sr.vertical [first] = NULL;
					}
					if (last > end_col) {
						last = end_col;
						sr.vertical [last+1] = NULL;
					}
					clear_top = (r->start.row != row);

					/* Clear the borders */
					for (i = first ; i <= last ; i++) {
						if (clear_top)
							sr.top [i] = NULL;
						if (clear_bottom)
							sr.bottom [i] = NULL;
						if (i > first)
							sr.vertical [i] = NULL;
					}
					continue;
				}
			}

plain_draw : /* a quick hack to deal with 142267 */
			if (dir < 0)
				x -= ci->size_pixels;
			style = sr.styles [col];
			item_grid_draw_background (drawable, ig,
				style, col, row, x, y,
				ci->size_pixels, ri->size_pixels,
				draw_selection);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->spans != NULL)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (NULL == ri->spans || NULL == (span = row_span_get (ri, col))) {

				/* If it is being edited pretend it is empty to
				 * avoid problems with the a long cells
				 * contents extending past the edge of the edit
				 * box.  Ignore blanks too.
				 */
				GnmCell const *cell = sheet_cell_get (sheet, col, row);
				if (!gnm_cell_is_empty (cell) && cell != edit_cell)
					cell_draw (cell, ig->gc.cell, drawable,
						   x, y, ci->size_pixels,
						   ri->size_pixels, -1);

			/* Only draw spaning cells after all the backgrounds
			 * that we are going to draw have been drawn.  No need
			 * to draw the edit cell, or blanks. */
			} else if (edit_cell != span->cell &&
				   (col == span->right || col == end_col)) {
				GnmCell const *cell = span->cell;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				int real_x = x;
				ColRowInfo const *cell_col =
					sheet_col_get_info (sheet, cell->pos.col);
				int center_offset = cell_col->size_pixels/2;
				int tmp_width = ci->size_pixels;

				if (col != cell->pos.col)
					style = sheet_style_get (sheet,
						cell->pos.col, row);

				/* x, y are relative to this cell origin, but the cell
				 * might be using columns to the left (if it is set to right
				 * justify or center justify) compute the pixel difference */
				if (start_span_col != cell->pos.col)
					center_offset += scg_colrow_distance_get (
						pane->simple.scg, TRUE,
						start_span_col, cell->pos.col);

				if (start_span_col != col) {
					offset = scg_colrow_distance_get (
						pane->simple.scg, TRUE,
						start_span_col, col);
					tmp_width += offset;
					if (dir > 0)
						real_x -= offset;
					sr.vertical [col] = NULL;
				}
				if (end_span_col != col) {
					offset = scg_colrow_distance_get (
						pane->simple.scg, TRUE,
						col+1, end_span_col + 1);
					tmp_width += offset;
					if (dir < 0)
						real_x -= offset;
				}

				cell_draw (cell, ig->gc.cell, drawable,
					   real_x, y, tmp_width,
					   ri->size_pixels, center_offset);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			if (dir > 0)
				x += ci->size_pixels;
		}
		gnm_style_borders_row_draw (prev_vert, &sr,
					drawable, start_x, y, y+ri->size_pixels,
					colwidths, TRUE, dir);

		/* In case there were hidden merges that trailed off the end */
		while (merged_active != NULL) {
			GnmRange const *r = merged_active->data;
			ptr = merged_active;
			merged_active = merged_active->next;
			if (r->end.row <= row) {
				ptr->next = merged_used;
				merged_used = ptr;
				MERGE_DEBUG (r, " : active3 -> used\n");
			} else {
				ptr->next = merged_active_seen;
				merged_active_seen = ptr;
				MERGE_DEBUG (r, " : active3 -> seen\n");
			}
		}

		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

		y += ri->size_pixels;
	}

	if (row >= ig->bound.end.row) {
		gnm_style_borders_row_draw (prev_vert, &sr,
			drawable, start_x, y, y, colwidths, FALSE, dir);
		if (pane->index >= 2)
			gdk_draw_line (drawable, ig->gc.bound, start_x, y, x, y);
	}
	if (col >= ig->bound.end.col &&
	    /* TODO : Add pane flags to avoid hard coding pane numbers */
	    (pane->index == 1 || pane->index == 2))
		gdk_draw_line (drawable, ig->gc.bound, x, start_y, x, y);

	g_slist_free (merged_used);	   /* merges with bottom in view */
	g_slist_free (merged_active_seen); /* merges with bottom the view */
	g_slist_free (merged_unused);	   /* merges in hidden rows */
	g_return_if_fail (merged_active == NULL);
}

static double
item_grid_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		 FooCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/***********************************************************************/

static gboolean
ig_obj_create_begin (ItemGrid *ig, GdkEventButton *event)
{
	GnmPane *pane = GNM_PANE (FOO_CANVAS_ITEM (ig)->canvas);
	SheetObject *so = ig->scg->new_object;
	SheetObjectAnchor anchor;
	double coords[4];

	g_return_val_if_fail (ig->scg->selected_objects == NULL, TRUE);
	g_return_val_if_fail (ig->scg->new_object != NULL, TRUE);

	coords[0] = coords[2] = event->x;
	coords[1] = coords[3] = event->y;
	sheet_object_anchor_init (&anchor, NULL, NULL, GOD_ANCHOR_DIR_DOWN_RIGHT);
	scg_object_coords_to_anchor (ig->scg, coords, &anchor);
	sheet_object_set_anchor (so, &anchor);
	sheet_object_set_sheet (so, scg_sheet (ig->scg));
	scg_object_select (ig->scg, so);
	gnm_pane_object_start_resize (pane, event, so, 7, TRUE);

	return TRUE;
}

/***************************************************************************/

static int
item_grid_button_press (ItemGrid *ig, GdkEventButton *event)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ig);
	FooCanvas    *canvas = item->canvas;
	GnmPane *pane = GNM_PANE (canvas);
	SheetControlGUI *scg = ig->scg;
	WBCGtk *wbcg = scg_wbcg (scg);
	SheetControl	*sc = (SheetControl *)scg;
	SheetView	*sv = sc_view (sc);
	Sheet		*sheet = sv_sheet (sv);
	GnmCellPos	pos;
	int x, y;
	gboolean edit_showed_dialog;
	gboolean already_selected;

	gnm_pane_slide_stop (pane);

	foo_canvas_w2c (canvas, event->x, event->y, &x, &y);
	pos.col = gnm_pane_find_col (pane, x, NULL);
	pos.row = gnm_pane_find_row (pane, y, NULL);

	/* GnmRange check first */
	if (pos.col >= gnm_sheet_get_max_cols (sheet))
		return TRUE;
	if (pos.row >= gnm_sheet_get_max_rows (sheet))
		return TRUE;

	/* A new object is ready to be realized and inserted */
	if (scg->new_object != NULL)
		return ig_obj_create_begin (ig, event);

	/* If we are not configuring an object then clicking on the sheet
	 * ends the edit.  */
	if (scg->selected_objects == NULL)
		wbcg_focus_cur_scg (wbcg);
	else if (wbc_gtk_get_guru (wbcg) == NULL)
		scg_mode_edit (scg);

	/* If we were already selecting a range of cells for a formula,
	 * reset the location to a new place, or extend the selection.
	 */
	if (event->button == 1 && scg->rangesel.active) {
		ig->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		if (event->state & GDK_SHIFT_MASK)
			scg_rangesel_extend_to (scg, pos.col, pos.row);
		else
			scg_rangesel_bound (scg, pos.col, pos.row, pos.col, pos.row);
		gnm_pane_slide_init (pane);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		return TRUE;
	}

	/* If the user is editing a formula (wbcg_rangesel_possible) then we
	 * enable the dynamic cell selection mode.
	 */
	if (event->button == 1 && wbcg_rangesel_possible (wbcg)) {
		scg_rangesel_start (scg, pos.col, pos.row, pos.col, pos.row);
		ig->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnm_pane_slide_init (pane);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		return TRUE;
	}

	/* While a guru is up ignore clicks */
	if (wbc_gtk_get_guru (wbcg) != NULL)
		return TRUE;

	/* This was a regular click on a cell on the spreadsheet.  Select it.
	 * but only if the entered expression is valid */
	if (!wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, &edit_showed_dialog))
		return TRUE;

	if (event->button == 1 && !sheet_selection_is_allowed (sheet, &pos))
		return TRUE;

	/* button 1 will always change the selection,  the other buttons will
	 * only effect things if the target is not already selected.  */
	already_selected = sv_is_pos_selected (sv, pos.col, pos.row);
	if (event->button == 1 || !already_selected) {
		if (!(event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)))
			sv_selection_reset (sv);

		if (event->button != 1 || !(event->state & GDK_SHIFT_MASK) ||
		    sv->selections == NULL) {
			sv_selection_add_pos (sv, pos.col, pos.row);
			sv_make_cell_visible (sv, pos.col, pos.row, FALSE);
		} else if (event->button != 2)
			sv_selection_extend_to (sv, pos.col, pos.row);
		sheet_update (sheet);
	}

	if (edit_showed_dialog)
		return TRUE;  /* we already ignored the button release */

	switch (event->button) {
	case 1: {
		guint32 double_click_time;

		/*
		 *  If the second click is on a different cell than the
		 *  first one this cannot be a double-click
		 */
		if (already_selected) {
			g_object_get (gtk_widget_get_settings (GTK_WIDGET (canvas)),
				      "gtk-double-click-time", &double_click_time,
				      NULL);

			if ((ig->last_click_time + double_click_time) > event->time &&
			    wbcg_edit_start (wbcg, FALSE, FALSE)) {
				break;
			}
		}

		ig->last_click_time = event->time;
		ig->selecting = ITEM_GRID_SELECTING_CELL_RANGE;
		gnm_pane_slide_init (pane);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		break;
	}

	case 2: break;

	case 3: scg_context_menu (scg, event, FALSE, FALSE);
		break;
	default :
		break;
	}

	return TRUE;
}

/*
 * Handle the selection
 */

static gboolean
cb_extend_cell_range (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	sv_selection_extend_to (scg_view (pane->simple.scg),
				info->col, info->row);
	return TRUE;
}

static gboolean
cb_extend_expr_range (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	scg_rangesel_extend_to (pane->simple.scg, info->col, info->row);
	return TRUE;
}

static gint
cb_cursor_come_to_rest (ItemGrid *ig)
{
	Sheet const *sheet = scg_sheet (ig->scg);
	FooCanvas *canvas = ig->canvas_item.canvas;
	GnmPane *pane = GNM_PANE (canvas);
	GnmHLink *link;
	int x, y;
	GnmCellPos pos;
	char const *tiptext;

	/* Be anal and look it up in case something has destroyed the link
	 * since the last motion */
	foo_canvas_w2c (canvas, ig->last_x, ig->last_y, &x, &y);
	pos.col = gnm_pane_find_col (pane, x, NULL);
	pos.row = gnm_pane_find_row (pane, y, NULL);

	link = sheet_hlink_find (sheet, &pos);
	if (link != NULL && (tiptext = gnm_hlink_get_tip (link)) != NULL) {
		g_return_val_if_fail (link == ig->cur_link, FALSE);

		if (ig->tip == NULL && strlen (tiptext) > 0) {
			ig->tip = gnumeric_create_tooltip ();
			gtk_label_set_text (GTK_LABEL (ig->tip), tiptext);
			gnumeric_position_tooltip (ig->tip, TRUE);
			gtk_widget_show_all (gtk_widget_get_toplevel (ig->tip));
		}
	}

	return FALSE;
}

static gint
item_grid_event (FooCanvasItem *item, GdkEvent *event)
{
	FooCanvas *canvas = item->canvas;
	GnmPane   *pane = GNM_PANE (canvas);
	ItemGrid  *ig = ITEM_GRID (item);
	SheetControlGUI *scg = ig->scg;
	Sheet *sheet = scg_sheet (scg);

	switch (event->type){
	case GDK_ENTER_NOTIFY:
		scg_set_display_cursor (scg);
		return TRUE;
	case GDK_LEAVE_NOTIFY:
		ig_clear_hlink_tip (ig);
		if (ig->cursor_timer != 0) {
			g_source_remove (ig->cursor_timer);
			ig->cursor_timer = 0;
		}
		return TRUE;

	case GDK_BUTTON_RELEASE: {
		ItemGridSelectionType selecting = ig->selecting;

		if (event->button.button != 1)
			return FALSE;

		gnm_pane_slide_stop (pane);

		switch (selecting) {
		case ITEM_GRID_NO_SELECTION:
			return TRUE;

		case ITEM_GRID_SELECTING_FORMULA_RANGE :
/*  Removal of this code (2 lines)                                                */
/*  should fix http://bugzilla.gnome.org/show_bug.cgi?id=63485                    */
/*			sheet_make_cell_visible (sheet,                           */
/*				sheet->edit_pos.col, sheet->edit_pos.row, FALSE); */
			/* Fall through */
		case ITEM_GRID_SELECTING_CELL_RANGE :
			wb_view_selection_desc (
				wb_control_view (scg_wbc (scg)), TRUE, NULL);
			break;

		default:
			g_assert_not_reached ();
		}

		ig->selecting = ITEM_GRID_NO_SELECTION;
		gnm_simple_canvas_ungrab (item, event->button.time);

		if (selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
			gnm_expr_entry_signal_update (
				wbcg_get_entry_logical (scg_wbcg (scg)), TRUE);

		if (selecting == ITEM_GRID_SELECTING_CELL_RANGE) {
			GnmCellPos const *pos = sv_is_singleton_selected (scg_view (scg));
			if (pos != NULL) {
				GnmHLink *link;
				/* check for hyper links */
				link = sheet_hlink_find (sheet, pos);
				if (link != NULL)
					gnm_hlink_activate (link, scg_wbc (scg));
			}
		}
		return TRUE;
	}

	case GDK_MOTION_NOTIFY: {
		GnmPaneSlideHandler slide_handler = NULL;
		switch (ig->selecting) {
		case ITEM_GRID_NO_SELECTION:
			if (ig->cursor_timer == 0)
				ig->cursor_timer = g_timeout_add (100,
					(GSourceFunc)cb_cursor_motion, ig);
			if (ig->tip_timer != 0)
				g_source_remove (ig->tip_timer);
			ig->tip_timer = g_timeout_add (500,
					(GSourceFunc)cb_cursor_come_to_rest, ig);
			ig->last_x = event->motion.x;
			ig->last_y = event->motion.y;
			return TRUE;
		case ITEM_GRID_SELECTING_CELL_RANGE :
			slide_handler = &cb_extend_cell_range;
			break;
		case ITEM_GRID_SELECTING_FORMULA_RANGE :
			slide_handler = &cb_extend_expr_range;
			break;
		default:
			g_assert_not_reached ();
		}

		gnm_pane_handle_motion (pane, canvas, &event->motion,
			GNM_PANE_SLIDE_X | GNM_PANE_SLIDE_Y |
			GNM_PANE_SLIDE_AT_COLROW_BOUND,
			slide_handler, NULL);
		return TRUE;
	}

	case GDK_BUTTON_PRESS:
		return item_grid_button_press (ig, &event->button);

	default:
		return FALSE;
	}

	return FALSE;
}

static void
item_grid_init (ItemGrid *ig)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ig);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	ig->selecting = ITEM_GRID_NO_SELECTION;
	ig->gc.fill = ig->gc.cell = ig->gc.empty = ig->gc.bound = NULL;
	range_init_full_sheet (&ig->bound);
	ig->cursor_timer = 0;
	ig->cur_link = NULL;
	ig->tip_timer = 0;
	ig->tip = NULL;
}

static void
item_grid_set_property (GObject *obj, guint param_id,
			GValue const *value, GParamSpec *pspec)
{
	ItemGrid *ig = ITEM_GRID (obj);
	GnmRange const *r;

	switch (param_id) {
	case ITEM_GRID_PROP_SHEET_CONTROL_GUI :
		ig->scg = g_value_get_object (value);
		break;

	case ITEM_GRID_PROP_BOUND :
		r = g_value_get_pointer (value);
		g_return_if_fail (r != NULL);
		ig->bound =  *r;
		break;
	}
}

static void
item_grid_class_init (GObjectClass *gobject_klass)
{
	FooCanvasItemClass *item_klass = (FooCanvasItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->finalize     = item_grid_finalize;
	gobject_klass->set_property = item_grid_set_property;
	g_object_class_install_property (gobject_klass, ITEM_GRID_PROP_SHEET_CONTROL_GUI,
		g_param_spec_object ("SheetControlGUI", "SheetControlGUI",
			"the sheet control gui controlling the item",
			SHEET_CONTROL_GUI_TYPE,
                        GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_GRID_PROP_BOUND,
		g_param_spec_pointer ("bound", "Bound",
			"The display bounds",
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->update      = item_grid_update;
	item_klass->realize     = item_grid_realize;
	item_klass->unrealize   = item_grid_unrealize;
	item_klass->draw        = item_grid_draw;
	item_klass->point       = item_grid_point;
	item_klass->event       = item_grid_event;
}

GSF_CLASS (ItemGrid, item_grid,
	   item_grid_class_init, item_grid_init,
	   FOO_TYPE_CANVAS_ITEM);
