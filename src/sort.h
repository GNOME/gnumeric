#ifndef GNUMERIC_SORT_H
#define GNUMERIC_SORT_H

#include "gnumeric.h"

typedef struct {
	int offset;
	int asc;
	gboolean cs;
	gboolean val;
} SortClause;

typedef struct {
	Sheet      *sheet;
	Range      *range;
	int         num_clause;
	SortClause *clauses;
	gboolean    top;
	gboolean retain_formats;
} SortData;

void        sort_clause_destroy (SortClause *clause);
void        sort_data_destroy (SortData *data);

void        sort_position (WorkbookControl *wbc, SortData *data, int *perm);

int         *sort_contents (WorkbookControl *wbc, SortData *data);

int         sort_data_length (const SortData *data);
int         *sort_permute_invert (const int *perm, int length);

#endif /* GNUMERIC_SORT_H */
