#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include "gnumeric.h"
#include <sys/types.h>
#include <regex.h>

typedef enum { SRE_fail = 0,
	       SRE_skip,
	       SRE_query,
	       SRE_error,
	       SRE_string } SearchReplaceError;

typedef enum { SRS_workbook = 0,
	       SRS_sheet,
	       SRS_range } SearchReplaceScope;

typedef enum { SRQ_fail,
	       SRQ_query,
	       SRQ_querycommment } SearchReplaceQuery;

typedef  int (*SearchReplaceQueryFunc) (SearchReplaceQuery q, SearchReplace *sr, ...);

struct _SearchReplace {
	char *search_text;
	char *replace_text;

	SearchReplaceScope scope;
	char *range_text;

	gboolean is_regexp;	/* Search text is a regular expression.  */
	gboolean ignore_case;	/* Consider "a" and "A" the same.  */
	gboolean query;		/* Ask before each change.  */
	gboolean preserve_case;	/* Like Emacs' case-replace.  */
	gboolean match_words;	/* Like grep -w.  */

	/* The following identify what kinds of cells are the target.  */
	gboolean replace_strings;
	gboolean replace_other_values;
	gboolean replace_expressions;
	gboolean replace_comments;

	SearchReplaceError error_behaviour;

	regex_t *comp_search;
	gboolean plain_replace;

	/*
	 * Query and info function.
	 *
	 * SRQ_fail (..., Cell *cell, const char *old, const char *new)
	 *   Inform the user that an error occurred in SRE_fail mode.
	 *
	 * SRQ_query (..., Cell *cell, const char *old, const char *new)
	 *   Ask user whether to change.  (-1=cancel, 0=yes, 1=no.)
	 *
	 * SRQ_querycommment (..., Sheet *sheet, CellPos *cp,
	 *                    const char *old, const char *new)
	 *   Ask user whether to change.  (-1=cancel, 0=yes, 1=no.)
	 */
	SearchReplaceQueryFunc query_func;
	void *user_data;
};

SearchReplace *search_replace_new (void);
void search_replace_free (SearchReplace *sr);
SearchReplace *search_replace_copy (const SearchReplace *sr);

char *search_replace_verify (SearchReplace *sr);

char *search_replace_string (SearchReplace *sr, const char *src);

#endif
