/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-data-allocator.c :
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

#include <gnumeric-config.h>
#include <goffice/graph/gog-data-allocator.h>

GType
gog_data_allocator_get_type (void)
{
	static GType gog_data_allocator_type = 0;

	if (!gog_data_allocator_type) {
		static GTypeInfo const gog_data_allocator_info = {
			sizeof (GogDataAllocatorClass),	/* class_size */
			NULL,		/* base_init */
			NULL,		/* base_finalize */
		};

		gog_data_allocator_type = g_type_register_static (G_TYPE_INTERFACE,
			"GogDataAllocator", &gog_data_allocator_info, 0);
	}

	return gog_data_allocator_type;
}

void
gog_data_allocator_allocate (GogDataAllocator *dalloc, GogPlot *plot)
{
	g_return_if_fail (IS_GOG_DATA_ALLOCATOR (dalloc));
	GOG_DATA_ALLOCATOR_GET_CLASS (dalloc)->allocate (dalloc, plot);
}

gpointer
gog_data_allocator_editor (GogDataAllocator *dalloc, GogDataset *set, int dim_i)
{
	g_return_val_if_fail (IS_GOG_DATA_ALLOCATOR (dalloc), NULL);
	return GOG_DATA_ALLOCATOR_GET_CLASS (dalloc)->editor (dalloc, set, dim_i);
}

/****************************************************************************/

GType
gog_dataset_get_type (void)
{
	static GType gog_dataset_type = 0;

	if (!gog_dataset_type) {
		static GTypeInfo const gog_dataset_info = {
			sizeof (GogDatasetClass),	/* class_size */
			NULL,		/* base_init */
			NULL,		/* base_finalize */
		};

		gog_dataset_type = g_type_register_static (G_TYPE_INTERFACE,
			"GogDataset", &gog_dataset_info, 0);
	}

	return gog_dataset_type;
}

/**
 * gog_dataset_dims :
 * @set : #GogDataset
 * @first : inclusive
 * @last : _inclusive_
 *
 * Returns the first and last valid indicises to get/set dim.
 **/
void
gog_dataset_dims (GogDataset const *set, int *first, int *last)
{
	GogDatasetClass *klass = GOG_DATASET_GET_CLASS (set);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (first != NULL);
	g_return_if_fail (last != NULL);
	return (klass->dims) (set, first, last);
}

/**
 * gog_dataset_get_dim :
 * @set : #GogDataset
 * @dim_i :
 *
 * Returns the GOData associated with dimension @dim_i.  Does NOT add a
 * reference.
 **/
GOData *
gog_dataset_get_dim (GogDataset const *set, int dim_i)
{
	g_return_val_if_fail (IS_GOG_DATASET (set), NULL);
	return GOG_DATASET_GET_CLASS (set)->get_dim (set, dim_i);
}

/**
 * gog_dataset_set_dim :
 * @series : #GogSeries
 * @dim_i :  < 0 gets the name
 * @val : #GOData
 * @err : #GError
 *
 * Absorbs a ref to @val if it is non NULL and updates the validity of the
 * series.  If @dim_i is a shared dimension update all of the other series
 * associated with the same plot.
 **/
void
gog_dataset_set_dim (GogDataset *set, int dim_i, GOData *val, GError **err)
{
	g_return_if_fail (IS_GOG_DATASET (set));
	GOG_DATASET_GET_CLASS (set)->set_dim (set, dim_i, val, err);
}

