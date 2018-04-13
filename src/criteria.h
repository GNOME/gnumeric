#ifndef _GNM_CRITERIA_H_
#define _GNM_CRITERIA_H_

#include <gnumeric.h>
#include <position.h>
#include <value.h>
#include <number-match.h>
#include <sheet.h>
#include <cell.h>
#include <gutils.h>
#include <workbook.h>
#include <collect.h>
#include <goffice/goffice.h>

#include <string.h>

G_BEGIN_DECLS

typedef struct _GnmCriteria GnmCriteria;

typedef gboolean (*GnmCriteriaFunc) (GnmValue const *x, GnmCriteria *crit);
struct _GnmCriteria {
        GnmCriteriaFunc fun;
        GnmValue *x;
        int column; /* absolute */
	CellIterFlags iter_flags;
	GODateConventions const *date_conv;
	GORegexp rx;
	gboolean has_rx;
	unsigned ref_count; /* for boxed type */
};
GType   gnm_criteria_get_type (void);

typedef struct {
        int     row;	/* absolute */
        GSList *conditions;
} GnmDBCriteria;

GnmCriteria *parse_criteria (GnmValue const *crit_val,
			     GODateConventions const *date_conv,
			     gboolean anchor_end);
void	gnm_criteria_unref	(GnmCriteria *criteria);
void	free_criterias		(GSList *criterias);
GSList *find_rows_that_match	(Sheet *sheet, int first_col,
				 int first_row, int last_col, int last_row,
				 GSList *criterias, gboolean unique_only);
GSList *parse_database_criteria (GnmEvalPos const *ep,
				 GnmValue const *database, GnmValue const *criteria);
int     find_column_of_field	(GnmEvalPos const *ep,
				 GnmValue const *database, GnmValue const *field);

GnmValue *gnm_ifs_func (GPtrArray *data, GPtrArray *crits, GnmValue const *vals,
			float_range_function_t fun, GnmStdError err,
			GnmEvalPos const *ep, CollectFlags flags);


G_END_DECLS

#endif
