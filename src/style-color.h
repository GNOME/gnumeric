#ifndef GNUMERIC_STYLE_COLOR_H
#define GNUMERIC_STYLE_COLOR_H

#include "gnumeric.h"
#include <gdk/gdkcolor.h>

struct _StyleColor {
	GdkColor color, selected_color;
	gushort  red;
	gushort  green;
	gushort  blue;
	char     *name;
	int      ref_count;
	gboolean is_auto;
};

/* Colors used by any GnumericSheet item */
extern GdkColor gs_white, gs_light_gray, gs_dark_gray, gs_black, gs_red, gs_lavender, gs_yellow;

StyleColor *style_color_new_name (char const *name);
StyleColor *style_color_new      (gushort red, gushort green, gushort blue);
StyleColor *style_color_new_i8    (guint8 red, guint8 green, guint8 blue);
StyleColor *style_color_auto_font (void);
StyleColor *style_color_auto_back (void);
StyleColor *style_color_auto_pattern (void);
StyleColor *style_color_ref      (StyleColor *sc);
void        style_color_unref    (StyleColor *sc);
gint        style_color_equal (const StyleColor *k1, const StyleColor *k2);
StyleColor *style_color_black    (void);
StyleColor *style_color_white    (void);
StyleColor *style_color_grid     (void);

void gnumeric_color_init     (void);
void gnumeric_color_shutdown (void);

#endif /* GNUMERIC_STYLE_COLOR_H */
