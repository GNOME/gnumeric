#ifndef GNUMERIC_STYLE_FONT_H
#define GNUMERIC_STYLE_FONT_H

#include "gnumeric.h"
#include <pango/pango.h>

struct _GnmFont {
	int	 ref_count;
	char	*font_name;
	double	 size_pts;
	double	 scale;
	struct {
		GOFont const *font;
		GOFontMetrics *metrics;
	} go;
	struct {
		PangoFont	  	*font;
	} pango;

	unsigned int is_bold : 1;
	unsigned int is_italic : 1;
};

PangoContext *gnm_pango_context_get (void);

#endif /* GNUMERIC_STYLE_FONT_H */
