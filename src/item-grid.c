/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * item-grid.c : A canvas item that is responsible for drawing gridlines and
 *     cell content.  One item per sheet displays all the cells.
 *
 * Authors:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "item-grid.h"

#include "gnumeric-canvas.h"
#include "workbook-edit.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "workbook-control-gui-priv.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
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
#include "style-border.h"
#include "style-color.h"
#include "pattern.h"
#include "commands.h"
#include "hlink.h"
#include "gui-util.h"

#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtklabel.h>
#include <gal/widgets/e-cursors.h>
#include <gsf/gsf-impl-utils.h>
#include <math.h>
#define GNUMERIC_ITEM "GRID"
#include "item-debug.h"

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
	ARG_SHEET_BOUND
};
typedef enum {
	ITEM_GRID_NO_SELECTION,
	ITEM_GRID_SELECTING_CELL_RANGE,
	ITEM_GRID_SELECTING_FORMULA_RANGE,
	ITEM_GRID_SELECTING_OBJECT_CREATION
} ItemGridSelectionType;

struct _ItemGrid {
	GnomeCanvasItem canvas_item;

	SheetControlGUI *scg;

	ItemGridSelectionType selecting;

	struct {
		GdkGC      *fill;	/* Default background fill gc */
		GdkGC      *cell;	/* Color used for the cell */
		GdkGC      *empty;	/* GC used for drawing empty cells */
		GdkGC      *bound;	/* the black line at the edge */
	} gc;

	Range bound;

	struct {
		double x, y;
		gboolean has_been_sized;
		GnomeCanvasItem *item;
	} obj_create;

	/* information for tha cursor motion handler */
	guint cursor_timer;
	double last_x, last_y;
	GnmHLink *cur_link; /* do not derference, just a pointer */
	GtkWidget *tip;
	guint tip_timer;

	guint32 last_click_time;
};

static void
ig_clear_hlink_tip (ItemGrid *ig)
{
	if (ig->tip_timer != 0) {
		gtk_timeout_remove (ig->tip_timer);
		ig->tip_timer = 0;
	}

	if (ig->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ig->tip));
		ig->tip = NULL;
	}
}

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *ig = ITEM_GRID (object);

	if (ig->cursor_timer != 0) {
		gtk_timeout_remove (ig->cursor_timer);
		ig->cursor_timer = 0;
	}
	ig_clear_hlink_tip (ig);
	ig->cur_link = NULL;

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	GdkWindow *window;
	GtkStyle  *style;
	ItemGrid  *ig;

	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)(item);

	ig = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less.
	 */
	style = gtk_style_copy (GTK_WIDGET (item->canvas)->style);
	style->bg [GTK_STATE_NORMAL] = gs_white;
	gtk_widget_set_style (GTK_WIDGET (item->canvas), style);
	gtk_style_unref (style);

	/* Configure the default grid gc */
	ig->gc.fill = gdk_gc_new (window);
	ig->gc.cell = gdk_gc_new (window);
	ig->gc.empty = gdk_gc_new (window);
	ig->gc.bound = gdk_gc_new (window);

	gdk_gc_set_foreground (ig->gc.fill,  &gs_white);
	gdk_gc_set_background (ig->gc.fill,  &gs_light_gray);
	gdk_gc_set_foreground (ig->gc.bound, &gs_black);
	gdk_gc_set_line_attributes (ig->gc.bound, 3, GDK_LINE_SOLID,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *ig = ITEM_GRID (item);

	gdk_gc_unref (ig->gc.fill);	ig->gc.fill = NULL;
	gdk_gc_unref (ig->gc.cell);	ig->gc.cell = NULL;
	gdk_gc_unref (ig->gc.empty);	ig->gc.empty = NULL;
	gdk_gc_unref (ig->gc.bound);	ig->gc.bound = NULL;

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
}

/**
 * item_grid_draw_merged_range:
 *
 * Handle the special drawing requirements for a 'merged cell'.
 * First draw the entire range (clipped to the visible region) then redraw any
 * segments that are selected.
 */
static void
item_grid_draw_merged_range (GdkDrawable *drawable, ItemGrid *ig,
			     int start_x, int start_y,
			     Range const *view, Range const *range,
			     gboolean draw_selection)
{
	int l, r, t, b, last;
	GdkGC *gc = ig->gc.empty;
	SheetView const *sv = ((SheetControl *) ig->scg)->view;
	Sheet const *sheet  = sv->sheet;
	Cell  const *cell   = sheet_cell_get (sheet, range->start.col, range->start.row);

	/* load style from corner which may not be visible */
	MStyle const *style = sheet_style_get (sheet, range->start.col, range->start.row);
	gboolean const is_selected = draw_selection &&
		(sv->edit_pos.col != range->start.col ||
		 sv->edit_pos.row != range->start.row) &&
		sv_is_full_range_selected (sv, range);

	l = r = start_x;
	if (view->start.col < range->start.col)
		l += scg_colrow_distance_get (ig->scg, TRUE,
			view->start.col, range->start.col);
	if (range->end.col <= (last = view->end.col))
		last = range->end.col;
	r += scg_colrow_distance_get (ig->scg, TRUE, view->start.col, last+1);

	t = b = start_y;
	if (view->start.row < range->start.row)
		t += scg_colrow_distance_get (ig->scg, FALSE,
			view->start.row, range->start.row);
	if (range->end.row <= (last = view->end.row))
		last = range->end.row;
	b += scg_colrow_distance_get (ig->scg, FALSE, view->start.row, last+1);

	if (l == r || t == b)
		return;

	/* Check for background THEN selection */
	if (gnumeric_background_set_gc (style, gc,
			ig->canvas_item.canvas, is_selected) ||
	    is_selected)
		/* Remember X excludes the far pixels */
		gdk_draw_rectangle (drawable, gc, TRUE, l, t, r-l+1, b-t+1);

	if (range->start.col < view->start.col)
		l -= scg_colrow_distance_get (ig->scg, TRUE,
			range->start.col, view->start.col);
	if (view->end.col < range->end.col)
		r += scg_colrow_distance_get (ig->scg, TRUE,
			view->end.col+1, range->end.col+1);
	if (range->start.row < view->start.row)
		t -= scg_colrow_distance_get (ig->scg, FALSE,
			range->start.row, view->start.row);
	if (view->end.row < range->end.row)
		b += scg_colrow_distance_get (ig->scg, FALSE,
			view->end.row+1, range->end.row+1);

	if (cell != NULL) {
		ColRowInfo const * const ri = cell->row_info;
		ColRowInfo const * const ci = cell->col_info;

		/* FIXME : get the margins from the far col/row too */
		cell_draw (cell, style, ig->gc.cell, drawable,
			   l, t,
			   r - l - (ci->margin_b + ci->margin_a + 1),
			   b - t - (ri->margin_b + ri->margin_a + 1), -1);
	}
	style_border_draw_diag (style, drawable, l, t, r, b);
}

static void
item_grid_draw_background (GdkDrawable *drawable, ItemGrid *ig,
			   MStyle const *style,
			   int col, int row, int x, int y, int w, int h,
			   gboolean draw_selection)
{
	GdkGC           *gc = ig->gc.empty;
	SheetView const *sv = ((SheetControl *) ig->scg)->view;
	gboolean const is_selected = draw_selection &&
		(sv->edit_pos.col != col || sv->edit_pos.row != row) &&
		sv_is_pos_selected (sv, col, row);
	gboolean const has_back =
		gnumeric_background_set_gc (style, gc,
					    ig->canvas_item.canvas,
					    is_selected);

	if (has_back || is_selected)
		/* Fill the entire cell (API excludes far pixel) */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);

	style_border_draw_diag (style, drawable, x, y, x+w, y+h);
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
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (canvas);
	Sheet const *sheet = ((SheetControl *) gcanvas->simple.scg)->sheet;
	Cell const * const edit_cell = gcanvas->simple.scg->wbcg->wb_control.editing_cell;
	ItemGrid *ig = ITEM_GRID (item);
	ColRowInfo const *ri = NULL, *next_ri = NULL;

	/* To ensure that far and near borders get drawn we pretend to draw +-2
	 * pixels around the target area which would include the surrounding
	 * borders if necessary */
	/* TODO : there is an opportunity to speed up the redraw loop by only
	 * painting the borders of the edges and not the content.
	 * However, that feels like more hassle that it is worth.  Look into this someday.
	 */
	int x, y, col, row, n;
	int start_col = gnm_canvas_find_col (gcanvas, draw_x-2, &x);
	int end_col = gnm_canvas_find_col (gcanvas, draw_x+width+2, NULL);
	int const diff_x = x - draw_x;
	int start_row = gnm_canvas_find_row (gcanvas, draw_y-2, &y);
	int end_row = gnm_canvas_find_row (gcanvas, draw_y+height+2, NULL);
	int const diff_y = y - draw_y;

	StyleRow sr, next_sr;
	MStyle const **styles;
	StyleBorder const **borders, **prev_vert;
	StyleBorder const *none =
		sheet->hide_grid ? NULL : style_border_none ();

	Range     view;
	GSList	 *merged_active, *merged_active_seen,
		 *merged_used, *merged_unused, *ptr, **lag;

	int *colwidths = NULL;

	gboolean const draw_selection =
		ig->scg->current_object == NULL &&
		ig->scg->new_object == NULL;

#if 0
	printf ("%s%s:", col_name(start_col), row_name(start_row));
	printf ("%s%s <= %d vs %d\n", col_name(end_col), row_name(end_row), y, draw_y);
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

	sheet_style_update_grid_color (sheet);

	/* Fill entire region with default background (even past far edge) */
	gdk_draw_rectangle (drawable, ig->gc.fill, TRUE,
			    draw_x, draw_y, width, height);

	/* Get ordered list of merged regions */
	merged_active = merged_active_seen = merged_used = NULL;
	merged_unused = sheet_merge_get_overlap (sheet,
		range_init (&view, start_col, start_row, end_col, end_row));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 *	- 6 arrays of n StyleBorder const *
	 *	- 2 arrays of n MStyle const *
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
							diff_x, y, &view, r, draw_selection);
				}
			} else {
				lag = &(ptr->next);
				ptr = ptr->next;
			}
		}

		for (col = start_col, x = diff_x; col <= end_col ; col++) {
			MStyle const *style;
			CellSpanInfo const *span;
			ColRowInfo const *ci = sheet_col_get_info (sheet, col);

			if (!ci->visible) {
				if (merged_active != NULL) {
					Range const *r = merged_active->data;
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
				Range const *r = merged_active->data;
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
							continue;
					} else {
						ptr->next = merged_active_seen;
						merged_active_seen = ptr;
						MERGE_DEBUG (r, " : active -> seen\n");
						if (next_sr.row <= r->end.row)
							clear_bottom = TRUE;
					}

					x += scg_colrow_distance_get (
						gcanvas->simple.scg, TRUE, col, last+1);
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

			style = sr.styles [col];
			item_grid_draw_background (drawable, ig,
				style, col, row, x, y,
				ci->size_pixels, ri->size_pixels,
				draw_selection);

			/* Is this part of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 || NULL == (span = row_span_get (ri, col))) {

				/* If it is being edited pretend it is empty to
				 * avoid problems with the a long cells
				 * contents extending past the edge of the edit
				 * box.  Ignore blanks too.
				 */
				Cell const *cell = sheet_cell_get (sheet, col, row);
				if (!cell_is_blank (cell) && cell != edit_cell)
					cell_draw (cell, style,
						   ig->gc.cell, drawable,
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
						cell->pos.col, row);

				/* x, y are relative to this cell origin, but the cell
				 * might be using columns to the left (if it is set to right
				 * justify or center justify) compute the pixel difference
				 */
				if (start_span_col != cell->pos.col)
					center_offset += scg_colrow_distance_get (
						gcanvas->simple.scg, TRUE,
						start_span_col, cell->pos.col);

				if (start_span_col != col) {
					int offset = scg_colrow_distance_get (
						gcanvas->simple.scg, TRUE,
						start_span_col, col);
					real_x -= offset;
					tmp_width += offset;
					sr.vertical [col] = NULL;
				}
				if (end_span_col != col) {
					tmp_width += scg_colrow_distance_get (
						gcanvas->simple.scg, TRUE,
						col+1, end_span_col + 1);
				}

				cell_draw (cell, style,
					   ig->gc.cell, drawable,
					   real_x, y, tmp_width, -1, center_offset);
			} else if (col != span->left)
				sr.vertical [col] = NULL;

			x += ci->size_pixels;
		}
		style_borders_row_draw (prev_vert, &sr,
					drawable, diff_x, y, y+ri->size_pixels,
					colwidths, TRUE);

		/* In case there were hidden merges that trailed off the end */
		while (merged_active != NULL) {
			Range const *r = merged_active->data;
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
		style_borders_row_draw (prev_vert, &sr,
					drawable, diff_x, y, y, colwidths, FALSE);
		if (gcanvas->pane->index >= 2)
			gdk_draw_line (drawable, ig->gc.bound, 0, y, x, y);
	}
	if (col >= ig->bound.end.col &&
	    /* TODO : Add pane flags to avoid hard coding pane numbers */
	    (gcanvas->pane->index == 1 || gcanvas->pane->index == 2))
		gdk_draw_line (drawable, ig->gc.bound, x, 0, x, y);

	g_slist_free (merged_used);	   /* merges with bottom in view */
	g_slist_free (merged_active_seen); /* merges with bottom the view */
	g_slist_free (merged_unused);	   /* merges in hidden rows */
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

/**
 * ig_obj_create_motion:
 * @ig : the grid
 * @new_x :
 * @new_y :
 *
 * Rubber band a rectangle to show where the object is going to go,
 * and support autoscroll.
 */
static void
ig_obj_create_motion (ItemGrid *ig, gdouble new_x, gdouble new_y)
{
	double x1, x2, y1, y2;

	if (new_x < ig->obj_create.x) {
		x1 = new_x;
		x2 = ig->obj_create.x;
	} else {
		x2 = new_x;
		x1 = ig->obj_create.x;
	}
	if (new_y < ig->obj_create.y) {
		y1 = new_y;
		y2 = ig->obj_create.y;
	} else {
		y2 = new_y;
		y1 = ig->obj_create.y;
	}

	if (!ig->obj_create.has_been_sized) {
		ig->obj_create.has_been_sized =
		    (fabs (new_x - ig->obj_create.x) > 5.) ||
		    (fabs (new_y - ig->obj_create.y) > 5.);
	}

	if (ig->obj_create.item == NULL) {
		SheetObject *so;
		double points [4];

		points [0] = ig->obj_create.x;
		points [1] = ig->obj_create.y;
		points [2] = new_x;
		points [3] = new_y;

		so = ig->scg->new_object;

		if (so->anchor.direction != SO_DIR_UNKNOWN) {
			so->anchor.direction = SO_DIR_NONE_MASK;
			if (new_x > ig->obj_create.x)
				so->anchor.direction |= SO_DIR_RIGHT;
			if (new_y > ig->obj_create.y)
				so->anchor.direction |= SO_DIR_DOWN;
		}

		scg_object_calc_position (ig->scg, so, points);
	} else
		gnome_canvas_item_set (ig->obj_create.item,
			"x1", x1, "y1", y1,
			"x2", x2, "y2", y2,
			NULL);
}

/**
 * ig_obj_create_finish :
 *
 * Invoked as the last step in object creation.
 */
static void
ig_obj_create_finish (ItemGrid *ig, GdkEventButton *event)
{
	double pts [4];

	SheetControlGUI *scg   = ig->scg;
	SheetObject	*so    = scg->new_object;
	Sheet		*sheet = ((SheetControl *) scg)->sheet;

	sheet_set_dirty (sheet, TRUE);

	/* If there has been some motion use the press and release coords */
	if (ig->obj_create.has_been_sized) {
		pts [0] = ig->obj_create.x;
		pts [1] = ig->obj_create.y;
		pts [2] = event->x;
		pts [3] = event->y;
	} else {
		/* Otherwise translate default size to use release point as top left */
		sheet_object_default_size (so, pts+2, pts+3);
		pts [2] += (pts [0] = ig->obj_create.x);
		pts [3] += (pts [1] = ig->obj_create.y);
	}

	scg_object_calc_position (scg, so, pts);

	if (!sheet_object_rubber_band_directly (so)) {
		sheet_object_set_sheet (so, sheet);
		gtk_object_destroy (GTK_OBJECT (ig->obj_create.item));
		ig->obj_create.item = NULL;
	}

	scg_mode_edit_object (scg, so);

	cmd_object_insert (WORKBOOK_CONTROL (scg_get_wbcg (scg)), so, sheet);
}

/*
 * ig_obj_create_begin :
 *
 * Starts the process of creating a SheetObject.  Handles the initial
 * button press on the GnumericCanvas.
 */
static gboolean
ig_obj_create_begin (ItemGrid *ig, GdkEventButton *event)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (ig);
	GnumericCanvas  *gcanvas = GNUMERIC_CANVAS (item->canvas);
	SheetControlGUI *scg;
	SheetObject *so;

	scg = ig->scg;

	g_return_val_if_fail (scg != NULL, TRUE);
	g_return_val_if_fail (scg->current_object == NULL, TRUE);
	g_return_val_if_fail (scg->new_object != NULL, TRUE);
	g_return_val_if_fail (ig->obj_create.item == NULL, TRUE);
	g_return_val_if_fail (ig->selecting == ITEM_GRID_NO_SELECTION, TRUE);

	ig->obj_create.has_been_sized = FALSE;
	ig->obj_create.x = event->x;
	ig->obj_create.y = event->y;

	so = scg->new_object;
	if (!sheet_object_rubber_band_directly (so)) {
		ig->obj_create.item = gnome_canvas_item_new (
			gcanvas->object_group,
			GNOME_TYPE_CANVAS_RECT,
			"outline_color", "black",
			"width_units",   2.0,
			NULL);
	} else {
		double points [4];

		points [0] = points [2] = event->x;
		points [1] = points [3] = event->y;

		scg_object_calc_position (scg, so, points);
		sheet_object_set_sheet (so, ((SheetControl *) scg)->sheet);
	}

	ig->selecting = ITEM_GRID_SELECTING_OBJECT_CREATION;
	gnm_canvas_slide_init (gcanvas);
	gnm_simple_canvas_grab (item,
		GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
		NULL, event->time);

	return TRUE;
}


/***************************************************************************/

static void
drag_start (GtkWidget *widget, GdkEventButton *event, Sheet *sheet)
{
        GtkTargetList *list;
        GdkDragContext *context;
	static GtkTargetEntry drag_types [] = {
		{ (char *)"bonobo/moniker", 0, 1 },
	};

        list = gtk_target_list_new (drag_types, 1);

        context = gtk_drag_begin (widget, list,
		(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK | GDK_ACTION_ASK),
		event->button, (GdkEvent *)event);
        gtk_drag_set_icon_default (context);

	gtk_target_list_unref (list);
}

static int
item_grid_button_press (ItemGrid *ig, GdkEventButton *event)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (ig);
	GnomeCanvas    *canvas = item->canvas;
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (canvas);
	SheetControlGUI *scg = ig->scg;
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	CellPos	pos;
	int x, y;

	gnm_canvas_slide_stop (gcanvas);

	gnome_canvas_w2c (canvas, event->x, event->y, &x, &y);
	pos.col = gnm_canvas_find_col (gcanvas, x, NULL);
	pos.row = gnm_canvas_find_row (gcanvas, y, NULL);

	/* Range check first */
	if (pos.col >= SHEET_MAX_COLS)
		return 1;
	if (pos.row >= SHEET_MAX_ROWS)
		return 1;

	/* A new object is ready to be realized and inserted */
	if (scg->new_object != NULL)
		return ig_obj_create_begin (ig, event);

	/* If we are not configuring an object then clicking on the sheet
	 * ends the edit.
	 */
	if (scg->current_object != NULL) {
		if (!wbcg_edit_has_guru (scg->wbcg))
			scg_mode_edit (sc);
	} else
		wbcg_focus_cur_scg (scg->wbcg);

	/* If we were already selecting a range of cells for a formula,
	 * reset the location to a new place, or extend the selection.
	 */
	if (event->button == 1 && scg->rangesel.active) {
		ig->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		if (event->state & GDK_SHIFT_MASK)
			scg_rangesel_extend_to (scg, pos.col, pos.row);
		else
			scg_rangesel_bound (scg, pos.col, pos.row, pos.col, pos.row);
		gnm_canvas_slide_init (gcanvas);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		return 1;
	}

	/* If the user is editing a formula (wbcg_rangesel_possible) then we
	 * enable the dynamic cell selection mode.
	 */
	if (event->button == 1 && wbcg_rangesel_possible (scg->wbcg)) {
		scg_rangesel_start (scg, pos.col, pos.row, pos.col, pos.row);
		ig->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnm_canvas_slide_init (gcanvas);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		return 1;
	}

	/* While a guru is up ignore clicks */
	if (wbcg_edit_has_guru (scg->wbcg))
		return 1;

	/* This was a regular click on a cell on the spreadsheet.  Select it.
	 * but only if the entered expression is valid
	 */
	if (!wbcg_edit_finish (scg->wbcg, TRUE))
		return 1;

	/* button 1 will always change the selection,  the other buttons will
	 * only effect things if the target is not already selected.
	 */
	if (event->button == 1 || !sv_is_pos_selected (sc->view, pos.col, pos.row)) {
		if (!(event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)))
			sv_selection_reset (sc->view);

		if (event->button != 1 || !(event->state & GDK_SHIFT_MASK) ||
		    sc->view->selections == NULL) {
			sv_selection_add_pos (sc->view, pos.col, pos.row);
			sv_make_cell_visible (sc->view, pos.col, pos.row, FALSE);
		} else if (event->button != 2)
			sv_selection_extend_to (sc->view, pos.col, pos.row);
		sheet_update (sheet);
	}

      	switch (event->button) {
	case 1: {
		guint32 double_click_time;
		g_object_get (gtk_widget_get_settings (GTK_WIDGET (canvas)),
			"gtk-double-click-time", &double_click_time,
			NULL);

		if ((ig->last_click_time + double_click_time) > event->time &&
		    wbcg_edit_start (scg->wbcg, FALSE, FALSE))
			break;

		ig->last_click_time = event->time;
		ig->selecting = ITEM_GRID_SELECTING_CELL_RANGE;
		gnm_canvas_slide_init (gcanvas);
		gnm_simple_canvas_grab (item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL, event->time);
		break;
	}

      	/* This is here just for demo purposes */
      	case 2: drag_start (GTK_WIDGET (item->canvas), event, sheet);
		break;

      	case 3: scg_context_menu (ig->scg, event, FALSE, FALSE);
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
cb_extend_cell_range (GnumericCanvas *gcanvas, int col, int row, gpointer ignored)
{
	sv_selection_extend_to (((SheetControl *) gcanvas->simple.scg)->view, col, row);
	return TRUE;
}

static gboolean
cb_extend_expr_range (GnumericCanvas *gcanvas, int col, int row, gpointer ignored)
{
	scg_rangesel_extend_to (gcanvas->simple.scg, col, row);
	return TRUE;
}

static gboolean
cb_extend_object_creation (GnumericCanvas *gcanvas, int col, int row, gpointer ignored)
{
	int x, y;
	gdouble new_x, new_y;
	SheetControlGUI *scg = gcanvas->simple.scg;

	x = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, col);
	x += gcanvas->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, row);
	y += gcanvas->first_offset.row;
	gnome_canvas_c2w (GNOME_CANVAS (gcanvas), x, y, &new_x, &new_y);

	ig_obj_create_motion (gcanvas->pane->grid, new_x, new_y);
	return TRUE;
}

static gint
cb_cursor_come_to_rest (ItemGrid *ig)
{
	Sheet const *sheet = ((SheetControl *) ig->scg)->view->sheet;
	GnomeCanvas *canvas = ig->canvas_item.canvas;
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (canvas);
	GnmHLink *link;
	int x, y;
	CellPos pos;

	/* Be anal and look it up in case something has destroyed the link
	 * since the last motion */
	gnome_canvas_w2c (canvas, ig->last_x, ig->last_y, &x, &y);
	pos.col = gnm_canvas_find_col (gcanvas, x, NULL);
	pos.row = gnm_canvas_find_row (gcanvas, y, NULL);

	link = sheet_hlink_find (sheet, &pos);
	if (link != NULL && gnm_hlink_get_tip (link) != NULL) {
		g_return_val_if_fail (link == ig->cur_link, FALSE);

		if (ig->tip == NULL) {
			ig->tip = gnumeric_create_tooltip ();
			gtk_label_set_text (GTK_LABEL (ig->tip),
				gnm_hlink_get_tip (link));
			gnumeric_position_tooltip (ig->tip, TRUE);
			gtk_widget_show_all (gtk_widget_get_toplevel (ig->tip));
		}
	}

	return FALSE;
}

static gint
cb_cursor_motion (ItemGrid *ig)
{
	Sheet const *sheet = ((SheetControl *) ig->scg)->view->sheet;
	GnomeCanvas *canvas = ig->canvas_item.canvas;
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (canvas);
	int x, y, cursor;
	CellPos pos;
	GnmHLink *old_link;

	gnome_canvas_w2c (canvas, ig->last_x, ig->last_y, &x, &y);
	pos.col = gnm_canvas_find_col (gcanvas, x, NULL);
	pos.row = gnm_canvas_find_row (gcanvas, y, NULL);

	old_link = ig->cur_link;
	ig->cur_link = sheet_hlink_find (sheet, &pos);
	cursor = (NULL == ig->cur_link) ? E_CURSOR_FAT_CROSS : E_CURSOR_PRESS;

	ig->cursor_timer = 0;
	if (gcanvas->pane->cursor_type != cursor) {
		gcanvas->pane->cursor_type = cursor;
		scg_set_display_cursor (ig->scg);
	}
	if (old_link != ig->cur_link && ig->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ig->tip));
		ig->tip = NULL;
	}
	return FALSE;
}

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (canvas);
	ItemGrid *ig = ITEM_GRID (item);
	SheetControlGUI *scg = ig->scg;
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;

	switch (event->type){
	case GDK_ENTER_NOTIFY:
		scg_set_display_cursor (scg);
		return TRUE;
	case GDK_LEAVE_NOTIFY:
		ig_clear_hlink_tip (ig);
		return TRUE;

	case GDK_BUTTON_RELEASE: {
		ItemGridSelectionType selecting = ig->selecting;

		if (event->button.button != 1)
			return FALSE;

		gnm_canvas_slide_stop (gcanvas);

		switch (selecting) {
		case ITEM_GRID_NO_SELECTION:
			return TRUE;

		case ITEM_GRID_SELECTING_FORMULA_RANGE :
/*  Removal of this code (2 lines)                                                */
/*  should fix http://bugzilla.gnome.org/show_bug.cgi?id=63485                    */
/* 			sheet_make_cell_visible (sheet,                           */
/* 				sheet->edit_pos.col, sheet->edit_pos.row, FALSE); */
			/* Fall through */
		case ITEM_GRID_SELECTING_CELL_RANGE :
			wb_view_selection_desc (
				wb_control_view (sc->wbc), TRUE, NULL);
			break;

		case ITEM_GRID_SELECTING_OBJECT_CREATION:
			ig_obj_create_finish (ig, &event->button);
			break;
		default:
			g_assert_not_reached ();
		};

		ig->selecting = ITEM_GRID_NO_SELECTION;
		gnm_simple_canvas_ungrab (item, event->button.time);

		if (selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
			gnm_expr_entry_end_of_drag (wbcg_get_entry_logical (scg->wbcg));

		if (selecting == ITEM_GRID_SELECTING_CELL_RANGE) {
			CellPos const *pos = sv_is_singleton_selected (sc_view (sc));
			if (pos != NULL) {
				GnmHLink *link;
				/* check for hyper links */
				link = sheet_hlink_find (sheet, pos);
				if (link != NULL)
					gnm_hlink_activate (link, sc->wbc);
			}
		}
		return TRUE;
	}

	case GDK_MOTION_NOTIFY: {
		GnumericCanvasSlideHandler slide_handler = NULL;
		switch (ig->selecting) {
		case ITEM_GRID_NO_SELECTION:
			if (ig->cursor_timer == 0)
				ig->cursor_timer = gtk_timeout_add (100,
					(GtkFunction)cb_cursor_motion, ig);
			if (ig->tip_timer != 0)
				gtk_timeout_remove (ig->tip_timer);
			ig->tip_timer = gtk_timeout_add (500,
					(GtkFunction)cb_cursor_come_to_rest, ig);
			ig->last_x = event->motion.x;
			ig->last_y = event->motion.y;
			return TRUE;
		case ITEM_GRID_SELECTING_CELL_RANGE :
			slide_handler = &cb_extend_cell_range;
			break;
		case ITEM_GRID_SELECTING_FORMULA_RANGE :
			slide_handler = &cb_extend_expr_range;
			break;
		case ITEM_GRID_SELECTING_OBJECT_CREATION:
			/* TODO : motion is still too granular along the internal axis
			 * when the other axis is external.
			 * eg  drag from middle of sheet down.  The x axis is still internal
			 * only the y is external, however, since we are autoscrolling
			 * we are limited to moving with col/row coords, not x,y.
			 * Possible solution would be to change the EXTERIOR_ONLY flag
			 * to something like USE_PIXELS_INSTEAD_OF_COLROW and change
			 * the semantics of the col,row args in the callback.  However,
			 * that is more work than I want to do right now.
			 */
			if (gnm_canvas_handle_motion (gcanvas, canvas, &event->motion,
						      GNM_SLIDE_X | GNM_SLIDE_Y,
						      &cb_extend_object_creation, NULL))
				ig_obj_create_motion (ig, event->motion.x, event->motion.y);

			return TRUE;
		default:
			g_assert_not_reached ();
		};

		gnm_canvas_handle_motion (gcanvas, canvas, &event->motion,
			GNM_SLIDE_X | GNM_SLIDE_Y | GNM_SLIDE_AT_COLROW_BOUND,
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

/*
 * Instance initialization
 */
static void
item_grid_init (ItemGrid *ig)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (ig);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	ig->selecting = ITEM_GRID_NO_SELECTION;
	ig->gc.fill = ig->gc.cell = ig->gc.empty = ig->gc.bound = NULL;
	range_init_full_sheet (&ig->bound);
	ig->obj_create.item = NULL;
	ig->cursor_timer = 0;
	ig->cur_link = NULL;
	ig->tip_timer = 0;
	ig->tip = NULL;
}

static void
item_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	ItemGrid *ig = ITEM_GRID (o);

	switch (arg_id) {
	case ARG_SHEET_CONTROL_GUI :
		ig->scg = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_SHEET_BOUND : {
		Range const *r = GTK_VALUE_POINTER (*arg);
		g_return_if_fail (r != NULL);
		ig->bound =  *r;
		break;
	}

	default :
		g_warning ("unknown arg %d", arg_id);
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

	item_grid_parent_class =
		g_type_class_peek (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);
	gtk_object_add_arg_type ("ItemGrid::Bound", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_BOUND);

	object_class->set_arg   = item_grid_set_arg;
	object_class->destroy   = item_grid_destroy;
	item_class->update      = item_grid_update;
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->event       = item_grid_event;
}

GSF_CLASS (ItemGrid, item_grid,
	   item_grid_class_init, item_grid_init,
	   GNOME_TYPE_CANVAS_ITEM);
