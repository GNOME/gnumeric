#ifndef SORT_H
#define SORT_H

#include "gnumeric.h"
#include "command-context.h"

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
} SortData;

void        sort_clause_destroy (SortClause *clause);
void        sort_data_destroy (SortData *data);

void        sort_position (CommandContext *context, SortData *data, int *perm);

int         *sort_contents (CommandContext *context, SortData *data);

#endif /* SORT_H */
