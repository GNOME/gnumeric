#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "gnumeric.h"
#include "numbers.h"

GList   *gnumeric_config_get_string_list (const gchar *config_path,
                                          const gchar *item_name_prefix);
void     gnumeric_config_set_string_list (GList *items,
                                          const gchar *config_path,
                                          const gchar *item_name_prefix);

GList    *g_create_list (gpointer item1, ...);
GList    *g_string_list_copy (GList *list);
GList    *g_strsplit_to_list (const gchar *string, const gchar *delimiter);

#define   g_lang_score_is_better(score_a, score_b) (score_a < score_b)
gint      g_lang_score_in_lang_list (gchar *lang, GList *lang_list);

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

char *    gnumeric_strescape (const char *string);

#endif /* GNUMERIC_UTILS_H */
