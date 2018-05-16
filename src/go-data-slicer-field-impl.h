/*
 * go-data-slicer-field-impl.h :
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
#ifndef GO_DATA_SLICER_FIELD_IMPL_H
#define GO_DATA_SLICER_FIELD_IMPL_H

#include <go-data-slicer-field.h>
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

struct _GODataSlicerField {
	GObject		base;

	GODataSlicer	*ds;
	GOString	*name;	/* if null uses name from underlying cache field */
	int	 indx;
	int	 data_cache_field_indx;	/* < 0 => undefined */

	int	 field_type_pos[GDS_FIELD_TYPE_MAX];

	unsigned int	 aggregations;
};
typedef struct {
	GObjectClass base;
} GODataSlicerFieldClass;

#endif
G_END_DECLS

#endif /* GO_DATA_SLICER_FIELD_IMPL_H */
