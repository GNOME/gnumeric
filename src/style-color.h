#ifndef GNUMERIC_STYLE_COLOR_H
#define GNUMERIC_STYLE_COLOR_H

#include "gnumeric.h"
#include <gdk/gdktypes.h>

struct _StyleColor {
	int      ref_count;
	GdkColor color;
	GdkColor selected_color;
	char     *name;
	gushort  red;
	gushort  green;
	gushort  blue;
};

/* Colors used by any GnumericSheet item */
extern GdkColor gs_white, gs_light_gray, gs_dark_gray, gs_black, gs_red, gs_lavender;

StyleColor *style_color_new_name (char const *name);
StyleColor *style_color_new      (gushort red, gushort green, gushort blue);
StyleColor *style_color_ref      (StyleColor *sc);
void        style_color_unref    (StyleColor *sc);
StyleColor *style_color_black    (void);
StyleColor *style_color_white    (void);
StyleColor *style_color_grid     (void);

void gnumeric_color_init     (void);
void gnumeric_color_shutdown (void);

#endif /* GNUMERIC_STYLE_COLOR_H */
