/*
 * go-data-cache-source.c:
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

#include <goffice/goffice.h>
#include <gnumeric-config.h>
#include <go-data-cache-source.h>

GType
go_data_cache_source_get_type (void)
{
	static GType go_data_cache_source_type = 0;

	if (!go_data_cache_source_type) {
		static GTypeInfo const go_data_cache_source_info = {
			sizeof (GODataCacheSourceClass),	/* class_size */
			NULL,		/* base_init */
			NULL,		/* base_finalize */
		};

		go_data_cache_source_type = g_type_register_static (G_TYPE_INTERFACE,
			"GODataCacheSource", &go_data_cache_source_info, 0);
	}

	return go_data_cache_source_type;
}

/**
 * go_data_cache_source_allocate:
 * @src: a #GODataCacheSource
 *
 * Creates a ref to a new #GODataCache from @src.
 *
 * Returns : a #GODataCache
 **/
GODataCache *
go_data_cache_source_allocate (GODataCacheSource const *src)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_SOURCE (src), NULL);

	return GO_DATA_CACHE_SOURCE_GET_CLASS (src)->allocate (src);
}

/**
 * go_data_cache_source_needs_update:
 * @src: a #GODataCacheSource
 *
 * Has @src changed since the last call to go_data_cache_source_allocate.
 *
 * Returns: %TRUE if @src has changed.
 **/
gboolean
go_data_cache_source_needs_update (GODataCacheSource const *src)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE_SOURCE (src), FALSE);
	return GO_DATA_CACHE_SOURCE_GET_CLASS (src)->needs_update (src);
}
