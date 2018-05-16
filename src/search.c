/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Copyright (C) 2001-2009 Morten Welinder (terra@gnome.org)
 */

#include <gnumeric-config.h>
#include <gnm-i18n.h>
#include <gnumeric.h>
#include <search.h>

#include <gutils.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <position.h>
#include <cell.h>
#include <number-match.h>
#include <value.h>
#include <sheet-object-cell-comment.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>
#include <stdlib.h>

static GObjectClass *parent_class;

typedef struct {
	GOSearchReplaceClass base_class;
} GnmSearchReplaceClass;

enum {
	PROP_0,
	PROP_IS_NUMBER,
	PROP_SEARCH_STRINGS,
	PROP_SEARCH_OTHER_VALUES,
	PROP_SEARCH_EXPRESSIONS,
	PROP_SEARCH_EXPRESSION_RESULTS,
	PROP_SEARCH_COMMENTS,
	PROP_SEARCH_SCRIPTS,
	PROP_INVERT,
	PROP_BY_ROW,
	PROP_QUERY,
	PROP_REPLACE_KEEP_STRINGS,
	PROP_SHEET,
	PROP_SCOPE,
	PROP_RANGE_TEXT
};

/* ------------------------------------------------------------------------- */

typedef struct {
	GnmCell *cell;
} GnmSearchReplaceValueResult;
static gboolean
gnm_search_replace_value (GnmSearchReplace *sr,
			  const GnmEvalPos *ep,
			  GnmSearchReplaceValueResult *res);

/* ------------------------------------------------------------------------- */

char *
gnm_search_normalize (const char *txt)
{
	return g_utf8_normalize (txt, -1, G_NORMALIZE_NFD);
}

static char *
gnm_search_normalize_result (const char *txt)
{
	return g_utf8_normalize (txt, -1, G_NORMALIZE_NFC);
}

/* ------------------------------------------------------------------------- */

static gboolean
check_number (GnmSearchReplace *sr)
{
	GODateConventions const *date_conv = sheet_date_conv (sr->sheet);
	GOSearchReplace *gosr = (GOSearchReplace *)sr;
	GnmValue *v = format_match_number (gosr->search_text, NULL, date_conv);

	if (v) {
		gnm_float f = value_get_as_float (v);
		if (f < 0) {
			sr->low_number = gnm_add_epsilon (f);
			sr->high_number = gnm_sub_epsilon (f);
		} else {
			sr->low_number = gnm_sub_epsilon (f);
			sr->high_number = gnm_add_epsilon (f);
		}
		value_release (v);
		return TRUE;
	} else {
		sr->low_number = sr->high_number = gnm_nan;
		return FALSE;
	}
}

static gboolean
gnm_search_match_value (GnmSearchReplace const *sr, GnmValue const *val)
{
	gnm_float f;
	if (!VALUE_IS_NUMBER (val))
		return FALSE;
	f = value_get_as_float (val);

	return (sr->low_number <= f && f <= sr->high_number);
}


char *
gnm_search_replace_verify (GnmSearchReplace *sr, gboolean repl)
{
	GError *error = NULL;
	GOSearchReplace *gosr = (GOSearchReplace *)sr;
	g_return_val_if_fail (sr != NULL, NULL);

	if (!go_search_replace_verify (gosr, repl, &error)) {
		char *msg = g_strdup (error->message);
		g_error_free (error);
		return msg;
	}

	if (sr->is_number && gosr->is_regexp)
		return g_strdup (_("Searching for regular expressions and numbers are mutually exclusive."));

	if (sr->is_number) {
		if (!check_number (sr))
			return g_strdup (_("The search text must be a number."));
	}

	if (sr->scope == GNM_SRS_RANGE) {
		GSList *range_list;

		if (!sr->range_text || sr->range_text[0] == 0)
			return g_strdup (_("You must specify a range to search."));

		if ((range_list = global_range_list_parse (sr->sheet, sr->range_text))
		    == NULL)
			return g_strdup (_("The search range is invalid."));
		range_list_destroy (range_list);
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */

static int
cb_order_sheet_row_col (void const *_a, void const *_b)
{
	GnmEvalPos const *a = *(GnmEvalPos const **)_a;
	GnmEvalPos const *b = *(GnmEvalPos const **)_b;
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
cb_order_sheet_col_row (void const *_a, void const *_b)
{
	GnmEvalPos const *a = *(GnmEvalPos const **)_a;
	GnmEvalPos const *b = *(GnmEvalPos const **)_b;
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
search_collect_cells_cb (GnmCellIter const *iter, gpointer user)
{
	GPtrArray *cells = user;
	GnmEvalPos *ep = g_new (GnmEvalPos, 1);

	g_ptr_array_add (cells, eval_pos_init_cell (ep, iter->cell));

	return NULL;
}

/**
 * gnm_search_collect_cells:
 * @sr: #GnmSearchReplace
 *
 * Collect a list of all cells subject to search.
 * Returns: (element-type GnmEvalPos) (transfer full): the newly created array.
 **/
GPtrArray *
gnm_search_collect_cells (GnmSearchReplace *sr)
{
	GPtrArray *cells;

	switch (sr->scope) {
	case GNM_SRS_WORKBOOK:
		g_return_val_if_fail (sr->sheet != NULL, NULL);
		cells = workbook_cells (sr->sheet->workbook, TRUE,
					GNM_SHEET_VISIBILITY_HIDDEN);
		break;

	case GNM_SRS_SHEET:
		cells = sheet_cell_positions (sr->sheet, TRUE);
		break;

	case GNM_SRS_RANGE:
	{
		GSList *range_list;
		GnmEvalPos ep;
		cells = g_ptr_array_new ();
		range_list = global_range_list_parse (sr->sheet, sr->range_text);
		global_range_list_foreach (range_list,
			   eval_pos_init_sheet (&ep, sr->sheet),
			   CELL_ITER_IGNORE_BLANK,
			   search_collect_cells_cb, cells);
		range_list_destroy (range_list);
		break;
	}

	default:
		cells = NULL;
		g_assert_not_reached ();
	}

	/* Sort our cells.  */
	g_ptr_array_sort (cells,
			  sr->by_row ? cb_order_sheet_row_col : cb_order_sheet_col_row);

	return cells;
}

/**
 * gnm_search_collect_cells_free:
 * @cells: (element-type GnmEvalPos) (transfer full):
 */
void
gnm_search_collect_cells_free (GPtrArray *cells)
{
	unsigned i;

	for (i = 0; i < cells->len; i++)
		g_free (g_ptr_array_index (cells, i));
	g_ptr_array_free (cells, TRUE);
}

/* ------------------------------------------------------------------------- */
/**
 * gnm_search_filter_matching:
 * @sr: The search spec.
 * @cells: (element-type GnmEvalPos): Cell positions to filter, presumably a result of gnm_search_collect_cells.
 *
 * Returns: (element-type GnmSearchFilterResult) (transfer full): matches
 */

GPtrArray *
gnm_search_filter_matching (GnmSearchReplace *sr, const GPtrArray *cells)
{
	unsigned i;
	GPtrArray *result = g_ptr_array_new ();

	if (sr->is_number)
		check_number (sr);

	for (i = 0; i < cells->len; i++) {
		GnmSearchReplaceCellResult cell_res;
		GnmSearchReplaceValueResult value_res;
		GnmSearchReplaceCommentResult comment_res;
		gboolean found;
		const GnmEvalPos *ep = g_ptr_array_index (cells, i);

		found = gnm_search_replace_cell (sr, ep, FALSE, &cell_res);
		g_free (cell_res.old_text);
		if (cell_res.cell != NULL && found != sr->invert) {
			GnmSearchFilterResult *item = g_new (GnmSearchFilterResult, 1);
			item->ep = *ep;
			item->locus = GNM_SRL_CONTENTS;
			g_ptr_array_add (result, item);
		}

		found = gnm_search_replace_value (sr, ep, &value_res);
		if (value_res.cell != NULL && gnm_cell_has_expr (value_res.cell) && found != sr->invert) {
			GnmSearchFilterResult *item = g_new (GnmSearchFilterResult, 1);
			item->ep = *ep;
			item->locus = GNM_SRL_VALUE;
			g_ptr_array_add (result, item);
		}

		found = gnm_search_replace_comment (sr, ep, FALSE, &comment_res);
		if (comment_res.comment != NULL && found != sr->invert) {
			GnmSearchFilterResult *item = g_new (GnmSearchFilterResult, 1);
			item->ep = *ep;
			item->locus = GNM_SRL_COMMENT;
			g_ptr_array_add (result, item);
		}
	}

	return result;
}

/**
 * gnm_search_filter_matching_free:
 * @matches: (element-type GnmSearchFilterResult) (transfer full): matches
 */
void
gnm_search_filter_matching_free (GPtrArray *matches)
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
			    GnmSearchReplaceCommentResult *res)
{
	gboolean found;
	char *norm_text;

	g_return_val_if_fail (res, FALSE);

	res->comment = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr, FALSE);

	if (!sr->search_comments) return FALSE;
	if (sr->is_number) return FALSE;

	res->comment = sheet_get_comment (ep->sheet, &ep->eval);
	if (!res->comment) return FALSE;

	res->old_text = cell_comment_text_get (res->comment);

	norm_text = gnm_search_normalize (res->old_text);

	if (repl) {
		res->new_text = go_search_replace_string (GO_SEARCH_REPLACE (sr),
							  norm_text);
		found = (res->new_text != NULL);
		if (found) {
			char *norm = gnm_search_normalize_result (res->new_text);
			g_free (res->new_text);
			res->new_text = norm;
		}
	} else
		found = go_search_match_string (GO_SEARCH_REPLACE (sr),
						norm_text);

	g_free (norm_text);

	return found;
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_search_replace_cell (GnmSearchReplace *sr,
			 const GnmEvalPos *ep,
			 gboolean repl,
			 GnmSearchReplaceCellResult *res)
{
	GnmCell *cell;
	GnmValue *v;
	gboolean is_expr, is_value, is_string, is_other;
	gboolean found = FALSE;

	g_return_val_if_fail (res, FALSE);

	res->cell = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr, FALSE);

	cell = res->cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	if (!cell) return FALSE;

	v = cell->value;

	is_expr = gnm_cell_has_expr (cell);
	is_value = !is_expr && !gnm_cell_is_empty (cell) && v;
	is_string = is_value && (VALUE_IS_STRING (v));
	is_other = is_value && !is_string;

	if (sr->is_number) {
		if (!is_value || !VALUE_IS_NUMBER (v))
			return FALSE;
		return gnm_search_match_value (sr, v);
	}

	if ((is_expr && sr->search_expressions) ||
	    (is_string && sr->search_strings) ||
	    (is_other && sr->search_other_values)) {
		char *actual_src;
		gboolean initial_quote;

		res->old_text = gnm_cell_get_entered_text (cell);
		initial_quote = (is_string && res->old_text[0] == '\'');

		actual_src = gnm_search_normalize (res->old_text + initial_quote);

		if (repl) {
			res->new_text = go_search_replace_string (GO_SEARCH_REPLACE (sr),
								  actual_src);
			if (res->new_text) {
				char *norm = gnm_search_normalize_result (res->new_text);
				g_free (res->new_text);
				res->new_text = norm;

				if (sr->replace_keep_strings && is_string) {
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
				found = TRUE;
			}
		} else
			found = go_search_match_string (GO_SEARCH_REPLACE (sr), actual_src);

		g_free (actual_src);
	}

	return found;
}

/* ------------------------------------------------------------------------- */

static gboolean
gnm_search_replace_value (GnmSearchReplace *sr,
			  const GnmEvalPos *ep,
			  GnmSearchReplaceValueResult *res)
{
	GnmCell *cell;

	g_return_val_if_fail (res, FALSE);

	res->cell = NULL;

	g_return_val_if_fail (sr, FALSE);

	if (!sr->search_expression_results)
		return FALSE;

	cell = res->cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	if (!cell || !gnm_cell_has_expr (cell) || !cell->value)
		return FALSE;
	else if (sr->is_number) {
		return gnm_search_match_value (sr, cell->value);
	} else {
		char *val = gnm_search_normalize (value_peek_string (cell->value));
		gboolean res = go_search_match_string (GO_SEARCH_REPLACE (sr), val);
		g_free (val);
		return res;
	}
}

/* ------------------------------------------------------------------------- */

void
gnm_search_replace_query_fail (GnmSearchReplace *sr,
			       const GnmSearchReplaceCellResult *res)
{
	if (!sr->query_func)
		return;

	sr->query_func (GNM_SRQ_FAIL, sr,
			res->cell, res->old_text, res->new_text);
}

int
gnm_search_replace_query_cell (GnmSearchReplace *sr,
			       const GnmSearchReplaceCellResult *res)
{
	if (!sr->query || !sr->query_func)
		return GTK_RESPONSE_YES;

	return sr->query_func (GNM_SRQ_QUERY, sr,
			       res->cell, res->old_text, res->new_text);
}


int
gnm_search_replace_query_comment (GnmSearchReplace *sr,
				  const GnmEvalPos *ep,
				  const GnmSearchReplaceCommentResult *res)
{
	if (!sr->query || !sr->query_func)
		return GTK_RESPONSE_YES;

	return sr->query_func (GNM_SRQ_QUERY_COMMENT, sr,
			       ep->sheet, &ep->eval,
			       res->old_text, res->new_text);
}

/* ------------------------------------------------------------------------- */

GType
gnm_search_replace_scope_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static const GEnumValue values[] = {
			{ GNM_SRS_WORKBOOK, "GNM_SRS_WORKBOOK", "workbook" },
			{ GNM_SRS_SHEET,    "GNM_SRS_SHEET",    "sheet" },
			{ GNM_SRS_RANGE,    "GNM_SRS_RANGE",    "range" },
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmSearchReplaceScope", values);
	}
	return etype;
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_init (GObject *obj)
{
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_get_property (GObject     *object,
				 guint        property_id,
				 GValue      *value,
				 GParamSpec  *pspec)
{
	GnmSearchReplace *sr = (GnmSearchReplace *)object;

	switch (property_id) {
	case PROP_IS_NUMBER:
		g_value_set_boolean (value, sr->is_number);
		break;
	case PROP_SEARCH_STRINGS:
		g_value_set_boolean (value, sr->search_strings);
		break;
	case PROP_SEARCH_OTHER_VALUES:
		g_value_set_boolean (value, sr->search_other_values);
		break;
	case PROP_SEARCH_EXPRESSIONS:
		g_value_set_boolean (value, sr->search_expressions);
		break;
	case PROP_SEARCH_EXPRESSION_RESULTS:
		g_value_set_boolean (value, sr->search_expression_results);
		break;
	case PROP_SEARCH_COMMENTS:
		g_value_set_boolean (value, sr->search_comments);
		break;
	case PROP_SEARCH_SCRIPTS:
		g_value_set_boolean (value, sr->search_scripts);
		break;
	case PROP_INVERT:
		g_value_set_boolean (value, sr->invert);
		break;
	case PROP_BY_ROW:
		g_value_set_boolean (value, sr->by_row);
		break;
	case PROP_QUERY:
		g_value_set_boolean (value, sr->query);
		break;
	case PROP_REPLACE_KEEP_STRINGS:
		g_value_set_boolean (value, sr->replace_keep_strings);
		break;
	case PROP_SHEET:
		g_value_set_object (value, sr->sheet);
		break;
	case PROP_SCOPE:
		g_value_set_enum (value, sr->scope);
		break;
	case PROP_RANGE_TEXT:
		g_value_set_string (value, sr->range_text);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_set_sheet (GnmSearchReplace *sr, Sheet *sheet)
{
	if (sheet)
		g_object_ref (sheet);
	if (sr->sheet)
		g_object_unref (sr->sheet);
	sr->sheet = sheet;
}

static void
gnm_search_replace_set_range_text (GnmSearchReplace *sr, char const *text)
{
	char *text_copy = g_strdup (text);
	g_free (sr->range_text);
	sr->range_text = text_copy;
}

static void
gnm_search_replace_set_property (GObject      *object,
				 guint         property_id,
				 GValue const *value,
				 GParamSpec   *pspec)
{
	GnmSearchReplace *sr = (GnmSearchReplace *)object;

	switch (property_id) {
	case PROP_IS_NUMBER:
		sr->is_number = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_STRINGS:
		sr->search_strings = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_OTHER_VALUES:
		sr->search_other_values = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_EXPRESSIONS:
		sr->search_expressions = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_EXPRESSION_RESULTS:
		sr->search_expression_results = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_COMMENTS:
		sr->search_comments = g_value_get_boolean (value);
		break;
	case PROP_SEARCH_SCRIPTS:
		sr->search_scripts = g_value_get_boolean (value);
		break;
	case PROP_INVERT:
		sr->invert = g_value_get_boolean (value);
		break;
	case PROP_BY_ROW:
		sr->by_row = g_value_get_boolean (value);
		break;
	case PROP_QUERY:
		sr->query = g_value_get_boolean (value);
		break;
	case PROP_REPLACE_KEEP_STRINGS:
		sr->replace_keep_strings = g_value_get_boolean (value);
		break;
	case PROP_SHEET:
		gnm_search_replace_set_sheet (sr, g_value_get_object (value));
		break;
	case PROP_SCOPE:
		sr->scope = g_value_get_enum (value);
		break;
	case PROP_RANGE_TEXT:
		gnm_search_replace_set_range_text (sr, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_finalize (GObject *obj)
{
	GnmSearchReplace *sr = (GnmSearchReplace *)obj;

	gnm_search_replace_set_sheet (sr, NULL);
	g_free (sr->range_text);

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* ------------------------------------------------------------------------- */

static void
gnm_search_replace_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_search_replace_finalize;
	gobject_class->get_property = gnm_search_replace_get_property;
	gobject_class->set_property = gnm_search_replace_set_property;

	g_object_class_install_property
		(gobject_class,
		 PROP_IS_NUMBER,
		 g_param_spec_boolean ("is-number",
				       P_("Is Number"),
				       P_("Search for Specific Number Regardless of Formatting?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_STRINGS,
		 g_param_spec_boolean ("search-strings",
				       P_("Search Strings"),
				       P_("Should strings be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_OTHER_VALUES,
		 g_param_spec_boolean ("search-other-values",
				       P_("Search Other Values"),
				       P_("Should non-strings be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_EXPRESSIONS,
		 g_param_spec_boolean ("search-expressions",
				       P_("Search Expressions"),
				       P_("Should expressions be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_EXPRESSION_RESULTS,
		 g_param_spec_boolean ("search-expression-results",
				       P_("Search Expression Results"),
				       P_("Should the results of expressions be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_COMMENTS,
		 g_param_spec_boolean ("search-comments",
				       P_("Search Comments"),
				       P_("Should cell comments be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SEARCH_SCRIPTS,
		 g_param_spec_boolean ("search-scripts",
				       P_("Search Scripts"),
				       P_("Should scrips (workbook, and worksheet) be searched?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_INVERT,
		 g_param_spec_boolean ("invert",
				       P_("Invert"),
				       P_("Collect non-matching items"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_BY_ROW,
		 g_param_spec_boolean ("by-row",
				       P_("By Row"),
				       P_("Is the search order by row?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_QUERY,
		 g_param_spec_boolean ("query",
				       P_("Query"),
				       P_("Should we query for each replacement?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_REPLACE_KEEP_STRINGS,
		 g_param_spec_boolean ("replace-keep-strings",
				       P_("Keep Strings"),
				       P_("Should replacement keep strings as strings?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SHEET,
		 g_param_spec_object ("sheet",
				      P_("Sheet"),
				      P_("The sheet in which to search."),
				      GNM_SHEET_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_SCOPE,
		 g_param_spec_enum ("scope",
				    P_("Scope"),
				    P_("Where to search."),
				    GNM_SEARCH_REPLACE_SCOPE_TYPE,
				    GNM_SRS_SHEET,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_RANGE_TEXT,
		 g_param_spec_string ("range-text",
				      P_("Range as Text"),
				      P_("The range in which to search."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
}

/* ------------------------------------------------------------------------- */

GSF_CLASS (GnmSearchReplace, gnm_search_replace,
	   gnm_search_replace_class_init, gnm_search_replace_init, GO_TYPE_SEARCH_REPLACE)
