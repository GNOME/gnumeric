/*
 * go-data-cache-source.h :
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
#ifndef GO_DATA_CACHE_SOURCE_H
#define GO_DATA_CACHE_SOURCE_H

#include <goffice-data.h>	/* remove after move to goffice */
#include <goffice/goffice.h>
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

typedef struct {
	GTypeInterface		   base;

	GODataCache *	(*allocate)	(GODataCacheSource const *src);
	GError *	(*validate)	(GODataCacheSource const *src);
	gboolean	(*needs_update)	(GODataCacheSource const *src);
} GODataCacheSourceClass;

#define GO_DATA_CACHE_SOURCE_TYPE	  (go_data_cache_source_get_type ())
#define GO_DATA_CACHE_SOURCE(o)		  (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_CACHE_SOURCE_TYPE, GODataCacheSource))
#define IS_GO_DATA_CACHE_SOURCE(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_CACHE_SOURCE_TYPE))
#define GO_DATA_CACHE_SOURCE_CLASS(k)	  (G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_CACHE_SOURCE_TYPE, GODataCacheSourceClass))
#define IS_GO_DATA_CACHE_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_CACHE_SOURCE_TYPE))
#define GO_DATA_CACHE_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GO_DATA_CACHE_SOURCE_TYPE, GODataCacheSourceClass))

GType go_data_cache_source_get_type (void);

GODataCache *	go_data_cache_source_allocate	  (GODataCacheSource const *src);
gboolean	go_data_cache_source_needs_update (GODataCacheSource const *src);

#endif
G_END_DECLS

#endif /* GO_DATA_CACHE_SOURCE_H */
