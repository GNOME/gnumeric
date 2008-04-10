/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUTILS_H_
# define _GNM_GUTILS_H_

#include "gnumeric.h"
#include <goffice/utils/regutf8.h>

G_BEGIN_DECLS

void gutils_init (void);
void gutils_shutdown (void);

/* System and user paths */
char const *gnm_sys_lib_dir    (void);
char const *gnm_sys_data_dir   (void);
char const *gnm_icon_dir       (void);
char const *gnm_locale_dir     (void);
char const *gnm_usr_dir	       (void);

#define PLUGIN_SUBDIR "plugins"

int gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags);

/* Locale utilities */
typedef struct _GnmLocale GnmLocale;
GnmLocale *gnm_push_C_locale (void);
void	   gnm_pop_C_locale  (GnmLocale *locale);

G_END_DECLS

#endif /* _GNM_GUTILS_H_ */
