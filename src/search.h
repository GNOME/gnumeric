#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include <gnumeric.h>
#include <position.h>
#include <goffice/utils/regutf8.h>
#include <sys/types.h>

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

typedef  int (*SearchReplaceQueryFunc) (SearchReplaceQuery q, GnmSearchReplace *sr, ...);

struct _GnmSearchReplace {
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

	go_regex_t *comp_search;
	gboolean plain_replace;

	/*
	 * Query and info function.
	 *
	 * SRQ_fail (..., GnmCell *cell, char const *old, char const *new)
	 *   Inform the user that an error occurred in SRE_fail mode.
	 *
	 * SRQ_query (..., GnmCell *cell, char const *old, char const *new)
	 *   Ask user whether to change.  (-1=cancel, 0=yes, 1=no.)
	 *
	 * SRQ_querycommment (..., Sheet *sheet, CellPos *cp,
	 *                    char const *old, char const *new)
	 *   Ask user whether to change.  (-1=cancel, 0=yes, 1=no.)
	 */
	SearchReplaceQueryFunc query_func;
	void *user_data;
};

GnmSearchReplace *search_replace_new (void);
void	search_replace_free (GnmSearchReplace *sr);
GnmSearchReplace *search_replace_copy (GnmSearchReplace const *sr);
char	*search_replace_verify (GnmSearchReplace *sr, gboolean repl);
char	*search_replace_string (GnmSearchReplace *sr, char const *src);
gboolean search_match_string (GnmSearchReplace *sr, char const *src);

GPtrArray *search_collect_cells (GnmSearchReplace *sr, Sheet *sheet);
void search_collect_cells_free (GPtrArray *cells);

typedef struct {
	GnmEvalPos ep;
	GnmCell *cell;
	GnmComment *comment;
	SearchReplaceLocus locus;
} SearchFilterResult;
GPtrArray *search_filter_matching (GnmSearchReplace *sr, GPtrArray const *cells);
void search_filter_matching_free (GPtrArray *matches);

typedef struct {
	GnmComment *comment;
	char const *old_text;
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCommentResult;
gboolean search_replace_comment (GnmSearchReplace *sr,
				 GnmEvalPos const *ep,
				 gboolean repl,
				 SearchReplaceCommentResult *res);

typedef struct {
	GnmCell *cell;
	char *old_text; /* Caller must free.  */
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCellResult;
gboolean search_replace_cell (GnmSearchReplace *sr,
			      GnmEvalPos const *ep,
			      gboolean repl,
			      SearchReplaceCellResult *res);

typedef struct {
	GnmCell *cell;
} SearchReplaceValueResult;
gboolean search_replace_value (GnmSearchReplace *sr,
			       GnmEvalPos const *ep,
			       SearchReplaceValueResult *res);

#endif
