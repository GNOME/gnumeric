#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include "gnumeric.h"
#include "position.h"
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

typedef enum { SRL_contents,
	       SRL_value,
	       SRL_commment } SearchReplaceLocus;

typedef  int (*SearchReplaceQueryFunc) (SearchReplaceQuery q, SearchReplace *sr, ...);

struct _SearchReplace {
	char *search_text;
	char *replace_text;

	SearchReplaceScope scope;
	char *range_text;
	Sheet *curr_sheet;

	gboolean is_regexp;	/* Search text is a regular expression.  */
	gboolean ignore_case;	/* Consider "a" and "A" the same.  */
	gboolean query;		/* Ask before each change.  */
	gboolean preserve_case;	/* Like Emacs' case-replace.  */
	gboolean match_words;	/* Like grep -w.  */

	/* The following identify what kinds of cells are the target.  */
	gboolean search_strings;
	gboolean search_other_values;
	gboolean search_expressions;
	gboolean search_expression_results;
	gboolean search_comments;

	SearchReplaceError error_behaviour;

	/*
	 * If true:  A1,B1,...,A2,B2,...
	 * If false: A1,A2,...,B1,B2,...
	 */
	gboolean by_row;

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
char *search_replace_verify (SearchReplace *sr, gboolean repl);
char *search_replace_string (SearchReplace *sr, const char *src);
gboolean search_match_string (SearchReplace *sr, const char *src);

GPtrArray *search_collect_cells (SearchReplace *sr, Sheet *sheet);
void search_collect_cells_free (GPtrArray *cells);

typedef struct {
	EvalPos ep;
	Cell *cell;
	CellComment *comment;
	SearchReplaceLocus locus;
} SearchFilterResult;
GPtrArray *search_filter_matching (SearchReplace *sr, const GPtrArray *cells);
void search_filter_matching_free (GPtrArray *matches);

typedef struct SearchReplaceCommentResult {
	CellComment *comment;
	const char *old_text;
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCommentResult;
gboolean search_replace_comment (SearchReplace *sr,
				 const EvalPos *ep,
				 gboolean repl,
				 SearchReplaceCommentResult *res);

typedef struct SearchReplaceCellResult {
	Cell *cell;
	char *old_text; /* Caller must free.  */
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCellResult;
gboolean search_replace_cell (SearchReplace *sr,
			      const EvalPos *ep,
			      gboolean repl,
			      SearchReplaceCellResult *res);

typedef struct SearchReplaceValueResult {
	Cell *cell;
} SearchReplaceValueResult;
gboolean search_replace_value (SearchReplace *sr,
			       const EvalPos *ep,
			       SearchReplaceValueResult *res);

#endif
