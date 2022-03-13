#ifndef _GNM_RENDERED_VALUE_H_
# define _GNM_RENDERED_VALUE_H_

#include <gnumeric.h>
#include <pango/pango.h>

G_BEGIN_DECLS

struct _GnmRenderedValue {
	PangoLayout *layout;

	/* In pango units:  */
	int layout_natural_width, layout_natural_height;

	/* In pixels:  */
	guint16 indent_left, indent_right;

	GOColor go_fore_color;

	guint effective_halign : 8;
	guint effective_valign : 5;
	guint variable_width : 1;   /* result depends on the width of cell */
	guint hfilled : 1;
	guint vfilled : 1;
	guint wrap_text : 1;
	guint might_overflow : 1;   /* Subject to ####### treatment.  */
	guint numeric_overflow : 1; /* ####### has happened.  */
	guint noborders : 1;        /* Valid for rotated only.  */
	guint drawn : 1;            /* Has drawing layout taken place?  */
	signed int rotation : 10;
};

struct _GnmRenderedRotatedValue {
	GnmRenderedValue rv;
	guint sin_a_neg : 1;
	int linecount;
	struct GnmRenderedRotatedValueInfo {
		int dx, dy;
	} *lines;
};

GnmRenderedValue *gnm_rendered_value_new       (GnmCell const *cell,
						PangoContext *context,
						gboolean allow_variable_width,
						double zoom);
void              gnm_rendered_value_destroy   (GnmRenderedValue *rv);

void              gnm_rendered_value_remeasure (GnmRenderedValue *rv);

/* Return the value as a single string without format infomation.  */
char const *gnm_rendered_value_get_text (GnmRenderedValue const * rv);

GOColor gnm_rendered_value_get_color (GnmRenderedValue const * rv);

/* ------------------------------------------------------------------------- */

struct _GnmRenderedValueCollection {
	PangoContext *context;

	gsize size;
	GHashTable *values;
};

GnmRenderedValueCollection *gnm_rvc_new (PangoContext *context,
					 gsize size);
void gnm_rvc_free (GnmRenderedValueCollection *rvc);
GnmRenderedValue *gnm_rvc_query (GnmRenderedValueCollection *rvc,
				 GnmCell const *cell);
void gnm_rvc_store (GnmRenderedValueCollection *rvc,
		    GnmCell const *cell,
		    GnmRenderedValue *rv);
void gnm_rvc_remove (GnmRenderedValueCollection *rvc,
		     GnmCell const *cell);

/* ------------------------------------------------------------------------- */

void gnm_rendered_value_init (void);
void gnm_rendered_value_shutdown (void);

G_END_DECLS

#endif /* _GNM_RENDERED_VALUE_H_ */
