/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-style.h : 
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef GO_GRAPH_STYLE_H
#define GO_GRAPH_STYLE_H

#include <goffice/graph/goffice-graph.h>
#include <goffice/utils/goffice-utils.h>
#include <goffice/utils/go-gradient.h>
#include <goffice/utils/go-pattern.h>
#include <glib-object.h>
#include <gtk/gtkwidget.h>
#include <command-context.h>	/* for CommandContext */

G_BEGIN_DECLS

#define GOG_STYLE_TYPE	(gog_style_get_type ())
#define GOG_STYLE(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_STYLE_TYPE, GogStyle))
#define IS_GOG_STYLE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_STYLE_TYPE))

GType gog_style_get_type (void);

typedef enum {
	GOG_STYLE_OUTLINE	= 1 << 0,
	GOG_STYLE_FILL		= 1 << 1,
	GOG_STYLE_LINE		= 1 << 2,
	GOG_STYLE_MARKER	= 1 << 3,
	GOG_STYLE_FONT		= 1 << 4
} GogStyleFlag;
typedef enum {
	GOG_FILL_STYLE_NONE	= 0,
	GOG_FILL_STYLE_PATTERN	= 1,
	GOG_FILL_STYLE_GRADIENT	= 2,
	GOG_FILL_STYLE_IMAGE	= 3
} GogFillStyle;

typedef enum {
	GOG_IMAGE_STRETCHED,
	GOG_IMAGE_WALLPAPER,
} GogImageType;

struct _GogStyle {
	GObject	base;

	struct {
		/* <0 == no outline,
		 * =0 == hairline : unscaled, minimum useful (can be bigger than visible) size.
		 * >0 in pts */
		float	 width;
		GOColor	 color;
		gboolean auto_color;
		/* border type from gnumeric */
	} outline, line;
	struct {
		GogFillStyle	type;
		gboolean	is_auto;
		gboolean	invert_if_negative; /* placeholder for XL */
		union {
			struct {
				GOPattern pat;
			} pattern;
			struct {
				GOGradientDirection dir;
				GOColor	start;
				GOColor end;
				float   brightness; /* < 0 => 2 color */
			} gradient;
			struct {
				GogImageType type;
				GdkPixbuf *image;
				char      *filename;
			} image;
		} u;
	} fill;
	GOMarker *marker;
	struct {
		GOColor	color;
		GOFont const *font;
		gboolean auto_scale;
	} font;
};

GogStyle  *gog_style_new		(void);
GogStyle  *gog_style_dup		(GogStyle const *style);
void	   gog_style_assign		(GogStyle *dst, GogStyle const *src);
void	   gog_style_merge		(GogStyle *dst, GogStyle const *src);
void	   gog_style_set_marker		(GogStyle *style, GOMarker *marker);
void	   gog_style_set_font		(GogStyle *style,
					 PangoFontDescription *desc);
gboolean   gog_style_is_different_size	(GogStyle const *a, GogStyle const *b);

GtkWidget *gog_style_editor		(GogObject *obj, CommandContext *cc,
					 GtkWidget *optional_notebook, guint32 enable);

typedef struct {
	unsigned i;
	GogStyle *style;
} GogSeriesElementStyle;

void gog_series_element_style_list_free (GogSeriesElementStyleList *list);
GogSeriesElementStyleList *gog_series_element_style_list_copy (GogSeriesElementStyleList *list);
GogSeriesElementStyleList *gog_series_element_style_list_add  (GogSeriesElementStyleList *list,
							       unsigned i,
							       GogStyle *style);

G_END_DECLS

#endif /* GO_GRAPH_STYLE_H */
