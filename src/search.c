/*
 * search.c:  Search-and-replace for Gnumeric.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 *
 */

#include <config.h>
#include <gnome.h>
#include "search.h"

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
search_replace_compile (SearchReplace *sr)
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
	*dst = *sr;
	if (sr->search_text) dst->search_text = g_strdup (sr->search_text);
	if (sr->replace_text) dst->replace_text = g_strdup (sr->replace_text);
	if (sr->range_text) dst->range_text = g_strdup (sr->range_text);
	if (sr->comp_search) {
		dst->comp_search = NULL;
		search_replace_compile (dst);
	}
	return dst;
}

/* ------------------------------------------------------------------------- */

char *
search_replace_verify (SearchReplace *sr)
{
	int err;
	g_return_val_if_fail (sr != NULL, NULL);

	if (!sr->search_text || sr->search_text[0] == 0)
		return g_strdup (_("Search string must not be empty."));

	if (!sr->replace_text)
		return g_strdup (_("Replacement string must be set."));

	if (sr->scope == SRS_range) {
		if (!sr->range_text || sr->range_text[0] == 0)
			return g_strdup (_("You must specify a range to search."));
	}

	err = search_replace_compile (sr);
	if (err)
		return g_strdup (_("Invalid search pattern."));

	return NULL;
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

		/* FIXME?  This does not account for $1 -- need it?  */
		g_string_append (res, sr->replace_text);

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
