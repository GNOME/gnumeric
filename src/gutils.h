#ifndef GNUMERIC_UTILS_H
#define GNUMERIC_UTILS_H

#include <goffice/utils/go-glib-extras.h>

/* System and user paths */
char	*gnm_sys_lib_dir    (char const *subdir);
char	*gnm_sys_data_dir   (char const *subdir);
char	*gnm_sys_glade_dir  (void);
char	*gnm_sys_plugin_dir (void);
char	*gnm_usr_dir	    (char const *subdir);
char	*gnm_usr_plugin_dir (void);

#endif /* GNUMERIC_UTILS_H */
