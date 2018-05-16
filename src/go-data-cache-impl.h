/*
 * go-data-cache-impl.h :
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
#ifndef GO_DATA_CACHE_IMPL_H
#define GO_DATA_CACHE_IMPL_H

#include <go-data-cache.h>
#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

struct _GODataCache {
	GObject		base;

	GODataCacheSource	*data_source;
	GPtrArray		*fields;

	unsigned int	 record_size;
	unsigned int	 records_len;
	unsigned int	 records_allocated;
	guint8		*records;

	char		*refreshed_by;
	GOVal		*refreshed_on;
	gboolean	 refresh_upgrades;

	/* store some XL specific versioning to simplify round trips */
	unsigned int	XL_created_ver, XL_refresh_ver;
};
typedef struct {
	GObjectClass base;
} GODataCacheClass;

/* utility macro */
#define go_data_cache_records_index(c, i)	((c)->records + ((c)->record_size * (i)))

#endif
G_END_DECLS

#endif /* GO_DATA_CACHE_IMPL_H */
