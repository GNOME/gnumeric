#ifndef GNUMERIC_RENDERED_VALUE_H
# define GNUMERIC_RENDERED_VALUE_H

#include "gnumeric.h"
#include <pango/pango.h>
#include <gdk/gdkcolor.h>

/**
 * RenderedValue:
 */
struct _RenderedValue {
	PangoLayout *layout;
	int layout_natural_width, layout_natural_height;
	guint16 indent_left, indent_right;

	/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
	GdkColor color;

	guint effective_halign : 8; /* 7 bits would be enough.  */
	guint effective_valign : 8; /* 4 bits would be enough.  */
 	guint variable_width : 1;   /* result depends on the width of cell */
	guint numeric_overflow : 1;
	guint hfilled : 1;
	guint vfilled : 1;
	guint wrap_text : 1;
	guint display_formula : 1;
};

RenderedValue *rendered_value_new     (GnmCell *cell, GnmStyle const *mstyle,
				       gboolean variable_width,
				       PangoContext *context);
void           rendered_value_destroy (RenderedValue *rv);

/* Return the value as a single string without format infomation.
 * Caller is responsible for freeing the result */
char const *rendered_value_get_text (RenderedValue const * rv);

void rendered_value_init (void);
void rendered_value_shutdown (void);

#endif /* GNUMERIC_RENDERED_VALUE_H */
