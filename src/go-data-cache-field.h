/*
 * go-data-cache-field.h :
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
#ifndef GO_DATA_CACHE_FIELD_H
#define GO_DATA_CACHE_FIELD_H

#include <goffice-data.h>	/* remove after move to goffice */
#include <goffice/goffice.h>
#include <glib-object.h>
#include <go-val.h>

#include <gnumeric.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

#define GO_DATA_CACHE_FIELD_TYPE  (go_data_cache_field_get_type ())
#define GO_DATA_CACHE_FIELD(o)	  (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_CACHE_FIELD_TYPE, GODataCacheField))
#define IS_GO_DATA_CACHE_FIELD(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_CACHE_FIELD_TYPE))

typedef enum {
	GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8,
	GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16,
	GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32,
	GO_DATA_CACHE_FIELD_TYPE_INLINE,
	GO_DATA_CACHE_FIELD_TYPE_NONE	/* e.g. grouped or calculated */
} GODataCacheFieldType;

GType go_data_cache_field_get_type (void);

GODataCache	 *go_data_cache_field_get_cache (GODataCacheField const *field);
GOVal const	 *go_data_cache_field_get_val   (GODataCacheField const *field, unsigned int record_num);
GOString 	 *go_data_cache_field_get_name  (GODataCacheField const *field);
GOValArray const *go_data_cache_field_get_vals  (GODataCacheField const *field, gboolean group_val);
void		  go_data_cache_field_set_vals  (GODataCacheField       *field, gboolean group_val,
						 GOValArray *a);

gboolean 	  go_data_cache_field_is_base (GODataCacheField const *field);
GODataCacheFieldType
		  go_data_cache_field_ref_type (GODataCacheField const *field);

#endif
G_END_DECLS

#endif /* GO_DATA_CACHE_FIELD_H */
