#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

/* Gets an integer in the buffer in start to end */
void      int_get_from_range     (char *start, char *end, int_t *t);
void      float_get_from_range   (char *start, char *end, float_t *t);

char      *cell_name             (int col, int row);
int       parse_cell_name        (char *cell_str, int *col, int *row);
char      *col_name              (int col);

guint     gnumeric_strcase_hash  (gconstpointer v);
gint      gnumeric_strcase_equal (gconstpointer v, gconstpointer v2);

/* return the gnumeric serial number of a special date */
guint32   g_date_serial          (GDate  *date);
GDate     *g_date_new_serial     (guint32 serial);

#endif /* GNUMERIC_UTILS_H */
