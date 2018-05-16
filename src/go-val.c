/*
 * go-val.c:
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

#include <gnumeric-config.h>
#include <go-val.h>
#include <gnm-datetime.h>

#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <string.h>

GOVal *
go_val_array_index_steal (GOValArray *a, int i)
{
	GOVal *res = go_val_array_index (a, i);
	go_val_array_index (a, i) = NULL;
	return res;
}

void
go_val_array_free (GOValArray *a)
{
	int i;

	if (NULL != a) {
		for (i = (int)a->len; i-- > 0 ; )
			go_val_free (go_val_array_index (a, i));
		g_ptr_array_free (a, TRUE);
	}
}

void
go_val_bucketer_init (GOValBucketer *bucketer)
{
	memset (bucketer, 0, sizeof (GOValBucketer));
	bucketer->type = GO_VAL_BUCKET_NONE;
}

GError *
go_val_bucketer_validate (GOValBucketer *bucketer)
{
	GError *failure = NULL;
	if (bucketer->type >= GO_VAL_BUCKET_SERIES_LINEAR) {
		if (bucketer->details.dates.minimum >=
		    bucketer->details.dates.maximum)
			failure = g_error_new (go_error_invalid (), 0, _("minima must be < maxima"));
		else if (bucketer->details.series.step <= 0)
			failure = g_error_new (go_error_invalid (), 0, _("step must be > 0"));
	} else if (bucketer->type != GO_VAL_BUCKET_NONE) {
		if (bucketer->details.dates.minimum >=
		    bucketer->details.dates.maximum)
			failure = g_error_new (go_error_invalid (), 0, _("minima must be < maxima"));

	}

	return failure;
}

/**
 * go_val_bucketer_apply:
 * @bucketer: #GOValBucketer
 * @v: #GOVal
 *
 * Calculate which bucket @v falls into.
 *
 * Returns -1 on general failure, and 0 for out of range below the start of the domain.
 *	Some bucketer types will also create a bucket on the high end for out of range above.
 **/
int
go_val_bucketer_apply (GOValBucketer const *bucketer, GOVal const *v)
{
	g_return_val_if_fail (bucketer != NULL, 0);
	g_return_val_if_fail (v != NULL, 0);

	if (bucketer->type == GO_VAL_BUCKET_NONE)
		return 0;

	/* Time based */
	if (bucketer->type <= GO_VAL_BUCKET_HOUR) {
		switch (bucketer->type) {
		case GO_VAL_BUCKET_SECOND:
			break;
		case GO_VAL_BUCKET_MINUTE:
			break;
		default : g_assert_not_reached ();
		}
	}
	/* date based */
	if (bucketer->type <= GO_VAL_BUCKET_YEAR) {
		static GODateConventions const default_conv = {FALSE};
		GDate d;
		if (!datetime_value_to_g (&d, v, &default_conv))
			return -1;

		switch (bucketer->type) {
		case GO_VAL_BUCKET_DAY_OF_YEAR :	return 1 + g_date_get_day_of_year (&d);
		case GO_VAL_BUCKET_MONTH :		return g_date_get_month (&d);
		case GO_VAL_BUCKET_CALENDAR_QUARTER :	return 1 + ((g_date_get_month (&d)-1) / 3);
		case GO_VAL_BUCKET_YEAR :		return 1 + g_date_get_year (&d);
		default : g_assert_not_reached ();
		}
	}

	/* >= GO_VAL_BUCKET_SERIES_LINEAR) */

	return 0;
}
