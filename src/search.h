#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include <gnumeric.h>
#include <position.h>
#include <goffice/utils/regutf8.h>
#include <sys/types.h>

#define GNM_SEARCH_REPLACE_TYPE        (gnm_search_replace_get_type ())
#define GNM_SEARCH_REPLACE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SEARCH_REPLACE_TYPE, GnmSearchReplace))
#define GNM_IS_SEARCH_REPLACE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SEARCH_REPLACE_TYPE))

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
	GoSearchReplace base;

	SearchReplaceScope scope;
	char *range_text;
	Sheet *curr_sheet;

	gboolean query;		/* Ask before each change.  */

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

GType gnm_search_replace_get_type (void);

char	*gnm_search_replace_verify (GnmSearchReplace *sr, gboolean repl);

GPtrArray *search_collect_cells (GnmSearchReplace *sr, Sheet *sheet);
void search_collect_cells_free (GPtrArray *cells);

typedef struct {
	GnmEvalPos ep;
	SearchReplaceLocus locus;
} SearchFilterResult;
GPtrArray *search_filter_matching (GnmSearchReplace *sr, GPtrArray const *cells);
void search_filter_matching_free (GPtrArray *matches);

typedef struct {
	GnmComment *comment;
	char const *old_text;
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCommentResult;
gboolean gnm_search_replace_comment (GnmSearchReplace *sr,
				 GnmEvalPos const *ep,
				 gboolean repl,
				 SearchReplaceCommentResult *res);

typedef struct {
	GnmCell *cell;
	char *old_text; /* Caller must free.  */
	char *new_text; /* Caller must free if replacing and found.  */
} SearchReplaceCellResult;
gboolean gnm_search_replace_cell (GnmSearchReplace *sr,
			      GnmEvalPos const *ep,
			      gboolean repl,
			      SearchReplaceCellResult *res);

typedef struct {
	GnmCell *cell;
} SearchReplaceValueResult;
gboolean gnm_search_replace_value (GnmSearchReplace *sr,
			       GnmEvalPos const *ep,
			       SearchReplaceValueResult *res);

#endif
