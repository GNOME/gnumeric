/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* format.c - attempts to emulate excel's number formatting ability.
 * Copyright (C) 1998 Chris Lahey, Miguel de Icaza
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
#include "str.h"

#include <goffice/utils/format-impl.h>
#include <goffice/utils/go-glib-extras.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean
gnm_style_format_condition (StyleFormatEntry const *entry, GnmValue const *value)
{
	if (entry->restriction_type == '*')
		return TRUE;

	switch (value->type) {
	case VALUE_BOOLEAN:
	case VALUE_STRING:
		return entry->restriction_type == '@';

	case VALUE_FLOAT:
		switch (entry->restriction_type) {
		case '<': return value->v_float.val < entry->restriction_value;
		case '>': return value->v_float.val > entry->restriction_value;
		case '=': return value->v_float.val == entry->restriction_value;
		case ',': return value->v_float.val <= entry->restriction_value;
		case '.': return value->v_float.val >= entry->restriction_value;
		case '+': return value->v_float.val != entry->restriction_value;
		default:
			return FALSE;
		}

	case VALUE_INTEGER:
		switch (entry->restriction_type) {
		case '<': return value->v_int.val < entry->restriction_value;
		case '>': return value->v_int.val > entry->restriction_value;
		case '=': return value->v_int.val == entry->restriction_value;
		case ',': return value->v_int.val <= entry->restriction_value;
		case '.': return value->v_int.val >= entry->restriction_value;
		case '+': return value->v_int.val != entry->restriction_value;
		default:
			return FALSE;
		}

	case VALUE_ERROR:
	default:
		return FALSE;
	}
}

/*
 * Returns NULL when the value should be formated as text
 */
void
format_value_gstring (GString *result, GOFormat const *format,
		      GnmValue const *value, GOColor *go_color,
		      double col_width, GODateConventions const *date_conv)
{
	StyleFormatEntry const *entry = NULL; /* default to General */
	GSList *list;
	gboolean need_abs = FALSE;

	if (go_color)
		*go_color = 0;

	g_return_if_fail (value != NULL);

	if (format == NULL)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.
	 * This wont work for ranges because we dont't have a location
	 */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	if (format) {
		for (list = format->entries; list; list = list->next)
			if (gnm_style_format_condition (list->data, value))
				break;

		if (list == NULL &&
		    (value->type == VALUE_INTEGER || value->type == VALUE_FLOAT))
			list = format->entries;

		/* If nothing matches treat it as General */
		if (list != NULL) {
			entry = list->data;

			/* Empty formats should be ignored */
			if (entry->format[0] == '\0')
				return;

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

		/* More than one format? -- abs the value.  */
		need_abs = entry && format->entries->next;
	}

	switch (value->type) {
	case VALUE_EMPTY:
		return;
	case VALUE_BOOLEAN:
		g_string_append (result, format_boolean (value->v_bool.val));
		return;
	case VALUE_INTEGER: {
		int val = value->v_int.val;
		if (need_abs)
			val = ABS (val);

		if (entry == NULL)
			fmt_general_int (result, val, col_width);
		else
			format_number (result, val, (int)col_width, entry, date_conv);
		return;
	}
	case VALUE_FLOAT: {
		gnm_float val = value->v_float.val;

		if (!gnm_finite (val)) {
			g_string_append (result, value_error_name (GNM_ERROR_VALUE, TRUE));
			return;
		}

		if (need_abs)
			val = gnm_abs (val);

		if (entry == NULL) {
			if (INT_MAX >= val && val >= INT_MIN && val == gnm_floor (val))
				fmt_general_int (result, (int)val, col_width);
			else
				fmt_general_float (result, val, col_width);
		} else
			format_number (result, val, (int)col_width, entry, date_conv);
		return;
	}
	case VALUE_ERROR:
		g_string_append (result, value->v_err.mesg->str);
		return;
	case VALUE_STRING:
		g_string_append (result, value->v_str.val->str);
		return;
	case VALUE_CELLRANGE:
		g_string_append (result, value_error_name (GNM_ERROR_VALUE, TRUE));
		return;
	case VALUE_ARRAY: /* Array of arrays ?? */
		g_string_append (result, _("ARRAY"));
		return;

	default:
		g_assert_not_reached ();
		return;
	}
}

gchar *
format_value (GOFormat const *format, GnmValue const *value, GOColor *go_color,
	      double col_width, GODateConventions const *date_conv)
{
	GString *result = g_string_sized_new (20);
	format_value_gstring (result, format, value, go_color, col_width, date_conv);
	return g_string_free (result, FALSE);
}

