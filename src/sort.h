#ifndef GNUMERIC_SORT_H
#define GNUMERIC_SORT_H

#include "gnumeric.h"

typedef struct {
	int	 offset;
	int	 asc;
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
};

void sort_clause_destroy (GnmSortClause *clause);
void sort_data_destroy   (GnmSortData *data);
void sort_position 	 (GnmSortData *data, int *perm, GnmCmdContext *cc);
int *sort_contents 	 (GnmSortData *data, GnmCmdContext *cc);
int  sort_data_length	 (GnmSortData const *data);
int *sort_permute_invert (int const *perm, int length);

#endif /* GNUMERIC_SORT_H */
