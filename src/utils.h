#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "sheet.h"

/* Gets an integer in the buffer in start to end */
void      int_get_from_range       (const char *start, const char *end, int_t *t);
void      float_get_from_range     (const char *start, const char *end, float_t *t);

const char *cell_name              (int col, int row);

/* Various parsing routines */
int         parse_cell_name        (const char *cell_str, int *col, int *row);
gboolean    parse_cell_range       (Sheet *sheet, const char *range, Value **v);
GSList     *parse_cell_name_list   (Sheet *sheet, const char *cell_name_str,
				    int *error_flag);

/*
 * Names
 */
const char *col_name               (int col);
int         col_from_name          (const char *cell_str);

guint       gnumeric_strcase_hash  (gconstpointer v);
gint        gnumeric_strcase_equal (gconstpointer v, gconstpointer v2);

/* return the gnumeric serial number of a special date */
guint32     g_date_serial          (GDate  *date);
GDate      *g_date_new_serial      (guint32 serial);

double      random_01              (void);
double      random_normal          (void);

#endif /* GNUMERIC_UTILS_H */
