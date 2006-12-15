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

#include <goffice/utils/format-impl.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-glib-extras.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <stdio.h>
#include <style-font.h>

#undef DEBUG_GENERAL

static gboolean
gnm_style_format_condition (GOFormatElement const *entry, GnmValue const *value)
{
	if (entry->restriction_type == '*')
		return TRUE;

	switch (value->type) {
	case VALUE_BOOLEAN:
	case VALUE_STRING:
		return entry->restriction_type == '@';

	case VALUE_FLOAT: {
		gnm_float f = value_get_as_float (value);
		switch (entry->restriction_type) {
		case '<': return f < entry->restriction_value;
		case '>': return f > entry->restriction_value;
		case '=': return f == entry->restriction_value;
		case ',': return f <= entry->restriction_value;
		case '.': return f >= entry->restriction_value;
		case '+': return f != entry->restriction_value;
		default:
			return FALSE;
		}
	}

	case VALUE_ERROR:
	default:
		return FALSE;
	}
}

static GOFormatElement const *
find_entry (GOFormat const *format, GnmValue const *value,
	    GOColor *go_color, gboolean *need_abs, gboolean *empty)
{
	GOFormatElement const *entry = NULL;

	if (go_color)
		*go_color = 0;

	if (format) {
		GSList *ptr;
		GOFormatElement const *last_entry = NULL;

		for (ptr = format->entries; ptr; ptr = ptr->next) {
			last_entry = ptr->data;			
			/* 142474 : only set entry if it matches */
			if (gnm_style_format_condition (ptr->data, value)) {
				entry = last_entry;
				break;
			}
		}

		/*
		 * 356140: floating point values need to use the last format
		 * if nothing else matched.
		 */
		if (entry == NULL && VALUE_IS_FLOAT (value))
			entry = last_entry;

		if (entry != NULL) {
			/* Empty formats should be ignored */
			if (entry->format[0] == '\0') {
				*empty = TRUE;
				return entry;
			}

			if (go_color && entry->go_color != 0)
				*go_color = entry->go_color;

			if (strcmp (entry->format, "@") == 0) {
				/* FIXME : Formatting a value as a text returns
				 * the entered text.  We need access to the
				 * parse format */
				entry = NULL;

			/* FIXME : Just containing General is enough to be
			 * general for now.  We'll ignore prefixes and suffixes
			 * for the time being */
			} else if (strstr (entry->format, "General") != NULL)
				entry = NULL;
		}
	}

	/* More than one format? -- abs the value.  */
	*need_abs = entry && format->entries->next;
	*empty = FALSE;

	return entry;
}


static char const *
format_nonnumber (GnmValue const *value)
{
	switch (value->type) {
	case VALUE_EMPTY:
		return "";

	case VALUE_BOOLEAN:
		return go_format_boolean (value->v_bool.val);

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


gchar *
format_value (GOFormat const *format, GnmValue const *value, GOColor *go_color,
	      double col_width, GODateConventions const *date_conv)
{
	GString *result = g_string_sized_new (20);
	format_value_gstring (result, format, value, go_color,
			      col_width, date_conv);
	return g_string_free (result, FALSE);
}

static void
hash_fill (PangoLayout *result, GOFontMetrics *metrics, int col_width)
{
	if (col_width <= 0)
		pango_layout_set_text (result, "", -1);
	else if (metrics->hash_width > 0) {
		int l = col_width / metrics->hash_width;
		char *s = g_new (char, l + 1);
		memset (s, '#', l);
		s[l] = 0;
		pango_layout_set_text (result, s, -1);
		g_free (s);
	} else
		pango_layout_set_text (result, "#", -1);
}

GOFormatNumberError
gnm_format_layout (PangoLayout *result,
		   GOFontMetrics *metrics,
		   GOFormat const *format,
		   GnmValue const *value, GOColor *go_color,
		   int col_width,
		   GODateConventions const *date_conv,
		   gboolean unicode_minus)
{
	GOFormatElement const *entry;
	gboolean need_abs, empty;

	g_return_val_if_fail (value != NULL, (GOFormatNumberError)-1);

	if (!format)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.  This will not work for
	 * ranges because we dont't have a location */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	entry = find_entry (format, value, go_color, &need_abs, &empty);

	/* Empty formats should be ignored */
	if (empty) {
		pango_layout_set_text (result, "", 0);
		return GO_FORMAT_NUMBER_OK;
	}

	if (VALUE_IS_FLOAT (value)) {
		gnm_float val = value_get_as_float (value);

		if (!gnm_finite (val)) {
			pango_layout_set_text (result, value_error_name (GNM_ERROR_VALUE, TRUE), -1);
			return GO_FORMAT_NUMBER_OK;
		}

		if (need_abs)
			val = gnm_abs (val);

		if (entry == NULL) {
			GString *str = g_string_sized_new (G_ASCII_DTOSTR_BUF_SIZE + GNM_DIG);
			gnm_render_general (result, str, go_format_measure_pango,
					    metrics, val,
					    col_width, unicode_minus);
			g_string_free (str, TRUE);
		} else {
			GString *str = g_string_sized_new (100);
			/* FIXME: -1 kills filling here.  */
			GOFormatNumberError err =
				gnm_format_number (str, val, -1, entry,
						   date_conv, unicode_minus);
			switch (err) {
			case GO_FORMAT_NUMBER_OK:
				pango_layout_set_text (result, str->str, str->len);
				break;
			case GO_FORMAT_NUMBER_INVALID_FORMAT:
				pango_layout_set_text (result, "", -1);
				break;
			case GO_FORMAT_NUMBER_DATE_ERROR:
				hash_fill (result, metrics, col_width);
				break;
			default:
				g_assert_not_reached ();
			}
			g_string_free (str, TRUE);
			return err;
		}
	} else
		pango_layout_set_text (result, format_nonnumber (value), -1);

	return GO_FORMAT_NUMBER_OK;
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
		      double col_width,
		      GODateConventions const *date_conv)
{
	GOFormatElement const *entry;
	gboolean need_abs, empty;
	gboolean unicode_minus = FALSE;

	g_return_val_if_fail (value != NULL, (GOFormatNumberError)-1);

	if (format == NULL)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.  This will not work for
	 * ranges because we dont't have a location */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	entry = find_entry (format, value, go_color, &need_abs, &empty);

	/* Empty formats should be ignored */
	if (empty)
		return GO_FORMAT_NUMBER_OK;

	if (VALUE_IS_FLOAT (value)) {
		GOFormatNumberError err;
		size_t oldlen = str->len;
		gnm_float val = value_get_as_float (value);

		if (need_abs)
			val = gnm_abs (val);

		if (!gnm_finite (val)) {
			g_string_append (str, value_error_name (GNM_ERROR_VALUE, TRUE));
			return GO_FORMAT_NUMBER_OK;
		}

		err = go_format_value_gstring (format, str, val,
					       col_width, date_conv,
					       unicode_minus);
		if (err) {
			if (col_width < 0)
				col_width = 0;
			g_string_set_size (str, oldlen + col_width);
			memset (str->str + oldlen, '#', col_width);
			return err;
		}
	} else {
		g_string_append (str, format_nonnumber (value));
	}

	return GO_FORMAT_NUMBER_OK;
}
