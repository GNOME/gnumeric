/* vim: set sw=8: */
/*
 * The Cursor Canvas Item: Implements a rectangular cursor
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "item-cursor.h"
#include "gnm-pane-impl.h"

#include "sheet-control-gui.h"
#include "sheet-control-priv.h"
#include "style-color.h"
#include "cell.h"
#include "clipboard.h"
#include "selection.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "value.h"
#include "workbook.h"
#include "wbc-gtk.h"
#include "gui-util.h"
#include "cmd-edit.h"
#include "commands.h"
#include "ranges.h"
#include "parse-util.h"
#include "gui-util.h"
#include "sheet-autofill.h"
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <goffice/app/go-cmd-context-impl.h>
#define GNUMERIC_ITEM "CURSOR"
#include "item-debug.h"

#define ITEM_CURSOR_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST ((k), item_cursor_get_type (), ItemCursorClass))

#define AUTO_HANDLE_WIDTH	2
#define AUTO_HANDLE_SPACE	(AUTO_HANDLE_WIDTH * 2)
#define CLIP_SAFETY_MARGIN      (AUTO_HANDLE_SPACE + 5)

struct _ItemCursor {
	FooCanvasItem canvas_item;

	SheetControlGUI *scg;
	gboolean	 pos_initialized;
	GnmRange	 pos;

	/* Offset of dragging cell from top left of pos */
	int col_delta, row_delta;

	/* Tip for movement */
	GtkWidget        *tip;
	GnmCellPos last_tip_pos;

	ItemCursorStyle style;
	GdkGC    *gc;
	int      state;
	int      animation_timer;

	/*
	 * For the autofill mode:
	 *     Where the action started (base_x, base_y) and the
	 *     width and heigth of the selection when the autofill
	 *     started.
	 */
	int   base_x, base_y;
	GnmRange autofill_src;
	int   autofill_hsize, autofill_vsize;

	/* cursor outline in canvas coords, the bounding box (Item::[xy][01])
	 * is slightly larger ) */
	struct {
		int x1, x2, y1, y2;
	} outline;
	int drag_button;
	guint drag_button_state;

	gboolean visible;
	gboolean use_color;
	gboolean auto_fill_handle_at_top;
	gboolean auto_fill_handle_at_left;

	GdkPixmap *stipple;
	GdkColor  color;
};
typedef FooCanvasItemClass ItemCursorClass;

static FooCanvasItemClass *parent_class;

enum {
	ITEM_CURSOR_PROP_0,
	ITEM_CURSOR_PROP_SHEET_CONTROL_GUI,
	ITEM_CURSOR_PROP_STYLE,
	ITEM_CURSOR_PROP_BUTTON,
	ITEM_CURSOR_PROP_COLOR
};

static int
cb_item_cursor_animation (ItemCursor *ic)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);

	ic->state = !ic->state;
	foo_canvas_item_request_update (item);
	return TRUE;
}

static void
item_cursor_dispose (GObject *obj)
{
	ItemCursor *ic = ITEM_CURSOR (obj);

	if (ic->tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ic->tip));
		ic->tip = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
item_cursor_realize (FooCanvasItem *item)
{
	ItemCursor *ic;
	GdkWindow  *window;

	if (parent_class->realize)
		(*parent_class->realize) (item);

	ic = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	ic->gc = gdk_gc_new (window);

	if (ic->style == ITEM_CURSOR_ANTED) {
		g_return_if_fail (ic->animation_timer == -1);
		ic->animation_timer = g_timeout_add (
			150, (GSourceFunc)cb_item_cursor_animation,
			ic);
	}

	if (ic->style == ITEM_CURSOR_DRAG || ic->style == ITEM_CURSOR_AUTOFILL) {
		static char const stipple_data [] = { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa };
		ic->stipple = gdk_bitmap_create_from_data (window, stipple_data, 8, 8);
	}
}

static void
item_cursor_unrealize (FooCanvasItem *item)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	g_object_unref (G_OBJECT (ic->gc));
	ic->gc = NULL;

	if (ic->stipple) {
		g_object_unref (ic->stipple);
		ic->stipple = NULL;
	}

	if (ic->animation_timer != -1) {
		g_source_remove (ic->animation_timer);
		ic->animation_timer = -1;
	}

	if (parent_class->unrealize)
		(*parent_class->unrealize) (item);
}

static void
item_cursor_update (FooCanvasItem *item, double i2w_dx, double i2w_dy, int flags)
{
	ItemCursor	*ic = ITEM_CURSOR (item);
	GnmPane		*pane = GNM_PANE (item->canvas);
	SheetControlGUI const * const scg = ic->scg;
	int tmp;

	int const left	 = ic->pos.start.col;
	int const right	 = ic->pos.end.col;
	int const top	 = ic->pos.start.row;
	int const bottom = ic->pos.end.row;

	foo_canvas_item_request_redraw (item); /* Erase the old cursor */

	ic->outline.x1 = pane->first_offset.col +
		scg_colrow_distance_get (scg, TRUE, pane->first.col, left);
	ic->outline.x2 = ic->outline.x1 +
		scg_colrow_distance_get (scg, TRUE, left, right+1);
	ic->outline.y1 = pane->first_offset.row +
		scg_colrow_distance_get (scg, FALSE, pane->first.row, top);
	ic->outline.y2 = ic->outline.y1 +
		scg_colrow_distance_get (scg, FALSE,top, bottom+1);

	if (scg_sheet (scg)->text_is_rtl) {
		tmp = ic->outline.x1;
		ic->outline.x1 = gnm_foo_canvas_x_w2c (item->canvas, ic->outline.x2);
		ic->outline.x2 = gnm_foo_canvas_x_w2c (item->canvas, tmp);
	}
	/* NOTE : sometimes y1 > y2 || x1 > x2 when we create a cursor in an
	 * invisible region such as above a frozen pane */

	item->x1 = ic->outline.x1 - 1;
	item->y1 = ic->outline.y1 - 1;

	/* for the autohandle */
	tmp = (ic->style == ITEM_CURSOR_SELECTION) ? AUTO_HANDLE_WIDTH : 0;
	item->x2 = ic->outline.x2 + 3 + tmp;
	item->y2 = ic->outline.y2 + 3 + tmp;

	foo_canvas_item_request_redraw (item); /* draw the new cursor */

	if (parent_class->update)
		(*parent_class->update) (item, i2w_dx, i2w_dy, flags);
}

static void
item_cursor_draw (FooCanvasItem *item, GdkDrawable *drawable,
		  GdkEventExpose *expose)
{
	GdkGCValues values;
	ItemCursor *ic = ITEM_CURSOR (item);
	int x0, y0, x1, y1; /* in canvas coordinates */
	GdkPoint points [5];
	int draw_thick, draw_handle;
	int premove = 0;
	GdkColor *fore = NULL, *back = NULL;
	gboolean draw_stippled, draw_center, draw_external, draw_internal, draw_xor;

#if 0
	g_print ("draw[%d] %d,%d %d,%d\n",
		 GNM_PANE (item->canvas)->index,
		 ic->outline.x1,
		 ic->outline.y1,
		 ic->outline.x2,
		 ic->outline.y2);
#endif
	if (!ic->visible || !ic->pos_initialized)
		return;

	x0 = ic->outline.x1;
	y0 = ic->outline.y1;
	x1 = ic->outline.x2;
	y1 = ic->outline.y2;

	/* only mostly in invisible areas (eg on creation of frozen panes) */
	if (x0 > x1 || y0 > y1)
		return;

	draw_external = FALSE;
	draw_internal = FALSE;
	draw_handle   = 0;
	draw_thick    = 1;
	draw_center   = FALSE;
	draw_stippled = FALSE;
	draw_xor      = TRUE;

	switch (ic->style) {
	case ITEM_CURSOR_AUTOFILL:
	case ITEM_CURSOR_DRAG:
		draw_center   = TRUE;
		draw_thick    = 3;
		draw_stippled = TRUE;
		fore          = &gs_white;
		back          = &gs_white;
		break;

	case ITEM_CURSOR_BLOCK:
		draw_center   = TRUE;
		draw_thick    = 3;
		draw_xor      = FALSE;
		break;

	case ITEM_CURSOR_SELECTION:
		draw_internal = TRUE;
		draw_external = TRUE;
		{
			GnmPane const *pane = GNM_PANE (item->canvas);
			GnmPane const *pane0 = scg_pane (pane->simple.scg, 0);

			/* In pane */
			if (ic->pos.end.row <= pane->last_full.row)
				draw_handle = 1;
			/* In pane below */
			else if ((pane->index == 2 || pane->index == 3) &&
				 ic->pos.end.row >= pane0->first.row &&
				 ic->pos.end.row <= pane0->last_full.row)
				draw_handle = 1;
			/* TODO : do we want to add checking for pane above ? */
			else if (ic->pos.start.row < pane->first.row)
				draw_handle = 0;
			else if (ic->pos.start.row != pane->first.row)
				draw_handle = 2;
			else
				draw_handle = 3;
		}
		break;

	case ITEM_CURSOR_ANTED:
		draw_center   = TRUE;
		draw_thick    = 2;
		if (ic->state) {
			fore = &gs_light_gray;
			back = &gs_dark_gray;
		} else {
			fore = &gs_dark_gray;
			back = &gs_light_gray;
		}
	}

	if (ic->use_color) {
		fore = &ic->color;
		back = &ic->color;
	}

	ic->auto_fill_handle_at_top = (draw_handle >= 2);

	gdk_gc_set_clip_rectangle (ic->gc, &expose->area);

	/* Avoid guint16 overflow during line drawing.  We can change
	 * the shape we draw, so long as no lines or parts of
	 * rectangles are moved from outside to inside the clipping
	 * region */
	x0 = MAX (x0, expose->area.x - CLIP_SAFETY_MARGIN);
	y0 = MAX (y0, expose->area.y - CLIP_SAFETY_MARGIN);
	x1 = MIN (x1, expose->area.x + expose->area.width + CLIP_SAFETY_MARGIN);
	y1 = MIN (y1, expose->area.y + expose->area.height + CLIP_SAFETY_MARGIN);

	if (x0 >= x1 || y0 >= y1)
		draw_handle = 0;

	gdk_gc_set_line_attributes (ic->gc, 1,
		GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
	gdk_gc_set_rgb_fg_color (ic->gc, &gs_white);
	gdk_gc_set_rgb_bg_color (ic->gc, &gs_white);
	if (draw_xor) {
		values.function = GDK_XOR;
		gdk_gc_set_values (ic->gc, &values, GDK_GC_FUNCTION);
	}

	if (draw_external) {
		switch (draw_handle) {
		/* Auto handle at bottom */
		case 1 :
			premove = AUTO_HANDLE_SPACE;
			/* Fall through */

		/* No auto handle */
		case 0 :
			points [0].x = x1 + 1;
			points [0].y = y1 + 1 - premove;
			points [1].x = points [0].x;
			points [1].y = y0 - 1;
			points [2].x = x0 - 1;
			points [2].y = y0 - 1;
			points [3].x = x0 - 1;
			points [3].y = y1 + 1;
			points [4].x = x1 + 1 - premove;
			points [4].y = points [3].y;
			break;

		/* Auto handle at top */
		case 2 : premove = AUTO_HANDLE_SPACE;
			 /* Fall through */

		/* Auto handle at top of sheet */
		case 3 :
			points [0].x = x1 + 1;
			points [0].y = y0 - 1 + AUTO_HANDLE_SPACE;
			points [1].x = points [0].x;
			points [1].y = y1 + 1;
			points [2].x = x0 - 1;
			points [2].y = points [1].y;
			points [3].x = points [2].x;
			points [3].y = y0 - 1;
			points [4].x = x1 + 1 - premove;
			points [4].y = points [3].y;
			break;

		default :
			g_assert_not_reached ();
		}
		gdk_draw_lines (drawable, ic->gc, points, 5);
	}

	if (draw_external && draw_internal) {
		if (draw_handle < 2) {
			points [0].x -= 2;
			points [1].x -= 2;
			points [1].y += 2;
			points [2].x += 2;
			points [2].y += 2;
			points [3].x += 2;
			points [3].y -= 2;
			points [4].y -= 2;
		} else {
			points [0].x -= 2;
			points [1].x -= 2;
			points [1].y -= 2;
			points [2].x += 2;
			points [2].y -= 2;
			points [3].x += 2;
			points [3].y += 2;
			points [4].y += 2;
		}
		gdk_draw_lines (drawable, ic->gc, points, 5);
	}

	if (draw_handle == 1 || draw_handle == 2) {
		int const y_off = (draw_handle == 1) ? y1 - y0 : 0;
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 - 2,
				    y0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 + 1,
				    y0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 - 2,
				    y0 + y_off + 1,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 + 1,
				    y0 + y_off + 1,
				    2, 2);
	} else if (draw_handle == 3) {
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 - 2,
				    y0 + 1,
				    2, 4);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    x1 + 1,
				    y0 + 1,
				    2, 4);
	}

	if (draw_center) {
		gdk_gc_set_rgb_fg_color (ic->gc, fore);
		gdk_gc_set_rgb_bg_color (ic->gc, back);

		if (draw_stippled) {
			gdk_gc_set_fill (ic->gc, GDK_STIPPLED);
			gdk_gc_set_stipple (ic->gc, ic->stipple);
			gdk_gc_set_line_attributes (ic->gc, draw_thick,
				GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
		} else
			gdk_gc_set_line_attributes (ic->gc, draw_thick,
				GDK_LINE_DOUBLE_DASH, GDK_CAP_BUTT, GDK_JOIN_MITER);

		/* Stay in the boundary */
		if ((draw_thick % 2) == 0) {
			x0++;
			y0++;
		}
		gdk_draw_rectangle (drawable, ic->gc, FALSE,
				    x0, y0,
				    abs (x1 - x0), abs (y1 - y0));
	}
}

gboolean
item_cursor_bound_set (ItemCursor *ic, GnmRange const *new_bound)
{
	g_return_val_if_fail (IS_ITEM_CURSOR (ic), FALSE);
	g_return_val_if_fail (range_is_sane (new_bound), FALSE);

	if (ic->pos_initialized && range_equal (&ic->pos, new_bound))
		return FALSE;

	ic->pos = *new_bound;
	ic->pos_initialized = TRUE;

	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ic));

	return TRUE;
}

/**
 * item_cursor_reposition : Re-compute the pixel position of the cursor.
 *
 * When a sheet is zoomed.  The pixel coords shift slightly.  The item cursor
 * must regenerate to stay in sync.
 **/
void
item_cursor_reposition (ItemCursor *ic)
{
	g_return_if_fail (ic != NULL);
	foo_canvas_item_request_update (&ic->canvas_item);
}

static double
item_cursor_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		   FooCanvasItem **actual_item)
{
	ItemCursor const *ic = ITEM_CURSOR (item);

	/* Cursor should not always receive events
	 * 1) when invisible
	 * 2) when animated
	 * 3) while a guru is up
	 */
	if (!ic->visible || ic->style == ITEM_CURSOR_ANTED ||
	    wbc_gtk_get_guru (scg_wbcg (ic->scg)) != NULL)
		return DBL_MAX;

	*actual_item = NULL;

	if (cx < item->x1-3)
		return DBL_MAX;
	if (cx > item->x2+3)
		return DBL_MAX;
	if (cy < item->y1-3)
		return DBL_MAX;
	if (cy > item->y2+3)
		return DBL_MAX;

	/* FIXME: the drag handle for ITEM_CURSOR_SELECTION needs work */
	if ((cx < (item->x1 + 4)) || (cx > (item->x2 - 8)) ||
	    (cy < (item->y1 + 4)) || (cy > (item->y2 - 8))) {
		*actual_item = item;
		return 0.0;
	}
	return DBL_MAX;
}

static void
item_cursor_setup_auto_fill (ItemCursor *ic, ItemCursor const *parent, int x, int y)
{
	Sheet const *sheet = scg_sheet (parent->scg);
	GSList *merges;

	ic->base_x = x;
	ic->base_y = y;

	ic->autofill_src = parent->pos;

	/* If there are arrays or merges in the region ensure that we
	 * need to ensure that an integer multiple of the original size
	 * is filled.   We could be fancy about this an allow filling as long
	 * as the merges would not be split, bu that is more work than it is
	 * worth right now (FIXME this is a nice project).
	 *
	 * We do not have to be too careful, the sheet guarantees that the
	 * cursor does not split merges, all we need is existence.
	 */
	merges = gnm_sheet_merge_get_overlap (sheet, &ic->autofill_src);
	if (merges != NULL) {
		g_slist_free (merges);
		ic->autofill_hsize = range_width (&ic->autofill_src);
		ic->autofill_vsize = range_height (&ic->autofill_src);
	} else
		ic->autofill_hsize = ic->autofill_vsize = 1;
}

static inline gboolean
item_cursor_in_drag_handle (ItemCursor *ic, int x, int y)
{
	int const y_test = ic->auto_fill_handle_at_top
		? ic->canvas_item.y1 + AUTO_HANDLE_WIDTH
		: ic->canvas_item.y2 - AUTO_HANDLE_WIDTH;

	if ((y_test-AUTO_HANDLE_SPACE) <= y &&
	    y <= (y_test+AUTO_HANDLE_SPACE)) {
		int const x_test = ic->auto_fill_handle_at_left
			? ic->canvas_item.x1 + AUTO_HANDLE_WIDTH
			: ic->canvas_item.x2 - AUTO_HANDLE_WIDTH;
		return (x_test-AUTO_HANDLE_SPACE) <= x &&
			x <= (x_test+AUTO_HANDLE_SPACE);
	 }
	return FALSE;
}

static void
item_cursor_set_cursor (FooCanvas *canvas, ItemCursor *ic, int x, int y)
{
	GdkCursorType cursor;

	if (item_cursor_in_drag_handle (ic, x, y))
		cursor = GDK_CROSSHAIR;
	else
		cursor = GDK_ARROW;

	gnm_widget_set_cursor_type (GTK_WIDGET (canvas), cursor);
}

static gint
item_cursor_selection_event (FooCanvasItem *item, GdkEvent *event)
{
	FooCanvas  *canvas = item->canvas;
	GnmPane  *pane = GNM_PANE (canvas);
	ItemCursor *ic = ITEM_CURSOR (item);
	int x, y;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		foo_canvas_w2c (canvas,
			event->crossing.x, event->crossing.y, &x, &y);
		item_cursor_set_cursor (canvas, ic, x, y);
		return TRUE;

	case GDK_MOTION_NOTIFY: {
		int style, button;
		ItemCursor *special_cursor;

		foo_canvas_w2c (canvas,
			event->motion.x, event->motion.y, &x, &y);

		if (ic->drag_button < 0) {
			item_cursor_set_cursor (canvas, ic, x, y);
			return TRUE;
		}

		/*
		 * determine which part of the cursor was clicked:
		 * the border or the handlebox
		 */
		if (item_cursor_in_drag_handle (ic, x, y))
			style = ITEM_CURSOR_AUTOFILL;
		else
			style = ITEM_CURSOR_DRAG;

		button = ic->drag_button;
		ic->drag_button = -1;
		gnm_simple_canvas_ungrab (item, event->button.time);

		scg_special_cursor_start (ic->scg, style, button);
		special_cursor = pane->cursor.special;
		special_cursor->drag_button_state = ic->drag_button_state;
		if (style == ITEM_CURSOR_AUTOFILL)
			item_cursor_setup_auto_fill (
				special_cursor, ic, x, y);

		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;
		/*
		 * Capture the offset of the current cell relative to
		 * the upper left corner.  Be careful handling the position
		 * of the cursor.  it is possible to select the exterior or
		 * interior of the cursor edge which behaves as if the cursor
		 * selection was offset by one.
		 */
		{
			int d_col = gnm_pane_find_col (pane, x, NULL) -
				ic->pos.start.col;
			int d_row = gnm_pane_find_row (pane, y, NULL) -
				ic->pos.start.row;

			if (d_col >= 0) {
				int tmp = ic->pos.end.col - ic->pos.start.col;
				if (d_col > tmp)
					d_col = tmp;
			} else
				d_col = 0;
			special_cursor->col_delta = d_col;

			if (d_row >= 0) {
				int tmp = ic->pos.end.row - ic->pos.start.row;
				if (d_row > tmp)
					d_row = tmp;
			} else
				d_row = 0;
			special_cursor->row_delta = d_row;
		}

		if (scg_special_cursor_bound_set (ic->scg, &ic->pos))
			foo_canvas_update_now (canvas);

		gnm_simple_canvas_grab (FOO_CANVAS_ITEM (special_cursor),
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
			NULL, event->button.time);
		gnm_pane_slide_init (pane);

		/*
		 * We flush after the grab to ensure that the new item-cursor
		 * gets created.  If it is not ready in time double click
		 * events will be disrupted and it will appear as if we are
		 * doing an button_press with a missing release.
		 */
		gdk_flush ();
		return TRUE;
	}

	case GDK_2BUTTON_PRESS: {
		Sheet *sheet = scg_sheet (ic->scg);
		int final_col = ic->pos.end.col;
		int final_row = ic->pos.end.row;

		if (ic->drag_button != (int)event->button.button)
			return TRUE;

		ic->drag_button = -1;
		gnm_simple_canvas_ungrab (item, event->button.time);

		if (sheet_is_region_empty (sheet, &ic->pos))
			return TRUE;

		/* If the cell(s) immediately below the ones in the
		 * auto-fill template are not blank then over-write
		 * them.
		 *
		 * Otherwise, only go as far as the next non-blank
		 * cells.
		 *
		 * The code below uses find_boundary twice.  a. to
		 * find the boundary of the column/row that acts as a
		 * template to define the region to file and b. to
		 * find the boundary of the region being filled.
		 */

		if (event->button.state & GDK_MOD1_MASK) {
			int template_col = ic->pos.end.col + 1;
			int template_row = ic->pos.start.row - 1;
			int boundary_col_for_target;
			int target_row;

			if (template_row < 0 || template_col >= gnm_sheet_get_max_cols (sheet) ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row)) {

				template_row = ic->pos.end.row + 1;
				if (template_row >= gnm_sheet_get_max_rows (sheet) ||
				    template_col >= gnm_sheet_get_max_cols (sheet) ||
				    sheet_is_cell_empty (sheet, template_col,
							 template_row))
					return TRUE;
			}

			if (template_col >= gnm_sheet_get_max_cols (sheet) ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row))
				return TRUE;
			final_col = sheet_find_boundary_horizontal (sheet,
				ic->pos.end.col, template_row,
				template_row, 1, TRUE);
			if (final_col <= ic->pos.end.col)
				return TRUE;

			/*
			   Find the boundary of the target region.
			   We don't want to go beyond this boundary.
			*/
			for (target_row = ic->pos.start.row; target_row <= ic->pos.end.row; target_row++) {
				/* find_boundary is designed for Ctrl-arrow movement.  (Ab)using it for
				 * finding autofill regions works fairly well.  One little gotcha is
				 * that if the current col is the last row of a block of data Ctrl-arrow
				 * will take you to then next block.  The workaround for this is to
				 * start the search at the last col of the selection, rather than
				 * the first col of the region being filled.
				 */
				boundary_col_for_target = sheet_find_boundary_horizontal
					(sheet,
					 ic->pos.end.col, target_row,
					 target_row, 1, TRUE);

				if (sheet_is_cell_empty (sheet, boundary_col_for_target-1, target_row) &&
				    ! sheet_is_cell_empty (sheet, boundary_col_for_target, target_row)) {
					/* target region was empty, we are now one col
					   beyond where it is safe to autofill. */
					boundary_col_for_target--;
				}
				if (boundary_col_for_target < final_col) {
					final_col = boundary_col_for_target;
				}
			}
		} else {
			int template_row = ic->pos.end.row + 1;
			int template_col = ic->pos.start.col - 1;
			int boundary_row_for_target;
			int target_col;

			if (template_col < 0 || template_row >= gnm_sheet_get_max_rows (sheet) ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row)) {

				template_col = ic->pos.end.col + 1;
				if (template_col >= gnm_sheet_get_max_cols (sheet) ||
				    template_row >= gnm_sheet_get_max_rows (sheet) ||
				    sheet_is_cell_empty (sheet, template_col,
							 template_row))
					return TRUE;
			}

			if (template_row >= gnm_sheet_get_max_rows (sheet) ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row))
				return TRUE;
			final_row = sheet_find_boundary_vertical (sheet,
				template_col, ic->pos.end.row,
				template_col, 1, TRUE);
			if (final_row <= ic->pos.end.row)
				return TRUE;

			/*
			   Find the boundary of the target region.
			   We don't want to go beyond this boundary.
			*/
			for (target_col = ic->pos.start.col; target_col <= ic->pos.end.col; target_col++) {
				/* find_boundary is designed for Ctrl-arrow movement.  (Ab)using it for
				 * finding autofill regions works fairly well.  One little gotcha is
				 * that if the current row is the last row of a block of data Ctrl-arrow
				 * will take you to then next block.  The workaround for this is to
				 * start the search at the last row of the selection, rather than
				 * the first row of the region being filled.
				 */
				boundary_row_for_target = sheet_find_boundary_vertical
					(sheet,
					 target_col, ic->pos.end.row,
					 target_col, 1, TRUE);
				if (sheet_is_cell_empty (sheet, target_col, boundary_row_for_target-1) &&
				    ! sheet_is_cell_empty (sheet, target_col, boundary_row_for_target)) {
					/* target region was empty, we are now one row
					   beyond where it is safe to autofill. */
					boundary_row_for_target--;
				}

				if (boundary_row_for_target < final_row) {
					final_row = boundary_row_for_target;
				}
			}
		}

		/* fill the row/column */
		cmd_autofill (scg_wbc (ic->scg), sheet, FALSE,
			      ic->pos.start.col, ic->pos.start.row,
			      ic->pos.end.col - ic->pos.start.col + 1,
			      ic->pos.end.row - ic->pos.start.row + 1,
			      final_col, final_row,
			      FALSE);

		return TRUE;
	}

	case GDK_BUTTON_PRESS:
		/* NOTE : this cannot be called while we are editing.  because
		 * the point routine excludes events.  so we do not need to
		 * call wbcg_edit_finish.
		 */

		/* scroll wheel events dont have corresponding release events */
		if (event->button.button > 3)
			return FALSE;

		/* If another button is already down ignore this one */
		if (ic->drag_button >= 0)
			return TRUE;

		if (event->button.button != 3) {
			int x, y;

			/* prepare to create fill or drag cursors, but dont until we
			 * move.  If we did create them here there would be problems
			 * with race conditions when the new cursors pop into existence
			 * during a double-click
			 */

			foo_canvas_w2c (canvas,
					event->button.x, event->button.y, &x, &y);
			if (item_cursor_in_drag_handle (ic, x, y))
				go_cmd_context_progress_message_set (GO_CMD_CONTEXT (scg_wbcg (ic->scg)),
								     _("Drag to autofill"));
			else
				go_cmd_context_progress_message_set (GO_CMD_CONTEXT (scg_wbcg (ic->scg)),
								     _("Drag to move"));

			ic->drag_button = event->button.button;
			ic->drag_button_state = event->button.state;
			gnm_simple_canvas_grab (item,
				GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
				NULL, event->button.time);
		} else
			scg_context_menu (ic->scg, &event->button, FALSE, FALSE);
		return TRUE;

	case GDK_BUTTON_RELEASE:
		if (ic->drag_button != (int)event->button.button)
			return TRUE;

		/* Double clicks may have already released the drag prep */
		if (ic->drag_button >= 0) {
			gnm_simple_canvas_ungrab (item, event->button.time);
			ic->drag_button = -1;
		}
		go_cmd_context_progress_message_set (GO_CMD_CONTEXT (scg_wbcg (ic->scg)),
						     " ");
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
item_cursor_target_region_ok (ItemCursor *ic)
{
	FooCanvasItem *gci = FOO_CANVAS_ITEM (ic);

	g_return_val_if_fail (gci != NULL, FALSE);
	g_return_val_if_fail (gci->canvas != NULL, FALSE);

	if (sv_is_region_empty_or_selected (scg_view (ic->scg), &ic->pos))
		return TRUE;

	return go_gtk_query_yes_no
		(wbcg_toplevel (scg_wbcg (ic->scg)),
		 TRUE,
		 _("The cells dragged will overwrite the contents of the\n"
		   "existing cells in that range.  Do you want me to replace\n"
		   "the contents in this region?"));
}

typedef enum {
	ACTION_NONE = 1,
	ACTION_MOVE_CELLS,
	ACTION_COPY_CELLS,
	ACTION_COPY_FORMATS,
	ACTION_COPY_VALUES,
	ACTION_SHIFT_DOWN_AND_COPY,
	ACTION_SHIFT_RIGHT_AND_COPY,
	ACTION_SHIFT_DOWN_AND_MOVE,
	ACTION_SHIFT_RIGHT_AND_MOVE
} ActionType;

static void
item_cursor_do_action (ItemCursor *ic, ActionType action)
{
	SheetView	*sv;
	Sheet		*sheet;
	WorkbookControl *wbc;
	GnmPasteTarget pt;

	g_return_if_fail (ic != NULL);

	if (action == ACTION_NONE || !item_cursor_target_region_ok (ic)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	sheet = scg_sheet (ic->scg);
	sv = scg_view (ic->scg);
	wbc = scg_wbc (ic->scg);

	switch (action) {
	case ACTION_COPY_CELLS:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES));
		break;

	case ACTION_MOVE_CELLS:
		if (!sv_selection_cut (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES));
		break;

	case ACTION_COPY_FORMATS:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_FORMATS));
		break;

	case ACTION_COPY_VALUES:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_AS_VALUES));
		break;

	case ACTION_SHIFT_DOWN_AND_COPY:
	case ACTION_SHIFT_RIGHT_AND_COPY:
	case ACTION_SHIFT_DOWN_AND_MOVE:
	case ACTION_SHIFT_RIGHT_AND_MOVE:
		g_warning ("Operation not yet implemented.");
		break;

	default :
		g_warning ("Invalid Operation %d.", action);
	}

	scg_special_cursor_stop	(ic->scg);
}

static gboolean
context_menu_hander (GnumericPopupMenuElement const *element,
		     gpointer ic)
{
	g_return_val_if_fail (element != NULL, TRUE);

	item_cursor_do_action (ic, element->index);
	return TRUE;
}

static void
item_cursor_popup_menu (ItemCursor *ic, GdkEventButton *event)
{
	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("_Move"),		NULL,
		    0, 0, ACTION_MOVE_CELLS },

		{ N_("_Copy"),		GTK_STOCK_COPY,
		    0, 0, ACTION_COPY_CELLS },

		{ N_("Copy _Formats"),		NULL,
		    0, 0, ACTION_COPY_FORMATS },
		{ N_("Copy _Values"),		NULL,
		    0, 0, ACTION_COPY_VALUES },

		{ "", NULL, 0, 0, 0 },

		{ N_("Shift _Down and Copy"),		NULL,
		    0, 0, ACTION_SHIFT_DOWN_AND_COPY },
		{ N_("Shift _Right and Copy"),		NULL,
		    0, 0, ACTION_SHIFT_RIGHT_AND_COPY },
		{ N_("Shift Dow_n and Move"),		NULL,
		    0, 0, ACTION_SHIFT_DOWN_AND_MOVE },
		{ N_("Shift Righ_t and Move"),		NULL,
		    0, 0, ACTION_SHIFT_RIGHT_AND_MOVE },

		{ "", NULL, 0, 0, 0 },

		{ N_("C_ancel"),		NULL,
		    0, 0, ACTION_NONE },

		{ NULL, NULL, 0, 0, 0 }
	};

	gnumeric_create_popup_menu (popup_elements,
				    &context_menu_hander, ic,
				    0, 0, event);
}

static void
item_cursor_do_drop (ItemCursor *ic, GdkEventButton *event)
{
	/* Only do the operation if something moved */
	SheetView const *sv = scg_view (ic->scg);
	GnmRange const *target = selection_first_range (sv, NULL, NULL);

	wbcg_set_status_text (scg_wbcg (ic->scg), "");
	if (range_equal (target, &ic->pos)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	if (event->button == 3)
		item_cursor_popup_menu (ic, event);
	else
		item_cursor_do_action (ic, (event->state & GDK_CONTROL_MASK)
				       ? ACTION_COPY_CELLS
				       : ACTION_MOVE_CELLS);
}

void
item_cursor_set_visibility (ItemCursor *ic, gboolean visible)
{
	g_return_if_fail (IS_ITEM_CURSOR (ic));

	if (ic->visible == visible)
		return;

	ic->visible = visible;
	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ic));
}

static void
item_cursor_tip_setlabel (ItemCursor *ic, char const *text)
{
	if (ic->tip == NULL) {
		ic->tip = gnumeric_create_tooltip ();
		gnumeric_position_tooltip (ic->tip, TRUE);
		gtk_widget_show_all (gtk_widget_get_toplevel (ic->tip));
	}

	g_return_if_fail (ic->tip != NULL);
	gtk_label_set_text (GTK_LABEL (ic->tip), text);
}

static gboolean
cb_move_cursor (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	ItemCursor *ic = info->user_data;
	int const w = (ic->pos.end.col - ic->pos.start.col);
	int const h = (ic->pos.end.row - ic->pos.start.row);
	GnmRange r;
	Sheet *sheet = ((SheetControl *) (pane->simple.scg))->sheet;

	r.start.col = info->col - ic->col_delta;
	if (r.start.col < 0)
		r.start.col = 0;
	else if (r.start.col >= (gnm_sheet_get_max_cols (sheet) - w))
		r.start.col = gnm_sheet_get_max_cols (sheet) - w - 1;

	r.start.row = info->row - ic->row_delta;
	if (r.start.row < 0)
		r.start.row = 0;
	else if (r.start.row >= (gnm_sheet_get_max_rows (sheet) - h))
		r.start.row = gnm_sheet_get_max_rows (sheet) - h - 1;

	item_cursor_tip_setlabel (ic, range_as_string (&ic->pos));

	r.end.col = r.start.col + w;
	r.end.row = r.start.row + h;
	scg_special_cursor_bound_set (ic->scg, &r);
	scg_make_cell_visible (ic->scg, info->col, info->row, FALSE, TRUE);
	return FALSE;
}

static void
item_cursor_handle_motion (ItemCursor *ic, GdkEvent *event,
			   GnmPaneSlideHandler slide_handler)
{
	FooCanvas *canvas = FOO_CANVAS_ITEM (ic)->canvas;

	gnm_pane_handle_motion (GNM_PANE (canvas),
		canvas, &event->motion,
		GNM_PANE_SLIDE_X | GNM_PANE_SLIDE_Y | GNM_PANE_SLIDE_AT_COLROW_BOUND,
		slide_handler, ic);
}

static gint
item_cursor_drag_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	switch (event->type) {
	case GDK_BUTTON_RELEASE:
		/* Note : see comment below, and bug 30507 */
		if ((int)event->button.button == ic->drag_button) {
			gnm_pane_slide_stop (GNM_PANE (item->canvas));
			gnm_simple_canvas_ungrab (item, event->button.time);
			item_cursor_do_drop (ic, (GdkEventButton *) event);
		}
		return TRUE;

	case GDK_BUTTON_PRESS:
		/* This kind of cursor is created and grabbed.  Then destroyed
		 * when the button is released.  If we are seeing a press it
		 * means that someone has pressed another button WHILE THE
		 * FIRST IS STILL DOWN.  Ignore this event.
		 */
		return TRUE;

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (ic, event, &cb_move_cursor);
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
cb_autofill_scroll (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	ItemCursor *ic = info->user_data;
	GnmRange r = ic->autofill_src;
	int col = info->col, row = info->row;
	int h, w;

	/* compass offsets are distances (in cells) from the edges of the
	 * selected area to the mouse cursor */
	int north_offset = r.start.row - row;
	int south_offset = row - r.end.row;
	int west_offset  = r.start.col - col;
	int east_offset  = col - r.end.col;

	/* Autofill by row or by col, NOT both. */
	if ( MAX (north_offset, south_offset) > MAX (west_offset, east_offset) ) {
		if (row < r.start.row)
			r.start.row -= ic->autofill_vsize * (int)(north_offset / ic->autofill_vsize);
		else
			r.end.row   += ic->autofill_vsize * (int)(south_offset / ic->autofill_vsize);
		if (col < r.start.col)
			col = r.start.col;
		else if (col > r.end.col)
			col = r.end.col;
	} else {
		if (col < r.start.col)
			r.start.col -= ic->autofill_hsize * (int)(west_offset / ic->autofill_hsize);
		else
			r.end.col   += ic->autofill_hsize * (int)(east_offset / ic->autofill_hsize);
		if (row < r.start.row)
			row = r.start.row;
		else if (row > r.end.row)
			row = r.end.row;
	}

	/* Check if we have moved to a new cell.  */
	if (col == ic->last_tip_pos.col && row == ic->last_tip_pos.row)
		return FALSE;
	ic->last_tip_pos.col = col;
	ic->last_tip_pos.row = row;

	scg_special_cursor_bound_set (ic->scg, &r);
	scg_make_cell_visible (ic->scg, col, row, FALSE, TRUE);

	w = range_width (&ic->autofill_src);
	h = range_height (&ic->autofill_src);
	if (ic->pos.start.col + w - 1 == ic->pos.end.col &&
	    ic->pos.start.row + h - 1 == ic->pos.end.row)
		item_cursor_tip_setlabel (ic, _("Autofill"));
	else {
		gboolean inverse_autofill =
			(ic->pos.start.col < ic->autofill_src.start.col ||
			 ic->pos.start.row < ic->autofill_src.start.row);
		gboolean default_increment =
			ic->drag_button_state & GDK_CONTROL_MASK;
		Sheet *sheet = scg_sheet (ic->scg);
		char *hint;

		if (inverse_autofill)
			hint = gnm_autofill_hint
				(sheet, default_increment,
				 ic->pos.end.col, ic->pos.end.row,
				 w, h,
				 ic->pos.start.col, ic->pos.start.row);
		else
			hint = gnm_autofill_hint
				(sheet, default_increment,
				 ic->pos.start.col, ic->pos.start.row,
				 w, h,
				 ic->pos.end.col, ic->pos.end.row);

		if (hint) {
			item_cursor_tip_setlabel (ic, hint);
			g_free (hint);
		} else
			item_cursor_tip_setlabel (ic, "");
	}

	return FALSE;
}

static gint
item_cursor_autofill_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);
	SheetControlGUI *scg = ic->scg;

	switch (event->type) {
	case GDK_BUTTON_RELEASE: {
		gboolean inverse_autofill =
			(ic->pos.start.col < ic->autofill_src.start.col ||
			 ic->pos.start.row < ic->autofill_src.start.row);
		gboolean default_increment =
			ic->drag_button_state & GDK_CONTROL_MASK;

		gnm_pane_slide_stop (GNM_PANE (item->canvas));
		gnm_simple_canvas_ungrab (item, event->button.time);

		cmd_autofill (scg_wbc (scg), scg_sheet (scg), default_increment,
			      ic->pos.start.col, ic->pos.start.row,
			      range_width (&ic->autofill_src),
			      range_height (&ic->autofill_src),
			      ic->pos.end.col, ic->pos.end.row,
			      inverse_autofill);

		scg_special_cursor_stop	(scg);
		return TRUE;
	}

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (ic, event, &cb_autofill_scroll);
		return TRUE;

	default:
		return FALSE;
	}
}

static gint
item_cursor_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	/* While editing nothing should be draggable */
	if (wbcg_is_editing (scg_wbcg (ic->scg)))
		return TRUE;

#if 0
	switch (event->type)
	{
	case GDK_BUTTON_RELEASE: printf ("release %d\n", ic->style); break;
	case GDK_BUTTON_PRESS: printf ("press %d\n", ic->style); break;
	case GDK_2BUTTON_PRESS: printf ("2press %d\n", ic->style); break;
	default :
	    break;
	}
#endif
	switch (ic->style) {
	case ITEM_CURSOR_ANTED:
		g_warning ("Animated cursors should not receive events, "
			   "the point method should preclude that");
		return FALSE;

	case ITEM_CURSOR_SELECTION:
		return item_cursor_selection_event (item, event);

	case ITEM_CURSOR_DRAG:
		return item_cursor_drag_event (item, event);

	case ITEM_CURSOR_AUTOFILL:
		return item_cursor_autofill_event (item, event);

	default:
		return FALSE;
	}
}

static void
item_cursor_set_property (GObject *obj, guint param_id,
			  GValue const *value, GParamSpec *pspec)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (obj);
	ItemCursor *ic = ITEM_CURSOR (obj);
	GdkColor color;
	char const *color_name;

	switch (param_id) {
	case ITEM_CURSOR_PROP_SHEET_CONTROL_GUI:
		ic->scg = g_value_get_object (value);
		break;
	case ITEM_CURSOR_PROP_STYLE:
		ic->style = g_value_get_int (value);
		break;
	case ITEM_CURSOR_PROP_BUTTON :
		ic->drag_button = g_value_get_int (value);
		break;
	case ITEM_CURSOR_PROP_COLOR:
		color_name = g_value_get_string (value);
		if (foo_canvas_get_color (item->canvas, color_name, &color)) {
			ic->color = color;
			ic->use_color = 1;
		}
	}
}

/*
 * ItemCursor class initialization
 */
static void
item_cursor_class_init (GObjectClass *gobject_klass)
{
	FooCanvasItemClass *item_klass = (FooCanvasItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->set_property = item_cursor_set_property;
	gobject_klass->dispose = item_cursor_dispose;
	g_object_class_install_property (gobject_klass, ITEM_CURSOR_PROP_SHEET_CONTROL_GUI,
		g_param_spec_object ("SheetControlGUI", "SheetControlGUI",
			"the sheet control gui controlling the item",
			SHEET_CONTROL_GUI_TYPE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_CURSOR_PROP_STYLE,
		g_param_spec_int ("style", "Style",
			"What type of cursor",
			0, G_MAXINT, 0,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_CURSOR_PROP_BUTTON,
		g_param_spec_int ("button", "Button",
			"what button initiated the drag",
			0, G_MAXINT, 0,
                        GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_CURSOR_PROP_COLOR,
		g_param_spec_string ("color", "Color",
			"name of the cursor's color",
			"black",
                        GSF_PARAM_STATIC |  G_PARAM_WRITABLE));

	item_klass->update      = item_cursor_update;
	item_klass->realize     = item_cursor_realize;
	item_klass->unrealize   = item_cursor_unrealize;
	item_klass->draw        = item_cursor_draw;
	item_klass->point       = item_cursor_point;
	item_klass->event       = item_cursor_event;
}

static void
item_cursor_init (ItemCursor *ic)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	ic->pos_initialized = FALSE;
	ic->pos.start.col = 0;
	ic->pos.end.col   = 0;
	ic->pos.start.row = 0;
	ic->pos.end.row   = 0;

	ic->col_delta = 0;
	ic->row_delta = 0;

	ic->tip = NULL;
	ic->last_tip_pos.col = -1;
	ic->last_tip_pos.row = -1;

	ic->style = ITEM_CURSOR_SELECTION;
	ic->gc = NULL;
	ic->state = 0;
	ic->animation_timer = -1;

	ic->visible = TRUE;
	ic->auto_fill_handle_at_top = FALSE;
	ic->auto_fill_handle_at_left = FALSE;
	ic->drag_button = -1;
}

GSF_CLASS (ItemCursor, item_cursor,
	   item_cursor_class_init, item_cursor_init,
	   FOO_TYPE_CANVAS_ITEM);
