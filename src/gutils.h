#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include <goffice/utils/regutf8.h>

/* System and user paths */
char	*gnm_sys_lib_dir    (char const *subdir);
char	*gnm_sys_data_dir   (char const *subdir);
char	*gnm_sys_glade_dir  (void);
char	*gnm_sys_plugin_dir (void);
char	*gnm_usr_dir	    (char const *subdir);
char	*gnm_usr_plugin_dir (void);

int gnumeric_regcomp_XL (go_regex_t *preg, char const *pattern, int cflags);

#endif /* GNUMERIC_UTILS_H */
