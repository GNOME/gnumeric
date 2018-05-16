/*
 * go-data-slicer.h :
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#ifndef GO_DATA_SLICER_H
#define GO_DATA_SLICER_H

#include <goffice-data.h>	/* remove after move to goffice */
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

#define GO_DATA_SLICER_TYPE	(go_data_slicer_get_type ())
#define GO_DATA_SLICER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SLICER_TYPE, GODataSlicer))
#define IS_GO_DATA_SLICER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SLICER_TYPE))

GType go_data_slicer_get_type (void);

GODataCache *go_data_slicer_get_cache (GODataSlicer const *ds);
void	     go_data_slicer_set_cache (GODataSlicer *ds, GODataCache *cache);

void		   go_data_slicer_add_field   (GODataSlicer *ds, GODataSlicerField *field);
GODataSlicerField *go_data_slicer_get_field   (GODataSlicer const *ds, unsigned int field_index);
unsigned int	   go_data_slicer_num_fields  (GODataSlicer const *ds);

#endif
G_END_DECLS

#endif /* GO_DATA_SLICER_H */
