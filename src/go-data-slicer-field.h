/*
 * go-data-slicer-field.h :
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
#ifndef GO_DATA_SLICER_FIELD_H
#define GO_DATA_SLICER_FIELD_H

#include <goffice-data.h>	/* remove after move to goffice */
#include <goffice/goffice.h>
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

#define GO_DATA_SLICER_FIELD_TYPE	(go_data_slicer_field_get_type ())
#define GO_DATA_SLICER_FIELD(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerField))
#define IS_GO_DATA_SLICER_FIELD(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SLICER_FIELD_TYPE))

GType go_data_slicer_field_get_type (void);
GOString 	 *go_data_slicer_field_get_name	       (GODataSlicerField const *dsf);
GODataCacheField *go_data_slicer_field_get_cache_field (GODataSlicerField const *dsf);

int	  	  go_data_slicer_field_get_field_type_pos (GODataSlicerField const *dsf,
							   GODataSlicerFieldType field_type);
void	  	  go_data_slicer_field_set_field_type_pos (GODataSlicerField *dsf,
							   GODataSlicerFieldType field_type,
							   int pos);

#endif
G_END_DECLS

#endif /* GO_DATA_SLICER_FIELD_H */
