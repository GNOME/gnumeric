#ifndef _GNM_SEARCH_H_
# define _GNM_SEARCH_H_

#include <gnumeric.h>
#include <position.h>
#include <numbers.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

char *gnm_search_normalize (const char *txt);

#define GNM_SEARCH_REPLACE_TYPE        (gnm_search_replace_get_type ())
#define GNM_SEARCH_REPLACE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SEARCH_REPLACE_TYPE, GnmSearchReplace))
#define GNM_IS_SEARCH_REPLACE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SEARCH_REPLACE_TYPE))

typedef enum {
	GNM_SRE_FAIL = 0,
	GNM_SRE_SKIP,
	GNM_SRE_QUERY,
	GNM_SRE_ERROR,
	GNM_SRE_STRING
} GnmSearchReplaceError;

typedef enum {
	GNM_SRS_WORKBOOK = 0,
	GNM_SRS_SHEET,
	GNM_SRS_RANGE
} GnmSearchReplaceScope;
GType gnm_search_replace_scope_get_type (void);
#define GNM_SEARCH_REPLACE_SCOPE_TYPE (gnm_search_replace_scope_get_type ())

typedef enum {
	GNM_SRQ_FAIL,
	GNM_SRQ_QUERY,
	GNM_SRQ_QUERY_COMMENT
} GnmSearchReplaceQuery;

typedef enum {
	GNM_SRL_CONTENTS,
	GNM_SRL_VALUE,
	GNM_SRL_COMMENT
} GnmSearchReplaceLocus;

typedef  int (*GnmSearchReplaceQueryFunc) (GnmSearchReplaceQuery q, GnmSearchReplace *sr, ...);

struct _GnmSearchReplace {
	GOSearchReplace base;

	GnmSearchReplaceScope scope;
	char *range_text;

	/*
	 * This is the default sheet for the range and used also to locate
	 * a workbook.
	 */
	Sheet *sheet;

	gboolean query;		/* Ask before each change.  */

	gboolean is_number;     /* Search for specific number.  */
	gnm_float low_number, high_number;   /* protected. */

	/* The following identify what kinds of cells are the target.  */
	gboolean search_strings;
	gboolean search_other_values;
	gboolean search_expressions;
	gboolean search_expression_results;
	gboolean search_comments;
	gboolean search_scripts;
	gboolean invert;

	GnmSearchReplaceError error_behaviour;
	gboolean replace_keep_strings;

	/*
	 * If true:  A1,B1,...,A2,B2,...
	 * If false: A1,A2,...,B1,B2,...
	 */
	gboolean by_row;

	/*
	 * Query and info function.
	 *
	 * GNM_SRQ_FAIL (..., GnmCell *cell, char const *old, char const *new)
	 *   Inform the user that an error occurred in GNM_SRE_FAIL mode.
	 *
	 * GNM_SRQ_QUERY (..., GnmCell *cell, char const *old, char const *new)
	 *   Ask user whether to change.  GTK_RESPONSE_(YES|NO|CANCEL)
	 *
	 * GNM_SRQ_QUERY_COMMENT (..., Sheet *sheet, CellPos *cp,
	 *                    char const *old, char const *new)
	 *   Ask user whether to change.  GTK_RESPONSE_(YES|NO|CANCEL)
	 */
	GnmSearchReplaceQueryFunc query_func;
	void *user_data;
};

GType gnm_search_replace_get_type (void);

char *gnm_search_replace_verify (GnmSearchReplace *sr, gboolean repl);

GPtrArray *gnm_search_collect_cells (GnmSearchReplace *sr);
void gnm_search_collect_cells_free (GPtrArray *cells);

typedef struct {
	GnmEvalPos ep;
	GnmSearchReplaceLocus locus;
} GnmSearchFilterResult;
GPtrArray *gnm_search_filter_matching (GnmSearchReplace *sr, GPtrArray const *cells);
void gnm_search_filter_matching_free (GPtrArray *matches);

typedef struct {
	GnmComment *comment;
	char const *old_text;
	char *new_text; /* Caller must free if replacing and found.  */
} GnmSearchReplaceCommentResult;
gboolean gnm_search_replace_comment (GnmSearchReplace *sr,
				     GnmEvalPos const *ep,
				     gboolean repl,
				     GnmSearchReplaceCommentResult *res);

typedef struct {
	GnmCell *cell;
	char *old_text; /* Caller must free.  */
	char *new_text; /* Caller must free if replacing and found.  */
} GnmSearchReplaceCellResult;
gboolean gnm_search_replace_cell (GnmSearchReplace *sr,
				  GnmEvalPos const *ep,
				  gboolean repl,
				  GnmSearchReplaceCellResult *res);

void gnm_search_replace_query_fail (GnmSearchReplace *sr,
				    const GnmSearchReplaceCellResult *res);

int gnm_search_replace_query_cell (GnmSearchReplace *sr,
				   const GnmSearchReplaceCellResult *res);

int gnm_search_replace_query_comment (GnmSearchReplace *sr,
				      const GnmEvalPos *ep,
				      const GnmSearchReplaceCommentResult *res);

G_END_DECLS

#endif /* _GNM_SEARCH_H_ */
