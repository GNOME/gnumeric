/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "search.h"

#include "gutils.h"
#include "ranges.h"
#include "sheet.h"
#include "workbook.h"
#include "position.h"
#include "cell.h"
#include "value.h"
#include "sheet-object-cell-comment.h"
#include <gsf/gsf-impl-utils.h>

#include <string.h>
#include <stdlib.h>

static GObjectClass *parent_class;

typedef struct {
	GoSearchReplaceClass base_class;
} GnmSearchReplaceClass;

enum {
	PROP_0
};

/* ------------------------------------------------------------------------- */

char *
gnm_search_replace_verify (GnmSearchReplace *sr, gboolean repl)
{
	GError *error = NULL;
	g_return_val_if_fail (sr != NULL, NULL);

	if (!go_search_replace_verify (GO_SEARCH_REPLACE (sr), repl, &error)) {
		char *msg = g_strdup (error->message);
		g_error_free (error);
		return msg;
	}

	if (sr->scope == SRS_range) {
		GSList *range_list;

		if (!sr->range_text || sr->range_text[0] == 0)
			return g_strdup (_("You must specify a range to search."));

		if ((range_list = global_range_list_parse (sr->curr_sheet, sr->range_text))
		    == NULL)
			return g_strdup (_("The search range is invalid."));
		range_list_destroy (range_list);
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */

static int
cb_order_sheet_row_col (const void *_a, const void *_b)
{
	const GnmEvalPos *a = *(const GnmEvalPos **)_a;
	const GnmEvalPos *b = *(const GnmEvalPos **)_b;
	int i;

	i = strcmp (a->sheet->name_unquoted_collate_key,
	            b->sheet->name_unquoted_collate_key);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	return i;
}

static int
cb_order_sheet_col_row (const void *_a, const void *_b)
{
	const GnmEvalPos *a = *(const GnmEvalPos **)_a;
	const GnmEvalPos *b = *(const GnmEvalPos **)_b;
	int i;

	i = strcmp (a->sheet->name_unquoted_collate_key,
	            b->sheet->name_unquoted_collate_key);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	return i;
}

static GnmValue *
search_collect_cells_cb (Sheet *sheet, int col, int row,
			 GnmCell *cell, GPtrArray *cells)
{
	GnmEvalPos *ep = g_new (GnmEvalPos, 1);

	ep->sheet = sheet;
	ep->eval.col = col;
	ep->eval.row = row;

	g_ptr_array_add (cells, ep);

	return NULL;

}

/* Collect a list of all cells subject to search.  */
GPtrArray *
search_collect_cells (GnmSearchReplace *sr, Sheet *sheet)
{
	GPtrArray *cells;

	switch (sr->scope) {
	case SRS_workbook:
		cells = workbook_cells (sheet->workbook, TRUE);
		break;

	case SRS_sheet:
		cells = sheet_cells (sheet,
				     0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
				     TRUE);
		break;

	case SRS_range:
	{
		GSList *range_list;
		GnmEvalPos ep;
		cells = g_ptr_array_new ();
		range_list = global_range_list_parse (sr->curr_sheet, sr->range_text);
		global_range_list_foreach (range_list,
			   eval_pos_init_sheet (&ep, sr->curr_sheet),
			   CELL_ITER_IGNORE_BLANK,
			   (CellIterFunc) &search_collect_cells_cb, cells);
		range_list_destroy (range_list);
		break;
	}

	default:
		cells = NULL;
		g_assert_not_reached ();
	}

	/* Sort our cells.  */
	qsort (&g_ptr_array_index (cells, 0),
	       cells->len,
	       sizeof (gpointer),
	       sr->by_row ? cb_order_sheet_row_col : cb_order_sheet_col_row);

	return cells;
}

void
search_collect_cells_free (GPtrArray *cells)
{
	unsigned i;

	for (i = 0; i < cells->len; i++)
		g_free (g_ptr_array_index (cells, i));
	g_ptr_array_free (cells, TRUE);
}

/* ------------------------------------------------------------------------- */
/*
 * sr: The search spec.
 * cells: An array of GnmEvalPos*, presumably a result of search_collect_cells.
 *
 * Returns an array of SearchFilterResult*s, which the caller must free.
 */

GPtrArray *
search_filter_matching (GnmSearchReplace *sr, const GPtrArray *cells)
{
	unsigned i;
	GPtrArray *result = g_ptr_array_new ();

	for (i = 0; i < cells->len; i++) {
		SearchReplaceCellResult cell_res;
		SearchReplaceValueResult value_res;
		SearchReplaceCommentResult comment_res;
		gboolean found;
		const GnmEvalPos *ep = g_ptr_array_index (cells, i);

		found = gnm_search_replace_cell (sr, ep, FALSE, &cell_res);
		g_free (cell_res.old_text);
		if (found) {
			SearchFilterResult *item = g_new (SearchFilterResult, 1);
			item->ep = *ep;
			item->locus = SRL_contents;
			g_ptr_array_add (result, item);
		}

		if (gnm_search_replace_value (sr, ep, &value_res)) {
			SearchFilterResult *item = g_new (SearchFilterResult, 1);
			item->ep = *ep;
			item->locus = SRL_value;
			g_ptr_array_add (result, item);
		}

		if (gnm_search_replace_comment (sr, ep, FALSE, &comment_res)) {
			SearchFilterResult *item = g_new (SearchFilterResult, 1);
			item->ep = *ep;
			item->locus = SRL_commment;
			g_ptr_array_add (result, item);
		}
	}

	return result;
}

void
search_filter_matching_free (GPtrArray *matches)
{
	unsigned i;
	for (i = 0; i < matches->len; i++)
		g_free (g_ptr_array_index (matches, i));
	g_ptr_array_free (matches, TRUE);
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_search_replace_comment (GnmSearchReplace *sr,
			    const GnmEvalPos *ep,
			    gboolean repl,
			    SearchReplaceCommentResult *res)
{
	g_return_val_if_fail (res, FALSE);

	res->comment = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr, FALSE);

	if (!sr->search_comments) return FALSE;

	res->comment = cell_has_comment_pos (ep->sheet, &ep->eval);
	if (!res->comment) return FALSE;

	res->old_text = cell_comment_text_get (res->comment);

	if (repl) {
		res->new_text = go_search_replace_string (GO_SEARCH_REPLACE (sr),
							  res->old_text);
		return (res->new_text != NULL);
	} else
		return go_search_match_string (GO_SEARCH_REPLACE (sr),
					       res->old_text);
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_search_replace_cell (GnmSearchReplace *sr,
			 const GnmEvalPos *ep,
			 gboolean repl,
			 SearchReplaceCellResult *res)
{
	GnmCell *cell;
	GnmValue *v;
	gboolean is_expr, is_value, is_string, is_other;

	g_return_val_if_fail (res, FALSE);

	res->cell = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr, FALSE);

	cell = res->cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	if (!cell) return FALSE;

	v = cell->value;

	is_expr = cell_has_expr (cell);
	is_value = !is_expr && !cell_is_empty (cell) && v;
	is_string = is_value && (v->type == VALUE_STRING);
	is_other = is_value && !is_string;

	if ((is_expr && sr->search_expressions) ||
	    (is_string && sr->search_strings) ||
	    (is_other && sr->search_other_values)) {
		const char *actual_src;
		gboolean initial_quote;

		res->old_text = cell_get_entered_text (cell);
		initial_quote = (is_value && res->old_text[0] == '\'');

		actual_src = res->old_text + (initial_quote ? 1 : 0);

		if (repl) {
			res->new_text = go_search_replace_string (GO_SEARCH_REPLACE (sr),
								  actual_src);
			if (res->new_text) {
				if (initial_quote) {
					/*
					 * The initial quote was not part of the s-a-r,
					 * so tack it back on.
					 */
					char *tmp = g_new (char, strlen (res->new_text) + 2);
					tmp[0] = '\'';
					strcpy (tmp + 1, res->new_text);
					g_free (res->new_text);
					res->new_text = tmp;
				}
				return TRUE;
			}
		} else
			return go_search_match_string (GO_SEARCH_REPLACE (sr), actual_src);
	}

	return FALSE;
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_search_replace_value (GnmSearchReplace *sr,
			  const GnmEvalPos *ep,
			  SearchReplaceValueResult *res)
{
	GnmCell *cell;

	g_return_val_if_fail (res, FALSE);

	res->cell = NULL;

	g_return_val_if_fail (sr, FALSE);

	if (!sr->search_expression_results)
		return FALSE;

	cell = res->cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	if (!cell || !cell_has_expr (cell) || !cell->value)
		return FALSE;
	else {
		char *val = value_get_as_string (cell->value);
		gboolean res = go_search_match_string (GO_SEARCH_REPLACE (sr), val);
		g_free (val);
		return res;
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_init (GObject *obj)
{
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_finalize (GObject *obj)
{
	GnmSearchReplace *sr = (GnmSearchReplace *)obj;

	g_free (sr->range_text);	

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_search_replace_finalize;
}

/* ------------------------------------------------------------------------- */

GSF_CLASS (GnmSearchReplace, gnm_search_replace,
	   gnm_search_replace_class_init, gnm_search_replace_init, GO_SEARCH_REPLACE_TYPE)
