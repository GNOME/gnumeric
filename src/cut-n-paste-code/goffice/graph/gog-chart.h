/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-chart.h :
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

#ifndef GOG_CHART_H
#define GOG_CHART_H

#include <goffice/graph/goffice-graph.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GOG_CHART_TYPE	(gog_chart_get_type ())
#define GOG_CHART(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_CHART_TYPE, GogChart))
#define IS_GOG_CHART(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_CHART_TYPE))

GType gog_chart_get_type (void);

gboolean  gog_chart_get_position  (GogChart const *chart, unsigned *x, unsigned *y,
				   unsigned *cols, unsigned *rows);
void	  gog_chart_set_position  (GogChart *chart, unsigned x, unsigned y,
				   unsigned cols, unsigned rows);

void	  gog_chart_request_cardinality_update (GogChart *chart);
unsigned  gog_chart_get_cardinality (GogChart *chart);
void	  gog_chart_foreach_elem  (GogChart *chart, gboolean only_visible,
				   GogEnumFunc handler, gpointer data);

gboolean  gog_chart_axis_set_is_valid (GogChart const *chart, GogAxisSet type);
gboolean  gog_chart_axis_set_assign   (GogChart *chart, GogAxisSet type);
GSList	 *gog_chart_get_axis	      (GogChart const *chart, GogAxisType type);

GogGrid  *gog_chart_get_grid	      (GogChart const *chart);

/* View utils */
void	  gog_chart_view_get_indents  (GogView const *view,
				       double *pre, double *post);

G_END_DECLS

#endif /* GOG_CHART_H */
