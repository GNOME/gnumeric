/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 *
 */

#include <config.h>
#include <string.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <ctype.h>
#include <stdlib.h>
#include "gnumeric.h"
#include "ranges.h"
#include "search.h"
#include "sheet.h"
#include "workbook.h"
#include "position.h"

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
		int start_col, start_row, end_col, end_row;

		if (!sr->range_text || sr->range_text[0] == 0)
			return g_strdup (_("You must specify a range to search."));

		/* FIXME: what about sheet name?  */
		if (!parse_range (sr->range_text,
				  &start_col, &start_row,
				  &end_col, &end_row))
			return g_strdup (_("The search range is invalid."));
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
				int i;
				int n = s[1] - '0';
				s++;

				g_assert (n > 0 && n <= (int)sr->comp_search->re_nsub);
				for (i = pm[n].rm_so; i < pm[n].rm_eo; i++)
					g_string_append_c (gres, src[i]);
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
	int ret;

	g_return_val_if_fail (sr && sr->comp_search, FALSE);

	ret = regexec (sr->comp_search, src, 0, 0, REG_NOSUB);

	switch (ret) {
	case 0: return TRUE;
	case REG_NOMATCH: return FALSE;
	default:
		g_error ("Unexpect error code from regexec: %d.", ret);
		return FALSE;
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

		if (pmatch[0].rm_so) {
			int i;
			/* This is terrible!  */
			for (i = 0; i < pmatch[0].rm_so; i++)
				g_string_append_c (res, src[i]);
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
	i = strcmp (a->sheet->name_unquoted, b->sheet->name_unquoted);

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
	i = strcmp (a->sheet->name_unquoted, b->sheet->name_unquoted);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	return i;
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
		int start_col, start_row, end_col, end_row;

		/* FIXME: what about sheet name?  */
		parse_range (sr->range_text,
			     &start_col, &start_row,
			     &end_col, &end_row);

		cells = sheet_cells (sheet,
				     start_col, start_row, end_col, end_row,
				     TRUE);
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

/* ------------------------------------------------------------------------- */
