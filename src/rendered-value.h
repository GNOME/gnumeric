#ifndef GNUMERIC_RENDERED_VALUE_H
# define GNUMERIC_RENDERED_VALUE_H

#include "gnumeric.h"
#include <pango/pango.h>

/**
 * RenderedValue:
 */
struct _RenderedValue {
	PangoLayout *layout;

	/* In pango units:  */
	int layout_natural_width, layout_natural_height;

	/* In pixels:  */
	guint16 indent_left, indent_right;

	/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
	GOColor go_fore_color;

	guint effective_halign : 8; /* 7 bits would be enough.  */
	guint effective_valign : 8; /* 4 bits would be enough.  */
 	guint variable_width : 1;   /* result depends on the width of cell */
	guint hfilled : 1;
	guint vfilled : 1;
	guint wrap_text : 1;
	guint might_overflow : 1;   /* Subject to ####### treatment.  */
	guint numeric_overflow : 1; /* ####### has happened.  */
	guint noborders : 1;        /* Valid for rotated only.  */
	signed int rotation : 10;
};

RenderedValue *rendered_value_new     (GnmCell *cell, GnmStyle const *mstyle,
				       gboolean variable_width,
				       PangoContext *context,
				       double zoom);
void           rendered_value_destroy (RenderedValue *rv);

RenderedValue *rendered_value_recontext (RenderedValue *rv,
					 PangoContext *context);
void           rendered_value_remeasure (RenderedValue *rv);

/* Return the value as a single string without format infomation.  */
char const *rendered_value_get_text (RenderedValue const * rv);

void rendered_value_init (void);
void rendered_value_shutdown (void);

#endif /* GNUMERIC_RENDERED_VALUE_H */
