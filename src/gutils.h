#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include "gnumeric.h"
#include "numbers.h"
#include <sys/types.h>

void     gnumeric_time_counter_push (void);
gdouble  gnumeric_time_counter_pop (void);

void	  g_ptr_array_insert (GPtrArray *array, gpointer value, int index);
GList    *g_create_list	     (gpointer item1, ...);
gint      g_list_index_custom (GList *list, gpointer data, GCompareFunc cmp_func);
void      g_list_free_custom (GList *list, GFreeFunc free_func);
GList    *g_string_list_copy (GList *list);
GList    *g_strsplit_to_list (const gchar *string, const gchar *delimiter);
#define   g_list_to_vector(vector,elem_type,list_expr) \
G_STMT_START { \
	GList *list, *l; \
	gint size, i; \
	list = (list_expr); \
	size = g_list_length (list); \
	(vector) = g_new (elem_type *, size + 1); \
	for (l = list, i = 0; l != NULL; l = l->next, i++) \
		(vector)[i] = (elem_type *) l->data; \
	(vector)[size] = NULL; \
} G_STMT_END
#define   g_list_to_vector_custom(vector,elem_type,list_expr,conv_func) \
G_STMT_START { \
	GList *list, *l; \
	gint size, i; \
	list = (list_expr); \
	size = g_list_length (list); \
	(vector) = g_new (elem_type *, size + 1); \
	for (l = list, i = 0; l != NULL; l = l->next, i++) \
		(vector)[i] = (elem_type *) conv_func (l->data); \
	(vector)[size] = NULL; \
} G_STMT_END
#define   g_vector_free_custom(vector_expr,elem_type,free_func_expr) \
G_STMT_START { \
	elem_type **vector, **v; \
	GFreeFunc free_func; \
	vector = (vector_expr); \
	free_func = (free_func_expr); \
	for (v = vector; *v != NULL; v++) \
		free_func (*v); \
	g_free (vector); \
} G_STMT_END

GSList    *g_create_slist	     (gpointer item1, ...);
void      g_slist_free_custom (GSList *list, GFreeFunc free_func);
GSList    *g_string_slist_copy (GSList *list);
GSList    *g_strsplit_to_slist (const gchar *string, const gchar *delimiter);
#define   g_slist_to_vector(vector,elem_type,list_expr) \
G_STMT_START { \
	GSList *list, *l; \
	gint size, i; \
	list = (list_expr); \
	size = g_slist_length (list); \
	(vector) = g_new (elem_type *, size + 1); \
	for (l = list, i = 0; l != NULL; l = l->next, i++) \
		(vector)[i] = (elem_type *) l->data; \
	(vector)[size] = NULL; \
} G_STMT_END

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

gnm_mem_chunk *gnm_mem_chunk_new (const char *, size_t, size_t);
void gnm_mem_chunk_destroy (gnm_mem_chunk *, gboolean);
gpointer gnm_mem_chunk_alloc (gnm_mem_chunk *);
gpointer gnm_mem_chunk_alloc0 (gnm_mem_chunk *);
void gnm_mem_chunk_free (gnm_mem_chunk *, gpointer);
void gnm_mem_chunk_foreach_leak (gnm_mem_chunk *, GFunc, gpointer);

#endif /* GNUMERIC_UTILS_H */
