/* format.c - attempts to emulate excel's number formatting ability.
 *
 * Copyright (C) 1998 Chris Lahey, Miguel de Icaza
 * Copyright (C) 2006-2007 Morten Welinder (terra@gnome.org)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnm-format.h>
#include <value.h>
#include <cell.h>

#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <style-font.h>

#define UTF8_NEWLINE "\xe2\x86\xa9" /* unicode U+21A9 */
#define UTF8_NEWLINE_RTL "\xe2\x86\xaa" /* unicode U+21AA */

static char const *
format_nonnumber (GnmValue const *value)
{
	switch (value->v_any.type) {
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
		if (str) g_string_truncate (str, 0);
		if (layout) pango_layout_set_text (layout, "", -1);
	} else {
		int l = metrics->hash_width > 0
			? col_width / metrics->hash_width
			: 1;
		GString *hashstr;

		if (str) {
			hashstr = str;
			g_string_truncate (hashstr, 0);
		} else {
			hashstr = g_string_sized_new (l);
		}
		go_string_append_c_n (hashstr, '#', l);
		if (layout)
			pango_layout_set_text (layout, hashstr->str, -1);
		if (str != hashstr)
			g_string_free (hashstr, TRUE);
	}
}

static GOFormatNumberError
format_value_common (PangoLayout *layout, GString *str,
		     const GOFormatMeasure measure,
		     const GOFontMetrics *metrics,
		     GOFormat const *format,
		     GnmValue const *value,
		     int col_width,
		     GODateConventions const *date_conv,
		     gboolean unicode_minus)
{
	GOFormatNumberError err;
	gnm_float val;
	const char *sval;
	char *sval_free = NULL;
	char type;

	g_return_val_if_fail (value != NULL, GO_FORMAT_NUMBER_INVALID_FORMAT);

	if (format == NULL)
		format = VALUE_FMT (value);
	if (format && go_format_is_markup (format))
		format = NULL;

	/* Use top left corner of an array result.  This will not work for
	 * ranges because we don't have a location */
	if (value->v_any.type == VALUE_ARRAY)
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
		if (sval != NULL && layout != NULL &&
		    pango_layout_get_single_paragraph_mode (layout)
		    && strchr (sval, '\n') != NULL) {
			/* We are in single paragraph mode. This happens in HALIGN_FILL */
			GString *str = g_string_new (sval);
			gchar *ptr;
			PangoDirection dir;
			gboolean rtl = FALSE;
			PangoLayoutLine *line;

			pango_layout_set_text (layout, sval, -1);
			line = pango_layout_get_line (layout, 0);
			if (line) {
				dir = line->resolved_dir;
				rtl = (dir == PANGO_DIRECTION_RTL || dir == PANGO_DIRECTION_TTB_RTL
				       || dir == PANGO_DIRECTION_WEAK_RTL);
			}

			while ((ptr = strchr (str->str, '\n')) != NULL)
				go_string_replace
					(str, ptr - str->str, 1, rtl ? UTF8_NEWLINE_RTL : UTF8_NEWLINE, -1);

			sval = sval_free = g_string_free (str, FALSE);
		}
	}
	err = gnm_format_value_gstring (layout, str, measure, metrics,
					format,
					val, type, sval, NULL,
					col_width, date_conv, unicode_minus);

	g_free (sval_free);

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
		   GnmValue const *value,
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
				   value,
				   col_width, date_conv, unicode_minus);

	g_string_free (tmp_str, TRUE);

	return err;
}

/**
 * format_value_gstring:
 * @str: append the result here.
 * @format: (nullable): #GOFormat.
 * @value: #GnmValue to convert
 * @col_width: maximum width in characters, -1 for unlimited
 * @date_conv: #GODateConventions.
 *
 **/
GOFormatNumberError
format_value_gstring (GString *str,
		      GOFormat const *format,
		      GnmValue const *value,
		      int col_width,
		      GODateConventions const *date_conv)
{
	GString *tmp_str = str->len ? g_string_sized_new (100) : NULL;
	GOFormatNumberError err;

	err = format_value_common (NULL, tmp_str ? tmp_str : str,
				   go_format_measure_strlen,
				   go_font_metrics_unit,
				   format,
				   value,
				   col_width, date_conv, FALSE);

	if (tmp_str) {
		if (!err)
			go_string_append_gstring (str, tmp_str);
		g_string_free (tmp_str, TRUE);
	}

	return err;
}

/**
 * format_value_layout:
 * @layout: A PangoLayout
 * @format: (nullable): #GOFormat.
 * @value: #GnmValue to convert
 * @col_width: optional limit on width, -1 for unlimited
 * @date_conv: #GODateConventions.
 *
 **/
GOFormatNumberError
format_value_layout (PangoLayout *layout,
		     GOFormat const *format,
		     GnmValue const *value,
		     int col_width,
		     GODateConventions const *date_conv)
{
	return format_value_common (layout, NULL,
				    go_format_measure_strlen,
				    go_font_metrics_unit,
				    format, value,
				    col_width, date_conv, FALSE);
}


gchar *
format_value (GOFormat const *format,
	      GnmValue const *value,
	      int col_width, GODateConventions const *date_conv)
{
	GString *result = g_string_sized_new (20);
	format_value_gstring (result, format, value,
			      col_width, date_conv);
	return g_string_free (result, FALSE);
}

GOFormat const *
gnm_format_specialize (GOFormat const *fmt, GnmValue const *value)
{
	char type;
	gnm_float val;

	g_return_val_if_fail (fmt != NULL, go_format_general ());
	g_return_val_if_fail (value != NULL, fmt);

	if (VALUE_IS_FLOAT (value)) {
		val = value_get_as_float (value);
		type = 'F';
	} else {
		val = 0;
		/* Close enough: */
		type = VALUE_IS_ERROR (value) ? 'E' : 'S';
	}

	return GNM_SUFFIX(go_format_specialize) (fmt, val, type, NULL);
}

int
gnm_format_is_date_for_value (GOFormat const *fmt,
			      GnmValue const *value)
{
	if (value)
		fmt = gnm_format_specialize (fmt, value);

	return go_format_is_date (fmt);
}

int
gnm_format_is_time_for_value (GOFormat const *fmt,
			      GnmValue const *value)
{
	if (value)
		fmt = gnm_format_specialize (fmt, value);

	return go_format_is_time (fmt);
}

int
gnm_format_month_before_day (GOFormat const *fmt,
			     GnmValue const *value)
{
	int mbd;

	if (value)
		fmt = gnm_format_specialize (fmt, value);

	mbd = go_format_month_before_day (fmt);
	if (mbd < 0)
		mbd = go_locale_month_before_day ();

	return mbd;
}

GOFormat *
gnm_format_for_date_editing (GnmCell const *cell)
{
	char *fmttxt;
	GOFormat *fmt;
	int mbd = cell
		? gnm_format_month_before_day (gnm_cell_get_format (cell),
					       cell->value)
		: go_locale_month_before_day ();

	switch (mbd) {
	case 0:
		fmttxt = gnm_format_frob_slashes ("d/m/yyyy");
		break;
	default:
	case 1:
		fmttxt = gnm_format_frob_slashes ("m/d/yyyy");
		break;
	case 2:
		fmttxt = gnm_format_frob_slashes ("yyyy-m-d");
		break;
	}

	fmt = go_format_new_from_XL (fmttxt);
	g_free (fmttxt);
	return fmt;
}

/*
 * Change slashes to whatever the locale uses for date separation.
 * Note: this operates on strings, not GOFormats.
 *
 * We aren't doing this completely right: a locale might use 24/12-1999 and
 * we'll just use the slash.
 *
 * If it wasn't so hacky, this should go to go-locale.c
 */
char *
gnm_format_frob_slashes (const char *fmt)
{
	const GString *df = go_locale_get_date_format();
	GString *res = g_string_new (NULL);
	gunichar date_sep = '/';
	const char *s;

	for (s = df->str; *s; s++) {
		switch (*s) {
		case 'd': case 'm': case 'y':
			while (g_ascii_isalpha (*s))
				s++;
			while (g_unichar_isspace (g_utf8_get_char (s)))
				s = g_utf8_next_char (s);
			if (*s != ',' &&
			    g_unichar_ispunct (g_utf8_get_char (s))) {
				date_sep = g_utf8_get_char (s);
				goto got_date_sep;
			}
			break;
		default:
			; /* Nothing */
		}
	}
got_date_sep:

	while (*fmt) {
		if (*fmt == '/') {
			g_string_append_unichar (res, date_sep);
		} else
			g_string_append_c (res, *fmt);
		fmt++;
	}

	return g_string_free (res, FALSE);
}


gboolean
gnm_format_has_hour (GOFormat const *fmt,
		     GnmValue const *value)
{
	if (value)
		fmt = gnm_format_specialize (fmt, value);

	return go_format_has_hour (fmt);
}

GOFormat *
gnm_format_import (const char *fmt,
		   GnmFormatImportFlags flags)
{
	GOFormat *res = go_format_new_from_XL (fmt);
	size_t len;

	if (!go_format_is_invalid (res))
		return res;

	len = strlen (fmt);
	if ((flags & GNM_FORMAT_IMPORT_PATCHUP_INCOMPLETE) &&
	    len > 0 &&
	    fmt[len - 1] == '_') {
		GString *fmt2 = g_string_new (fmt);
		GOFormat *res2;

		g_string_append_c (fmt2, ')');
		res2 = go_format_new_from_XL (fmt2->str);
		g_string_free (fmt2, TRUE);

		if (!go_format_is_invalid (res2)) {
			go_format_unref (res);
			return res2;
		}
	}

	if (flags & GNM_FORMAT_IMPORT_NULL_INVALID) {
		go_format_unref (res);
		res = NULL;
	}

	return res;
}
