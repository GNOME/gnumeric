#ifndef _GNM_STYLE_COLOR_H_
# define _GNM_STYLE_COLOR_H_

#include <gnumeric.h>
#include <libgnumeric.h>
#include <goffice/goffice.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

struct _GnmColor {
	GOColor	 go_color;
	int      ref_count;
	gboolean is_auto;
};

#define GNM_COLOR_TYPE (gnm_color_get_type ())
GType     gnm_color_get_type    (void);
GnmColor *gnm_color_new_go    (GOColor c);
GnmColor *gnm_color_new_rgba16(guint16 red, guint16 green, guint16 blue, guint16 alpha);
GnmColor *gnm_color_new_rgb8  (guint8 red, guint8 green, guint8 blue);
GnmColor *gnm_color_new_rgba8 (guint8 red, guint8 green, guint8 blue, guint8 alpha);
GnmColor *gnm_color_new_pango (PangoColor const *c);
GnmColor *gnm_color_new_gdk   (GdkRGBA const *c);
GnmColor *gnm_color_new_auto  (GOColor c);
GnmColor *style_color_auto_font (void);
GnmColor *style_color_auto_back (void);
GnmColor *style_color_auto_pattern (void);
GnmColor *style_color_ref      (GnmColor *sc);
void      style_color_unref    (GnmColor *sc);
gint      style_color_equal    (GnmColor const *a, GnmColor const *b);
GnmColor *style_color_black    (void);
GnmColor *style_color_white    (void);
GnmColor *style_color_grid     (GtkStyleContext *context);

/****************************************************************/
/* Internal */
void gnm_color_init     (void);
void gnm_color_shutdown (void);

G_END_DECLS

#endif /* _GNM_STYLE_COLOR_H_ */
