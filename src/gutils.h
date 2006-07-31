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

/* The right way to access these is via gnm_sys_lib_dir() and
   gnm_sys_data_dir(), but option processing needs write access.
 */
extern char const *gnumeric_lib_dir;
extern char const *gnumeric_data_dir;

int gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags);

#endif /* GNUMERIC_UTILS_H */
