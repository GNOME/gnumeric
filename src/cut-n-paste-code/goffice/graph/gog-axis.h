/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-axis.h : 
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
#ifndef GOG_AXIS_H
#define GOG_AXIS_H

#include <goffice/graph/goffice-graph.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	GOG_AXIS_AT_LOW = -1,
	GOG_AXIS_IN_MIDDLE = 0,
	GOG_AXIS_AT_HIGH = 1
} GogAxisPosition;

enum {
	AXIS_ELEM_MIN = 0,
	AXIS_ELEM_MAX,
	AXIS_ELEM_MAJOR_TICK,
	AXIS_ELEM_MINOR_TICK,
	AXIS_ELEM_CROSS_POINT,
	AXIS_ELEM_MAX_ENTRY
};

#define GOG_AXIS_TYPE	(gog_axis_get_type ())
#define GOG_AXIS(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_AXIS_TYPE, GogAxis))
#define IS_GOG_AXIS(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_AXIS_TYPE))

GType gog_axis_get_type (void);

GogAxisType	gog_axis_get_atype 	 (GogAxis const *axis);
GogAxisPosition gog_axis_get_pos 	 (GogAxis const *axis);
gboolean        gog_axis_is_discrete     (GogAxis const *axis);
gboolean	gog_axis_get_bounds 	 (GogAxis const *axis,
					  double *minima, double *maxima);
void		gog_axis_get_ticks 	 (GogAxis const *axis,
					  double *major, double *minor);
GOData	       *gog_axis_get_labels	 (GogAxis const *axis,
					  GogPlot **plot_that_labeled_axis);

void 	      gog_axis_add_contributor	  (GogAxis *axis, GogObject *contrib);
void 	      gog_axis_del_contributor	  (GogAxis *axis, GogObject *contrib);
GSList const *gog_axis_contributors	  (GogAxis *axis);
void	      gog_axis_clear_contributors (GogAxis *axis);
void	      gog_axis_bound_changed	  (GogAxis *axis, GogObject *contrib);

G_END_DECLS

#endif /* GOG_AXIS_H */
