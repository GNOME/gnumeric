#ifndef GNUMERIC_SORT_H
#define GNUMERIC_SORT_H

#include "gnumeric.h"

typedef struct {
	int	 offset;
	gboolean asc;
	gboolean cs;
	gboolean val;
} GnmSortClause;

struct _GnmSortData {
	Sheet		*sheet;
	GnmRange	*range;
	int		 num_clause;
	GnmSortClause	*clauses;
	gboolean	 top;
	gboolean	 retain_formats;
	char            *locale;
};

void gnm_sort_data_destroy   (GnmSortData *data);
void gnm_sort_position 	     (GnmSortData *data, int *perm, GOCmdContext *cc);
int *gnm_sort_contents 	     (GnmSortData *data, GOCmdContext *cc);
int  gnm_sort_data_length    (GnmSortData const *data);
int *gnm_sort_permute_invert (int const *perm, int length);

#endif /* GNUMERIC_SORT_H */
