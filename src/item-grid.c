/*
 * The Grid Gnome Canvas Item: Implements the grid and
 * spreadsheet information display.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-debug.h"
#include "color.h"
#include "dialogs.h"
#include "cursors.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "selection.h"
#include "main.h"
#include "border.h"
#include "application.h"
#include "workbook-cmd-format.h"
#include "workbook-edit.h"
#include "sheet-object.h"
#include "pattern.h"
#include "workbook-view.h"
#include "workbook.h"
#include "cell-draw.h"
#include "cellspan.h"
#include "commands.h"
#include "parse-util.h"
#include "cmd-edit.h"

#undef PAINT_DEBUG

static GnomeCanvasItemClass *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_VIEW,
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
	GnomeCanvas *canvas = item->canvas;
	GdkVisual *visual;
	GdkWindow *window;
	GtkStyle  *style;
	ItemGrid  *item_grid;
	GdkGC     *gc;

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
	item_grid->grid_gc = gc = gdk_gc_new (window);
	item_grid->fill_gc = gdk_gc_new (window);
	item_grid->gc = gdk_gc_new (window);
	item_grid->empty_gc = gdk_gc_new (window);

	/* Allocate the default colors */
	item_grid->background = gs_white;
	item_grid->grid_color = gs_light_gray;
	item_grid->default_color = gs_black;

	gdk_gc_set_foreground (gc, &item_grid->grid_color);
	gdk_gc_set_background (gc, &item_grid->background);

	gdk_gc_set_foreground (item_grid->fill_gc, &item_grid->background);
	gdk_gc_set_background (item_grid->fill_gc, &item_grid->grid_color);

	/* Find out how we need to draw the selection with the current visual */
	visual = gtk_widget_get_visual (GTK_WIDGET (canvas));

	switch (visual->type) {
	case GDK_VISUAL_STATIC_GRAY:
	case GDK_VISUAL_TRUE_COLOR:
	case GDK_VISUAL_STATIC_COLOR:
		item_grid->visual_is_paletted = 0;
		break;

	default:
		item_grid->visual_is_paletted = 1;
	}
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);

	gdk_gc_unref (item_grid->grid_gc);
	gdk_gc_unref (item_grid->fill_gc);
	gdk_gc_unref (item_grid->gc);
	gdk_gc_unref (item_grid->empty_gc);
	item_grid->grid_gc = 0;
	item_grid->fill_gc = 0;
	item_grid->gc = 0;
	item_grid->empty_gc = 0;

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

/*
 * NOTE : preview_grid_draw calls this routine also (preview-grid.c)
 */
void
item_grid_draw_border (GdkDrawable *drawable, MStyle *mstyle,
		       int x, int y, int w, int h,
		       gboolean const extended_left)
{
	MStyleBorder const * const top =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_TOP);
	MStyleBorder const * const left = extended_left ? NULL :
	    mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT);
	MStyleBorder const * const diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_DIAGONAL);
	MStyleBorder const * const rev_diag =
	    mstyle_get_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL);

	if (top != NULL && top->line_type != STYLE_BORDER_NONE)
		style_border_draw (top, MSTYLE_BORDER_TOP, drawable,
				   x, y, x + w, y, left, NULL);
	if (left != NULL && left->line_type != STYLE_BORDER_NONE)
		style_border_draw (left, MSTYLE_BORDER_LEFT, drawable,
				   x, y, x, y + h, top, NULL);

	if (diag != NULL && diag->line_type != STYLE_BORDER_NONE)
		style_border_draw (diag, MSTYLE_BORDER_DIAGONAL, drawable,
				   x, y + h, x + w, y, NULL, NULL);
	if (rev_diag != NULL && rev_diag->line_type != STYLE_BORDER_NONE)
		style_border_draw (rev_diag, MSTYLE_BORDER_REV_DIAGONAL, drawable,
				   x, y, x + w, y + h, NULL, NULL);
}

static MStyle *
item_grid_draw_background (GdkDrawable *drawable, ItemGrid *item_grid,
			   ColRowInfo const * const ci, ColRowInfo const * const ri,
			   /* Pass the row, col because the ColRowInfos may be the default. */
			   int col, int row, int x, int y,
			   gboolean const extended_left)
{
	Sheet  *sheet  = item_grid->sheet_view->sheet;
	MStyle *mstyle = sheet_style_compute (sheet, col, row);
	GdkGC  * const gc     = item_grid->empty_gc;
	int const w    = ci->size_pixels;
	int const h    = ri->size_pixels;

	gboolean const is_selected = !(sheet->cursor.edit_pos.col == col && sheet->cursor.edit_pos.row == row) &&
	    sheet_is_cell_selected (sheet, col, row);

	if (gnumeric_background_set_gc (mstyle, gc, item_grid->canvas_item.canvas, is_selected))
		/* Fill the entire cell including the right & left grid line */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);
	else if (extended_left && sheet->show_grid)
		/* Fill the entire cell including left & excluding right grid line */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y+1, w, h-1);
	else if (is_selected)
		/* Fill the entire cell excluding the right & left grid line */
		gdk_draw_rectangle (drawable, gc, TRUE, x+1, y+1, w-1, h-1);

	item_grid_draw_border (drawable, mstyle, x, y, w, h, extended_left);

	return mstyle;
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	GnomeCanvas *canvas = item->canvas;
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	Sheet *sheet = gsheet->sheet_view->sheet;
	Cell const * const edit_cell = sheet->workbook->editing_cell;
	ItemGrid *item_grid = ITEM_GRID (item);
	GdkGC *grid_gc = item_grid->grid_gc;
	int col, row;

	int x_paint, y_paint;
	int const paint_col = gnumeric_sheet_find_col (gsheet, x, &x_paint);
	int const paint_row = gnumeric_sheet_find_row (gsheet, y, &y_paint);
	int const diff_x = x - x_paint;
	int const diff_y = y - y_paint;

	/* One pixel PAST the end of the drawable */
	int const end_x  = width + diff_x;
	int const end_y  = height + diff_y;

	/* We can relax this eventually. See comment in gnumeric_sheet_find_col */
	g_return_if_fail (x >= 0);
	g_return_if_fail (y >= 0);

	/* 1. The default background */
	gdk_draw_rectangle (drawable, item_grid->fill_gc, TRUE,
			    0, 0, width, height);

	/* 2. the grids */
	if (sheet->show_grid) {
#ifdef PAINT_DEBUG
		fprintf (stderr, "paint : %s%d:", col_name(paint_col), paint_row+1);
#endif
		col = paint_col;
		x_paint = -diff_x;
		gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
		while (x_paint < end_x && col < SHEET_MAX_COLS) {
			ColRowInfo const * const ci = sheet_col_get_info (sheet, col++);
			if (ci->visible) {
				x_paint += ci->size_pixels;
				gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
			}
		}

		row = paint_row;
		y_paint = -diff_y;
		gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		while (y_paint < end_y && row < SHEET_MAX_ROWS) {
			ColRowInfo const * const ri = sheet_row_get_info (sheet, row++);
			if (ri->visible) {
				y_paint += ri->size_pixels;
				gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
			}
		}
#ifdef PAINT_DEBUG
		fprintf (stderr, "%s%d\n", col_name(col-1), row);
#endif
	}

	gdk_gc_set_function (item_grid->gc, GDK_COPY);

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y && row < SHEET_MAX_ROWS; ++row) {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (!ri->visible)
			continue;

		col = paint_col;
		/* DO NOT increment the column here, spanning cols are different */
		for (x_paint = -diff_x; x_paint < end_x && col < SHEET_MAX_COLS; ) {
			CellSpanInfo const * span;
			ColRowInfo const * ci = sheet_col_get_info (sheet, col);
			if (!ci->visible) {
				++col;
				continue;
			}

			/* Is this the start of a span?
			 * 1) There are cells allocated in the row
			 *       (indicated by ri->pos != -1)
			 * 2) Look in the rows hash table to see if
			 *    there is a span descriptor.
			 */
			if (ri->pos == -1 ||
			    NULL == (span = row_span_get (ri, col)) || edit_cell == span->cell) {
				Cell *cell = sheet_cell_get (sheet, col, row);
				MStyle *mstyle = item_grid_draw_background (
					drawable, item_grid, ci, ri,
					col, row, x_paint, y_paint, FALSE);

				if (!cell_is_blank(cell))
					cell_draw (cell, mstyle, NULL,
						   item_grid->gc, drawable, 
						   x_paint, y_paint);
				mstyle_unref (mstyle);

				/* Increment the column */
				x_paint += ci->size_pixels;
				++col;
			} else {
				Cell const *cell = span->cell;
				int const real_col = cell->pos.col;
				int const start_span_col = span->left;
				int const end_span_col = span->right;
				int real_x = -1;
				MStyle *real_style = NULL;
				gboolean const is_visible =
				    cell->row_info->visible && cell->col_info->visible;

				/* Paint the backgrounds & borders */
				for (; x_paint < end_x && col <= end_span_col ; ++col) {
					ci = sheet_col_get_info (sheet, col);
					if (ci->visible) {
						MStyle *mstyle = item_grid_draw_background (
							drawable, item_grid, ci, ri,
							col, row, x_paint, y_paint,
							col != start_span_col && is_visible);
						if (col == real_col) {
							real_style = mstyle;
							real_x = x_paint;
						} else
							mstyle_unref (mstyle);

						x_paint += ci->size_pixels;
					}
				}

				/* The real cell is not visible, we have not painted it.
				 * Compute the style, and offset
				 */
				if (real_style == NULL) {
					real_style = sheet_style_compute (sheet, real_col, ri->pos);
					real_x = x_paint +
					    sheet_col_get_distance_pixels (cell->base.sheet,
									   col, cell->pos.col);
				}

				if (is_visible)
					cell_draw (cell, real_style, span,
						   item_grid->gc, drawable, real_x, y_paint);
				mstyle_unref (real_style);
			}
		}
		y_paint += ri->size_pixels;
	}
}

static double
item_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
item_grid_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_grid_translate %g, %g\n", dx, dy);
}

static void
context_destroy_menu (GtkWidget *widget)
{
	gtk_widget_destroy (gtk_widget_get_toplevel (widget));
}

static void
context_cut_cmd (GtkWidget *widget, Sheet *sheet)
{
	sheet_selection_cut (workbook_command_context_gui (sheet->workbook), sheet);
	context_destroy_menu (widget);
}

static void
context_copy_cmd (GtkWidget *widget, Sheet *sheet)
{
	sheet_selection_copy (workbook_command_context_gui (sheet->workbook), sheet);
	context_destroy_menu (widget);
}

static void
context_paste_cmd (GtkWidget *widget, Sheet *sheet)
{
	Workbook *wb = sheet->workbook;
	cmd_paste_to_selection (workbook_command_context_gui (wb),
				sheet, PASTE_DEFAULT);
	context_destroy_menu (widget);
}

static void
context_paste_special_cmd (GtkWidget *widget, Sheet *sheet)
{
	Workbook *wb = sheet->workbook;
	int flags = dialog_paste_special (wb);
	if (flags != 0)
		cmd_paste_to_selection (workbook_command_context_gui (wb),
					sheet, flags);
	context_destroy_menu (widget);
}

static void
context_insert_cmd (GtkWidget *widget, Sheet *sheet)
{
	dialog_insert_cells (sheet->workbook, sheet);
	context_destroy_menu (widget);
}

static void
context_delete_cmd (GtkWidget *widget, Sheet *sheet)
{
	dialog_delete_cells (sheet->workbook, sheet);
	context_destroy_menu (widget);
}

static void
context_clear_cmd (GtkWidget *widget, Sheet *sheet)
{
	cmd_clear_selection (workbook_command_context_gui (sheet->workbook), sheet, CLEAR_VALUES);
	context_destroy_menu (widget);
}

static void
context_cell_format_cmd (GtkWidget *widget, Sheet *sheet)
{
	dialog_cell_format (sheet->workbook, sheet);
	context_destroy_menu (widget);
}

static void
context_column_width (GtkWidget *widget, Sheet *sheet)
{
	sheet_dialog_set_column_width (widget, sheet->workbook);
	context_destroy_menu (widget);
}

static void
context_row_height (GtkWidget *widget, Sheet *sheet)
{
	sheet_dialog_set_row_height (widget, sheet->workbook);
	context_destroy_menu (widget);
}

static void
context_col_hide (GtkWidget *widget, Sheet *sheet)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (sheet->workbook),
				      sheet, TRUE, FALSE);
	context_destroy_menu (widget);
}
static void
context_col_unhide (GtkWidget *widget, Sheet *sheet)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (sheet->workbook),
				      sheet, TRUE, TRUE);
	context_destroy_menu (widget);
}
static void
context_row_hide (GtkWidget *widget, Sheet *sheet)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (sheet->workbook),
				      sheet, FALSE, FALSE);
	context_destroy_menu (widget);
}
static void
context_row_unhide (GtkWidget *widget, Sheet *sheet)
{
	cmd_hide_selection_rows_cols (workbook_command_context_gui (sheet->workbook),
				      sheet, FALSE, TRUE);
	context_destroy_menu (widget);
}

typedef enum {
	IG_ALWAYS,
	IG_SEPARATOR,
	IG_PASTE_SPECIAL,
	IG_ROW	   = 0x8,
	IG_COLUMN  = 0x10,
	IG_DISABLE = 0x20
} popup_types;

static struct {
	char const * const name;
	char const * const pixmap;
	void         (*fn)(GtkWidget *widget, Sheet *item_grid);
	popup_types  const type;
} item_grid_context_menu [] = {
	{ N_("Cu_t"),           GNOME_STOCK_PIXMAP_CUT,
	    &context_cut_cmd,           IG_ALWAYS },
	{ N_("_Copy"),          GNOME_STOCK_PIXMAP_COPY,
	    &context_copy_cmd,          IG_ALWAYS },
	{ N_("_Paste"),         GNOME_STOCK_PIXMAP_PASTE,
	    &context_paste_cmd,         IG_ALWAYS },
	{ N_("Paste _Special"),	NULL,
	    &context_paste_special_cmd, IG_PASTE_SPECIAL },

	{ "", NULL, NULL, IG_SEPARATOR },

	{ N_("_Insert..."),	NULL,
	    &context_insert_cmd,        IG_ALWAYS },
	{ N_("_Delete..."),	NULL,
	    &context_delete_cmd,        IG_ALWAYS },
	{ N_("Clear Co_ntents"),NULL,
	    &context_clear_cmd,         IG_ALWAYS },

	{ "", NULL, NULL, IG_SEPARATOR      },

	{ N_("_Format Cells..."),GNOME_STOCK_PIXMAP_PREFERENCES,
	    &context_cell_format_cmd,   IG_ALWAYS },

	/* Column specific functions */
	{ N_("Column _Width..."),NULL,
	    &context_column_width, IG_COLUMN },
	{ N_("_Hide"),		 NULL,
	    &context_col_hide,   IG_COLUMN },
	{ N_("_Unhide"),	 NULL,
	    &context_col_unhide,   IG_COLUMN },

	/* Row specific functions (Note some of the labels are duplicated */
	{ N_("_Row Height..."),	 NULL,
	    &context_row_height,   IG_ROW },
	{ N_("_Hide"),		 NULL,
	    &context_row_hide,   IG_ROW },
	{ N_("_Unhide"),	 NULL,
	    &context_row_unhide,   IG_ROW },
	{ NULL, NULL, NULL, 0 }
};

static GtkWidget *
create_popup_menu (Sheet *sheet,
		   gboolean const include_paste_special,
		   gboolean const is_col,
		   gboolean const is_row)
{
	GtkWidget *menu, *item;
	int i;

	menu = gtk_menu_new ();
	item = NULL;

	for (i = 0; item_grid_context_menu [i].name; i++) {
		popup_types const type = item_grid_context_menu [i].type;
		char const * const pix_name = item_grid_context_menu [i].pixmap;

		if (type == IG_ROW && !is_row)
		    continue;
		if (type == IG_COLUMN && !is_col)
		    continue;

		switch (item_grid_context_menu [i].type) {
		case IG_SEPARATOR:
			item = gtk_menu_item_new ();
			break;

		/* Desesitize later */
		case IG_PASTE_SPECIAL :
		case IG_ROW :	case IG_COLUMN :
		case IG_ALWAYS:
		{
			/* ICK ! There should be a utility routine for this in gnome or gtk */
			GtkWidget *label;
			guint label_accel;
			label = gtk_accel_label_new ("");
			label_accel = gtk_label_parse_uline (
				GTK_LABEL (label),
				_(item_grid_context_menu [i].name));

			gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
			gtk_widget_show (label);

			item = gtk_pixmap_menu_item_new	();
			gtk_container_add (GTK_CONTAINER (item), label);

			if (label_accel != GDK_VoidSymbol) {
				gtk_widget_add_accelerator (
					item,
					"activate_item",
					gtk_menu_ensure_uline_accel_group (GTK_MENU (menu)),
					label_accel, 0,
					GTK_ACCEL_LOCKED);
			}
			break;
		}

		default:
			g_warning ("Never reached");
		}

		if ((type == IG_PASTE_SPECIAL && !include_paste_special) ||
		    item_grid_context_menu [i].fn == NULL)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		if (pix_name != NULL) {
			GtkWidget *pixmap =
				gnome_stock_pixmap_widget (GTK_WIDGET (item),
							   pix_name);
			gtk_widget_show (pixmap);
			gtk_pixmap_menu_item_set_pixmap (
				GTK_PIXMAP_MENU_ITEM (item),
				pixmap);
		}
		if (item_grid_context_menu [i].fn)
			gtk_signal_connect (
				GTK_OBJECT (item), "activate",
				GTK_SIGNAL_FUNC (item_grid_context_menu [i].fn),
				sheet);

		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);
	}

	return menu;
}

void
item_grid_popup_menu (Sheet *sheet, GdkEvent *event, int col, int row,
		       gboolean const is_col,
		       gboolean const is_row)
{
	GtkWidget *menu;

	/*
	 * Paste special does not apply to cut cells.  Enable
	 * when there is nothing in the local clipboard, or when
	 * the clipboard has the results of a copy.
	 */
	gboolean const show_paste_special =
	    application_clipboard_is_empty () ||
	    (application_clipboard_contents_get () != NULL);

	menu = create_popup_menu (sheet, show_paste_special,
				  is_col, is_row);

	gnumeric_popup_menu (GTK_MENU (menu), (GdkEventButton *) event);
}

static int
item_grid_button_1 (Sheet *sheet, GdkEventButton *event,
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
	if (sheet->new_object != NULL)
		return sheet_object_begin_creation (gsheet, event);

	if (sheet->current_object != NULL)
		sheet_mode_edit	(sheet);

	/*
	 * If we were already selecting a range of cells for a formula,
	 * reset the location to a new place.
	 */
	if (gsheet->selecting_cell) {
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnumeric_sheet_selection_cursor_place (gsheet, col, row);
		gnumeric_sheet_selection_cursor_base (gsheet, col, row);
		return 1;
	}

	/*
	 * If the user is editing a formula (gnumeric_sheet_can_select_expr_range)
	 * then we enable the dynamic cell selection mode.
	 */
	if (gnumeric_sheet_can_select_expr_range (gsheet)){
		gnumeric_sheet_start_cell_selection (gsheet, col, row);
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnumeric_sheet_selection_cursor_place (gsheet, col, row);
		return 1;
	}

	/* While a guru is up ignore clicks */
	if (workbook_edit_has_guru (sheet->workbook))
		return 1;

	/* This was a regular click on a cell on the spreadsheet.  Select it. */
	workbook_finish_editing (sheet->workbook, TRUE);

	if (!(event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)))
		sheet_selection_reset_only (sheet);

	item_grid->selecting = ITEM_GRID_SELECTING_CELL_RANGE;

	if ((event->state & GDK_SHIFT_MASK) && sheet->selections)
		sheet_selection_extend_to (sheet, col, row);
	else {
		sheet_selection_add (sheet, col, row);
		sheet_make_cell_visible (sheet, col, row);
	}
	sheet_update (sheet);

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
cb_extend_cell_range (SheetView *sheet_view, int col, int row, gpointer ignored)
{
	sheet_selection_extend_to (sheet_view->sheet, col, row);
	return TRUE;
}
static gboolean
cb_extend_expr_range (SheetView *sheet_view, int col, int row, gpointer ignored)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet_view->sheet_view);
	gnumeric_sheet_selection_extend (gsheet, col, row);
	return TRUE;
}

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	Sheet *sheet = item_grid->sheet_view->sheet;
	int col, row, x, y, left, top;
	int width, height;

	switch (event->type){
	case GDK_ENTER_NOTIFY: {
		int cursor;

		if (sheet->new_object != NULL)
			cursor = GNUMERIC_CURSOR_THIN_CROSS;
		else if (sheet->current_object != NULL)
			cursor = GNUMERIC_CURSOR_ARROW;
		else
			cursor = GNUMERIC_CURSOR_FAT_CROSS;

		cursor_set_widget (canvas, cursor);
		return TRUE;
	}

	case GDK_BUTTON_RELEASE:
		switch (event->button.button) {
		case 1 :
			sheet_view_stop_sliding (item_grid->sheet_view);

			if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
				sheet_make_cell_visible (sheet, sheet->cursor.edit_pos.col,
							 sheet->cursor.edit_pos.row);

			workbook_set_region_status (sheet->workbook,
						    cell_pos_name (&sheet->cursor.edit_pos));

			item_grid->selecting = ITEM_GRID_NO_SELECTION;
			gnome_canvas_item_ungrab (item, event->button.time);
			return 1;

		case 4 : /* Roll Up or Left */
		case 5 : /* Roll Down or Right */
			if ((event->button.state & GDK_MOD1_MASK)) {
				col = gsheet->col.last_full - gsheet->col.first;
				if (event->button.button == 4)
					col = MAX (gsheet->col.first - col, 0);
				else
					col = MIN (gsheet->col.last_full , SHEET_MAX_COLS-1);
				gnumeric_sheet_set_left_col (gsheet, col);
			} else {
				row = gsheet->row.last_full - gsheet->row.first;
				if (event->button.button == 4)
					row = MAX (gsheet->row.first - row, 0);
				else
					row = MIN (gsheet->row.last_full , SHEET_MAX_ROWS-1);
				gnumeric_sheet_set_top_row (gsheet, row);
			}
			return FALSE;

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
			SheetViewSlideHandler slide_handler = NULL;

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

			if (sheet_view_start_sliding (item_grid->sheet_view,
						      slide_handler, NULL,
						      col, row, dx, dy))

				return TRUE;
		}

		sheet_view_stop_sliding (item_grid->sheet_view);

		if (item_grid->selecting == ITEM_GRID_NO_SELECTION)
			return 1;

		if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE){
			gnumeric_sheet_selection_extend (gsheet, col, row);
			return 1;
		}

		if (event->motion.x < 0)
			event->motion.x = 0;
		if (event->motion.y < 0)
			event->motion.y = 0;

		sheet_selection_extend_to (sheet, col, row);
		return TRUE;

	case GDK_BUTTON_PRESS:
		sheet_view_stop_sliding (item_grid->sheet_view);

		gnome_canvas_w2c (canvas, event->button.x, event->button.y, &x, &y);
		col = gnumeric_sheet_find_col (gsheet, x, NULL);
		row = gnumeric_sheet_find_row (gsheet, y, NULL);

		/* While a guru is up ignore clicks */
		if (workbook_edit_has_guru (sheet->workbook) && event->button.button != 1)
			return TRUE;

		switch (event->button.button){
		case 1:
			return item_grid_button_1 (sheet, &event->button,
						   item_grid, col, row, x, y);

		case 2:
			g_warning ("This is here just for demo purposes");
			drag_start (GTK_WIDGET (item->canvas), event, sheet);
			return TRUE;

		case 3:
			item_grid_popup_menu (sheet,
					      event, col, row,
					      FALSE, FALSE);
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
	case ARG_SHEET_VIEW:
		item_grid->sheet_view = GTK_VALUE_POINTER (*arg);
		break;
	}
}

/*
 * ItemGrid class initialization
 */
static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::SheetView", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_VIEW);

	object_class->set_arg = item_grid_set_arg;
	object_class->destroy = item_grid_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_grid_update;
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->translate   = item_grid_translate;
	item_class->event       = item_grid_event;
}

GtkType
item_grid_get_type (void)
{
	static GtkType item_grid_type = 0;

	if (!item_grid_type) {
		GtkTypeInfo item_grid_info = {
			"ItemGrid",
			sizeof (ItemGrid),
			sizeof (ItemGridClass),
			(GtkClassInitFunc) item_grid_class_init,
			(GtkObjectInitFunc) item_grid_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_grid_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_grid_info);
	}

	return item_grid_type;
}
