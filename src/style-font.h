#ifndef GNUMERIC_STYLE_FONT_H
#define GNUMERIC_STYLE_FONT_H

#include "gnumeric.h"
#include <pango/pango.h>
#include <libgnomeprint/gnome-font.h>

struct _GnmFont {
	int	 ref_count;
	char	*font_name;
	double	 size_pts;
	double	 scale;
	struct {
		/* This does not belong here.  */
		struct {
			double digit, decimal, sign, E, e, hash;
		} pixels, pts;
	} approx_width;
	double	 height;
	struct {
		PangoFont	  	*font;
		PangoFontDescription  	*font_descr;
	} pango;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
};

/* Unused place holder */
typedef struct {
	GnmFont	*font;
	PangoFontMetrics	*metrics;
	double	 scale;
	double	 height;
	struct {
		double digit, decimal, sign, E, e, hash;
	} approx_width;
} GnmFontMetrics;

PangoContext *gnm_pango_context_get (void);

#endif /* GNUMERIC_STYLE_FONT_H */
