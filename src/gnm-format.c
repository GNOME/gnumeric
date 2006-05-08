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

#include <goffice/utils/format-impl.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-font.h>
#include <glib/gi18n.h>
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
		GSList *list;

		for (list = format->entries; list; list = list->next) {
			entry = list->data;
			if (gnm_style_format_condition (entry, value))
				break;
		}

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


static const char *
format_nonnumber (const GnmValue *value)
{
	switch (value->type) {
	case VALUE_EMPTY:
		return "";

	case VALUE_BOOLEAN:
		return format_boolean (value->v_bool.val);

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

static gboolean
convert_minus (GString *str, size_t i)
{
	if (str->str[i] != '-')
		return FALSE;

	str->str[i] = 0xe2;
	g_string_insert_len (str, i + 1, "\x88\x92", 2);
	return TRUE;
}


#define HANDLE_MINUS(i) do { if (unicode_minus) convert_minus (str, (i)); } while (0)
#define SETUP_LAYOUT do { if (layout) pango_layout_set_text (layout, str->str, -1); } while (0)

typedef int (*GnmFormatMeasure) (const GString *str, PangoLayout *layout);

static int
zero_measure (const GString *str, PangoLayout *layout)
{
	return 0;
}

/* This should go back to goffice.  */
/*
 * gnm_format_general:
 * @layout: Optional PangoLayout, probably preseeded with font attribute.
 * @str: a GString to store (not append!) the resulting string in.
 * @measure: Function to measure width of string/layout.
 * @metrics: Font metrics corresponding to @mesaure.
 * @val: floating-point value.  Must be finite.
 * @col_width: intended max width of layout in pango units.  -1 means
 *             no restriction.
 * @unicode_minus: Use unicode minuses, not hyphens.
 *
 * Render a floating-point value into @layout in such a way that the
 * layouting width does not needlessly exceed @col_width.  Optionally
 * use unicode minus instead of hyphen.
 */
static void
gnm_format_general (PangoLayout *layout, GString *str,
		    GnmFormatMeasure measure, const GOFontMetrics *metrics,
		    gnm_float val,
		    int col_width,
		    gboolean unicode_minus)
{
	gnm_float aval, l10;
	int prec, safety, digs, maxdigits;
	size_t epos;
	gboolean rounds_to_0;
	int sign_width;

	if (col_width == -1) {
		measure = zero_measure;
		maxdigits = GNM_DIG;
		col_width = INT_MAX;
		sign_width = 0;
	} else {
		maxdigits = MIN (GNM_DIG, col_width / metrics->min_digit_width);
		sign_width = unicode_minus
			? metrics->minus_width
			: metrics->hyphen_width;
	}

#ifdef DEBUG_GENERAL
	g_print ("Rendering %" GNM_FORMAT_g " to width %d (<=%d digits)\n",
		 val, col_width, maxdigits);
#endif
	if (val == 0)
		goto zero;

	aval = gnm_abs (val);
	if (aval >= GNM_const(1e15) || aval < GNM_const(1e-4))
		goto e_notation;
	l10 = gnm_log10 (aval);

	/* Number of digits in [aval].  */
	digs = (aval >= 1 ? 1 + (int)l10 : 1);

	/* Check if there is room for the whole part, including sign.  */
	safety = metrics->avg_digit_width / 2;

	if (digs * metrics->min_digit_width > col_width) {
#ifdef DEBUG_GENERAL
		g_print ("No room for whole part.\n");
#endif
		goto e_notation;
	} else if (digs * metrics->max_digit_width + safety <
		   col_width - (val > 0 ? 0 : sign_width)) {
#ifdef DEBUG_GENERAL
		g_print ("Room for whole part.\n");
#endif
		if (val == gnm_floor (val) || digs == maxdigits) {
			g_string_printf (str, "%.0" GNM_FORMAT_f, val);
			HANDLE_MINUS (0);
			SETUP_LAYOUT;
			return;
		}
	} else {
		int w;
#ifdef DEBUG_GENERAL
		g_print ("Maybe room for whole part.\n");
#endif

		g_string_printf (str, "%.0" GNM_FORMAT_f, val);
		HANDLE_MINUS (0);
		SETUP_LAYOUT;
		w = measure (str, layout);
		if (w > col_width)
			goto e_notation;

		if (val == gnm_floor (val) || digs == maxdigits)
			return;
	}

	prec = maxdigits - digs;
	g_string_printf (str, "%.*" GNM_FORMAT_f, prec, val);
	HANDLE_MINUS (0);
	while (str->str[str->len - 1] == '0') {
		g_string_truncate (str, str->len - 1);
		prec--;
	}
	if (prec == 0) {
		/* We got "xxxxxx.000" and dropped the zeroes.  */
		const char *dot = g_utf8_prev_char (str->str + str->len);
		g_string_truncate (str, dot - str->str);
		SETUP_LAYOUT;
		return;
	}

	while (prec > 0) {
		int w;

		SETUP_LAYOUT;
		w = measure (str, layout);
		if (w <= col_width)
			return;

		prec--;
		g_string_printf (str, "%.*" GNM_FORMAT_f, prec, val);
		HANDLE_MINUS (0);
	}

	SETUP_LAYOUT;
	return;

 e_notation:
	rounds_to_0 = (aval < 0.5);
	prec = (col_width -
		(val > 0 ? 0 : sign_width) -
		(aval < 1 ? sign_width : metrics->plus_width) -
		metrics->E_width) / metrics->min_digit_width - 3;
	if (prec <= 0) {
#ifdef DEBUG_GENERAL
		if (prec == 0)
			g_print ("Maybe room for E notation with no decimals.\n");
		else
			g_print ("No room for E notation.\n");
#endif
		/* Certainly too narrow for precision.  */
		if (prec == 0 || !rounds_to_0) {
			int w;

			g_string_printf (str, "%.0" GNM_FORMAT_E, val);
			HANDLE_MINUS (0);
			epos = strchr (str->str, 'E') - str->str;
			HANDLE_MINUS (epos + 1);
			SETUP_LAYOUT;
			if (!rounds_to_0)
				return;

			w = measure (str, layout);
			if (w <= col_width)
				return;
		}

		goto zero;
	}
	prec = MIN (prec, GNM_DIG - 1);
	g_string_printf (str, "%.*" GNM_FORMAT_E, prec, val);
	epos = strchr (str->str, 'E') - str->str;
	digs = 0;
	while (str->str[epos - 1 - digs] == '0')
		digs++;
	if (digs) {
		epos -= digs;
		g_string_erase (str, epos, digs);
		prec -= digs;
		if (prec == 0) {
			int dot = 1 + (str->str[0] == '-');
			g_string_erase (str, dot, epos - dot);
		}
	}

	while (1) {
		int w;

		HANDLE_MINUS (0);
		epos = strchr (str->str + prec + 1, 'E') - str->str;
		HANDLE_MINUS (epos + 1);
		SETUP_LAYOUT;
		w = measure (str, layout);
		if (w <= col_width)
			return;

		if (prec > 2 && w - metrics->max_digit_width > col_width)
			prec -= 2;
		else {
			prec--;
			if (prec < 0)
				break;
		}
		g_string_printf (str, "%.*" GNM_FORMAT_E, prec, val);
	}

	if (rounds_to_0)
		goto zero;

	SETUP_LAYOUT;
	return;

 zero:
#ifdef DEBUG_GENERAL
	g_print ("Zero.\n");
#endif
	g_string_assign (str, "0");
	SETUP_LAYOUT;
	return;
}


static int
pango_measure (const GString *str, PangoLayout *layout)
{
	int w;
	pango_layout_get_size (layout, &w, NULL);
#ifdef DEBUG_GENERAL
	g_print ("[%s] --> %d\n", str->str, w);
#endif
	return w;
}

void
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

	g_return_if_fail (value != NULL);

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
		return;
	}

	if (VALUE_IS_FLOAT (value)) {
		gnm_float val = value_get_as_float (value);

		if (!gnm_finite (val)) {
			pango_layout_set_text (result, value_error_name (GNM_ERROR_VALUE, TRUE), -1);
			return;
		}

		if (need_abs)
			val = gnm_abs (val);

		if (entry == NULL) {
			GString *str = g_string_sized_new (G_ASCII_DTOSTR_BUF_SIZE + GNM_DIG);
			gnm_format_general (result, str, pango_measure,
					    metrics, val,
					    col_width, unicode_minus);
			g_string_free (str, TRUE);
		} else {
			GString *str = g_string_sized_new (100);
			/* FIXME: -1 kills filling here.  */
			gnm_format_number (str, val, -1, entry, date_conv);
			/* FIXME: have gnm_format_number handle minus.  */
			if (format->family != GO_FORMAT_DATE && val < 1.0) {
				size_t i;
				for (i = 0; i < str->len; i++)
					if (convert_minus (str, i))
						i += 2;
			}
			pango_layout_set_text (result, str->str, str->len);
			g_string_free (str, TRUE);
		}
	} else
		pango_layout_set_text (result, format_nonnumber (value), -1);
}

static int
strlen_measure (const GString *str, PangoLayout *layout)
{
	return g_utf8_strlen (str->str, -1);
}

void
format_value_gstring (GString *str, GOFormat const *format,
		      GnmValue const *value, GOColor *go_color,
		      double col_width,
		      GODateConventions const *date_conv)
{
	GOFormatElement const *entry;
	gboolean need_abs, empty;
	gboolean unicode_minus = FALSE;

	g_return_if_fail (value != NULL);

	if (format == NULL)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.  This will not work for
	 * ranges because we dont't have a location */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	entry = find_entry (format, value, go_color, &need_abs, &empty);

	/* Empty formats should be ignored */
	if (empty)
		return;

	if (VALUE_IS_FLOAT (value)) {
		gnm_float val = value_get_as_float (value);

		if (!gnm_finite (val)) {
			g_string_append (str, value_error_name (GNM_ERROR_VALUE, TRUE));
			return;
		}

		if (need_abs)
			val = gnm_abs (val);

		if (entry == NULL) {
			GString *new_str = NULL;

			if (str->len)
				new_str = g_string_sized_new (G_ASCII_DTOSTR_BUF_SIZE + GNM_DIG);
			gnm_format_general (NULL, str->len ? new_str : str,
					    strlen_measure,
					    go_font_metrics_unit,
					    val, col_width, unicode_minus);
			if (new_str) {
				go_string_append_gstring (str, new_str);
				g_string_free (new_str, TRUE);
			}
		} else
			gnm_format_number (str, val, (int)col_width, entry, date_conv);
	} else {
		g_string_append (str, format_nonnumber (value));
	}
}
