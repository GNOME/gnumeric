/*
 * Sheet.c:  Implements the sheet management and per-sheet storage
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "gnumeric-util.h"
#include "eval.h"
#include "number-match.h"
#include "format.h"

#define GNUMERIC_SHEET_VIEW(p) GNUMERIC_SHEET (SHEET_VIEW(p)->sheet_view);

/* Used to locate cells in a sheet */
typedef struct {
	int col;
	int row;
} CellPos;

void
sheet_redraw_all (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		
		sheet_view_redraw_all (sheet_view);
	}
}

static void
sheet_redraw_cols (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		
		sheet_view_redraw_columns (sheet_view);
	}
}

static void
sheet_redraw_rows (Sheet *sheet)
{
	GList *l;
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;
		
		sheet_view_redraw_rows (sheet_view);
	}
}

static void
sheet_init_default_styles (Sheet *sheet)
{
	/* The default column style */
	sheet->default_col_style.pos        = -1;
	sheet->default_col_style.units      = 80;
	sheet->default_col_style.pixels     = 0;
	sheet->default_col_style.margin_a   = 1;
	sheet->default_col_style.margin_b   = 1;
	sheet->default_col_style.data       = NULL;

	/* The default row style */
	sheet->default_row_style.pos      = -1;
	sheet->default_row_style.units    = 18;
	sheet->default_row_style.pixels   = 0;
	sheet->default_row_style.margin_a = 1;
	sheet->default_row_style.margin_b = 1;
	sheet->default_row_style.data     = NULL;
}

/* Initialize some of the columns and rows, to test the display engine */
static void
sheet_init_dummy_stuff (Sheet *sheet)
{
	ColRowInfo *cp, *rp;
	int x, y;

	for (x = 0; x < 40; x += 2){
		cp = sheet_row_new (sheet);
		cp->pos = x;
		cp->units = (x+1) * 30;
		sheet_col_add (sheet, cp);
	}

	for (y = 0; y < 6; y += 2){
		rp = sheet_row_new (sheet);
		rp->pos = y;
		rp->units = (20 * (y + 1));
		sheet_row_add (sheet, rp);
	}
}

static guint
cell_hash (gconstpointer key)
{
	CellPos *ca = (CellPos *) key;

	return (ca->row << 8) | ca->col;
}

static gint
cell_compare (gconstpointer a, gconstpointer b)
{
	CellPos *ca, *cb;

	ca = (CellPos *) a;
	cb = (CellPos *) b;

	if (ca->row != cb->row)
		return 0;
	if (ca->col != cb->col)
		return 0;
	
	return 1;
}

void
sheet_rename (Sheet *sheet, const char *new_name)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (new_name != NULL);

	g_free (sheet->name);
	sheet->name = g_strdup (new_name);
}

Sheet *
sheet_new (Workbook *wb, char *name)
{
	GtkWidget *sheet_view;
	Sheet *sheet;
	Style *sheet_style;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	
	sheet = g_new0 (Sheet, 1);
	sheet->signature = SHEET_SIGNATURE;
	sheet->workbook = wb;
	sheet->name = g_strdup (name);
	sheet->last_zoom_factor_used = -1.0;
	sheet->max_col_used = 0;
	sheet->max_row_used = 0;

	sheet->cell_hash = g_hash_table_new (cell_hash, cell_compare);
	
	sheet_style = style_new ();
	sheet_style_attach (sheet, 0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1, sheet_style);
	
	sheet_init_default_styles (sheet);
	
	/* Dummy initialization */
	if (0)
		sheet_init_dummy_stuff (sheet);

	sheet_view = sheet_view_new (sheet);
	gtk_object_ref (GTK_OBJECT (sheet_view));

	sheet->sheet_views = g_list_prepend (sheet->sheet_views, sheet_view);

	sheet_selection_append (sheet, 0, 0);

	gtk_widget_show (sheet_view);
	
	sheet_set_zoom_factor (sheet, 1.0);
	return sheet;
}

static void
cell_hash_free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

void
sheet_foreach_col (Sheet *sheet, sheet_col_row_callback callback, void *user_data)
{
	GList *l = sheet->cols_info;

	/* Invoke the callback for the default style */
	(*callback)(sheet, &sheet->default_col_style, user_data);

	/* And then for the rest */
	while (l){
		(*callback)(sheet, l->data, user_data);
		l = l->next;
	}
}

void
sheet_foreach_row (Sheet *sheet, sheet_col_row_callback callback, void *user_data)
{
	GList *l = sheet->rows_info;

	/* Invoke the callback for the default style */
	(callback)(sheet, &sheet->default_row_style, user_data);

	/* And then for the rest */
	while (l){
		(*callback)(sheet, l->data, user_data);
		l = l->next;
	}
}

static void
sheet_compute_col_row_new_size (Sheet *sheet, ColRowInfo *ci, void *data)
{
	double pix_per_unit = sheet->last_zoom_factor_used;

	ci->pixels = (ci->units * pix_per_unit) +
		ci->margin_a + ci->margin_b + 1;
}

void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	GList *l, *cl;
		
	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	sheet_compute_col_row_new_size (sheet, &sheet->default_row_style, NULL);
 	sheet_compute_col_row_new_size (sheet, &sheet->default_col_style, NULL);

	/* Then every column and row */
	sheet_foreach_col (sheet, sheet_compute_col_row_new_size, NULL);
	sheet_foreach_row (sheet, sheet_compute_col_row_new_size, NULL);

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_set_zoom_factor (sheet_view, factor);

	}

	for (cl = sheet->comment_list; cl; cl = cl->next){
		Cell *cell = cl->data;

		cell_comment_reposition (cell);
	}
}

/*
 * Duplicates a column or row
 */
ColRowInfo *
sheet_duplicate_colrow (ColRowInfo *original)
{
	ColRowInfo *info = g_new (ColRowInfo, 1);

	*info = *original;
	
	return info;
}

ColRowInfo *
sheet_row_new (Sheet *sheet)
{
	ColRowInfo *ri;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ri = sheet_duplicate_colrow (&sheet->default_row_style);
	row_init_span (ri);

	return ri;
}

ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	
	ci = sheet_duplicate_colrow (&sheet->default_col_style);
	ci->data = NULL;
	
	return ci;
}


static gint
CRsort (gconstpointer a, gconstpointer b)
{
	ColRowInfo *ia = (ColRowInfo *) a;
	ColRowInfo *ib = (ColRowInfo *) b;

	return (ia->pos - ib->pos);
}

void
sheet_col_add (Sheet *sheet, ColRowInfo *cp)
{
	if (cp->pos > sheet->max_col_used){
		GList *l;

		sheet->max_col_used = cp->pos;
		
		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GtkAdjustment *ha = GTK_ADJUSTMENT (sheet_view->ha);
		
			if (sheet->max_col_used > ha->upper){
				ha->upper = sheet->max_col_used;
				gtk_adjustment_changed (ha);
			}
		}
	}
	
	sheet->cols_info = g_list_insert_sorted (sheet->cols_info, cp, CRsort);
}

void
sheet_row_add (Sheet *sheet, ColRowInfo *rp)
{
	if (rp->pos > sheet->max_row_used){
		GList *l;

		sheet->max_row_used = rp->pos;

		for (l = sheet->sheet_views; l; l = l->next){
			SheetView *sheet_view = l->data;
			GtkAdjustment *va = GTK_ADJUSTMENT (sheet_view->va);
		
			if (sheet->max_row_used > va->upper){
				va->upper = sheet->max_row_used;
				gtk_adjustment_changed (va);
			}
		}
	}
	sheet->rows_info = g_list_insert_sorted (sheet->rows_info, rp, CRsort);
}

ColRowInfo *
sheet_col_get_info (Sheet *sheet, int col)
{
	GList *l = sheet->cols_info;

	for (; l; l = l->next){
		ColRowInfo *ci = l->data;

		if (ci->pos == col)
			return ci;
	}

	return &sheet->default_col_style;
}

ColRowInfo *
sheet_row_get_info (Sheet *sheet, int row)
{
	GList *l = sheet->rows_info;

	for (; l; l = l->next){
		ColRowInfo *ri = l->data;

		if (ri->pos == row)
			return ri;
	}

	return &sheet->default_row_style;
}

void
sheet_compute_visible_ranges (Sheet *sheet)
{
	GList *l;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_compute_visible_ranges (gsheet);
	}
}

static void
colrow_set_units (Sheet *sheet, ColRowInfo *info)
{
	double pix = sheet->last_zoom_factor_used;
	
	info->units  = (info->pixels -
			(info->margin_a + info->margin_b + 1)) / pix;
}

static void
sheet_reposition_comments (Sheet *sheet, int row)
{
	GList *l;
	
	/* Move any cell comments */
	for (l = sheet->comment_list; l; l = l->next){
		Cell *cell = l->data;

		if (cell->row->pos >= row)
			cell_comment_reposition (cell);
	}
}

void
sheet_row_info_set_height (Sheet *sheet, ColRowInfo *ri, int height, gboolean height_set_by_user)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);
	
	if (height_set_by_user)
		ri->hard_size = 1;

	ri->pixels = height;
	colrow_set_units (sheet, ri);
	
	sheet_compute_visible_ranges (sheet);

	sheet_reposition_comments (sheet, ri->pos);
	sheet_redraw_all (sheet);
}

/**
 * sheet_row_set_height:
 * @sheet:         	The sheet
 * @row:           	The row
 * @height:        	The desired height
 * @height_set_by_user: TRUE if this was done by a user (ie, user manually
 *                      set the width)
 *
 * Sets the height of a row in terms of the total visible space (as opossite
 * to the internal required space, which does not include the margins).
 */
void
sheet_row_set_height (Sheet *sheet, int row, int height, gboolean height_set_by_user)
{
	ColRowInfo *ri;
	int add = 0;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	ri = sheet_row_get_info (sheet, row);
	if (ri == &sheet->default_row_style){
		ri = sheet_duplicate_colrow (ri);
		ri->pos = row;
		add = 1;
	}

	sheet_row_info_set_height (sheet, ri, height, height_set_by_user);
	
	if (add)
		sheet_row_add (sheet, ri);
}

/**
 * sheet_row_set_internal_height:
 * @sheet:         	The sheet
 * @row:           	The row
 * @height:        	The desired height
 *
 * Sets the height of a row in terms of the internal required space (the total
 * size of the row will include the margins.
 */
void
sheet_row_set_internal_height (Sheet *sheet, ColRowInfo *ri, int height)
{
	double pix;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ri != NULL);
	
	pix = sheet->last_zoom_factor_used;
	
	if (ri->units == height)
		return;

	ri->units = height;
	ri->pixels = (ri->units * pix) + (ri->margin_a + ri->margin_b - 1);

	sheet_compute_visible_ranges (sheet);
	sheet_reposition_comments (sheet, ri->pos);
	sheet_redraw_all (sheet);
}

/**
 * sheet_recompute_spans_for_col:
 * @sheet: the sheet
 * @col:   The column that changed
 *
 * This routine recomputes the column span for the cells that touches
 * the column.
 */
static void
sheet_recompute_spans_for_col (Sheet *sheet, int col)
{
	GList *l, *cells;
	Cell *cell;

	cells = NULL;
	for (l = sheet->rows_info; l; l = l->next){
		ColRowInfo *ri = l->data;

		if (!(cell = sheet_cell_get (sheet, col, ri->pos)))
			cell = row_cell_get_displayed_at (ri, col);
		
		if (cell)
			cells = g_list_prepend (cells, cell);
	}

	/* No spans, just return */
	if (!cells)
		return;
	
	/* Unregister those cells that touched this column */
	for (l = cells; l; l = l->next){
		int left, right;
		
		cell = l->data;

		cell_unregister_span (cell);
		cell_get_span (cell, &left, &right);
		if (left != right)
			cell_register_span (cell, left, right);
	}

	g_list_free (cells);
}

void
sheet_col_info_set_width (Sheet *sheet, ColRowInfo *ci, int width)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (ci != NULL);

	
	ci->pixels = width;
	colrow_set_units (sheet, ci);
	
	sheet_compute_visible_ranges (sheet);
	sheet_redraw_all (sheet);
}

void
sheet_col_set_width (Sheet *sheet, int col, int width)
{
	ColRowInfo *ci;
	GList *l;
	int add = 0;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	ci = sheet_col_get_info (sheet, col);
	if (ci == &sheet->default_col_style){
		ci->pos = col;
		ci = sheet_duplicate_colrow (ci);
		add = 1;
	}

	sheet_col_info_set_width (sheet, ci, width);
	
	if (add)
		sheet_col_add (sheet, ci);

	/* Compute the spans */
	sheet_recompute_spans_for_col (sheet, col);

	/* Move any cell comments */
	for (l = sheet->comment_list; l; l = l->next){
		Cell *cell = l->data;

		if (cell->col->pos >= col)
			cell_comment_reposition (cell);
	}
}

static inline int
col_row_distance (GList *list, int from, int to, int default_pixels)
{
	ColRowInfo *cri;
	int pixels = 0, n = 0;
	GList *l;
	
	if (to == from)
		return 0;

	n = to - from;
	
	for (l = list; l; l = l->next){
		cri = l->data;
		
		if (cri->pos >= to)
			break;
		
		if (cri->pos >= from){
			n--;
			pixels += cri->pixels;
		}
	}
	pixels += n * default_pixels;
	
	return pixels;
}

/**
 * sheet_col_get_distance:
 *
 * Return the number of pixels between from_col to to_col
 */
int
sheet_col_get_distance (Sheet *sheet, int from_col, int to_col)
{
	g_assert (from_col <= to_col);
	g_assert (sheet != NULL);

	return col_row_distance (sheet->cols_info, from_col, to_col, sheet->default_col_style.pixels);
}

/**
 * sheet_row_get_distance:
 *
 * Return the number of pixels between from_row to to_row
 */
int
sheet_row_get_distance (Sheet *sheet, int from_row, int to_row)
{
	g_assert (from_row <= to_row);
	g_assert (sheet != NULL);
	
	return col_row_distance (sheet->rows_info, from_row, to_row, sheet->default_row_style.pixels);
}

int
sheet_selection_equal (SheetSelection *a, SheetSelection *b)
{
	if (a->start_col != b->start_col)
		return 0;
	if (a->start_row != b->start_row)
		return 0;
	
	if (a->end_col != b->end_col)
		return 0;
	if (a->end_row != b->end_row)
		return 0;
	return 1;
}

void
sheet_update_auto_expr (Sheet *sheet)
{
	Workbook *wb = sheet->workbook;
	Value *v;
	char  *error;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
		
	/* defaults */
	v = NULL;
	error = "ERROR";
	if (wb->auto_expr)
		v = eval_expr (sheet, wb->auto_expr, 0, 0, &error);
	
	if (v){
		char *s;

		s = value_string (v);
		workbook_auto_expr_label_set (wb, s);
		g_free (s);
		value_release (v);
	} else
		workbook_auto_expr_label_set (wb, error);
}

static char *
sheet_get_selection_name (Sheet *sheet)
{
	SheetSelection *ss = sheet->selections->data;
	static char buffer [40];
	
	if (ss->start_col == ss->end_col && ss->start_row == ss->end_row){
		return cell_name (ss->start_col, ss->start_row);
	} else {
		snprintf (buffer, sizeof (buffer), "%dLx%dC",
			  ss->end_row - ss->start_row + 1,
			  ss->end_col - ss->start_col + 1);
		return buffer;
	}
}

void
sheet_set_text (Sheet *sheet, int col, int row, char *str)
{
	GList *l;
	Cell *cell;
	double v;
	char *format, *text;
	int  text_set = FALSE;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cell = sheet_cell_get (sheet, col, row);
	
	if (!cell)
		cell = sheet_cell_new (sheet, col, row);

	text = gtk_entry_get_text (GTK_ENTRY (sheet->workbook->ea_input));

	if (*text == '@'){
		char *new_text = g_strdup (text);
		
		*new_text = '=';
		gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), new_text);
		g_free (new_text);
	}
	
	/*
	 * Figure out if a format matches, and for sanity compare that to
	 * a rendered version of the text, if they compare equally, then
	 * use that.
	 */
	if (!CELL_IS_FORMAT_SET (cell) && (*text != '=' && format_match (text, &v, &format))){
		StyleFormat *sf;
		char *new_text;
		char buffer [50];
		Value *vf = value_float (v);

		/* Render it */
		sf = style_format_new (format);
		new_text = format_value (sf, vf, NULL);
		value_release (vf);
		style_format_unref (sf);

		/* Compare it */
		if (strcasecmp (new_text, text) == 0){
			cell_set_format_simple (cell, format);
			sprintf (buffer, "%g", v);
			cell_set_text (cell, buffer);
			text_set = TRUE;
		}
		g_free (new_text);
	}

	if (!text_set)
		cell_set_text (cell, text);
	
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		
		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}

	workbook_recalc (sheet->workbook);
	
}

void
sheet_set_current_value (Sheet *sheet)
{
	char *str;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	str = gtk_entry_get_text (GTK_ENTRY (sheet->workbook->ea_input));
	sheet_set_text (sheet, sheet->cursor_col, sheet->cursor_row, str);
}

static void
sheet_stop_editing (Sheet *sheet)
{
	sheet->editing = FALSE;

	if (sheet->editing_saved_text){
		string_unref (sheet->editing_saved_text);
		sheet->editing_saved_text = NULL;
		sheet->editing_cell = NULL;
	}
}

void
sheet_accept_pending_input (Sheet *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	if (!sheet->editing)
		return;
			
	sheet_set_current_value (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_stop_editing (gsheet);
	}
	sheet_stop_editing (sheet);
}

void
sheet_cancel_pending_input (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!sheet->editing)
		return;

	if (sheet->editing_cell){
		cell_set_text (sheet->editing_cell, sheet->editing_saved_text->str);
		sheet_stop_editing (sheet);
	}

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_destroy_editing_cursor (gsheet);
	}
}

void
sheet_load_cell_val (Sheet *sheet)
{
	GtkEntry *entry;
	Cell *cell;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	entry = GTK_ENTRY (sheet->workbook->ea_input);
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);

	if (cell){
		char *text;
		
		text = cell_get_text (cell);
		gtk_entry_set_text (entry, text);
		g_free (text);
	} else
		gtk_entry_set_text (entry, ""); 
}

void
sheet_start_editing_at_cursor (Sheet *sheet)
{
	GList *l;
	Cell  *cell;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	gtk_entry_set_text (GTK_ENTRY (sheet->workbook->ea_input), "");

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_create_editing_cursor (gsheet);
	}
	
	sheet->editing = TRUE;
	cell = sheet_cell_get (sheet, sheet->cursor_col, sheet->cursor_row);
	if (cell){
		char *text;

		text = cell_get_text (cell);
		sheet->editing_saved_text = string_get (text);
		g_free (text);
		
		sheet->editing_cell = cell;
		cell_set_text (cell, "");
	}
}

/**
 * sheet_update_controls:
 *
 * This routine is ran every time the seleciton has changed.  It checks
 * what the status of various toolbar feedback controls should be
 */
static void
sheet_update_controls (Sheet *sheet)
{
	GList *cells, *l;
	int   bold_first, italic_first;
	int   bold_common, italic_common;
	
	cells = sheet_selection_to_list (sheet);

	if (cells){
		Cell *cell = cells->data;

		bold_first = cell->style->font->hint_is_bold;
		italic_first = cell->style->font->hint_is_italic;
		
		l = cells->next;
	}
	else
	{
		/*
		 * If no cells are on the selection, use the first cell
		 * in the range to compute the values
		 */
		SheetSelection *ss = sheet->selections->data;
		Style *style;
		
		style = sheet_style_compute (sheet, ss->start_col, ss->start_row, NULL);
		bold_first = style->font->hint_is_bold;
		italic_first = style->font->hint_is_italic;
		style_destroy (style);
			
		/* Initialize the pointer that is going to be used next */
		l = cells;
	}

	bold_common = italic_common = TRUE;

	/* Check every cell on the range */
	for (; l; l = l->next){
		Cell *cell = l->data;

		if (italic_first != cell->style->font->hint_is_italic)
			italic_common = FALSE;

		if (bold_first != cell->style->font->hint_is_bold)
			bold_common = FALSE;

		if (bold_common == FALSE && italic_common == FALSE)
			break;
	}
	g_list_free (cells);

	/* Update the toolbar */
	if (bold_common)
		workbook_feedback_set (
			sheet->workbook,
			WORKBOOK_FEEDBACK_BOLD,
			GINT_TO_POINTER(bold_first));

	if (italic_common)
		workbook_feedback_set (
			sheet->workbook,
			WORKBOOK_FEEDBACK_ITALIC,
			GINT_TO_POINTER(italic_first));
}

static void
sheet_selection_changed_hook (Sheet *sheet)
{
	sheet_update_auto_expr (sheet);
	sheet_update_controls  (sheet);
	workbook_set_region_status (sheet->workbook, sheet_get_selection_name (sheet));
}

void
sheet_selection_append_range (Sheet *sheet,
			      int base_col,  int base_row,
			      int start_col, int start_row,
			      int end_col,   int end_row)
{
	SheetSelection *ss;
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	
	ss = g_new0 (SheetSelection, 1);

	ss->base_col  = base_col;
	ss->base_row  = base_row;

	ss->start_col = start_col;
	ss->end_col   = end_col;
	ss->start_row = start_row;
	ss->end_row   = end_row;
	
	sheet->selections = g_list_prepend (sheet->selections, ss);

	sheet_accept_pending_input (sheet);
	sheet_load_cell_val (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		
		gnumeric_sheet_set_selection (gsheet, ss);
	}
	sheet_redraw_selection (sheet, ss);
	
	sheet_redraw_cols (sheet);
	sheet_redraw_rows (sheet);

	sheet_selection_changed_hook (sheet);
}

void
sheet_selection_append (Sheet *sheet, int col, int row)
{
	sheet_selection_append_range (sheet, col, row, col, row, col, row);
}

/**
 * sheet_selection_extend_to:
 * @sheet: the sheet
 * @col:   column that gets covered
 * @row:   row that gets covered
 *
 * This extends the selection to cover col, row
 */
void
sheet_selection_extend_to (Sheet *sheet, int col, int row)
{
	SheetSelection *ss, old_selection;
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	
	g_assert (sheet->selections);

	ss = (SheetSelection *) sheet->selections->data;

	old_selection = *ss;
	
	if (col < ss->base_col){
		ss->start_col = col;
		ss->end_col   = ss->base_col;
	} else {
		ss->start_col = ss->base_col;
		ss->end_col   = col;
	}

	if (row < ss->base_row){
		ss->end_row   = ss->base_row;
		ss->start_row = row;
	} else {
		ss->end_row   = row;
		ss->start_row = ss->base_row;
	}

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		
		gnumeric_sheet_set_selection (gsheet, ss);
	}
	
	sheet_selection_changed_hook (sheet);
	
	sheet_redraw_selection (sheet, &old_selection);
	sheet_redraw_selection (sheet, ss);
	
	if (ss->start_col != old_selection.start_col ||
	    ss->end_col != old_selection.end_col ||
	    ((ss->start_row == 0 && ss->end_row == SHEET_MAX_ROWS-1) ^
	     (old_selection.start_row == 0 &&
	      old_selection.end_row == SHEET_MAX_ROWS-1)))
		sheet_redraw_cols (sheet);
	
	if (ss->start_row != old_selection.start_row ||
	    ss->end_row != old_selection.end_row ||
	    ((ss->start_col == 0 && ss->end_col == SHEET_MAX_COLS-1) ^
	     (old_selection.start_col == 0 &&
	      old_selection.end_col == SHEET_MAX_COLS-1)))
		sheet_redraw_rows (sheet);
}

/**
 * sheet_select_all:
 * Sheet: The sheet
 *
 * Selects all of the cells in the sheet
 */
void
sheet_select_all (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_make_cell_visible (sheet, 0, 0);
	sheet_cursor_move (sheet, 0, 0);
	sheet_selection_append_range (sheet, 0, 0, 0, 0,
		SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	/* Queue redraws for columns and rows */
	sheet_redraw_rows (sheet);
	sheet_redraw_cols (sheet);
}

int
sheet_is_all_selected (Sheet *sheet)
{
	SheetSelection *ss;
	GList *l;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	
	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->start_col == 0 &&
		    ss->start_row == 0 &&
		    ss->end_col == SHEET_MAX_COLS-1 &&
		    ss->end_row == SHEET_MAX_ROWS-1)
			return TRUE;
	}
	return FALSE;
}

int
sheet_col_selection_type (Sheet *sheet, int col)
{
	SheetSelection *ss;
	GList *l;
	int ret = ITEM_BAR_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (sheet->selections == NULL){
		if (col == sheet->cursor_col)
			return ITEM_BAR_PARTIAL_SELECTION;
		return ret;
	}
	
	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->start_col > col ||
		    ss->end_col < col)
			continue;
			
		if (ss->start_row == 0 &&
		    ss->end_row == SHEET_MAX_ROWS-1)
			return ITEM_BAR_FULL_SELECTION;
		
		ret = ITEM_BAR_PARTIAL_SELECTION;
	}
	
	return ret;
}

int
sheet_row_selection_type (Sheet *sheet, int row)
{
	SheetSelection *ss;
	GList *l;
	int ret = ITEM_BAR_NO_SELECTION;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (sheet->selections == NULL){
		if (row == sheet->cursor_row)
			return ITEM_BAR_PARTIAL_SELECTION;
		return ret;
	}
	
	for (l = sheet->selections; l != NULL; l = l->next){
		ss = l->data;

		if (ss->start_row > row ||
		    ss->end_row < row)
			continue;
			
		if (ss->start_col == 0 &&
		    ss->end_col == SHEET_MAX_COLS-1)
			return ITEM_BAR_FULL_SELECTION;
		
		ret = ITEM_BAR_PARTIAL_SELECTION;
	}
	
	return ret;
}

/*
 * This routine is used to queue the redraw regions for the 
 * cell region specified.
 *
 * It is usually called before a change happens to a region,
 * and after the change has been done to queue the regions
 * for the old contents and the new contents.
 */
void
sheet_redraw_cell_region (Sheet *sheet,
			  int start_col, int start_row,
			  int end_col,   int end_row)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_redraw_cell_region (
			sheet_view,
			start_col, start_row,
			end_col, end_row);
	}
}

void
sheet_redraw_selection (Sheet *sheet, SheetSelection *ss)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail (ss != NULL);
	
	sheet_redraw_cell_region (sheet,
				  ss->start_col, ss->start_row,
				  ss->end_col, ss->end_row);
}

int
sheet_row_check_bound (int row, int diff)
{
	int new_val = row + diff;

	if (new_val < 0)
		return 0;
	if (new_val >= SHEET_MAX_ROWS)
		return SHEET_MAX_ROWS - 1;

	return new_val;
}

int
sheet_col_check_bound (int col, int diff)
{
	int new_val = col + diff;

	if (new_val < 0)
		return 0;
	if (new_val >= SHEET_MAX_COLS)
		return SHEET_MAX_COLS - 1;

	return new_val;
}

static void
sheet_selection_change (Sheet *sheet, SheetSelection *old, SheetSelection *new)
{
	GList *l;
	
	if (sheet_selection_equal (old, new))
		return;
		
	sheet_accept_pending_input (sheet);
	sheet_redraw_selection (sheet, old);
	sheet_redraw_selection (sheet, new);
	sheet_selection_changed_hook (sheet);

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);
		
		gnumeric_sheet_set_selection (gsheet, new);
	}
	
	if (new->start_col != old->start_col ||
	    new->end_col != old->end_col ||
	    ((new->start_row == 0 && new->end_row == SHEET_MAX_ROWS-1) ^
	     (old->start_row == 0 &&
	      old->end_row == SHEET_MAX_ROWS-1)))
		sheet_redraw_cols (sheet);
	
	if (new->start_row != old->start_row ||
	    new->end_row != old->end_row ||
	    ((new->start_col == 0 && new->end_col == SHEET_MAX_COLS-1) ^
	     (old->start_col == 0 &&
	      old->end_col == SHEET_MAX_COLS-1)))
		sheet_redraw_rows (sheet);
}

/**
 * sheet_selection_extend_horizontal:
 *
 * @sheet:  The Sheet *
 * @count:  units to extend the selection horizontally
 */
void
sheet_selection_extend_horizontal (Sheet *sheet, int n)
{
	SheetSelection *ss;
	SheetSelection old_selection;

	/* FIXME: right now we only support units (1 or -1)
	 * to fix this we need to account for the fact that
	 * the selection boundary might change and adjust
	 * appropiately
	 */
	 
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail ((n == 1 || n == -1));
	
	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;
	
	if (ss->base_col < ss->end_col)
		ss->end_col = sheet_col_check_bound (ss->end_col, n);
	else if (ss->base_col > ss->start_col)
		ss->start_col = sheet_col_check_bound (ss->start_col, n);
	else {
		if (n > 0)
			ss->end_col = sheet_col_check_bound (ss->end_col, 1);
		else
			ss->start_col = sheet_col_check_bound (ss->start_col, -1);
	}

	sheet_selection_change (sheet, &old_selection, ss);
}

/*
 * sheet_selection_extend_vertical
 * @sheet:  The Sheet *
 * @n:      units to extend the selection vertically
 */
void
sheet_selection_extend_vertical (Sheet *sheet, int n)
{
	SheetSelection *ss;
	SheetSelection old_selection;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail ((n == 1 || n == -1));
	
	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;
	
	if (ss->base_row < ss->end_row)
		ss->end_row = sheet_row_check_bound (ss->end_row, n);
	else if (ss->base_row > ss->start_row)
		ss->start_row = sheet_row_check_bound (ss->start_row, n);
	else {
		if (n > 0)
			ss->end_row = sheet_row_check_bound (ss->end_row, 1);
		else
			ss->start_row = sheet_row_check_bound (ss->start_row, -1);
	}

	sheet_selection_change (sheet, &old_selection, ss);
}

void
sheet_selection_set (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	SheetSelection *ss;
	SheetSelection old_selection;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	
	ss = (SheetSelection *)sheet->selections->data;
	old_selection = *ss;
	
	ss->start_row = start_row;
	ss->end_row = end_row;
	ss->start_col = start_col;
	ss->end_col = end_col;
	
	sheet_selection_change (sheet, &old_selection, ss);
}

/**
 * sheet_selections_free
 * @sheet: the sheet
 *
 * Releases the selection associated with this sheet
 */
static void
sheet_selections_free (Sheet *sheet)
{
	g_list_free (sheet->selections);
	sheet->selections = NULL;
}

/*
 * sheet_selection_reset
 * sheet:  The sheet
 *
 * Clears all of the selection ranges.
 * Warning: This does not set a new selection, this should
 * be taken care on the calling routine. 
 */
void
sheet_selection_reset_only (Sheet *sheet)
{
	GList *list = sheet->selections;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 

	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		sheet_redraw_selection (sheet, ss);
		g_free (ss);
	}
	sheet_selections_free (sheet);
	
	sheet->walk_info.current = NULL;
		
	/* Redraw column bar */
	sheet_redraw_cols (sheet);

	/* Redraw the row bar */
	sheet_redraw_rows (sheet);
}

/**
 * sheet_selection_reset:
 * sheet:  The sheet
 *
 * Clears all of the selection ranges and resets it to a
 * selection that only covers the cursor
 */
void
sheet_selection_reset (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	
	sheet_selection_reset_only (sheet);
	sheet_selection_append (sheet, sheet->cursor_col, sheet->cursor_row);
}

int
sheet_selection_is_cell_selected (Sheet *sheet, int col, int row)
{
	GList *list = sheet->selections;

	for (list = sheet->selections; list; list = list->next){
		SheetSelection *ss = list->data;

		if ((ss->start_col <= col) && (col <= ss->end_col) &&
		    (ss->start_row <= row) && (row <= ss->end_row)){
			return 1;
		}
	}
	return 0;
}

/*
 * walk_boundaries: implements the decitions for walking a region
 * returns TRUE if the cursor left the boundary region
 */
static int
walk_boundaries (int lower_col,   int lower_row,
		 int upper_col,   int upper_row,
		 int inc_x,       int inc_y,
		 int current_col, int current_row,
		 int *new_col,    int *new_row)
{
	if (current_row + inc_y > upper_row ||
	    current_col + inc_x > upper_col){
		*new_row = current_row;
		*new_col = current_col;
		return TRUE;
	} else {
		if (current_row + inc_y < lower_row ||
		    current_col + inc_x < lower_col){
			*new_row = current_row;
			*new_col = current_col;
			return TRUE;
		} else {
			*new_row = current_row + inc_y;
			*new_col = current_col + inc_x;
		}
	}
	return FALSE;
}

/*
 * walk_boundaries: implements the decitions for walking a region
 * returns TRUE if the cursor left the boundary region.  This
 * version implements wrapping on the regions.
 */
static int
walk_boundaries_wrapped (int lower_col,   int lower_row,
			 int upper_col,   int upper_row,
			 int inc_x,       int inc_y,
			 int current_col, int current_row,
			 int *new_col,    int *new_row)
{
	if (current_row + inc_y > upper_row){
		if (current_col + 1 > upper_col)
			goto overflow;

		*new_row = lower_row;
		*new_col = current_col + 1;
		return FALSE;
	}

	if (current_row + inc_y < lower_row){
		if (current_col - 1 < lower_col)
			goto overflow;
		
		*new_row = upper_row;
		*new_col = current_col - 1;
		return FALSE;
	}
	
	if (current_col + inc_x > upper_col){
		if (current_row + 1 > upper_row)
			goto overflow;
		
		*new_row = current_row + 1;
		*new_col = lower_col;
		return FALSE;
	}

	if (current_col + inc_x < lower_col){
		if (current_row - 1 < lower_row)
			goto overflow;
		*new_row = current_row - 1;
		*new_col = upper_col;
		return FALSE;
	}
	
	*new_row = current_row + inc_y;
	*new_col = current_col + inc_x;
	return FALSE;
	
overflow:
	*new_row = current_row;
	*new_col = current_col;
	return TRUE;
}

int
sheet_selection_walk_step (Sheet *sheet, int forward, int horizontal,
			   int current_col, int current_row,
			   int *new_col, int *new_row)
{
	SheetSelection *ss;
	int inc_x = 0, inc_y = 0;
	int selections_count, diff, overflow;;
	
	diff = forward ? 1 : -1;

	if (horizontal)
		inc_x = diff;
	else
		inc_y = diff;
				 
	selections_count = g_list_length (sheet->selections);
	
	if (selections_count == 1){
		ss = sheet->selections->data;

		/* If there is no selection besides the cursor, plain movement */
		if (ss->start_col == ss->end_col && ss->start_row == ss->end_row){
			walk_boundaries (0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
					 inc_x, inc_y, current_col, current_row,
					 new_col, new_row);
			return FALSE;
		}
	}

	if (!sheet->walk_info.current)
		sheet->walk_info.current = sheet->selections->data;

	ss = sheet->walk_info.current;

	overflow = walk_boundaries_wrapped (
		ss->start_col, ss->start_row,
		ss->end_col,   ss->end_row,
		inc_x, inc_y, current_col, current_row,
		new_col, new_row);
	
	if (overflow){
		int p;
		
		p = g_list_index (sheet->selections, ss);
		p += diff;
		if (p < 0)
			p = selections_count - 1;
		else if (p == selections_count)
			p = 0;
		
		ss = g_list_nth (sheet->selections, p)->data;
		sheet->walk_info.current = ss;
		
		if (forward){
			*new_col = ss->start_col;
			*new_row = ss->start_row;
		} else {
			*new_col = ss->end_col;
			*new_row = ss->end_row;
		}
	}
	return TRUE;
}

/*
 * assemble_cell_list: A callback for sheet_cell_foreach_range
 * intented to assemble a list of cells in a region.
 *
 * The closure parameter should be a pointer to a GList.
 */
static int
assemble_cell_list (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	GList **l = (GList **) user_data;

	*l = g_list_prepend (*l, cell);
	return TRUE;
}

CellList *
sheet_selection_to_list (Sheet *sheet)
{
	GList *selections;
	CellList *list;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->selections, NULL);

	list = NULL;
	for (selections = sheet->selections; selections; selections = selections->next){
		SheetSelection *ss = selections->data;

		sheet_cell_foreach_range (
			sheet, TRUE,
			ss->start_col, ss->start_row,
			ss->end_col, ss->end_row,
			assemble_cell_list, &list);
	}

	return list;
}

/**
 * sheet_col_get:
 *
 * Returns an allocated column:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_col_get (Sheet *sheet, int pos)
{
	GList *clist;
	ColRowInfo *col;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 
	
	for (clist = sheet->cols_info; clist; clist = clist->next){
		col = (ColRowInfo *) clist->data;

		if (col->pos == pos)
			return col;
	}
	col = sheet_col_new (sheet);
	col->pos = pos;
	sheet_col_add (sheet, col);
	
	return col;
}

/**
 * sheet_row_get:
 *
 * Returns an allocated row:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_row_get (Sheet *sheet, int pos)
{
	GList *rlist;
	ColRowInfo *row;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 
	
	for (rlist = sheet->rows_info; rlist; rlist = rlist->next){
		row = (ColRowInfo *) rlist->data;

		if (row->pos == pos)
			return row;
	}
	row = sheet_row_new (sheet);
	row->pos = pos;
	sheet_row_add (sheet, row);
	
	return row;
}

static int
gen_row_blanks (Sheet *sheet, int col, int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int row;

	for (row = 0; row < end_row; row++)
		if (!(*callback)(sheet, col, row, NULL, closure))
			return FALSE;

	return TRUE;
}

static int
gen_col_blanks (Sheet *sheet, int start_col, int end_col,
		int start_row, int end_row,
		sheet_cell_foreach_callback callback, void *closure)
{
	int col;
       
	for (col = 0; col < end_col; col++)
		if (!gen_row_blanks (sheet, col, start_row, end_row, callback, closure))
			return FALSE;
	
	return TRUE;
}

/**
 * sheet_cell_get:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (Cell *) containing the Cell, or NULL if
 * the cell does not exist
 */
Cell *
sheet_cell_get (Sheet *sheet, int col, int row)
{
	Cell *cell;
	CellPos cellpos;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 

	cellpos.col = col;
	cellpos.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &cellpos);

	return cell;
}

/**
 * sheet_cell_fetch:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (Cell *) containing the Cell at col, row.
 * If no cell existed at that location before, it is created.
 */
Cell *
sheet_cell_fetch (Sheet *sheet, int col, int row)
{
	Cell *cell;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		cell = sheet_cell_new (sheet, col, row);

	return cell;
}

/**
 * sheet_cell_foreach_range:
 *
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Return value:
 *    FALSE if some invoked routine requested to stop (by returning FALSE). 
 */
int
sheet_cell_foreach_range (Sheet *sheet, int only_existing,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  sheet_cell_foreach_callback callback,
			  void *closure)
{
	GList *col;
	GList *row;
	int   last_col_gen = -1, last_row_gen = -1;
	int   cont;

	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE); 
	g_return_val_if_fail (callback != NULL, FALSE);
	
	col = sheet->cols_info;
	for (; col; col = col->next){
		ColRowInfo *ci = col->data;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		if (!only_existing){
			if ((last_col_gen > 0) && (ci->pos != last_col_gen+1))
				if (!gen_col_blanks (sheet, last_col_gen, ci->pos,
						     start_row, end_row, callback,
						     closure))
				    return FALSE;
					
			if (ci->pos > start_col)
				if (!gen_col_blanks (sheet, start_col, ci->pos,
						     start_row, end_row, callback,
						     closure))
					return FALSE;
		}
		last_col_gen = ci->pos;

		last_row_gen = -1;
		for (row = (GList *) ci->data; row; row = row->next){
			Cell *cell = (Cell *) row->data;
			int  row_pos = cell->row->pos;

			if (row_pos < start_row)
				continue;

			if (row_pos > end_row)
				break;

			if (!only_existing){
				if (last_row_gen > 0){
					if (row_pos != last_row_gen+1)
						if (!gen_row_blanks (sheet, ci->pos,
								     last_row_gen,
								     row_pos,
								     callback,
								     closure))
							return FALSE;
				}
				if (row_pos > start_row){
					if (!gen_row_blanks (sheet, ci->pos,
							     row_pos, start_row,
							     callback, closure))
						return FALSE;
				}
			}
			cont = (*callback)(sheet, ci->pos, row_pos, cell, closure);
			if (cont == FALSE)
				return FALSE;
		}
	}
	return TRUE;
}

static gint
CRowSort (gconstpointer a, gconstpointer b)
{
	Cell *ca = (Cell *) a;
	Cell *cb = (Cell *) b;

	return ca->row->pos - cb->row->pos;
}

/**
 * sheet_cell_add_to_hash:
 * @sheet The sheet where the cell is inserted
 * @cell  The cell, it should already have col/pos pointers
 *        initialized pointing to the correct ColRowInfo
 */
static void
sheet_cell_add_to_hash (Sheet *sheet, Cell *cell)
{
	CellPos *cellpos;
	Cell *cell_on_spot;
	int left, right;
		
	/* See if another cell was displaying in our spot */
	cell_on_spot = row_cell_get_displayed_at (cell->row, cell->col->pos);
	if (cell_on_spot)
		cell_unregister_span (cell_on_spot);
	
	cellpos = g_new (CellPos, 1);
	cellpos->col = cell->col->pos;
	cellpos->row = cell->row->pos;

	g_hash_table_insert (sheet->cell_hash, cellpos, cell);

	/*
	 * Now register the sizes of our cells
	 */
	if (cell_on_spot){
		cell_get_span (cell_on_spot, &left, &right);
		if (left != right)
			cell_register_span (cell_on_spot, left, right);
	}
	cell_get_span (cell, &left, &right);
	if (left != right)
		cell_register_span (cell, left, right);
}

void
sheet_cell_add (Sheet *sheet, Cell *cell, int col, int row)
{
	cell->sheet = sheet;
	cell->col   = sheet_col_get (sheet, col);
	cell->row   = sheet_row_get (sheet, row);

	cell_realize (cell);
	
	if (!cell->style){
		int flags;
		
		cell->style = sheet_style_compute (sheet, col, row, &flags);

		if (flags & STYLE_FORMAT)
			cell->flags |= CELL_FORMAT_SET;
	}
	cell_calc_dimensions (cell);

	sheet_cell_add_to_hash (sheet, cell);
	cell->col->data = g_list_insert_sorted (cell->col->data, cell, CRowSort);
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 

	cell = g_new0 (Cell, 1);

	sheet_cell_add (sheet, cell, col, row);
	return cell;
}

static void
sheet_cell_remove_from_hash (Sheet *sheet, Cell *cell)
{
	CellPos cellpos;
	void    *original_key;

	cellpos.col = cell->col->pos;
	cellpos.row = cell->row->pos;

	cell_unregister_span (cell);
	g_hash_table_lookup_extended (sheet->cell_hash, &cellpos, &original_key, NULL);
	g_hash_table_remove (sheet->cell_hash, &cellpos);
	g_free (original_key);
}

static void
sheet_cell_remove_internal (Sheet *sheet, Cell *cell)
{
	if (cell->parsed_node)
		sheet_cell_formula_unlink (cell);

	sheet_cell_remove_from_hash (sheet, cell);

	cell_unrealize (cell);
}

/**
 * sheet_cell_remove_to_eot:
 *
 * Removes all of the cells from CELL_LIST point on.
 */
static void
sheet_cell_remove_to_eot (Sheet *sheet, GList *cell_list)
{
	while (cell_list){
		Cell *cell = cell_list->data;

		if (cell->parsed_node)
			sheet_cell_formula_unlink (cell);
		
		sheet_cell_remove_from_hash (sheet, cell);
		cell_destroy (cell);
	}
}

void
sheet_cell_remove (Sheet *sheet, Cell *cell)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Queue a redraw on the region used by the cell being removed */
	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
				  
	sheet_cell_remove_internal (sheet, cell);
	cell->col->data = g_list_remove (cell->col->data, cell);

	sheet_redraw_cell_region (sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

void
sheet_cell_comment_link (Cell *cell)
{
	Sheet *sheet;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);

	sheet = cell->sheet;
	
	sheet->comment_list = g_list_prepend (sheet->comment_list, cell);
}

void
sheet_cell_comment_unlink (Cell *cell)
{
	Sheet *sheet;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->sheet != NULL);
	g_return_if_fail (cell->comment != NULL);
	
	sheet = cell->sheet;
	sheet->comment_list = g_list_remove (sheet->comment_list, cell);
}

void
sheet_cell_formula_link (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);
	
	sheet = cell->sheet;

	sheet->workbook->formula_cell_list = g_list_prepend (sheet->workbook->formula_cell_list, cell);
	cell_add_dependencies (cell);
}

void
sheet_cell_formula_unlink (Cell *cell)
{
	Sheet *sheet;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);
	
	sheet = cell->sheet;
	cell_drop_dependencies (cell);
	sheet->workbook->formula_cell_list = g_list_remove (sheet->workbook->formula_cell_list, cell);
}

/**
 * sheet_col_destroy:
 *
 * Destroys a ColRowInfo from the Sheet with all of its cells
 */
static void
sheet_col_destroy (Sheet *sheet, ColRowInfo *ci)
{
	GList *l;

	for (l = ci->data; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_remove_internal (sheet, cell);
		cell_destroy (cell);
	}
	
	sheet->cols_info = g_list_remove (sheet->cols_info, ci);
	g_list_free (ci->data);
	g_free (ci);
}

/*
 * Destroys a row ColRowInfo
 */
static void
sheet_row_destroy (Sheet *sheet, ColRowInfo *ri)
{
	sheet->rows_info = g_list_remove (sheet->rows_info, ri);
	row_destroy_span (ri);
	
	g_free (ri);
}

static void
sheet_destroy_styles (Sheet *sheet)
{
	GList *l;

	for (l = sheet->style_list; l; l = l->next){
		StyleRegion *sr = l->data;
		
		style_destroy (sr->style);
		g_free (sr);
	}
	g_list_free (l);
}

static void
sheet_destroy_columns_and_rows (Sheet *sheet)
{
	GList *l;

	for (l = sheet->cols_info; l; l = l->next)
		sheet_col_destroy (sheet, l->data);

	for (l = sheet->rows_info; l; l = l->next)
		sheet_row_destroy (sheet, l->data);
}

/**
 * sheet_destroy:
 * @sheet: the sheet to destroy
 *
 * Destroys a Sheet.
 *
 * Please note that you need to unattach this sheet before
 * calling this routine or you will get a warning.
 */
void
sheet_destroy (Sheet *sheet)
{
	GList *l;
	
	g_assert (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet)); 
	g_return_if_fail (sheet->workbook == NULL);
			  
	sheet_selections_free (sheet);
	g_free (sheet->name);
	
	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		gtk_object_unref (GTK_OBJECT (sheet_view));
	}
	g_list_free (sheet->sheet_views);
	g_list_free (sheet->comment_list);
	
	g_hash_table_foreach (sheet->cell_hash, cell_hash_free_key, NULL);
	g_hash_table_destroy (sheet->cell_hash);

	sheet_destroy_columns_and_rows (sheet);
	sheet_destroy_styles (sheet);

	sheet->signature = 0;
	g_free (sheet);
}


/**
 * sheet_clear_region:
 *
 * Clears are region of cells
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_cell_foreach_range routine
 */
void
sheet_clear_region (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	GList *destroyable_cells, *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the cells being removed */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);
	
	destroyable_cells = NULL;
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		assemble_cell_list, &destroyable_cells);

	for (l = destroyable_cells; l; l = l->next){
		Cell *cell = l->data;
		
		sheet_cell_remove (sheet, cell);
		cell_destroy (cell);
	}
	g_list_free (destroyable_cells);
}

void
sheet_selection_clear (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_clear_region (sheet,
				    ss->start_col, ss->start_row,
				    ss->end_col, ss->end_row);
	}
}

static int
clear_cell_content (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_set_text (cell, "");
	return TRUE;
}

/**
 * sheet_clear_region_content:
 * @sheet:     The sheet on which we operate
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 * 
 * Clears the contents in a region of cells
 */
void
sheet_clear_region_content (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the region being redrawn */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);
	
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		clear_cell_content, NULL);
}

/**
 * sheet_selection_clear_content:
 * @sheet:  The sheet where we operate
 * 
 * Removes the contents of all the cells in the current selection.
 **/
void
sheet_selection_clear_content (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_clear_region_content (sheet,
					    ss->start_col, ss->start_row,
					    ss->end_col, ss->end_row);
	}
}

static int
clear_cell_comments (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_comment_destroy (cell);
	return TRUE;
}

/**
 * sheet_clear_region_comments:
 * @sheet:     The sheet on which we operate
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 * 
 * Removes all of the comments in the cells in the specified range.
 **/
void
sheet_clear_region_comments (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a redraw for the region being redrawn */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);

	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col,   end_row,
		clear_cell_comments, NULL);
}

/**
 * sheet_selection_clear_comments:
 * @sheet:  The sheet where we operate
 * 
 * Removes all of the comments on the range of selected cells.
 **/
void
sheet_selection_clear_comments (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_clear_region_comments (sheet,
					     ss->start_col, ss->start_row,
					     ss->end_col, ss->end_row);
	}
}

static int
clear_cell_format (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	cell_set_format (cell, "General");
	return TRUE;
}

void
sheet_clear_region_formats (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	/* Queue a draw for the region being modified */
	sheet_redraw_cell_region (sheet, start_col, start_row, end_col, end_row);
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row,
		end_col, end_row,
		clear_cell_format, NULL);
}

void
sheet_selection_clear_formats (Sheet *sheet)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_clear_region_formats (sheet,
					    ss->start_col, ss->start_row,
					    ss->end_col, ss->end_row);
	}
}

gboolean
sheet_verify_selection_simple (Sheet *sheet, char *command_name)
{
	char *msg;
	
	if (g_list_length (sheet->selections) == 1)
		return TRUE;

	msg = g_strconcat (
		"The command `", command_name,
		"' can not be performed with multiple selections", NULL);
	gnumeric_notice (sheet->workbook, GNOME_MESSAGE_BOX_ERROR, msg);
	g_free (msg);
	
	return FALSE;
}

gboolean
sheet_selection_copy (Sheet *sheet)
{
	SheetSelection *ss;
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!sheet_verify_selection_simple (sheet, "copy"))
		return FALSE;
	
	ss = sheet->selections->data;

	if (sheet->workbook->clipboard_contents)
		clipboard_release (sheet->workbook->clipboard_contents);

	sheet->workbook->clipboard_contents = clipboard_copy_cell_range (
		sheet,
		ss->start_col, ss->start_row,
		ss->end_col, ss->end_row);

	return TRUE;
}

gboolean
sheet_selection_cut (Sheet *sheet)
{
	SheetSelection *ss;
	
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (sheet->selections, FALSE);

	if (!sheet_verify_selection_simple (sheet, "cut"))
		return FALSE;

	ss = sheet->selections->data;

	sheet_selection_copy (sheet);
	sheet_clear_region (sheet, ss->start_col, ss->start_row, ss->end_col, ss->end_row);

	return TRUE;
}

void
sheet_selection_paste (Sheet *sheet, int dest_col, int dest_row, int paste_flags, guint32 time)
{
	CellRegion *content;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->selections);

	content = sheet->workbook->clipboard_contents;
	
	if (content)
		if (!sheet_verify_selection_simple (sheet, _("Paste")))
			return;

	clipboard_paste_region (content, sheet, dest_col, dest_row, paste_flags, time);
}

static void
sheet_move_column (Sheet *sheet, ColRowInfo *ci, int new_column)
{
	GList *rows, *column_cells, *l;
	int diff = new_column - ci->pos;
	
	/* remove the cells */
	column_cells = NULL;
	for (rows = ci->data; rows; rows = rows->next){
		Cell *cell = rows->data;
		
		sheet_cell_remove_from_hash (sheet, cell);
		column_cells = g_list_prepend (column_cells, cell);
	}
	
	/* Update the column position */
	ci->pos = new_column;
	
	/* Insert the cells back */
	for (l = column_cells; l; l = l->next){
		Cell *cell = l->data;
		
		sheet_cell_add_to_hash (sheet, cell);
		
		cell_relocate (cell, diff, 0);
	}
	g_list_free (column_cells);
}

/**
 * sheet_insert_col:
 * @sheet   The sheet
 * @col     At which position we want to insert
 * @count   The number of columns to be inserted
 */
void
sheet_insert_col (Sheet *sheet, int col, int count)
{
	GList   *cur_col, *deps;
	int   col_count;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);
	
	col_count = g_list_length (sheet->cols_info);
	if (col_count == 0)
		return;

	/* 1. Start scaning from the last column toward the goal column
	 *    moving all of the cells to their new location
	 */
	cur_col = g_list_nth (sheet->cols_info, col_count - 1);

	do {
		ColRowInfo *ci;
		int new_column;
		
		ci = cur_col->data;
		if (ci->pos < col)
			break;

		/* 1.1 Move every cell on this column count positions */
		new_column = ci->pos + count;

		if (new_column > SHEET_MAX_COLS-1){
			sheet_col_destroy (sheet, ci);
			
			/* Skip to next */
			cur_col = cur_col->prev;
			continue;
		}

		sheet_move_column (sheet, ci, new_column);

		/* 1.4 Go to the next column */
		cur_col = cur_col->prev;
	} while (cur_col);

	/* 2. Recompute dependencies */
	deps = region_get_dependencies (sheet, col, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);

	/* 3. Redraw */
	sheet_redraw_all (sheet);
	
}

/*
 * sheet_delete_col
 * @sheet   The sheet
 * @col     At which position we want to start deleting columns
 * @count   The number of columns to be deleted
 */
void
sheet_delete_col (Sheet *sheet, int col, int count)
{
	GList *cols, *deps, *destroy_list, *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* Is there any work to do? */
	if (g_list_length (sheet->cols_info) == 0)
		return;

	/* Assemble the list of columns to destroy */
	destroy_list = NULL;
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;
		
		if (ci->pos < col)
			continue;

		if (ci->pos > col+count-1)
			break;
		
		destroy_list = g_list_prepend (destroy_list, ci);
	}

	for (l = destroy_list; l; l = l->next){
		ColRowInfo *ci = l->data;

		sheet_col_destroy (sheet, ci);
	}
	g_list_free (destroy_list);

	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;

		if (ci->pos < col)
			continue;

		g_assert (ci->pos > col+count-1);
		sheet_move_column (sheet, ci, ci->pos-count);
	}
	
	/* Recompute dependencies */
	deps = region_get_dependencies (sheet, col, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);

	sheet_redraw_all (sheet);
}

/**
 * colrow_closest_above:
 *
 * Returns the closest column (from above) to pos
 */
static GList *
colrow_closest_above (GList *l, int pos)
{
	for (; l; l = l->next){
		ColRowInfo *info = l->data;

		if (info->pos >= pos)
			return l;
	}
	return NULL;
}

/**
 * sheet_shift_row:
 * @sheet the sheet
 * @row   row where the shifting takes place
 * @col   first column
 * @count numbers of columns to shift.  anegative numbers will
 *        delete count columns, positive number will insert
 *        count columns.
 */
void
sheet_shift_row (Sheet *sheet, int col, int row, int count)
{
	GList *cur_col, *deps, *l, *cell_list;
	int   col_count, new_column;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	col_count = g_list_length (sheet->cols_info);
	
	if (count < 0){
		sheet_clear_region (sheet, col, row, col - count - 1, row);
		cur_col = colrow_closest_above (sheet->cols_info, col);
	} else 
		cur_col = g_list_nth (sheet->cols_info, col_count - 1);


	/* If nothing interesting found, return */
	if (cur_col == NULL)
		return;

	cell_list = NULL;
	do {
		ColRowInfo *ci;
		
		ci = cur_col->data;
		if (count > 0){
			if (ci->pos < col)
				break;
		} else {
			if (ci->pos < col){
				cur_col = cur_col->next;
				continue;
			}
		}
			
		new_column = ci->pos + count;

		/* Search for this row */
		for (l = ci->data; l; l = l->next){
			Cell *cell = l->data;
			
			if (cell->row->pos > row)
				break;
			
			if (cell->row->pos < row)
				continue;

			cell_list = g_list_prepend (cell_list, cell);
		}

		/* Advance to the next column */
		if (count > 0)
			cur_col = cur_col->prev;
		else
			cur_col = cur_col->next;
	} while (cur_col);


	/* Now relocate the cells */
	l = g_list_nth (cell_list, g_list_length (cell_list)-1);
	for (; l; l = l->prev){
		Cell *cell = l->data;

		new_column = cell->col->pos + count;
		
		/* If it overflows, remove it */
		if (new_column > SHEET_MAX_COLS-1){
			sheet_cell_remove (sheet, cell);
			cell_destroy (cell);
			break;
		}
		
		/* Relocate the cell */
		sheet_cell_remove (sheet, cell);
		sheet_cell_add (sheet, cell, new_column, row);
		cell_relocate (cell, count, 0);
	}
	g_list_free (l);
	
	/* Check the dependencies and recompute them */
	deps = region_get_dependencies (sheet, col, row, SHEET_MAX_COLS-1, row);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);
	
	sheet_redraw_all (sheet);
}

void
sheet_shift_rows (Sheet *sheet, int col, int start_row, int end_row, int count)
{
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);
	g_return_if_fail (start_row <= end_row);
	
	for (i = start_row; i <= end_row; i++)
		sheet_shift_row (sheet, col, i, count);
}

/**
 * sheet_insert_row:
 * @sheet   The sheet
 * @row     At which position we want to insert
 * @count   The number of rows to be inserted
 */
void
sheet_insert_row (Sheet *sheet, int row, int count)
{
	GList *cell_store, *cols, *l, *rows, *deps, *destroy_list;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	cell_store = NULL;
	
	/* 1. Walk every column, see which cells are out of range */
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;
		GList *cells;
		
		for (cells = ci->data; cells; cells = cells->next){
			Cell *cell = cells->data;

			if (cell->row->pos < row)
				continue;

			/* If the new position is out of range, destroy the cell */
			if (cell->row->pos + count > SHEET_MAX_ROWS-1){
				sheet_cell_remove_to_eot (sheet, cells);

				/* Remove any trace of the tail that just got deleted */
				if (cells->prev)
					cells->prev->next = NULL;
				
				g_list_free (cells);
				break;
			}

			/* At this point we now we can move the cell safely */
			sheet_cell_remove_from_hash (sheet, cell);

			/* Keep track of it */
			cell_store = g_list_prepend (cell_store, cell);
		}
	}

	/* 2. Relocate the row information pointers, destroy overflowed rows */
	destroy_list = NULL;
	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri = rows->data;

		if (ri->pos < row)
			continue;

		if (ri->pos + count > SHEET_MAX_ROWS-1){
			destroy_list = g_list_prepend (destroy_list, ri);
			continue;
		}

		ri->pos += count;
	}

	/* Destroy those row infos that are gone */
	for (l = destroy_list; l; l = l->next){
		ColRowInfo *ri = l->data;

		sheet_row_destroy (sheet, ri);
	}
	g_list_free (destroy_list);
	
	/* 3. Put back the moved cells in their new spot */
	for (l = cell_store; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_add_to_hash (sheet, cell);
		
		cell_relocate (cell, 0, count);
	}

	g_list_free (cell_store);

	/* 4. Recompute any changes required */
	deps = region_get_dependencies (sheet, 0, row, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);
	
	/* 5. Redraw everything */
	sheet_redraw_all (sheet);
}

/**
 * sheet_delete_row:
 * @sheet   The sheet
 * @row     At which position we want to delete
 * @count   The number of rows to be deleted
 */
void
sheet_delete_row (Sheet *sheet, int row, int count)
{
	GList *destroy_list, *cols, *rows, *cell_store, *deps, *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	/* 1. Remove cells from hash tables and grab all dangling rows */
	cell_store = NULL;
	for (cols = sheet->cols_info; cols; cols = cols->next){
		ColRowInfo *ci = cols->data;
		GList *cells;

		destroy_list = NULL;
		for (cells = ci->data; cells; cells = cells->next){
			Cell *cell = cells->data;
			
			if (cell->row->pos < row)
				continue;
			
			sheet_cell_remove_from_hash (sheet, cell);
			
			if (cell->row->pos >= row && cell->row->pos <= row+count-1){
				destroy_list = g_list_prepend (destroy_list, cell);
				continue;
			}

			cell_store = g_list_prepend (cell_store, cell);
		}

		/* Destroy the cells in the range */
		for (l = destroy_list; l; l = l->next){
			Cell *cell = l->data;

			cell->col->data = g_list_remove (cell->col->data, cell);
			cell_destroy (cell);
		}

		g_list_free (destroy_list);
	}

	/* 2. Relocate row information pointers, destroy unused rows */
	destroy_list = NULL;
	for (rows = sheet->rows_info; rows; rows = rows->next){
		ColRowInfo *ri = rows->data;

		if (ri->pos < row)
			continue;

		if (ri->pos >= row && ri->pos <= row+count-1){
			destroy_list = g_list_prepend (destroy_list, ri);
			continue;
		}
		ri->pos -= count;
	}
	for (l = destroy_list; l; l = l->next){
		ColRowInfo *ri = l->data;

		sheet_row_destroy (sheet, ri);
	}
	g_list_free (destroy_list);

	/* 3. Put back the cells at their new location */
	for (l = cell_store; l; l = l->next){
		Cell *cell = l->data;

		sheet_cell_add_to_hash (sheet, cell);

		cell_relocate (cell, 0, -count);
	}
	g_list_free (cell_store);

	/* 4. Recompute dependencies */
	deps = region_get_dependencies (sheet, 0, row, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);

	/* 5. Redraw everything */
	sheet_redraw_all (sheet);
}

/**
 * sheet_shift_col:
 * @sheet the sheet
 * @row   first row
 * @col   column where the shifting takes place
 * @count numbers of rows to shift.  a negative numbers will
 *        delete count rows, positive number will insert
 *        count rows.
 */
void
sheet_shift_col (Sheet *sheet, int col, int row, int count)
{
	GList *row_list, *cur_row, *deps, *cell_list, *l;
	ColRowInfo *ci;
	int row_count;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	ci = sheet_col_get_info (sheet, col);

	/* Check if the column did exist, if not, then shift_col is a no-op */
	if (ci->pos != col)
		return;
	
	if (count < 0){
		sheet_clear_region (sheet, col, row, col, row -count - 1);
		ci = sheet_col_get_info (sheet, col);
		row_list = ci->data;
		cur_row = colrow_closest_above (row_list, row);
	} else {
		row_list = ci->data;
		row_count = g_list_length (row_list);
		cur_row = g_list_nth (row_list, row_count-1);
	}

	/* If nothing interesting found, return */
	if (cur_row == NULL)
		return;

	cell_list = NULL;
	do {
		Cell *cell = cur_row->data;
		int new_row;

		if (count > 0){
			if (cell->row->pos < row)
				break;
		} else {
			if (cell->row->pos < row){
				cur_row = cur_row->next;
				continue;
			}
		}

		new_row = cell->row->pos + count;

		cell_list = g_list_prepend (cell_list, cell);
		
		/* Advance to next row */
		if (count > 0)
			cur_row = cur_row->prev;
		else
			cur_row = cur_row->next;
	} while (cur_row);

	/* Relocate the cells */
	l = g_list_nth (cell_list, g_list_length (cell_list)-1);
	
	for (; l; l = l->prev){
		Cell *cell = l->data;
		int old_pos = cell->row->pos;
		int new_row = old_pos + count;

		sheet_cell_remove (sheet, cell);

		/* if it overflows */
		if (new_row > SHEET_MAX_ROWS-1){
			cell_destroy (cell);
			continue;
		}

		sheet_cell_add (sheet, cell, col, new_row);
		cell_relocate (cell, 0, count);
	}
	g_list_free (cell_list);

	/* Recompute dependencies on the changed data */
	deps = region_get_dependencies (sheet, col, row, col, SHEET_MAX_ROWS-1);
	cell_queue_recalc_list (deps);
	workbook_recalc (sheet->workbook);

	sheet_redraw_all (sheet);
}

/**
 * sheet_shift_cols:
 * @sheet the sheet
 * @start_col first column
 * @end_col   end column
 * @row       first row where the shifting takes place.
 * @count     numbers of rows to shift.  a negative numbers will
 *            delete count rows, positive number will insert
 *            count rows.
 */
void
sheet_shift_cols (Sheet *sheet, int start_col, int end_col, int row, int count)
{
	int i;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (count != 0);

	for (i = start_col; i <= end_col; i++)
		sheet_shift_col (sheet, i, row, count);
}
		 
void
sheet_style_attach (Sheet *sheet, int start_col, int start_row, int end_col, int end_row, Style *style)
{
	StyleRegion *sr;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (style != NULL);
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	sr = g_new (StyleRegion, 1);
	sr->range.start_col = start_col;
	sr->range.start_row = start_row;
	sr->range.end_col = end_col;
	sr->range.end_row = end_row;
	sr->style = style;
	
	sheet->style_list = g_list_prepend (sheet->style_list, sr);
}

/**
 * sheet_style_compute:
 * @sheet:   	 Which sheet we are looking up
 * @col:     	 column
 * @row:     	 row
 * @non_default: A pointer where we store the attributes
 *               the cell has which are not part of the
 *               default style.
 */
Style *
sheet_style_compute (Sheet *sheet, int col, int row, int *non_default)
{
	GList *l;
	Style *style;
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL); 

	style = style_new_empty ();
	
	/* Look in the styles applied to the sheet */
	for (l = sheet->style_list; l; l = l->next){
		StyleRegion *sr = l->data;
		int is_default_style = l->next == NULL;
		int flags;
		
		flags = style->valid_flags;

		if (range_contains (&sr->range, col, row)){
			style_merge_to (style, sr->style);
			if (style->valid_flags == STYLE_ALL){
				if (non_default){
					if (!is_default_style)
						*non_default = STYLE_ALL;
					else
						*non_default = flags;
				}
				return style;
			}
		}
	}

	g_warning ("Strange, no style available here\n");
	return style_new ();
}

void
sheet_make_cell_visible (Sheet *sheet, int col, int row)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_make_cell_visible (gsheet, col, row);
	}
}

void
sheet_cursor_move (Sheet *sheet, int col, int row)
{
	GList *l;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_accept_pending_input (sheet);

	sheet->cursor_col = col;
	sheet->cursor_row = row;
	
	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_cursor_set (gsheet, col, row);
		gnumeric_sheet_set_cursor_bounds (gsheet, col, row, col, row);
	}
	sheet_load_cell_val (sheet);
}

void
sheet_cursor_set (Sheet *sheet, int base_col, int base_row, int start_col, int start_row, int end_col, int end_row)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);
	
	sheet_accept_pending_input (sheet);
	
	sheet->cursor_col = base_col;
	sheet->cursor_row = base_row;

	for (l = sheet->sheet_views; l; l = l->next){
		GnumericSheet *gsheet = GNUMERIC_SHEET_VIEW (l->data);

		gnumeric_sheet_cursor_set (gsheet, base_col, base_row);
		gnumeric_sheet_set_cursor_bounds (
			gsheet,
			start_col, start_row,
			end_col, end_row);
	}
	sheet_load_cell_val (sheet);
}

void
sheet_fill_selection_with (Sheet *sheet, char *str)
{
	GList *l;
	int  col, row;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (str != NULL);

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;

		for (col = ss->start_col; col <= ss->end_col; col++)
			for (row = ss->start_row; row <= ss->end_row; row++)
				sheet_set_text (sheet, col, row, str);
	}
}

void
sheet_hide_cursor (Sheet *sheet)
{
	GList *l; 

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_hide_cursor (sheet_view);
	}
}

void
sheet_show_cursor (Sheet *sheet)
{
	GList *l; 

	for (l = sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = l->data;

		sheet_view_show_cursor (sheet_view);
	}
}

char *
cellref_name (CellRef *cell_ref, Sheet *eval_sheet, int eval_col, int eval_row)
{
	static char buffer [sizeof (long) * 4 + 4];
	char *p = buffer;
	int col, row;
	
	if (cell_ref->col_relative)
		col = eval_col + cell_ref->col;
	else {
		*p++ = '$';
		col = cell_ref->col;
	}
	
	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);
		
		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	if (cell_ref->row_relative)
		row = eval_row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	sprintf (p, "%d", row+1);

	/* If it is a non-local reference, add the path to the external sheet */
	if (cell_ref->sheet == eval_sheet || cell_ref->sheet == NULL)
		return g_strdup (buffer);
	else {
		Sheet *sheet = cell_ref->sheet;
		char *s;
		
		if (strchr (sheet->name, ' '))
			s = g_strconcat ("\"", sheet->name, "\"!", buffer, NULL);
		else
			s = g_strconcat (sheet->name, "!", buffer, NULL);

		return s;
	}

}

void
sheet_mark_clean (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet->modified = FALSE;
}

/**
 * sheet_lookup_by_name:
 * @sheet: Local sheet.
 * @name:  a sheet name.
 *
 * This routine parses @name for a reference to another sheet
 * in this workbook.  If this fails, it will try to parse a
 * filename in @name and load the given workbook and lookup
 * the sheet name from that workbook.
 *
 * The routine might return NULL.
 */
Sheet *
sheet_lookup_by_name (Sheet *base, char *name)
{
	Sheet *sheet;
	
	g_return_val_if_fail (base != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (base), NULL);

	/*
	 * FIXME: currently we only try to lookup the sheet name
	 * inside the workbook, we need to lookup external files as
	 * well.
	 */
	sheet = workbook_sheet_lookup (base->workbook, name);

	if (sheet)
		return sheet;

	return NULL;
}
