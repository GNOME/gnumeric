#ifndef GNUMERIC_SORT_H
#define GNUMERIC_SORT_H

#include "gnumeric.h"

typedef struct {
	int offset;
	int asc;
	gboolean cs;
	gboolean val;
} SortClause;

struct _SortData {
	Sheet      *sheet;
	Range      *range;
	int         num_clause;
	SortClause *clauses;
	gboolean    top;
	gboolean retain_formats;
};

void        sort_clause_destroy (SortClause *clause);
void        sort_data_destroy (SortData *data);

void        sort_position (SortData *data, int *perm, CommandContext *cc);

int         *sort_contents (SortData *data, CommandContext *cc);

int         sort_data_length	 (SortData const *data);
int         *sort_permute_invert (int const *perm, int length);

#endif /* GNUMERIC_SORT_H */
