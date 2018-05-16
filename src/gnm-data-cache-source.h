/*
 * gnm-data-cache-source.h : GODataCacheSource from a GnmSheet
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
#ifndef GNM_DATA_CACHE_SOURCE_H
#define GNM_DATA_CACHE_SOURCE_H

#include <goffice/goffice.h>
#include <glib-object.h>

#include <gnumeric.h>
#include <goffice-data.h>

G_BEGIN_DECLS

#define GNM_DATA_CACHE_SOURCE_TYPE	(gnm_data_cache_source_get_type ())
#define GNM_DATA_CACHE_SOURCE(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_DATA_CACHE_SOURCE_TYPE, GnmDataCacheSource))
#define GNM_IS_DATA_CACHE_SOURCE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_DATA_CACHE_SOURCE_TYPE))

GType gnm_data_cache_source_get_type (void);

typedef struct _GnmDataCacheSource GnmDataCacheSource;

#ifndef GOFFICE_NAMESPACE_DISABLE
GODataCacheSource *gnm_data_cache_source_new (Sheet *src_sheet,
					      GnmRange const *src_range, char const *src_name);
#endif

Sheet		*gnm_data_cache_source_get_sheet (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_sheet (GnmDataCacheSource *src, Sheet *sheet);
GnmRange const	*gnm_data_cache_source_get_range (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_range (GnmDataCacheSource *src, GnmRange const *r);
char const	*gnm_data_cache_source_get_name  (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_name  (GnmDataCacheSource *src, char const *name);

G_END_DECLS

#endif /* GNM_DATA_CACHE_SOURCE_H */
