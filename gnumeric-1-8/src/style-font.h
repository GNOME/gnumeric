/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_FONT_H_
# define _GNM_STYLE_FONT_H_

#include "gnumeric.h"
#include "libgnumeric.h"
#include <pango/pango.h>

G_BEGIN_DECLS

struct _GnmFont {
	int	 ref_count;
	char	*font_name;
	double	 size_pts;
	double	 scale;
	struct {
		GOFont const *font;
		GOFontMetrics *metrics;
	} go;
	struct {
		PangoFont	  	*font;
	} pango;

	unsigned int is_bold : 1;
	unsigned int is_italic : 1;
};

GnmFont *gnm_font_new   (PangoContext *context,
			 char const *font_name,
			 double size_pts, double scale,
			 gboolean bold, gboolean italic);
void     gnm_font_ref   (GnmFont *gfont);
void     gnm_font_unref (GnmFont *gfont);
guint    gnm_font_hash  (gconstpointer v);
gint     gnm_font_equal (gconstpointer v, gconstpointer v2);

GNM_VAR_DECL double gnm_font_default_width;

/****************************************************************/
/* Internal */
void     gnm_font_init  	(void);
void	 gnm_font_shutdown     (void);

/****************************************************************/
/* Internal : Deprecated : Wrong place */
PangoContext *gnm_pango_context_get (void);

G_END_DECLS

#endif /* _GNM_STYLE_FONT_H_ */
