#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include <goffice/utils/regutf8.h>

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

#endif /* GNUMERIC_UTILS_H */
