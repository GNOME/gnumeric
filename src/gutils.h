#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "gnumeric.h"
#include "numbers.h"
#include <sys/types.h>

void     gnumeric_time_counter_push (void);
gdouble  gnumeric_time_counter_pop (void);

typedef gpointer (*GnmMapFunc) (gpointer value);

gunichar const *g_unichar_strchr  	(gunichar const *str, gunichar c);
gunichar const *g_unichar_strstr_utf8	(gunichar const *haystack, gchar const *needle);
size_t	g_unichar_strlen  (gunichar const *str);
int	g_unichar_strncmp (gunichar const *a, gunichar const *b, size_t n);

void	  g_ptr_array_insert (GPtrArray *array, gpointer value, int index);
int	  g_str_compare	     (void const *x, void const *y);
GList    *g_create_list	     (gpointer item1, ...);
gint      g_list_index_custom (GList *list, gpointer data, GCompareFunc cmp_func);
void      g_list_free_custom (GList *list, GFreeFunc free_func);
GList    *g_strsplit_to_list (const gchar *string, const gchar *delimiter);
#define GNM_LIST_FOREACH(list,valtype,val,stmnt) \
G_STMT_START { \
	GList *gnm_l; \
	for (gnm_l = (list); gnm_l != NULL; gnm_l = gnm_l->next) { \
		valtype *val = gnm_l->data; \
		stmnt \
		; \
	} \
} G_STMT_END
#define GNM_LIST_PREPEND(list,item) \
	(list = g_list_prepend (list, item))
#define GNM_LIST_APPEND(list,item) \
	(list = g_list_append (list, item))
#define GNM_LIST_REMOVE(list,item) \
	(list = g_list_remove (list, item))
#define GNM_LIST_CONCAT(list_a,list_b) \
	(list_a = g_list_concat (list_a, list_b))
#define GNM_LIST_REVERSE(list) \
	(list = g_list_reverse (list))
#define GNM_LIST_SORT(list,cmp_func) \
	(list = g_list_sort (list, cmp_func))

GSList   *g_slist_map        (GSList *list, GnmMapFunc map_func);
GSList    *g_create_slist	     (gpointer item1, ...);
void      g_slist_free_custom (GSList *list, GFreeFunc free_func);
#define   g_string_slist_copy(list) g_slist_map (list, (GnmMapFunc) g_strdup)
GSList    *g_strsplit_to_slist (const gchar *string, const gchar *delimiter);
#define GNM_SLIST_FOREACH(list,valtype,val,stmnt) \
G_STMT_START { \
	GSList *gnm_l; \
	for (gnm_l = (list); gnm_l != NULL; gnm_l = gnm_l->next) { \
		valtype *val = gnm_l->data; \
		stmnt \
		; \
	} \
} G_STMT_END
#define GNM_SLIST_PREPEND(list,item) \
	(list = g_slist_prepend (list, item))
#define GNM_SLIST_APPEND(list,item) \
	(list = g_slist_append (list, item))
#define GNM_SLIST_REMOVE(list,item) \
	(list = g_slist_remove (list, item))
#define GNM_SLIST_CONCAT(list_a,list_b) \
	(list_a = g_slist_concat (list_a, list_b))
#define GNM_SLIST_REVERSE(list) \
	(list = g_slist_reverse (list))
#define GNM_SLIST_SORT(list,cmp_func) \
	(list = g_slist_sort (list, cmp_func))

#define GNM_SIZEOF_ARRAY(array) ((int) (sizeof (array) / sizeof ((array)[0])))

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

char *    gnumeric_strescape (const char *string);

char *    gnumeric_utf8_strcapital (const char *p, ssize_t len);

gnm_mem_chunk *gnm_mem_chunk_new (const char *, size_t, size_t);
void gnm_mem_chunk_destroy (gnm_mem_chunk *, gboolean);
gpointer gnm_mem_chunk_alloc (gnm_mem_chunk *);
gpointer gnm_mem_chunk_alloc0 (gnm_mem_chunk *);
void gnm_mem_chunk_free (gnm_mem_chunk *, gpointer);
void gnm_mem_chunk_foreach_leak (gnm_mem_chunk *, GFunc, gpointer);

#endif /* GNUMERIC_UTILS_H */
