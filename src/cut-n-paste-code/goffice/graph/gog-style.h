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
#include <glib-object.h>
#include <gtk/gtkwidget.h>
#include <src/command-context.h>	/* for CommandContext */

G_BEGIN_DECLS

#define GOG_STYLE_TYPE	(gog_style_get_type ())
#define GOG_STYLE(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_STYLE_TYPE, GogStyle))
#define IS_GOG_STYLE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_STYLE_TYPE))

GType gog_style_get_type (void);

typedef enum {
	GOG_STYLE_OUTLINE	= 1 << 0,
	GOG_STYLE_FILL		= 1 << 1,
	GOG_STYLE_MARKER	= 1 << 2,
} GogStyleFlag;
typedef enum {
	GOG_FILL_STYLE_NONE,
	GOG_FILL_STYLE_SOLID,
	GOG_FILL_STYLE_PATTERN,
	GOG_FILL_STYLE_GRADIENT,
	GOG_FILL_STYLE_IMAGE
} GogFillStyle;

typedef guint32	GOColor;

struct _GogStyle {
	GObject	base;
	guint32	flags;

	struct {
		/* <0 == no outline,
		 * =0 == hairline (no scale 1 unit)
		 * >0 in pts */
		float	 width;
		GOColor	 color;
		/* border type from gnumeric */
	} outline;
	struct {
		GogFillStyle type;
		union {
			struct {
				GOColor color;
			} solid;
			struct {
				GOColor	fore, back;
				/* pattern from gnumeric */
			} pattern;
			struct {
				GOColor	start, end;
				/* direction as enum or vector ? */
			} gradient;
			struct {
				char *image_file;
				/* ? */
			} image;
		} u;
	} fill;
	struct {
	} marker;
};

GogStyle  *gog_style_auto		(void);
GogStyle  *gog_style_new		(void);
GogStyle  *gog_style_dup		(GogStyle const *style);
gboolean   gog_style_has_marker		(GogStyle const *style);
gboolean   gog_style_is_different_size	(GogStyle const *a, GogStyle const *b);

GtkWidget *gog_style_editor		(GogObject *item, CommandContext *cc,
					 guint32 enable);

typedef struct {
	unsigned i;
	GogStyle *style;
} GogSeriesElementStyle;

void gog_series_element_style_list_free (GogSeriesElementStyleList *list);
GogSeriesElementStyleList *gog_series_element_style_list_copy (GogSeriesElementStyleList *list);
GogSeriesElementStyleList *gog_series_element_style_list_add  (GogSeriesElementStyleList *list,
							       unsigned i,
							       GogStyle *style);

/* Some utils that belong in ColorCombo */
GOColor color_combo_get_gocolor (GtkWidget *cc);
void    color_combo_set_gocolor (GtkWidget *cc, GOColor c);

G_END_DECLS

#endif /* GO_GRAPH_STYLE_H */
