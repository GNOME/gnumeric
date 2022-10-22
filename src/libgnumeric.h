#ifndef _GNM_LIBGNUMERIC_H_
# define _GNM_LIBGNUMERIC_H_

#include <gnumeric.h>

G_BEGIN_DECLS

#ifndef GNM_VAR_DECL
#  ifdef WIN32
#    ifdef GNUMERIC_INTERNAL
#      define GNM_VAR_DECL __declspec(dllexport)
#    else
#      define GNM_VAR_DECL extern __declspec(dllimport)
#    endif
#  else
#    define GNM_VAR_DECL extern
#  endif
#endif /* GNM_VAR_DECL */

char const **gnm_pre_parse_init     (int argc, gchar const **argv);
void	     gnm_pre_parse_shutdown (void);
void	     gnm_init		    (void);
void	     gnm_shutdown	    (void);

char const * gnm_get_argv0          (void);

GOptionGroup *gnm_get_option_group (void);

G_END_DECLS

#endif /* _GNM_LIBGNUMERIC_H_ */
