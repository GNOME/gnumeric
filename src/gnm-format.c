/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* format.c - attempts to emulate excel's number formatting ability.
 *
 * Copyright (C) 1998 Chris Lahey, Miguel de Icaza
 * Copyright (C) 2006 Morten Welinder (terra@gnome.org)
 *
 * Redid the format parsing routine to make it accept more of the Excel
 * formats.  The number rendeing code from Chris has not been touched,
 * that routine is pretty good.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include "gnm-format.h"
#include "value.h"

#include <goffice/utils/go-font.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-locale.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <stdio.h>
#include <style-font.h>

static char const *
format_nonnumber (GnmValue const *value)
{
	switch (value->type) {
	case VALUE_EMPTY:
		return "";

	case VALUE_BOOLEAN:
		return go_locale_boolean_name (value->v_bool.val);

	case VALUE_ERROR:
	case VALUE_STRING:
		return value_peek_string (value);

	case VALUE_CELLRANGE:
		return value_error_name (GNM_ERROR_VALUE, TRUE);

	case VALUE_ARRAY:
	case VALUE_FLOAT:
	default:
		g_assert_not_reached ();
	}
	return "";
}

static void
hash_fill (PangoLayout *layout, GString *str, const GOFontMetrics *metrics, int col_width)
{
	if (col_width <= 0) {
		if (layout) pango_layout_set_text (layout, "", -1);
	} else {
		int l = metrics->hash_width > 0
			? col_width / metrics->hash_width
			: 1;
		g_string_set_size (str, l);
		memset (str->str, '#', str->len);
		if (layout)
			pango_layout_set_text (layout, str->str, -1);
	}
}

static GOFormatNumberError
format_value_common (PangoLayout *layout, GString *str,
		     const GOFormatMeasure measure,
		     const GOFontMetrics *metrics,
		     GOFormat const *format,
		     GnmValue const *value, GOColor *go_color,
		     int col_width,
		     GODateConventions const *date_conv,
		     gboolean unicode_minus)
{
	GOFormatNumberError err;
	gnm_float val;
	const char *sval;
	char type;

	g_return_val_if_fail (value != NULL, GO_FORMAT_NUMBER_INVALID_FORMAT);

	if (format == NULL)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.  This will not work for
	 * ranges because we dont't have a location */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	if (VALUE_IS_FLOAT (value)) {
		val = value_get_as_float (value);
		type = 'F';
		sval = NULL;
	} else {
		val = 0;
		/* Close enough: */
		type = VALUE_IS_ERROR (value) ? 'E' : 'S';
		sval = format_nonnumber (value);
	}
	err = gnm_format_value_gstring (layout, str, measure, metrics,
					format,
					val, type, sval,
					go_color,
					col_width, date_conv, unicode_minus);

	switch (err) {
	case GO_FORMAT_NUMBER_OK:
		break;
	case GO_FORMAT_NUMBER_INVALID_FORMAT:
		break;
	case GO_FORMAT_NUMBER_DATE_ERROR:
		hash_fill (layout, str, metrics, col_width);
		break;
	default:
		g_assert_not_reached ();
	}

	return err;
}


GOFormatNumberError
gnm_format_layout (PangoLayout *layout,
		   GOFontMetrics *metrics,
		   GOFormat const *format,
		   GnmValue const *value, GOColor *go_color,
		   int col_width,
		   GODateConventions const *date_conv,
		   gboolean unicode_minus)
{
	GString *tmp_str = g_string_sized_new (100);
	GOFormatNumberError err;

	err = format_value_common (layout, tmp_str,
				   go_format_measure_pango,
				   metrics,
				   format,
				   value, go_color,
				   col_width, date_conv, unicode_minus);

	g_string_free (tmp_str, TRUE);

	return err;
}

/**
 * format_value_gstring :
 * @str : append the result here.
 * @format : #GOFormat.
 * @value : #GnmValue to convert
 * @go_color : return the #GOColor to use
 * col_width : optional
 * @date_conv : #GODateConventions.
 *
 **/
GOFormatNumberError
format_value_gstring (GString *str, GOFormat const *format,
		      GnmValue const *value, GOColor *go_color,
		      int col_width,
		      GODateConventions const *date_conv)
{
	gboolean unicode_minus = FALSE;
	GString *tmp_str = str->len ? g_string_sized_new (100) : NULL;
	GOFormatNumberError err;

	err = format_value_common (NULL, tmp_str ? tmp_str : str,
				   go_format_measure_strlen,
				   go_font_metrics_unit,
				   format,
				   value, go_color,
				   col_width, date_conv, unicode_minus);

	if (tmp_str) {
		if (!err)
			go_string_append_gstring (str, tmp_str);
		g_string_free (tmp_str, TRUE);
	}

	return err;
}

gchar *
format_value (GOFormat const *format, GnmValue const *value, GOColor *go_color,
	      int col_width, GODateConventions const *date_conv)
{
	GString *result = g_string_sized_new (20);
	format_value_gstring (result, format, value, go_color,
			      col_width, date_conv);
	return g_string_free (result, FALSE);
}

int
gnm_format_is_date_for_value (GOFormat const *fmt,
			      GnmValue const *value)
{
	char type;
	gnm_float val;

	g_return_val_if_fail (fmt != NULL, -1);
	g_return_val_if_fail (value != NULL, -1);

	if (VALUE_IS_FLOAT (value)) {
		val = value_get_as_float (value);
		type = 'F';
	} else {
		val = 0;
		/* Close enough: */
		type = VALUE_IS_ERROR (value) ? 'E' : 'S';
	}

	return
#ifdef WITH_LONG_DOUBLE
		go_format_is_date_for_valuel
#else
		go_format_is_date_for_value
#endif
		(fmt, val, type);
}
