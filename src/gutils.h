/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUTILS_H_
# define _GNM_GUTILS_H_

#include "gnumeric.h"
#include <goffice/goffice.h>
#include <numbers.h>

G_BEGIN_DECLS

void gutils_init (void);
void gutils_shutdown (void);

/* System and user paths */
char const *gnm_sys_lib_dir    (void);
char const *gnm_sys_data_dir   (void);
char const *gnm_icon_dir       (void);
char const *gnm_locale_dir     (void);
char const *gnm_usr_dir	       (gboolean versioned);

#define PLUGIN_SUBDIR "plugins"

int gnm_regcomp_XL (GORegexp *preg, char const *pattern,
		    int cflags, gboolean full);

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

G_END_DECLS

#endif /* _GNM_GUTILS_H_ */
