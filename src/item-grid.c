/* vim: set sw=8: */

/*
 * item-grid.c : A canvas item that is responsible for drawing gridlines and
 *     cell content.  One item per sheet displays all the cells.
 *
 * Authors:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>

#include "item-grid.h"
#include "item-debug.h"
#include "gnumeric-sheet.h"
#include "workbook-edit.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "workbook-control-gui-priv.h"
#include "sheet-control-gui.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet-object-impl.h"
#include "gnumeric-type-util.h"
#include "cell.h"
#include "cell-draw.h"
#include "cellspan.h"
#include "ranges.h"
#include "selection.h"
#include "parse-util.h"
#include "mstyle.h"
#include "style-border.h"
#include "style-color.h"
#include "pattern.h"
#include "portability.h"
#include <gal/widgets/e-cursors.h>
#include <math.h>

#undef PAINT_DEBUG
#if 0
#define MERGE_DEBUG(range, str) do { range_dump (range, str); } while (0)
#else
#define MERGE_DEBUG(range, str)
#endif

static GnomeCanvasItemClass *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,
};
typedef enum {
	ITEM_GRID_NO_SELECTION,
	ITEM_GRID_SELECTING_CELL_RANGE,
	ITEM_GRID_SELECTING_FORMULA_RANGE
} ItemGridSelectionType;

struct _ItemGrid {
	GnomeCanvasItem canvas_item;

	SheetControlGUI *scg;

	ItemGridSelectionType selecting;

	struct {
		GdkGC      *fill;	/* Default background fill gc */
		GdkGC      *cell;	/* Color used for the cell */
		GdkGC      *empty;	/* GC used for drawing empty cells */
	} gc;
};


static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	GdkWindow *window;
	GtkStyle  *style;
	ItemGrid  *item_grid;

	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)(item);

	item_grid = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less.
	 */
	style = gtk_style_copy (GTK_WIDGET (item->canvas)->style);
	style->bg [GTK_STATE_NORMAL] = gs_white;
	gtk_widget_set_style (GTK_WIDGET (item->canvas), style);
	gtk_style_unref (style);

	/* Configure the default grid gc */
	item_grid->gc.fill = gdk_gc_new (window);
	item_grid->gc.cell = gdk_gc_new (window);
	item_grid->gc.empty = gdk_gc_new (window);

	gdk_gc_set_foreground (item_grid->gc.fill, &gs_white);
	gdk_gc_set_background (item_grid->gc.fill, &gs_light_gray);
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);

	gdk_gc_unref (item_grid->gc.fill);
	gdk_gc_unref (item_grid->gc.cell);
	gdk_gc_unref (item_grid->gc.empty);
	item_grid->gc.fill = 0;
	item_grid->gc.cell = 0;
	item_grid->gc.empty = 0;

	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)(item);
}

static void
item_grid_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->update)
		(* GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->update) (item, affine, clip_path, flags);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

/**
 * item_grid_draw_merged_range:
 *
 * Handle the special drawing requirements for a 'merged cell'.
 * First draw the entire range (clipped to the visible region) then redraw any
 * segments that are selected.
 */
static void
item_grid_draw_merged_range (GdkDrawable *drawable, ItemGrid *grid,
			     int start_x, int start_y,
			     Range const *view, Range const *range)
{
	int l, r, t, b, last;
	GdkGC *gc = grid->gc.empty;
	Sheet const *sheet   = grid->scg->sheet;
	Cell  const *cell    = sheet_cell_get (sheet, range->start.col, range->start.row);

	/* load style from corner which may not be visible */
	MStyle const *mstyle = sheet_style_get (sheet, range->start.col, range->start.row);
	gboolean const is_selected = (sheet->edit_pos.col != range->start.col ||
				      sheet->edit_pos.row != range->start.row) &&
				     sheet_is_full_range_selected (sheet, range);

	l = r = start_x;
	if (view->start.col < range->start.col)
		l += scg_colrow_distance_get (grid->scg, TRUE,
			view->start.col, range->start.col);
	if (range->end.col <= (last = view->end.col))
		last = range->end.col;
	r += scg_colrow_distance_get (grid->scg, TRUE, view->start.col, last+1);

	t = b = start_y;
	if (view->start.row < range->start.row)
		t += scg_colrow_distance_get (grid->scg, FALSE,
			view->start.row, range->start.row);
	if (range->end.row <= (last = view->end.row))
		last = range->end.row;
	b += scg_colrow_distance_get (grid->scg, FALSE, view->start.row, last+1);

	/* Check for background THEN selection */
	if (gnumeric_background_set_gc (mstyle, gc, grid->canvas_item.canvas, is_selected) ||
	    is_selected)
		/* Remember X excludes the far pixels */
		gdk_draw_rectangle (drawable, gc, TRUE, l, t, r-l+1, b-t+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;
		ColRowInfo const * const ci = cell->col_info;

		if (range->start.col < view->start.col)
			l -= scg_colrow_distance_get (grid->scg, TRUE,
				range->start.col, view->start.col);
		if (view->end.col < range->end.col)
			r += scg_colrow_distance_get (grid->scg, TRUE,
				view->end.col+1, range->end.col+1);
		if (range->start.row < view->start.row)
			t -= scg_colrow_distance_get (grid->scg, FALSE,
				range->start.row, view->start.row);
		if (view->end.row < range->end.row)
			b += scg_colrow_distance_get (grid->scg, FALSE,
				view->end.row+1, range->end.row+1);

		/* FIXME : get the margins from the far col/row too */
		cell_draw (cell, mstyle, grid->gc.cell, drawable,
			   l, t,
			   r - l - (ci->margin_b + ci->margin_a + 1),
			   b - t - (ri->margin_b + ri->margin_a + 1), -1);
	}
}

static void
item_grid_draw_background (GdkDrawable *drawable, ItemGrid *ig,
			   MStyle const *style,
			   int col, int row, int x, int y, int w, int h)
{
	GdkGC                 *gc    = ig->gc.empty;
	SheetControlGUI const *scg   = ig->scg;
	Sheet const	      *sheet = scg->sheet;
	gboolean const is_selected =
		scg->current_object == NULL && scg->new_object == NULL &&
		!(sheet->edit_pos.col == col && sheet->edit_pos.row == row) &&
		sheet_is_cell_selected (sheet, col, row);
	gboolean const has_back = 
		gnumeric_background_set_gc (style, gc, ig->canvas_item.canvas,
					    is_selected);

	if (has_back || is_selected)
		/* Fill the entire cell (API excludes far pixel) */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);
}

static gint
merged_col_cmp (Range const *a, Range const *b)
{
	return a->start.col - b->start.col;
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int draw_x, int draw_y, int width, int height)
{
	GnomeCanvas *canvas = item->canvas;
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	Sheet const *sheet = gsheet->scg->sheet;
	Cell const * const edit_cell = gsheet->scg->wbcg->editing_cell;
	ItemGrid *item_grid = ITEM_GRID (item);
	ColRowInfo const *ri = NULL, *next_ri = NULL;

	/* To ensure that far and near borders get drawn we pretend to draw +-2
	 * pixels around the target area which would include the surrounding
	 * borders if necessary */
	/* TODO : there is an opportunity to speed up the redraw loop by only
	 * painting the borders of the edges and not the content.
	 * However, that feels like more hassle that it is worth.  Look into this someday.
	 */
	int x, y, col, row, n;
	int const start_col = gnumeric_sheet_find_col (gsheet, draw_x-2, &x);
	int end_col = gnumeric_sheet_find_col (gsheet, draw_x+width+2, NULL);
	int const diff_x = x - draw_x;
	int start_row = gnumeric_sheet_find_row (gsheet, draw_y-2, &y);
	int end_row = gnumeric_sheet_find_row (gsheet, draw_y+height+2, NULL);
	int const diff_y = y - draw_y;

	StyleRow sr, next_sr;
	MStyle const **styles;
	StyleBorder const **borders, **prev_vert;
	StyleBorder const *none =
		sheet->hide_grid ? NULL : style_border_none ();

	Range     view;
	gboolean  first_row;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	int *colwidths = NULL;

#if 0
	printf ("%s%d:", col_name(start_col), start_row+1);
	printf ("%s%d <= %d vs %d\n", col_name(end_col), end_row+1, y, draw_y);
#endif

	/* Skip any hidden rows at the start */
	for (; start_row <= end_row ; ++start_row) {
		ri = sheet_row_get_info (sheet, start_row);
		if (ri->visible)
			break;
	}

	/* Fill entire region with default background (even past far edge) */
	gdk_draw_rectangle (drawable, item_grid->gc.fill, TRUE,
			    draw_x, draw_y, width, height);

	/* Get ordered list of merged regions */
	first_row = TRUE;
	merged_active = merged_active_seen = merged_used = NULL;
	merged_unused = sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 * 	- 6 arrays of n StyleBorder const *
	 * 	- 2 arrays of n MStyle const *
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
	
	for (y = diff_y; row <= end_row; row = sr.row = next_sr.row, ri = next_ri) {
		/* Restore the set of ranges seen, but still active.
		 * Reinverting list to maintain the original order */
		g_return_if_fail (merged_active == NULL);

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
			Range * const r = ptr->data;

			if ((r->start.row == row) ||
			    (first_row && r->start.row < row)) {
				ColRowInfo const *ci =
					sheet_col_get_info (sheet, r->start.col);
				GSList *tmp = ptr;
				ptr = *lag = tmp->next;
				g_slist_free_1 (tmp);
				merged_active = g_slist_insert_sorted (merged_active, r,
							(GCompareFunc)merged_col_cmp);
				MERGE_DEBUG (r, " : unused -> active\n");

				if (ci->visible)
					item_grid_draw_merged_range (drawable, item_grid,
								     diff_x, y, &view, r);
			} else {
				lag = &(ptr->next);
				ptr = ptr->next;
			}
		}
		first_row = FALSE;

		for (col = start_col, x = diff_x; col <= end_col ; col++) {
			MStyle const *style;
			CellSpanInfo const *span;
			ColRowInfo const *ci = sheet_col_get_info (sheet, col);

			if (!ci->visible)
				continue;
				
			/* Skip any merged regions */
			if (merged_active) {
				Range const *r = merged_active->data;
				if (r->start.col <= col) {
					gboolean clear_top, clear_bottom = TRUE;
					int i, first = r->start.col;
					int last  = r->end.col;

					x += scg_colrow_distance_get (
						gsheet->scg, TRUE, col, last+1);
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

					ptr = merged_active;
					merged_active = merged_active->next;
					if (r->end.row == row) {
						clear_bottom = FALSE;
						ptr->next = merged_used;
						merged_used = ptr;
						MERGE_DEBUG (r, " : active -> used\n");
					} else {
						ptr->next = merged_active_seen;
						merged_active_seen = ptr;
						MERGE_DEBUG (r, " : active -> seen\n");
					}

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

			style = sr.styles [col];
			item_grid_draw_background (drawable, item_grid,
						   style, col, row, x, y,
						   ci->size_pixels,
						   ri->size_pixels);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 || NULL == (span = row_span_get (ri, col))) {

				/*
				 * If it is being edited pretend it is empty to
				 * avoid problems with the a long cells
				 * contents extending past the edge of the edit
				 * box.  Ignore blanks too.
				 */
				Cell const *cell = sheet_cell_get (sheet, col, row);
				if (!cell_is_blank (cell) && cell != edit_cell)
					cell_draw (cell, style,
						   item_grid->gc.cell, drawable,
						   x, y, -1, -1, -1);

			/* Only draw spaning cells after all the backgrounds
			 * that we are goign to draw have been drawn.  No need
			 * to draw the edit cell, or blanks.
			 */
			} else if (edit_cell != span->cell &&
				   (col == span->right || col == end_col)) {
				Cell const *cell = span->cell;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				int real_x = x;
				int center_offset = cell->col_info->size_pixels/2;
				/* TODO : Use the spanning margins */
				int tmp_width = ci->size_pixels -
					ci->margin_b - ci->margin_a;

				if (col != cell->pos.col)
					style = sheet_style_get (sheet,
						cell->pos.col, ri->pos);

				/* x, y are relative to this cell origin, but the cell
				 * might be using columns to the left (if it is set to right
				 * justify or center justify) compute the pixel difference
				 */
				if (start_span_col != cell->pos.col)
					center_offset += scg_colrow_distance_get (
						gsheet->scg, TRUE,
						start_span_col, cell->pos.col);

				if (start_span_col != col) {
					int offset = scg_colrow_distance_get (
						gsheet->scg, TRUE,
						start_span_col, col);
					real_x -= offset;
					tmp_width += offset;
					sr.vertical [col] = NULL;
				}
				if (end_span_col != col) {
					tmp_width += scg_colrow_distance_get (
						gsheet->scg, TRUE,
						col+1, end_span_col + 1);
				}

				cell_draw (cell, style,
					   item_grid->gc.cell, drawable,
					   real_x, y, tmp_width, -1, center_offset);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			x += ci->size_pixels;
		}
		style_borders_row_draw (prev_vert, &sr,
					drawable, diff_x, y, y+ri->size_pixels,
					colwidths, TRUE);

		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

		y += ri->size_pixels;
	}
	if (sr.row >= SHEET_MAX_ROWS-1)
		style_borders_row_draw (prev_vert, &sr,
					drawable, diff_x, y, y, colwidths, FALSE);
	
	if (merged_used)	/* ranges whose bottoms are in the view */
		g_slist_free (merged_used);
	if (merged_active_seen) /* ranges whose bottoms are below the view */
		g_slist_free (merged_active_seen);

	g_return_if_fail (merged_unused == NULL);
	g_return_if_fail (merged_active == NULL);
}

static double
item_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/***********************************************************************/

typedef struct {
	SheetControlGUI *scg;
	double x, y;
	gboolean has_been_sized;
	GnomeCanvasItem *item;
} SheetObjectCreationData;

/*
 * cb_obj_create_motion:
 * @gsheet :
 * @event :
 * @closure :
 *
 * Rubber band a rectangle to show where the object is going to go,
 * and support autoscroll.
 *
 * TODO : Finish autoscroll
 */
static gboolean
cb_obj_create_motion (GnumericSheet *gsheet, GdkEventMotion *event,
		      SheetObjectCreationData *closure)
{
	double tmp_x, tmp_y;
	double x1, x2, y1, y2;

	g_return_val_if_fail (gsheet != NULL, TRUE);

	gnome_canvas_window_to_world (GNOME_CANVAS (gsheet),
				      event->x, event->y,
				      &tmp_x, &tmp_y);

	if (tmp_x < closure->x) {
		x1 = tmp_x;
		x2 = closure->x;
	} else {
		x2 = tmp_x;
		x1 = closure->x;
	}
	if (tmp_y < closure->y) {
		y1 = tmp_y;
		y2 = closure->y;
	} else {
		y2 = tmp_y;
		y1 = closure->y;
	}

	if (!closure->has_been_sized) {
		closure->has_been_sized =
		    (fabs (tmp_x - closure->x) > 5.) ||
		    (fabs (tmp_y - closure->y) > 5.);
	}

	gnome_canvas_item_set (closure->item,
			       "x1", x1, "y1", y1,
			       "x2", x2, "y2", y2,
			       NULL);

	return TRUE;
}

/**
 * cb_obj_create_button_release :
 *
 * Invoked as the last step in object creation.
 */
static gboolean
cb_obj_create_button_release (GnumericSheet *gsheet, GdkEventButton *event,
			      SheetObjectCreationData *closure)
{
	double pts [4];
	SheetObject *so;
	SheetControlGUI *scg;

	g_return_val_if_fail (gsheet != NULL, 1);
	g_return_val_if_fail (closure != NULL, -1);
	g_return_val_if_fail (closure->scg != NULL, -1);
	g_return_val_if_fail (closure->scg->new_object != NULL, -1);

	scg = closure->scg;
	so = scg->new_object;
	sheet_set_dirty (scg->sheet, TRUE);

	/* If there has been some motion use the press and release coords */
	if (closure->has_been_sized) {
		pts [0] = closure->x;
		pts [1] = closure->y;
		gnome_canvas_window_to_world (GNOME_CANVAS (gsheet),
					      event->x, event->y,
					      pts + 2, pts + 3);
	} else {
	/* Otherwise translate default size to use release point as top left */
		sheet_object_default_size (so, pts+2, pts+3);
		pts [2] += (pts [0] = closure->x);
		pts [3] += (pts [1] = closure->y);
	}

	scg_object_calc_position (scg, so, pts);
	sheet_object_set_sheet (so, scg->sheet);

	gtk_signal_disconnect_by_func (
		GTK_OBJECT (gsheet),
		GTK_SIGNAL_FUNC (cb_obj_create_motion), closure);
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (gsheet),
		GTK_SIGNAL_FUNC (cb_obj_create_button_release), closure);

	gtk_object_destroy (GTK_OBJECT (closure->item));
	g_free (closure);

	/* move object from creation to edit mode */
	scg->new_object = NULL;
	scg_mode_edit_object (scg, so);

	return TRUE;
}

/*
 * sheet_object_begin_creation :
 *
 * Starts the process of creating a SheetObject.  Handles the initial
 * button press on the GnumericSheet.
 *
 * TODO : when we being supporting panes this will need to move into the
 * sheet-control-gui layer.
 */
static gboolean
sheet_object_begin_creation (GnumericSheet *gsheet, GdkEventButton *event)
{
	SheetControlGUI *scg;
	SheetObject *so;
	SheetObjectCreationData *closure;

	g_return_val_if_fail (gsheet != NULL, TRUE);
	g_return_val_if_fail (gsheet->scg != NULL, TRUE);

	scg = gsheet->scg;

	g_return_val_if_fail (scg != NULL, TRUE);
	g_return_val_if_fail (scg->current_object == NULL, TRUE);
	g_return_val_if_fail (scg->new_object != NULL, TRUE);

	so = scg->new_object;

	closure = g_new (SheetObjectCreationData, 1);
	closure->scg = scg;
	closure->has_been_sized = FALSE;
	closure->x = event->x;
	closure->y = event->y;

	closure->item = gnome_canvas_item_new (
		gsheet->scg->object_group,
		gnome_canvas_rect_get_type (),
		"outline_color", "black",
		"width_units",   2.0,
		NULL);

	gtk_signal_connect (GTK_OBJECT (gsheet), "button_release_event",
			    GTK_SIGNAL_FUNC (cb_obj_create_button_release), closure);
	gtk_signal_connect (GTK_OBJECT (gsheet), "motion_notify_event",
			    GTK_SIGNAL_FUNC (cb_obj_create_motion), closure);

	return TRUE;
}


/***************************************************************************/

static int
item_grid_button_1 (SheetControlGUI *scg, GdkEventButton *event,
		    ItemGrid *item_grid, int col, int row, int x, int y)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);
	GnomeCanvas   *canvas = item->canvas;
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);

	/* Range check first */
	if (col >= SHEET_MAX_COLS)
		return 1;
	if (row >= SHEET_MAX_ROWS)
		return 1;

	/* A new object is ready to be realized and inserted */
	if (scg->new_object != NULL)
		return sheet_object_begin_creation (gsheet, event);

	/* If we are not configuring an object then clicking on the sheet
	 * ends the edit.
	 */
	if (scg->current_object != NULL) {
		if (!workbook_edit_has_guru (scg->wbcg))
			scg_mode_edit (scg);
	} else
		wb_control_gui_focus_cur_sheet (gsheet->scg->wbcg);

	/*
	 * If we were already selecting a range of cells for a formula,
	 * reset the location to a new place, or extend the selection.
	 */
	if (gsheet->selecting_cell) {
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		if (event->state & GDK_SHIFT_MASK)
			gnumeric_sheet_rangesel_cursor_extend (gsheet, col, row);
		else
			gnumeric_sheet_rangesel_cursor_bounds (gsheet, col, row, col, row);
		return 1;
	}

	/*
	 * If the user is editing a formula (gnumeric_sheet_can_select_expr_range)
	 * then we enable the dynamic cell selection mode.
	 */
	if (gnumeric_sheet_can_select_expr_range (gsheet)){
		gnumeric_sheet_start_cell_selection (gsheet, col, row);
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		return 1;
	}

	/* While a guru is up ignore clicks */
	if (workbook_edit_has_guru (gsheet->scg->wbcg))
		return 1;

	/* This was a regular click on a cell on the spreadsheet.  Select it.
	 * but only if the entered expression is valid
	 */
	if (workbook_finish_editing (gsheet->scg->wbcg, TRUE) == FALSE)
		return 1;

	if (!(event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)))
		sheet_selection_reset (scg->sheet);

	item_grid->selecting = ITEM_GRID_SELECTING_CELL_RANGE;

	if ((event->state & GDK_SHIFT_MASK) && scg->sheet->selections)
		sheet_selection_extend_to (scg->sheet, col, row);
	else {
		sheet_selection_add (scg->sheet, col, row);
		sheet_make_cell_visible (scg->sheet, col, row);
	}
	sheet_update (scg->sheet);

	gnome_canvas_item_grab (item,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL,
				event->time);
	return 1;
}

static void
drag_start (GtkWidget *widget, GdkEvent *event, Sheet *sheet)
{
        GtkTargetList *list;
        GdkDragContext *context;
	static GtkTargetEntry drag_types [] = {
		{ "bonobo/moniker", 0, 1 },
	};

        list = gtk_target_list_new (drag_types, 1);

        context = gtk_drag_begin (widget, list,
                                  (GDK_ACTION_COPY | GDK_ACTION_MOVE
                                   | GDK_ACTION_LINK | GDK_ACTION_ASK),
                                  event->button.button, event);
        gtk_drag_set_icon_default (context);
}

/*
 * Handle the selection
 */

static gboolean
cb_extend_cell_range (SheetControlGUI *scg, int col, int row, gpointer ignored)
{
	sheet_selection_extend_to (scg->sheet, col, row);
	return TRUE;
}

static gboolean
cb_extend_expr_range (SheetControlGUI *scg, int col, int row, gpointer ignored)
{
	scg_rangesel_cursor_extend (scg, col, row);
	return TRUE;
}

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	SheetControlGUI *scg = item_grid->scg;
	int col, row, x, y, left, top, width, height;

	switch (event->type){
	case GDK_ENTER_NOTIFY: {
		int cursor;

		if (scg->new_object != NULL)
			cursor = E_CURSOR_THIN_CROSS;
		else if (scg->current_object != NULL)
			cursor = E_CURSOR_ARROW;
		else
			cursor = E_CURSOR_FAT_CROSS;

		e_cursor_set_widget (canvas, cursor);
		return TRUE;
	}

	case GDK_BUTTON_RELEASE:
		switch (event->button.button) {
		case 1 :
			scg_stop_sliding (item_grid->scg);

			if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
				sheet_make_cell_visible (scg->sheet,
							 scg->sheet->edit_pos.col,
							 scg->sheet->edit_pos.row);

			wb_view_selection_desc (wb_control_view (
				WORKBOOK_CONTROL (scg->wbcg)), TRUE, NULL);

			item_grid->selecting = ITEM_GRID_NO_SELECTION;
			gnome_canvas_item_ungrab (item, event->button.time);
			return 1;

		default:
			return FALSE;
		};
		break;

	case GDK_MOTION_NOTIFY:
		gnome_canvas_w2c (canvas, event->motion.x, event->motion.y, &x, &y);
		gnome_canvas_get_scroll_offsets (canvas, &left, &top);

		width = GTK_WIDGET (canvas)->allocation.width;
		height = GTK_WIDGET (canvas)->allocation.height;

		col = gnumeric_sheet_find_col (gsheet, x, NULL);
		row = gnumeric_sheet_find_row (gsheet, y, NULL);

		if (x < left || y < top || x >= left + width || y >= top + height) {
			int dx = 0, dy = 0;
			SheetControlGUISlideHandler slide_handler = NULL;

			if (item_grid->selecting == ITEM_GRID_NO_SELECTION)
				return 1;

			if (x < left)
				dx = x - left;
			else if (x >= left + width)
				dx = x - width - left;

			if (y < top)
				dy = y - top;
			else if (y >= top + height)
				dy = y - height - top;

			if (item_grid->selecting == ITEM_GRID_SELECTING_CELL_RANGE)
				slide_handler = &cb_extend_cell_range;
			else if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
				slide_handler = &cb_extend_expr_range;

			if (scg_start_sliding (item_grid->scg,
						      slide_handler, NULL,
						      col, row, dx, dy))

				return TRUE;
		}

		scg_stop_sliding (item_grid->scg);

		if (item_grid->selecting == ITEM_GRID_NO_SELECTION)
			return 1;

		if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE){
			gnumeric_sheet_rangesel_cursor_extend (gsheet, col, row);
			return 1;
		}

		if (event->motion.x < 0)
			event->motion.x = 0;
		if (event->motion.y < 0)
			event->motion.y = 0;

		sheet_selection_extend_to (scg->sheet, col, row);
		return TRUE;

	case GDK_BUTTON_PRESS:
		scg_stop_sliding (item_grid->scg);

		gnome_canvas_w2c (canvas, event->button.x, event->button.y, &x, &y);
		col = gnumeric_sheet_find_col (gsheet, x, NULL);
		row = gnumeric_sheet_find_row (gsheet, y, NULL);

		/* While a guru is up ignore clicks */
		if (workbook_edit_has_guru (gsheet->scg->wbcg) && event->button.button != 1)
			return TRUE;

		switch (event->button.button){
		case 1:
			return item_grid_button_1 (scg, &event->button,
						   item_grid, col, row, x, y);

		case 2:
			g_warning ("This is here just for demo purposes");
			drag_start (GTK_WIDGET (item->canvas), event, scg->sheet);
			return TRUE;

		case 3:
			scg_context_menu (item_grid->scg,
					  &event->button, FALSE, FALSE);
			return TRUE;

		default :
			return FALSE;
		}

	default:
		return FALSE;
	}

	return FALSE;
}

/*
 * Instance initialization
 */
static void
item_grid_init (ItemGrid *item_grid)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	item_grid->selecting = ITEM_GRID_NO_SELECTION;
}

static void
item_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemGrid *item_grid;

	item = GNOME_CANVAS_ITEM (o);
	item_grid = ITEM_GRID (o);

	switch (arg_id){
	case ARG_SHEET_CONTROL_GUI:
		item_grid->scg = GTK_VALUE_POINTER (*arg);
		break;
	}
}

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemGridClass;
static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);

	object_class->set_arg = item_grid_set_arg;
	object_class->destroy = item_grid_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_grid_update;
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->event       = item_grid_event;
}

GNUMERIC_MAKE_TYPE (item_grid, "ItemGrid", ItemGrid,
		    item_grid_class_init, item_grid_init,
		    gnome_canvas_item_get_type ())
