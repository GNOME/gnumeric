#ifndef GNUMERIC_STYLE_FONT_H
#define GNUMERIC_STYLE_FONT_H

#include "gnumeric.h"
#include <pango/pango.h>
#include <libgnomeprint/gnome-font.h>

/* Needs to move to Goffice.  */
struct _GnmFontMetrics {
	int digit_widths[10];
	int min_digit_width;
	int max_digit_width;
	int avg_digit_width;
	int hyphen_width, minus_width, plus_width;
	int E_width;
};

extern const GnmFontMetrics *gnm_font_metrics_unit;
GnmFontMetrics *gnm_font_metrics_new (PangoContext *context,
				      PangoFontDescription *font_descr);
void gnm_font_metrics_free (GnmFontMetrics *metrics);


struct _GnmFont {
	int	 ref_count;
	char	*font_name;
	double	 size_pts;
	double	 scale;
	GnmFontMetrics *metrics;
	struct {
		PangoFont	  	*font;
		PangoFontDescription  	*font_descr;
	} pango;

	unsigned int is_bold : 1;
	unsigned int is_italic : 1;
};

PangoContext *gnm_pango_context_get (void);

#endif /* GNUMERIC_STYLE_FONT_H */
