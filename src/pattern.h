#ifndef _GNM_PATTERN_H_
# define _GNM_PATTERN_H_

#include <style.h>

G_BEGIN_DECLS

#define GNM_PATTERNS_MAX 25

gboolean    gnm_pattern_background_set	(GnmStyle const *mstyle,
					 cairo_t *cr,
					 gboolean const is_selected,
					 GtkStyleContext *ctxt);

G_END_DECLS

#endif /* _GNM_PATTERN_H_ */
