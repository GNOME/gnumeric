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
	Cell       **cells;
	SortClause *clauses;
	int         num_clause;
	int         pos;
} SortData;

void        sort_clause_destroy (SortClause *clause);

void        sort_position (Sheet *sheet, Range *range,
			   SortData *data, gboolean columns);

void        sort_contents (Sheet *sheet, Range *range,
			   SortData *data, gboolean columns);

#endif /* SORT_H */
