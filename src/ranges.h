#ifndef GNUMERIC_RANGES_H
#define GNUMERIC_RANGES_H

#include "sheet.h"
#include "expr.h"
#include "cell.h"
#include "style.h"

gboolean    range_parse             (Sheet *sheet, const char *range, Value **v);
GSList     *range_list_parse        (Sheet *sheet, const char *cell_name_str);
void        range_list_destroy      (GSList *ranges);
void        range_list_foreach_full (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data, gboolean create_empty);
void        range_list_foreach_all  (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data);
void        range_list_foreach      (GSList *ranges,
				     void (*callback)(Cell *cell, void *data),
				     void *data);
void        range_set_style         (GSList *ranges, MStyle *style);

gboolean    range_is_singleton (Range const *r);
gboolean    range_equal        (Range const *a, Range const *b);
gboolean    range_contains     (Range const *range, int col, int row);
void        range_dump         (Range const *src);
Range*      range_duplicate    (Range const *src);

#endif /* GNUMERIC_RANGES_H */
