/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "search.h"

#include "ranges.h"
#include "sheet.h"
#include "workbook.h"
#include "position.h"
#include "cell.h"
#include "value.h"
#include "sheet-object-cell-comment.h"

#include <string.h>
#include <libgnome/gnome-i18n.h>
#include <ctype.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */

SearchReplace *
search_replace_new (void)
{
	return g_new0 (SearchReplace, 1);
}

/* ------------------------------------------------------------------------- */

void
search_replace_free (SearchReplace *sr)
{
	g_free (sr->search_text);
	g_free (sr->replace_text);
	g_free (sr->range_text);
	if (sr->comp_search) {
		regfree (sr->comp_search);
		g_free (sr->comp_search);
	}
	g_free (sr);
}

/* ------------------------------------------------------------------------- */

static int
search_replace_compile (SearchReplace *sr, gboolean repl)
{
	const char *pattern;
	char *tmp;
	int flags = 0;
	int res;

	if (sr->comp_search) {
		regfree (sr->comp_search);
		g_free (sr->comp_search);
	}

	if (sr->is_regexp) {
		pattern = sr->search_text;
		tmp = NULL;
		sr->plain_replace =
			repl && (strchr (sr->replace_text, '$') == 0 &&
				 strchr (sr->replace_text, '\\') == 0);
	} else {
		/*
		 * Create a regular expression equivalent to the search
		 * string.  (Thus hoping the regular expression search
		 * routines are pretty good.)
		 */

		const char *src = sr->search_text;
		char *dst = tmp = g_new (char, strlen (src) * 2 + 1);
		pattern = tmp;

		for (; *src; src++) {
			switch (*src) {
			case '.': case '[': case '\\':
			case '*': case '^': case '$':
				*dst++ = '\\';
				/* Fall through. */
			default:
				*dst++ = *src;
				break;
			}
		}
		*dst = 0;

		sr->plain_replace = TRUE;
	}

	if (sr->ignore_case) flags |= REG_ICASE;

	sr->comp_search = g_new0 (regex_t, 1);
	res = regcomp (sr->comp_search, pattern, flags);

	g_free (tmp);

	return res;
}

/* ------------------------------------------------------------------------- */

SearchReplace *
search_replace_copy (const SearchReplace *sr)
{
	SearchReplace *dst = search_replace_new ();
	gboolean repl = (sr->replace_text != NULL);

	*dst = *sr;
	if (sr->search_text) dst->search_text = g_strdup (sr->search_text);
	if (sr->replace_text) dst->replace_text = g_strdup (sr->replace_text);
	if (sr->range_text) dst->range_text = g_strdup (sr->range_text);
	if (sr->comp_search) {
		dst->comp_search = NULL;
		search_replace_compile (dst, repl);
	}
	return dst;
}

/* ------------------------------------------------------------------------- */

char *
search_replace_verify (SearchReplace *sr, gboolean repl)
{
	int err;
	g_return_val_if_fail (sr != NULL, NULL);

	if (!sr->search_text || sr->search_text[0] == 0)
		return g_strdup (_("Search string must not be empty."));

	if (repl && !sr->replace_text)
		return g_strdup (_("Replacement string must be set."));

	if (sr->scope == SRS_range) {
		GSList *range_list;

		if (!sr->range_text || sr->range_text[0] == 0)
			return g_strdup (_("You must specify a range to search."));

		if ((range_list = global_range_list_parse (sr->curr_sheet, sr->range_text))
		    == NULL)
			return g_strdup (_("The search range is invalid."));
		range_list_destroy (range_list);
	}

	err = search_replace_compile (sr, repl);
	if (err)
		return g_strdup (_("Invalid search pattern."));

	if (repl && !sr->plain_replace) {
		const char *s;

		for (s = sr->replace_text; *s; s++) {
			switch (*s) {
			case '$':
				s++;
				switch (*s) {
				case '1': case '2': case '3': case '4': case '5':
				case '6': case '7': case '8': case '9':
				{
					int n = *s - '0';
					if (n > (int)sr->comp_search->re_nsub)
						return g_strdup (_("Invalid $-specification in replacement."));
					break;
				}
				default:
					return g_strdup (_("Invalid $-specification in replacement."));
				}
				break;
			case '\\':
				if (s[1] == 0)
					return g_strdup (_("Invalid trailing backslash in replacement."));
				s++;
				break;
			}
		}
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */

static gboolean
match_is_word (SearchReplace *sr, const char *src,
	       const regmatch_t *pm, gboolean bolp)
{
	/* The empty string is not a word.  */
	if (pm->rm_so == pm->rm_eo)
		return FALSE;

	if (pm->rm_so > 0 || !bolp) {
		/* We get here when something actually preceded the match.  */
		char c_pre = src[pm->rm_so - 1];
		if (isalnum ((unsigned char)c_pre))
			return FALSE;
	}

	{
		char c_post = src[pm->rm_eo];
		if (c_post != 0 && isalnum ((unsigned char)c_post))
			return FALSE;
	}

	return TRUE;
}

/* ------------------------------------------------------------------------- */

static char *
calculate_replacement (SearchReplace *sr, const char *src, const regmatch_t *pm)
{
	char *res;

	if (sr->plain_replace) {
		res = g_strdup (sr->replace_text);
	} else {
		const char *s;
		GString *gres = g_string_sized_new (strlen (sr->replace_text));

		for (s = sr->replace_text; *s; s++) {
			switch (*s) {
			case '$':
			{
				int n = s[1] - '0';
				s++;

				g_assert (n > 0 && n <= (int)sr->comp_search->re_nsub);
				g_string_append_len (gres,
						     src + pm[n].rm_so,
						     pm[n].rm_eo - pm[n].rm_so);
				break;
			}
			case '\\':
				s++;
				g_assert (*s != 0);
				g_string_append_c (gres, *s);
				break;
			default:
				g_string_append_c (gres, *s);
				break;
			}
		}

		res = gres->str;
		g_string_free (gres, FALSE);
	}

	/*
	 * Try to preserve the case during replacement, i.e., do the
	 * following substitutions:
	 *
	 * search -> replace
	 * Search -> Replace
	 * SEARCH -> REPLACE
	 * TheSearch -> TheReplace
	 */
	if (sr->preserve_case) {
		gboolean is_upper, is_capital, has_letter;
		int i;

		is_upper = TRUE;
		has_letter = FALSE;
		for (i = pm->rm_so; i < pm->rm_eo; i++) {
			unsigned char c = (unsigned char)src[i];
			if (isalpha (c)) {
				has_letter = TRUE;
				if (!isupper (c)) {
					is_upper = FALSE;
					break;
				}
			}
		}
		if (!has_letter) is_upper = FALSE;

		if (!is_upper && has_letter) {
			gboolean up = TRUE;
			is_capital = TRUE;
			for (i = pm->rm_so; i < pm->rm_eo; i++) {
				unsigned char c = (unsigned char)src[i];
				if (isalpha (c)) {
					if (up ? !isupper (c) : !islower (c)) {
						is_capital = FALSE;
						break;
					}
					up = FALSE;
				} else
					up = TRUE;
			}
		} else
			is_capital = FALSE;

		if (is_upper) {
			unsigned char *p = (unsigned char *)res;
			while (*p) {
				*p = toupper (*p);
				p++;
			}
		} else if (is_capital) {
			gboolean up = TRUE;
			unsigned char *p = (unsigned char *)res;
			for (; *p; p++) {
				if (isalpha (*p)) {
					*p = up ? toupper (*p) : tolower (*p);
					up = FALSE;
				} else
					up = TRUE;
			}
		}
	}

	return res;
}

/* ------------------------------------------------------------------------- */

gboolean
search_match_string (SearchReplace *sr, const char *src)
{
	int flags = 0;

	g_return_val_if_fail (sr && sr->comp_search, FALSE);

	while (1) {
		regmatch_t match;
		int ret = regexec (sr->comp_search, src, 1, &match, flags);

		switch (ret) {
		case 0:
			if (!sr->match_words)
				return TRUE;

			if (match_is_word (sr, src, &match, (flags & REG_NOTBOL) != 0))
				return TRUE;

			/*
			 * We had a match, but it's not a word.  Pretend we saw
			 * a one-character match and continue after that.
			 */
			flags |= REG_NOTBOL;
			src += match.rm_so + 1;
			break;

		case REG_NOMATCH:
			return FALSE;

		default:
			g_error ("Unexpected error code from regexec: %d.", ret);
			return FALSE;
		}
	}
}

/* ------------------------------------------------------------------------- */

/*
 * Returns NULL if nothing changed, or a g_malloc string otherwise.
 */
char *
search_replace_string (SearchReplace *sr, const char *src)
{
	int nmatch;
	regmatch_t *pmatch;
	GString *res = NULL;
	int ret;
	int flags = 0;

	g_return_val_if_fail (sr && sr->comp_search, NULL);

	nmatch = 1 + sr->comp_search->re_nsub;
	pmatch = g_new (regmatch_t, nmatch);

	while ((ret = regexec (sr->comp_search, src, nmatch, pmatch, flags)) == 0) {
		if (!res) {
			/* The size here is a bit arbitrary.  */
			int size = strlen (src) +
				10 * strlen (sr->replace_text);
			res = g_string_sized_new (size);
		}

		if (pmatch[0].rm_so > 0) {
			g_string_append_len (res, src, pmatch[0].rm_so);
		}

		if (sr->match_words && !match_is_word (sr, src, pmatch,
						       (flags & REG_NOTBOL) != 0)) {
			/*  We saw a fake match.  */
			if (pmatch[0].rm_so < pmatch[0].rm_eo) {
				g_string_append_c (res, src[pmatch[0].rm_so]);
				/* Pretend we saw a one-character match.  */
				pmatch[0].rm_eo = pmatch[0].rm_so + 1;
			}
		} else {
			char *replacement =
				calculate_replacement (sr, src, pmatch);
			g_string_append (res, replacement);
			g_free (replacement);
		}

		if (pmatch[0].rm_eo > 0) {
			src += pmatch[0].rm_eo;
			flags |= REG_NOTBOL;
		}

		if (pmatch[0].rm_so == pmatch[0].rm_eo) {
			/*
			 * We have matched a null string at the current point.
			 * This might happen searching for just an anchor, for
			 * example.  Don't loop forever...
			 */
			break;
		}
	}

	g_free (pmatch);

	if (res) {
		char *tmp;

		g_string_append (res, src);
		tmp = g_strdup (res->str);
		g_string_free (res, TRUE);
		return tmp;
	} else {
		return NULL;
	}
}

/* ------------------------------------------------------------------------- */

static int
cb_order_sheet_row_col (const void *_a, const void *_b)
{
	const EvalPos *a = *(const EvalPos **)_a;
	const EvalPos *b = *(const EvalPos **)_b;
	int i;

	/* By sheet name.  FIXME: Any better way than this?  */
	i = g_utf8_collate (a->sheet->name_unquoted, b->sheet->name_unquoted);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	return i;
}

static int
cb_order_sheet_col_row (const void *_a, const void *_b)
{
	const EvalPos *a = *(const EvalPos **)_a;
	const EvalPos *b = *(const EvalPos **)_b;
	int i;

	/* By sheet name.  FIXME: Any better way than this?  */
	i = g_utf8_collate (a->sheet->name_unquoted, b->sheet->name_unquoted);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	return i;
}

static Value *
search_collect_cells_cb (Sheet *sheet, int col, int row,
			 Cell *cell, GPtrArray *cells)
{
	EvalPos *ep = g_new (EvalPos, 1);

	ep->sheet = sheet;
	ep->eval.col = col;
	ep->eval.row = row;

	g_ptr_array_add (cells, ep);

	return NULL;

}

/* Collect a list of all cells subject to search.  */
GPtrArray *
search_collect_cells (SearchReplace *sr, Sheet *sheet)
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
		EvalPos ep;
		cells = g_ptr_array_new ();
		range_list = global_range_list_parse (sr->curr_sheet, sr->range_text);
		global_range_list_foreach (range_list,
			   eval_pos_init_sheet (&ep, sr->curr_sheet), TRUE,
			   (ForeachCellCB) search_collect_cells_cb, cells);
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
 * cells: An array of EvalPos*, presumably a result of search_collect_cells.
 *
 * Returns an array of SearchFilterResult*s, which the caller must free.
 */

GPtrArray *
search_filter_matching (SearchReplace *sr, const GPtrArray *cells)
{
	unsigned i;
	GPtrArray *result = g_ptr_array_new ();

	for (i = 0; i < cells->len; i++) {
		SearchReplaceCellResult cell_res;
		SearchReplaceCommentResult comment_res;
		gboolean found;
		const EvalPos *ep = g_ptr_array_index (cells, i);

		found = search_replace_cell (sr, ep, FALSE, &cell_res);
		g_free (cell_res.old_text);
		if (found) {
			SearchFilterResult *item = g_new (SearchFilterResult, 1);
			item->ep = *ep;
			item->cell = cell_res.cell;
			item->comment = NULL;
			g_ptr_array_add (result, item);
		}

		if (search_replace_comment (sr, ep, FALSE, &comment_res)) {
			SearchFilterResult *item = g_new (SearchFilterResult, 1);
			item->ep = *ep;
			item->cell = NULL;
			item->comment = comment_res.comment;
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
search_replace_comment (SearchReplace *sr,
			const EvalPos *ep,
			gboolean repl,
			SearchReplaceCommentResult *res)
{
	g_return_val_if_fail (res, FALSE);

	res->comment = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr && sr->comp_search, FALSE);

	if (!sr->search_comments) return FALSE;

	res->comment = cell_has_comment_pos (ep->sheet, &ep->eval);
	if (!res->comment) return FALSE;

	res->old_text = cell_comment_text_get (res->comment);

	if (repl) {
		res->new_text = search_replace_string (sr, res->old_text);
		return (res->new_text != NULL);
	} else
		return search_match_string (sr, res->old_text);
}

/* ------------------------------------------------------------------------- */

gboolean
search_replace_cell (SearchReplace *sr,
		     const EvalPos *ep,
		     gboolean repl,
		     SearchReplaceCellResult *res)
{
	Cell *cell;
	Value *v;
	gboolean is_expr, is_value, is_string, is_other;

	g_return_val_if_fail (res, FALSE);

	res->cell = NULL;
	res->old_text = NULL;
	res->new_text = NULL;

	g_return_val_if_fail (sr && sr->comp_search, FALSE);

	cell = res->cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	if (!cell) return FALSE;

	v = cell->value;

	is_expr = cell_has_expr (cell);
	is_value = !is_expr && !cell_is_blank (cell) && v;
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
			res->new_text = search_replace_string (sr, actual_src);
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
			return search_match_string (sr, actual_src);
	}

	return FALSE;
}

/* ------------------------------------------------------------------------- */
