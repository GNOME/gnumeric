/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-data-allocator.h :  
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

#ifndef GOG_DATA_ALLOCATOR_H
#define GOG_DATA_ALLOCATOR_H

#include <goffice/graph/goffice-graph.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct {
	GTypeInterface		   base;

	void	 (*allocate) (GogDataAllocator *a, GogPlot *plot);
	gpointer (*editor)   (GogDataAllocator *a, GogDataset *set, int dim_i);
} GogDataAllocatorClass;

#define GOG_DATA_ALLOCATOR_TYPE		(gog_data_allocator_get_type ())
#define GOG_DATA_ALLOCATOR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_DATA_ALLOCATOR_TYPE, GogDataAllocator))
#define IS_GOG_DATA_ALLOCATOR(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_DATA_ALLOCATOR_TYPE))
#define GOG_DATA_ALLOCATOR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), GOG_DATA_ALLOCATOR_TYPE, GogDataAllocatorClass))
#define IS_GOG_DATA_ALLOCATOR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GOG_DATA_ALLOCATOR_TYPE))
#define GOG_DATA_ALLOCATOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), GOG_DATA_ALLOCATOR_TYPE, GogDataAllocatorClass))

GType gog_data_allocator_get_type (void);

void	 gog_data_allocator_allocate (GogDataAllocator *a, GogPlot *plot);
gpointer gog_data_allocator_editor   (GogDataAllocator *dalloc,
				      GogDataset *set, int dim_i);

/****************************************************************************/

typedef struct {
	GTypeInterface		   base;

	GOData	     *(*get_dim) (GogDataset const *set, int dim_i);
	void	      (*set_dim) (GogDataset *set, int dim_i,
				  GOData *val, GError **err);
} GogDatasetClass;

#define GOG_DATASET_TYPE		(gog_dataset_get_type ())
#define GOG_DATASET(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_DATASET_TYPE, GogDataset))
#define IS_GOG_DATASET(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_DATASET_TYPE))
#define GOG_DATASET_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GOG_DATASET_TYPE, GogDatasetClass))
#define IS_GOG_DATASET_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), GOG_DATASET_TYPE))
#define GOG_DATASET_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), GOG_DATASET_TYPE, GogDatasetClass))

GType gog_dataset_get_type (void);

GOData *gog_dataset_get_dim (GogDataset const *set, int dim_i);
void	gog_dataset_set_dim (GogDataset *set, int dim_i,
			     GOData *val, GError **err);

G_END_DECLS

#endif /* GOG_DATA_ALLOCATOR_H */
