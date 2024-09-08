#ifndef _GNM_GUTILS_H_
# define _GNM_GUTILS_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <numbers.h>

G_BEGIN_DECLS

void gutils_init (void);
void gutils_shutdown (void);

/* System and user paths */
char const *gnm_sys_lib_dir    (void);
char const *gnm_sys_data_dir   (void);
char const *gnm_sys_extern_plugin_dir    (void);
char const *gnm_locale_dir     (void);
char const *gnm_usr_dir	       (gboolean versioned);

gnm_float gnm_utf8_strto (const char *s, char **end);
long gnm_utf8_strtol (const char *s, char **end);

#define PLUGIN_SUBDIR "plugins"

int gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags,
		    gboolean anchor_start, gboolean anchor_end);
int gnm_excel_search_impl (const char *needle, const char *haystack,
			   size_t skip);

gboolean gnm_pango_attr_list_equal (PangoAttrList const *l1, PangoAttrList const *l2);

/* Locale utilities */
typedef struct _GnmLocale GnmLocale;
GnmLocale *gnm_push_C_locale (void);
void	   gnm_pop_C_locale  (GnmLocale *locale);

gboolean   gnm_debug_flag (const char *flag);

void       gnm_string_add_number (GString *buf, gnm_float d);

/* Some Meta handling functions */

void       gnm_insert_meta_date (GODoc *doc, char const *name);

gboolean   gnm_object_get_bool (gpointer o, const char *name);
gboolean   gnm_object_has_readable_prop (gconstpointer obj,
					 const char *property,
					 GType typ, gpointer pres);

gint gnm_float_equal (gnm_float const *a, const gnm_float *b);
guint gnm_float_hash (gnm_float const *d);

typedef int (*GnmHashTableOrder) (gpointer key_a, gpointer val_a,
				  gpointer key_b, gpointer val_b,
				  gpointer user);

void gnm_hash_table_foreach_ordered (GHashTable *h,
				     GHFunc callback,
				     GnmHashTableOrder order,
				     gpointer user);

void gnm_xml_in_doc_dispose_on_exit (GsfXMLInDoc **pdoc);
void gnm_xml_out_end_element_check (GsfXMLOut *xout, char const *id);

Sheet *gnm_file_saver_get_sheet (GOFileSaver const *fs,
				 WorkbookView const *wbv);
GPtrArray *gnm_file_saver_get_sheets (GOFileSaver const *fs,
				      WorkbookView const *wbv,
				      gboolean default_all);

gboolean gnm_file_saver_common_export_option (GOFileSaver const *fs,
					      Workbook const *wb,
					      const char *key,
					      const char *value,
					      GError **err);

char *gnm_cpp (const char *src, GHashTable *vars);

gboolean gnm_shortest_rep_in_files(void);

#ifdef GNM_SUPPLIES_GNM_SSCANF
int gnm_sscanf (const char *str, const char *fmt, ...);
#endif

// Interface for ssconvert --export-range
int gnm_export_range_for_sheet (Sheet const *sheet, GnmRange *dest);




G_END_DECLS

#endif /* _GNM_GUTILS_H_ */
