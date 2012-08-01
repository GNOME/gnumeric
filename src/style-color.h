/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_COLOR_H_
# define _GNM_STYLE_COLOR_H_

#include "gnumeric.h"
#include "libgnumeric.h"
#include <goffice/goffice.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define GNM_STYLE_COLOR_TYPE                 (gnm_style_color_get_type ())
GType gnm_style_color_get_type (void);

struct _GnmColor {
	GOColor	 go_color;
	int      ref_count;
	gboolean is_auto;
};

/* Colors used by any GnumericSheet item */
GNM_VAR_DECL GdkRGBA gs_white;
GNM_VAR_DECL GdkRGBA gs_light_gray;
GNM_VAR_DECL GdkRGBA gs_dark_gray;
GNM_VAR_DECL GdkRGBA gs_black;
GNM_VAR_DECL GdkRGBA gs_lavender;
GNM_VAR_DECL GdkRGBA gs_yellow;

GnmColor *style_color_new_go    (GOColor c);
GnmColor *style_color_new_name  (char const *name);
GnmColor *style_color_new_rgba16(gushort red, gushort green, gushort blue, gushort alpha);
GnmColor *style_color_new_rgb8  (guint8 red, guint8 green, guint8 blue);
GnmColor *style_color_new_rgba8 (guint8 red, guint8 green, guint8 blue, guint8 alpha);
GnmColor *style_color_new_pango (PangoColor const *c);
GnmColor *style_color_new_gdk   (GdkRGBA const *c);
GnmColor *style_color_auto_font (void);
GnmColor *style_color_auto_back (void);
GnmColor *style_color_auto_pattern (void);
GnmColor *style_color_ref      (GnmColor *sc);
void      style_color_unref    (GnmColor *sc);
gint      style_color_equal    (GnmColor const *a, GnmColor const *b);
GnmColor *style_color_black    (void);
GnmColor *style_color_white    (void);
GnmColor *style_color_grid     (void);

/****************************************************************/
/* Internal */
void gnm_color_init     (void);
void gnm_color_shutdown (void);

G_END_DECLS

#endif /* _GNM_STYLE_COLOR_H_ */
