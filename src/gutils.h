#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "gnumeric.h"
#include "numbers.h"

/* Gets an integer in the buffer in start to end */
void      int_get_from_range       (const char *start, const char *end, int_t *t);
void      float_get_from_range     (const char *start, const char *end, float_t *t);

guint       gnumeric_strcase_hash  (gconstpointer v);
gint        gnumeric_strcase_equal (gconstpointer v, gconstpointer v2);

#endif /* GNUMERIC_UTILS_H */
