#ifndef GNUMERIC_STYLE_COLOR_H
#define GNUMERIC_STYLE_COLOR_H

#include "gnumeric.h"
#include <gdk/gdkcolor.h>

struct _GnmStyleColor {
	GdkColor color, selected_color;
	char     *name;
	int      ref_count;
	gboolean is_auto;
};

/* Colors used by any GnumericSheet item */
extern GdkColor gs_white, gs_light_gray, gs_dark_gray, gs_black, gs_lavender, gs_yellow;

GnmStyleColor *style_color_new_name (char const *name);
GnmStyleColor *style_color_new      (gushort red, gushort green, gushort blue);
GnmStyleColor *style_color_new_i8    (guint8 red, guint8 green, guint8 blue);
GnmStyleColor *style_color_auto_font (void);
GnmStyleColor *style_color_auto_back (void);
GnmStyleColor *style_color_auto_pattern (void);
GnmStyleColor *style_color_ref      (GnmStyleColor *sc);
void        style_color_unref    (GnmStyleColor *sc);
gint        style_color_equal (const GnmStyleColor *k1, const GnmStyleColor *k2);
GnmStyleColor *style_color_black    (void);
GnmStyleColor *style_color_white    (void);
GnmStyleColor *style_color_grid     (void);

void gnumeric_color_init     (void);
void gnumeric_color_shutdown (void);

#endif /* GNUMERIC_STYLE_COLOR_H */
