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
#include "pattern.h"

static GnomeCanvasItem *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_VIEW,
};

enum {
	SELECTING_CELL_RANGE
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
	style->bg[GTK_STATE_NORMAL] = gs_white;
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

	switch (visual->type){
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
 * item_grid_find_col: return the column where x belongs to
 */
int
item_grid_find_col (ItemGrid *item_grid, int x, int *col_origin)
{
	int col   = item_grid->left_col;
	int pixel = item_grid->left_offset;

	/*
	 * FIXME:  This should probably take negative numbers
	 * as well to provide sliding when moving backwards (look
	 * in item-cursor.c
	 */
	g_return_val_if_fail (x >= 0, 0);

	do {
		ColRowInfo *ci = sheet_col_get_info (item_grid->sheet, col);

		if (x >= pixel && x <= pixel + ci->pixels){
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		col++;
		pixel += ci->pixels;
	} while (col < SHEET_MAX_COLS);

	return SHEET_MAX_COLS-1;
}

/*
 * item_grid_find_row: return the row where y belongs to
 */
int
item_grid_find_row (ItemGrid *item_grid, int y, int *row_origin)
{
	int row   = item_grid->top_row;
	int pixel = item_grid->top_offset;

	/*
	 * FIXME:  This should probably take negative numbers
	 * as well to provide sliding when moving backwards (look
	 * in item-cursor.c
	 */
	g_return_val_if_fail (y >= 0, 0);

	do {
		ColRowInfo *ri = sheet_row_get_info (item_grid->sheet, row);

		if (y >= pixel && y <= pixel + ri->pixels){
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		row++;
		pixel += ri->pixels;
	} while (row < SHEET_MAX_ROWS);
	return SHEET_MAX_ROWS-1;
}

/*
 * Sets the gc appropiately for inverting the color of a region
 * in the ItemGrid.
 *
 * We are mainly interested in getting accurrated black/white
 * inversion.
 *
 * On palleted displays, we play the trick of XORing the value
 * (this will render other colors sometimes randomly, depending on the
 * pallette contents).  On non-palleted displays, we can use the
 * GDK_INVERT operation to invert the pixel value.
 */
static void
item_grid_invert_gc (ItemGrid *item_grid)
{
	GdkGC *gc;

	gc = item_grid->gc;
	gdk_gc_set_clip_rectangle (gc, NULL);

	if (item_grid->visual_is_paletted){
		gdk_gc_set_function (gc, GDK_XOR);
		gdk_gc_set_foreground (gc, &gs_white);
	} else {
		gdk_gc_set_function (gc, GDK_INVERT);
		gdk_gc_set_foreground (gc, &gs_black);
	}
}

/*
 * TODO TODO TODO
 * Correctly support extended cells. Multi-line, or extending to the left
 * are incorrect currently.
 */
static void
item_grid_draw_border (GdkDrawable *drawable, MStyle *mstyle,
		       int x, int y, int w, int h,
		       gboolean const extended_right,
		       gboolean const extended_left)
{
	if (mstyle_is_element_set (mstyle, MSTYLE_BORDER_TOP))
		style_border_draw (drawable,
				   mstyle_get_border (mstyle, MSTYLE_BORDER_TOP),
				   x, y, x + w, y);
	if (!extended_left &&
	    mstyle_is_element_set (mstyle, MSTYLE_BORDER_LEFT))
		style_border_draw (drawable, 
				   mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT),
				   x, y, x, y + h);
	if (mstyle_is_element_set (mstyle, MSTYLE_BORDER_BOTTOM))
		style_border_draw (drawable, 
				   mstyle_get_border (mstyle, MSTYLE_BORDER_BOTTOM),
				   x, y + h, x + w, y + h);
	if (!extended_right &&
	    mstyle_is_element_set (mstyle, MSTYLE_BORDER_RIGHT))
		style_border_draw (drawable, 
				   mstyle_get_border (mstyle, MSTYLE_BORDER_RIGHT),
				   x + w, y, x + w, y + h);
	
	if (mstyle_is_element_set (mstyle, MSTYLE_BORDER_DIAGONAL))
		style_border_draw (drawable,
				   mstyle_get_border (mstyle, MSTYLE_BORDER_DIAGONAL),
				   x, y + h, x + w, y);
	if (mstyle_is_element_set (mstyle, MSTYLE_BORDER_REV_DIAGONAL))
		style_border_draw (drawable,
				   mstyle_get_border (mstyle, MSTYLE_BORDER_REV_DIAGONAL),
				   x, y, x + w, y + h);
}

/*
 * Draw a cell.  It gets pixel level coordinates
 *
 * Returns the number of columns used by the cell.
 */
static int
item_grid_draw_cell (GdkDrawable *drawable, ItemGrid *item_grid, Cell *cell, int x1, int y1)
{
	GdkGC      *gc     = item_grid->gc;
	GdkColor   *col;
	int         count;
	int         w, h;
	MStyle     *mstyle;
	
	mstyle = sheet_style_compute (item_grid->sheet, cell->col->pos,
				      cell->row->pos);

	/* setup foreground */
	gdk_gc_set_foreground (gc, &item_grid->default_color);
	if (cell->render_color)
		gdk_gc_set_foreground (gc, &cell->render_color->color);
	else if (mstyle_is_element_set (mstyle, MSTYLE_COLOR_FORE)) {
		col = &mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color;
		gdk_gc_set_foreground (gc, col);
	}

	if (mstyle_is_element_set (mstyle, MSTYLE_COLOR_BACK)) {
		col = &mstyle_get_color (mstyle, MSTYLE_COLOR_BACK)->color;
		gdk_gc_set_background (gc, col);
	} else
		gdk_gc_set_background (gc, &item_grid->background);

	w = cell->col->pixels;
	h = cell->row->pixels;

	/* Draw cell contents BEFORE border */
	count = cell_draw (cell, item_grid->sheet_view, gc, drawable, x1, y1);

	item_grid_draw_border (drawable, mstyle, x1, y1, w, h, count > 1, FALSE);

	mstyle_unref (mstyle);

	return count;
}

static void
item_grid_paint_empty_cell (GdkDrawable *drawable, ItemGrid *item_grid,
			    ColRowInfo *ci, ColRowInfo *ri, int col, int row,
			    int x, int y,
			    int const span_count)
{
	MStyle *mstyle;
	GdkGC  *gc = item_grid->empty_gc;
	
	mstyle = sheet_style_compute (item_grid->sheet, col, row);

	if (gnumeric_background_set_gc (mstyle, gc, item_grid->canvas_item.canvas))
		/* Ignore margins. Fill the entire cell (including the right
		 * hand divider) */
		gdk_draw_rectangle (drawable, gc, TRUE,
				    x, y, ci->pixels+1, ri->pixels+1);

	item_grid_draw_border (drawable, mstyle, x, y, ci->pixels, ri->pixels,
			       span_count > 1,
			       span_count > 0);

	mstyle_unref (mstyle);
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet   = item_grid->sheet;
	GdkGC *grid_gc = item_grid->grid_gc;
	Cell  *cell;
	int end_x, end_y;
	int paint_col, paint_row, max_paint_col, max_paint_row;
	int col, row;
	int x_paint, y_paint, real_x;
	int diff_x, diff_y;
	int span_count = 0;

	if (x < 0){
		g_warning ("x < 0\n");
		return;
	}

	if (y < 0){
		g_warning ("y < 0\n");
		return;
	}

	max_paint_col = max_paint_row = 0;
	paint_col = item_grid_find_col (item_grid, x, &x_paint);
	paint_row = item_grid_find_row (item_grid, y, &y_paint);

	col = paint_col;

	diff_x = x - x_paint;
	end_x = width + diff_x;
	diff_y = y - y_paint;
	end_y = height + diff_y;

	/* 1. The default background */
	gdk_draw_rectangle (drawable, item_grid->fill_gc, TRUE,
			    0, 0, width, height);

	/* 2. the grids */
	for (x_paint = -diff_x; x_paint < end_x && col < SHEET_MAX_COLS; col++){
		ColRowInfo *ci;

		ci = sheet_col_get_info (sheet, col);
		g_assert (ci->pixels != 0);

		gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);

		x_paint += ci->pixels;
		max_paint_col = col;
	}

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y && row < SHEET_MAX_ROWS; row++){
		ColRowInfo *ri;

		ri = sheet_row_get_info (sheet, row);
		gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		y_paint += ri->pixels;
		max_paint_row = row;
	}

	gdk_gc_set_function (item_grid->gc, GDK_COPY);

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y && row < SHEET_MAX_ROWS; row++){
		ColRowInfo *ri;

		ri = sheet_row_get_info (sheet, row);
		col = paint_col;

		for (x_paint = -diff_x; x_paint < end_x && col < SHEET_MAX_COLS; ++col) {
			ColRowInfo *ci;
			
			ci = sheet_col_get_info (sheet, col);

			cell = sheet_cell_get (sheet, col, row);

			/* If the cell does not exist paint it as an empty cell */
			if (cell == NULL){
				item_grid_paint_empty_cell (
					drawable, item_grid, ci, ri,
					col, row, x_paint, y_paint,
					--span_count);
			} else {
				span_count = item_grid_draw_cell (
					drawable, item_grid, cell,
					x_paint, y_paint);
			}

			if (cell_is_blank (cell) && (ri->pos != -1)){
				/*
				 * If there was no cell, and the row has any cell allocated
				 * (indicated by ri->pos != -1)
				 */

				real_x = x_paint;
				cell = row_cell_get_displayed_at (ri, col);

				/*
				 * We found the cell that paints over this
				 * cell, adjust x to point to the beginning
				 * of that cell.
				 */
				if (cell){
					int i, count, end_col;

					/*
					 * Either adjust the left part
					 */
					for (i = cell->col->pos; i < col; i++){
						ColRowInfo *tci;

						tci = sheet_col_get_info (sheet, i);
						real_x -= tci->pixels;
					}

					/*
					 * Or adjust the right part
					 */
					for (i = col; i < cell->col->pos; i++){
						ColRowInfo *tci;

						tci = sheet_col_get_info (
							sheet, i);
						real_x += tci->pixels;
					}

					/*
					 * Draw the cell
					 */
					count = item_grid_draw_cell (
						drawable, item_grid, cell,
						real_x, y_paint);
					/*
					 * Optimization: advance over every
					 * cell we already painted on
					 *
					 * FIXME : This will lose borders.
					 */
					end_col = cell->col->pos + count;

					for (i = col+1; i < end_col; i++, col++){
						ColRowInfo *tci;

						tci = sheet_col_get_info (
							sheet, i);

						x_paint += tci->pixels;
					}
				} /* if cell */
			}
			x_paint += ci->pixels;
		}
		y_paint += ri->pixels;
	}

	/* Now invert any selected cells */
	item_grid_invert_gc (item_grid);

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y && row < SHEET_MAX_ROWS; row++){
		ColRowInfo *ri, *ci;

		ri = sheet_row_get_info (sheet, row);
		col = paint_col;

		for (x_paint = -diff_x;
		     x_paint < end_x && col < SHEET_MAX_COLS;
		     ++col, x_paint += ci->pixels){
			ci = sheet_col_get_info (sheet, col);

			if (sheet->cursor_col == col && sheet->cursor_row == row)
				continue;

			if (!sheet_selection_is_cell_selected (sheet, col, row))
				continue;

			gdk_draw_rectangle (drawable, item_grid->gc, TRUE,
					    x_paint+1, y_paint+1,
					    ci->pixels-1, ri->pixels-1);
		}
		y_paint += ri->pixels;
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
	sheet_selection_cut (sheet);
	context_destroy_menu (widget);
}

static void
context_copy_cmd (GtkWidget *widget, Sheet *sheet)
{
	sheet_selection_copy (sheet);
	context_destroy_menu (widget);
}

static void
context_paste_cmd (GtkWidget *widget, Sheet *sheet)
{
	sheet_selection_paste (sheet,
			       sheet->cursor_col,
			       sheet->cursor_row,
			       PASTE_DEFAULT,
			       GDK_CURRENT_TIME);
	context_destroy_menu (widget);
}

static void
context_paste_special_cmd (GtkWidget *widget, Sheet *sheet)
{
	int flags;

	flags = dialog_paste_special (sheet->workbook);
	if (flags != 0)
		sheet_selection_paste (sheet,
				       sheet->cursor_col,
				       sheet->cursor_row,
				       flags,
				       GDK_CURRENT_TIME);
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
	sheet_selection_clear_content (sheet);
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
	workbook_cmd_format_column_width (widget, sheet->workbook);
	context_destroy_menu (widget);
}

static void
context_row_height (GtkWidget *widget, Sheet *sheet)
{
	workbook_cmd_format_row_height (widget, sheet->workbook);
	context_destroy_menu (widget);
}

typedef enum {
	IG_ALWAYS,
	IG_SEPARATOR,
	IG_PASTE,
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
	    &context_paste_cmd,         IG_PASTE },
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
	    NULL,   IG_COLUMN },
	{ N_("_Unhide"),	 NULL,
	    NULL,   IG_COLUMN },

	/* Row specific functions (Note some of the labels are duplicated */
	{ N_("_Row Height..."),	 NULL,
	    &context_row_height,   IG_ROW },
	{ N_("_Hide"),		 NULL,
	    NULL,   IG_ROW },
	{ N_("_Unhide"),	 NULL,
	    NULL,   IG_ROW },
	{ NULL, NULL, NULL, 0 }
};

static GtkWidget *
create_popup_menu (Sheet *sheet,
		   gboolean const include_paste,
		   gboolean const include_paste_special,
		   gboolean const is_col,
		   gboolean const is_row)
{
	GtkWidget *menu, *item;
	int i;

	menu = gtk_menu_new ();
	item = NULL;

	for (i = 0; item_grid_context_menu [i].name; ++i) {
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
		case IG_PASTE : case IG_PASTE_SPECIAL :
		case IG_ROW :	case IG_COLUMN :
		case IG_ALWAYS:
		{
			/* ICK ! There should be a utility routine for this in gnome or gtk */
			GtkWidget *label;
			guint label_accel;
			label = gtk_accel_label_new ("");
			label_accel = gtk_label_parse_uline (
				GTK_LABEL (label),
				item_grid_context_menu [i].name);

			gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
			gtk_widget_show (label);

			item = gtk_pixmap_menu_item_new	();
			gtk_container_add (GTK_CONTAINER (item), label);

			if (label_accel != GDK_VoidSymbol) {
			    if (GTK_IS_MENU (menu))
				    gtk_widget_add_accelerator (item,
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

		if ((type == IG_PASTE && !include_paste) ||
		    (type == IG_PASTE_SPECIAL && !include_paste_special) ||
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

	/* We can paste if there is something in the clipboard */
	gboolean const show_paste = !application_clipboard_is_empty ();
	/* Paste special only applies to copied cells not cut */
	gboolean const show_paste_special = show_paste &&
	    (application_clipboard_contents_get () != NULL);

	menu = create_popup_menu (sheet, show_paste, show_paste_special,
				  is_col, is_row);

	gnumeric_popup_menu (GTK_MENU (menu), (GdkEventButton *) event);
}

static int
item_grid_button_1 (Sheet *sheet, GdkEvent *event, ItemGrid *item_grid, int col, int row, int x, int y)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);
	GnomeCanvas   *canvas = item->canvas;
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);

	/*
	 * Range check first
	 */
	if (col >= SHEET_MAX_COLS)
		return 1;
	if (row >= SHEET_MAX_ROWS)
		return 1;

	/*
	 * If we were already selecting a range of cells for a formula,
	 * reset the location to a new place.
	 */
	if (gsheet->selecting_cell){
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnumeric_sheet_selection_cursor_place (gsheet, col, row);
		gnumeric_sheet_selection_cursor_base (gsheet, col, row);
		return 1;
	}
	
	/*
	 * If the user is editing a formula (gnumeric_sheet_can_move_cursor)
	 * then we enable the dynamic cell selection mode.
	 */
	if (gnumeric_sheet_can_move_cursor (gsheet)){
		gnumeric_sheet_start_cell_selection (gsheet, col, row);
		item_grid->selecting = ITEM_GRID_SELECTING_FORMULA_RANGE;
		gnumeric_sheet_selection_cursor_place (gsheet, col, row);
		return 1;
	}

	/*
	 * This was a regular click on a cell on the spreadsheet.  Select it.
	 */
	sheet_accept_pending_input (sheet);

	if (!(event->button.state & GDK_SHIFT_MASK)) {
		sheet_make_cell_visible (sheet, col, row);
		/* Handle the selection localy */
		sheet_cursor_move (sheet, col, row, FALSE, FALSE);
	}

	if (!(event->button.state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)))
		sheet_selection_reset_only (sheet);

	item_grid->selecting = ITEM_GRID_SELECTING_CELL_RANGE;

	if ((event->button.state & GDK_SHIFT_MASK) && sheet->selections)
		sheet_selection_extend_to (sheet, col, row);
	else
		sheet_selection_append (sheet, col, row);

	gnome_canvas_item_grab (item,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL,
				event->button.time);
	return 1;
}

static void
item_grid_stop_sliding (ItemGrid *item_grid)
{
	if (item_grid->sliding == -1)
		return;

	gtk_timeout_remove (item_grid->sliding);
	item_grid->sliding = -1;
}

static gint
item_grid_sliding_callback (gpointer data)
{
	GnomeCanvasItem *item = data;
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	int change = 0;
	int col, row;

	col = item_grid->sliding_col;
	row = item_grid->sliding_row;

	if (item_grid->sliding_x < 0){
		if (gsheet->top_col){
			change = 1;
			if (item_grid->sliding_x >= -8)
				col = 1;
			else if (item_grid->sliding_x >= -16)
				col = 10;
			else
				col = 50;
			col = gsheet->top_col - col;
			if (col < 0)
				col = 0;
		} else
			col = 0;
	}

	if (item_grid->sliding_x > 0){
		if (gsheet->last_full_col < SHEET_MAX_COLS-1){
			change = 1;
			if (item_grid->sliding_x <= 7)
				col = 1;
			else if (item_grid->sliding_x <= 14)
				col = 10;
			else
				col = 50;
			col = gsheet->last_visible_col + col;
			if (col >= SHEET_MAX_COLS)
				col = SHEET_MAX_COLS-1;
		} else
			col = SHEET_MAX_COLS-1;
	}

	if (item_grid->sliding_y < 0){
		if (gsheet->top_row){
			change = 1;
			if (item_grid->sliding_y >= -8)
				row = 1;
			else if (item_grid->sliding_y >= -16)
				row = 25;
			else if (item_grid->sliding_y >= -64)
				row = 250;
			else
				row = 1000;
			row = gsheet->top_row - row;
			if (row < 0)
				row = 0;
		} else
			row = 0;
	}
	if (item_grid->sliding_y > 0){
		if (gsheet->last_full_row < SHEET_MAX_ROWS-1){
			change = 1;
			if (item_grid->sliding_y <= 8)
				row = 1;
			else if (item_grid->sliding_y <= 16)
				row = 25;
			else if (item_grid->sliding_y <= 64)
				row = 250;
			else
				row = 1000;
			row = gsheet->last_visible_row + row;
			if (row >= SHEET_MAX_ROWS)
				row = SHEET_MAX_ROWS-1;
		} else
			row = SHEET_MAX_ROWS-1;
	}

	if (!change){
		item_grid_stop_sliding (item_grid);
		return TRUE;
	}

	if (item_grid->selecting == ITEM_GRID_SELECTING_CELL_RANGE)
		sheet_selection_extend_to (item_grid->sheet, col, row);
	else if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
		gnumeric_sheet_selection_extend (gsheet, col, row);

	gnumeric_sheet_make_cell_visible (gsheet, col, row);

	return TRUE;
}

static void
item_grid_start_sliding (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);

	item_grid->sliding = gtk_timeout_add (
		100, item_grid_sliding_callback, item);
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
#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	Sheet *sheet = item_grid->sheet;
	int col, row, x, y;
	int width, height;

	switch (event->type){
	case GDK_ENTER_NOTIFY: {
		int cursor;

		if (sheet->mode == SHEET_MODE_SHEET)
			cursor = GNUMERIC_CURSOR_FAT_CROSS;
		else
			cursor = GNUMERIC_CURSOR_ARROW;

		cursor_set_widget (canvas, cursor);
		return TRUE;
	}

	case GDK_BUTTON_RELEASE:
		item_grid_stop_sliding (item_grid);

		if (event->button.button == 1){
			if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE)
				sheet_make_cell_visible (sheet, sheet->cursor_col, sheet->cursor_row);

			item_grid->selecting = ITEM_GRID_NO_SELECTION;
			gnome_canvas_item_ungrab (item, event->button.time);
			return 1;
		}
		break;

	case GDK_MOTION_NOTIFY:
		convert (canvas, event->motion.x, event->motion.y, &x, &y);

		gnome_canvas_get_scroll_offsets (canvas, &col, &row);

		width = GTK_WIDGET (canvas)->allocation.width;
		height = GTK_WIDGET (canvas)->allocation.height;
		if (x < col || y < row || x >= col + width || y >= row + height){
			int dx = 0, dy = 0;

			if (item_grid->selecting == ITEM_GRID_NO_SELECTION)
				return 1;

			if (x < col)
				dx = x - col;
			else if (x >= col + width)
				dx = x - width - col;

			if (y < row)
				dy = y - row;
			else if (y >= row + height)
				dy = y - height - row;

			if ((!dx || (dx < 0 && !gsheet->top_col) ||
			     (dx >= 0 && gsheet->last_full_col == SHEET_MAX_COLS-1)) &&
			    (!dy || (dy < 0 && !gsheet->top_row) ||
			     (dy >= 0 && gsheet->last_full_row == SHEET_MAX_ROWS-1))){
				item_grid_stop_sliding (item_grid);
				return 1;
			}

			item_grid->sliding_x = dx/5;
			item_grid->sliding_y = dy/5;
			if (!dx){
				item_grid->sliding_col = item_grid_find_col (item_grid, x, NULL);
				if (item_grid->sliding_col >= SHEET_MAX_COLS)
					item_grid->sliding_col = SHEET_MAX_COLS-1;
			}
			if (!dy){
				item_grid->sliding_row = item_grid_find_row (item_grid, y, NULL);
				if (item_grid->sliding_row >= SHEET_MAX_ROWS)
					item_grid->sliding_row = SHEET_MAX_ROWS-1;
			}

			if (item_grid->sliding != -1)
				return 1;

			item_grid_sliding_callback (item);

			item_grid_start_sliding (item);

			return 1;
		}

		item_grid_stop_sliding (item_grid);

		col = item_grid_find_col (item_grid, x, NULL);
		row = item_grid_find_row (item_grid, y, NULL);

		if (item_grid->selecting == ITEM_GRID_NO_SELECTION)
			return 1;

		if (col > SHEET_MAX_COLS-1)
			col = SHEET_MAX_COLS-1;
		if (row > SHEET_MAX_ROWS-1)
			row = SHEET_MAX_ROWS-1;

		if (item_grid->selecting == ITEM_GRID_SELECTING_FORMULA_RANGE){
			gnumeric_sheet_selection_extend (gsheet, col, row);
			return 1;
		}

		if (event->motion.x < 0)
			event->motion.x = 0;
		if (event->motion.y < 0)
			event->motion.y = 0;

		sheet_selection_extend_to (sheet, col, row);
		return 1;

	case GDK_BUTTON_PRESS:
		sheet_set_mode_type (sheet, SHEET_MODE_SHEET);

		item_grid_stop_sliding (item_grid);

		convert (canvas, event->button.x, event->button.y, &x, &y);
		col = item_grid_find_col (item_grid, x, NULL);
		row = item_grid_find_row (item_grid, y, NULL);

		switch (event->button.button){
		case 1:
			return item_grid_button_1 (sheet, event, item_grid, col, row, x, y);

		case 2:
			g_warning ("This is here just for demo purposes");
			drag_start (GTK_WIDGET (item->canvas), event, sheet);
			return 1;
			
		case 3:
			item_grid_popup_menu (item_grid->sheet,
					      event, col, row,
					      FALSE, FALSE);
			return 1;
		}

	default:
		return 0;
	}

	return 0;
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

	item_grid->left_col = 0;
	item_grid->top_row  = 0;
	item_grid->top_offset = 0;
	item_grid->left_offset = 0;
	item_grid->selecting = ITEM_GRID_NO_SELECTION;
	item_grid->sliding = -1;
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
		item_grid->sheet = item_grid->sheet_view->sheet;
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
