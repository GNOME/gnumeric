/*
 * go-data-cache.h :
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
#ifndef GO_DATA_CACHE_H
#define GO_DATA_CACHE_H

#include <goffice-data.h>	/* remove after move to goffice */
#include <goffice/goffice.h>
#include <go-val.h>
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

#define GO_DATA_CACHE_TYPE	(go_data_cache_get_type ())
#define GO_DATA_CACHE(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_CACHE_TYPE, GODataCache))
#define IS_GO_DATA_CACHE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_CACHE_TYPE))

GType go_data_cache_get_type (void);

GODataCacheSource *go_data_cache_get_source (GODataCache const *dc);
void		   go_data_cache_set_source (GODataCache *dc,
					     GODataCacheSource *src);
void go_data_cache_add_field    (GODataCache *dc,
				 GODataCacheField *field);
void go_data_cache_import_start (GODataCache *dc, unsigned int expected_records);
void go_data_cache_import_done  (GODataCache *dc, unsigned int actual_records);
void	     go_data_cache_set_index (GODataCache *dc,
				      int field, unsigned int record_num, unsigned int idx);
void	     go_data_cache_set_val   (GODataCache *dc,
				      int field, unsigned int record_num, GOVal *v);
int	     go_data_cache_get_index (GODataCache const *dc,
				      GODataCacheField const *field, unsigned int record_num);

/* Data Access */
unsigned int	  go_data_cache_num_items  (GODataCache const *dc);
unsigned int	  go_data_cache_num_fields (GODataCache const *dc);
GODataCacheField *go_data_cache_get_field  (GODataCache const *dc, int i);

/* Actions */
void go_data_cache_permute (GODataCache const *dc,
			    GArray const *field_order,
			    GArray *permutation);

/* debug util */
void go_data_cache_dump (GODataCache *dc,
			 GArray const *field_order,
			 GArray const *permutation);
void go_data_cache_dump_value (GOVal const *v);

#endif
G_END_DECLS

#endif /* GO_DATA_CACHE_H */
