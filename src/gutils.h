#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "gnumeric.h"
#include "numbers.h"

/* Gets an integer in the buffer in start to end */
void      int_get_from_range     (const char *start, const char *end, int_t *t);
void      float_get_from_range   (const char *start, const char *end, float_t *t);

guint     gnumeric_strcase_hash  (gconstpointer v);
gint      gnumeric_strcase_equal (gconstpointer v, gconstpointer v2);

/**
 * System and user paths
 */
char *    gnumeric_sys_lib_dir    (const char *subdir);
char *    gnumeric_sys_data_dir   (const char *subdir);
char *    gnumeric_sys_glade_dir  (void);
char *    gnumeric_sys_plugin_dir (void);

char *    gnumeric_usr_dir        (const char *subdir);
char *    gnumeric_usr_plugin_dir (void);

/*
 * Function to help with accessing non-aligned little-endian data.
 */
gint16    gnumeric_get_le_int16 (const void *p);
guint16   gnumeric_get_le_uint16 (const void *p);
gint32    gnumeric_get_le_int32 (const void *p);
guint32   gnumeric_get_le_uint32 (const void *p);
double    gnumeric_get_le_double (const void *p);
void      gnumeric_set_le_double (void *p, double d);

#endif /* GNUMERIC_UTILS_H */
