/*
 * go-val.h :
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

#ifndef GO_VAL_H
#define GO_VAL_H

#include <value.h>	/* remove after move to goffice */
#include <gnm-format.h>	/* remove after move to goffice */

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

typedef GnmValue	GOVal;
#define GO_VAL_TYPE	gnm_value_get_type()

#define	go_val_free		value_release
#define	go_val_dup		value_dup
#define	go_val_new_empty	value_new_empty
#define	go_val_new_float	value_new_float
#define	go_val_new_bool		value_new_bool
#define	go_val_new_str		value_new_string
#define	go_val_new_str_nocopy	value_new_string_nocopy
#define go_val_get_fmt(v)	VALUE_FMT(v)

#define go_val_as_float		value_get_as_float

#define	go_val_cmp		value_cmp

/*******************************************************/

typedef GPtrArray GOValArray;
void   go_val_array_free (GOValArray *a);
GOVal *go_val_array_index_steal (GOValArray *a, int i);
#define go_val_array_index  g_ptr_array_index

/*******************************************************/

typedef enum {
	GO_VAL_BUCKET_NONE,

	GO_VAL_BUCKET_SECOND,
	GO_VAL_BUCKET_MINUTE,
	GO_VAL_BUCKET_HOUR,
	GO_VAL_BUCKET_DAY_OF_YEAR,
	GO_VAL_BUCKET_WEEKDAY,
	GO_VAL_BUCKET_MONTH,
	GO_VAL_BUCKET_CALENDAR_QUARTER,
	GO_VAL_BUCKET_YEAR,

	GO_VAL_BUCKET_SERIES_LINEAR,
	GO_VAL_BUCKET_SERIES_LOG
} GOValBucketType;

typedef struct {
	GOValBucketType	type;
	union {
		struct {
			gnm_float minimum;
			gnm_float maximum;
		} dates;
		struct {
			gnm_float minimum;
			gnm_float maximum;
			gnm_float step;
		} series;
	} details;
} GOValBucketer;

void	go_val_bucketer_init	 (GOValBucketer *bucketer);
GError *go_val_bucketer_validate (GOValBucketer *bucketer);
int	go_val_bucketer_apply	 (GOValBucketer const *bucketer, GOVal const *v);

#endif
G_END_DECLS

#endif /* GO_VAL_H */
