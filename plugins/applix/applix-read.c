/*
 * applix-read.c : Routines to read applix version 4 & 5 spreadsheets.
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

/*
 * I do not have much in the way of useful docs.
 * This is a guess based on some sample sheets with a few pointers from
 *	http://www.vistasource.com/products/axware/fileformats/wptchc01.html
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "applix.h"

#include <application.h>
#include <expr.h>
#include <expr-name.h>
#include <func.h>
#include <value.h>
#include <sheet.h>
#include <sheet-view.h>
#include <number-match.h>
#include <cell.h>
#include <parse-util.h>
#include <sheet-style.h>
#include <style.h>
#include <style-border.h>
#include <style-color.h>
#include <selection.h>
#include <position.h>
#include <ranges.h>
#include <command-context.h>
#include <workbook-view.h>
#include <workbook.h>
#include <parse-util.h>
#include <gutils.h>

#include <goffice/goffice.h>

#include <gsf/gsf-input-textline.h>

#include <string.h>
#include <stdlib.h>

typedef struct {
	GsfInputTextline *input;
	GOErrorInfo     *parse_error;
	WorkbookView  *wb_view;
	Workbook      *wb;
	GHashTable    *exprs, *styles;
	GPtrArray     *colors;
	GPtrArray     *attrs;
	GPtrArray     *font_names;

	unsigned char *buffer;
	size_t buffer_size;
	size_t line_len;
	int zoom;
	GSList *sheet_order;
	GSList *std_names, *real_names;

	GnmConventions *convs;
	GIConv converter;
} ApplixReadState;

/* #define NO_DEBUG_APPLIX */
#ifndef NO_DEBUG_APPLIX
#define d(level, code)	do { if (debug_applix_read > level) { code } } while (0)
static int debug_applix_read = 0;
#else
#define d(level, code)
#endif

#define a_strncmp(buf, str) strncmp ((buf), str, sizeof (str) - 1)

static long
au_strtol (const unsigned char *str, unsigned char **end)
{
	char *send;
	long res = strtol ((const char *)str, &send, 10);
	if (end) *end = (unsigned char *)send;
	return res;
}

static long
a_strtol (const char *str, char **end)
{
	return strtol (str, end, 10);
}


/* The maximum numer of character potentially involved in a new line */
#define MAX_END_OF_LINE_SLOP	16

static int applix_parse_error (ApplixReadState *, char const *format, ...)
	G_GNUC_PRINTF (2, 3);

static int
applix_parse_error (ApplixReadState *state, char const *format, ...)
{
	va_list args;
	char *err;

	if (state->parse_error == NULL)
		state->parse_error = go_error_info_new_str (
			_("Parse error while reading Applix file."));

	va_start (args, format);
	err = g_strdup_vprintf (format, args);
	va_end (args);

	go_error_info_add_details (state->parse_error, go_error_info_new_str (err));
	g_free (err);

	return -1;
}

/**
 * applix_parse_value : Parse applix's optionally quoted values.
 *
 * @follow: A pointer to a char * that is adjusted to point 2 chars AFTER the
 *           end of the string.
 *
 * returns the strings and null terminates it.
 */
static char *
applix_parse_value (char *buf, char **follow)
{
	/* Is the value a quoted string */
	if (*buf == '"') {
		char *src = ++buf, *dest = src;
		while (*src && *src != '"') {
			if (*src == '\\')
				src++;
			*dest = *src++;
		}
		g_return_val_if_fail (*src == '"', NULL);
		*follow = src;
		**follow = '\0';
		*follow += 3;
	} else {
		*follow = strchr (buf, ' ');
		g_return_val_if_fail (*follow != NULL, NULL);
		**follow = '\0';
		*follow += 2;
	}

	return buf;
}

/* A..Z, AA..ZZ */
#define APPLIX_SHEET_MAX_COLS 702
#define APPLIX_SHEET_MAX_ROWS 65536

static const GnmSheetSize applix_sheet_size = {
  APPLIX_SHEET_MAX_COLS, APPLIX_SHEET_MAX_ROWS
};

static gboolean
valid_col (Sheet const *sheet, int c)
{
	return c >= 0 && c < gnm_sheet_get_max_cols (sheet);
}

static gboolean
valid_row (Sheet const *sheet, int r)
{
	return r >= 0 && r < gnm_sheet_get_max_rows (sheet);
}

static gboolean
valid_cellpos (Sheet const *sheet, const GnmCellPos *cpos)
{
	return (sheet &&
		valid_col (sheet, cpos->col) &&
		valid_row (sheet, cpos->row));
}

static char const *
applix_col_parse (char const *str, int *res, unsigned char *relative)
{
	return col_parse (str, &applix_sheet_size, res, relative);
}

static char const *
applix_row_parse (char const *str, int *res, unsigned char *relative)
{
	return row_parse (str, &applix_sheet_size, res, relative);
}

static char const *
applix_cellpos_parse (char const *cell_str, Sheet const *sheet,
		      GnmCellPos *res, gboolean strict)
{
	unsigned char dummy_relative;

	cell_str = applix_col_parse (cell_str, &res->col, &dummy_relative);
	if (!cell_str)
		return NULL;

	cell_str = applix_row_parse (cell_str, &res->row, &dummy_relative);
	if (!cell_str)
		return NULL;

	if (*cell_str != 0 && strict)
		return NULL;

	return cell_str;
}

static char const *
applix_sheetref_parse (char const *start, Sheet **sheet, Workbook const *wb)
{
	char const *end, *begin;
	char *name;

	begin = end = (*start == '$') ? start + 1 : start;
	while (*end && g_ascii_isalnum (*end))
		end++;

	if (*end != ':') {
		*sheet = NULL;
		return start;
	}

	name = g_strndup (begin, end - begin);
	*sheet = workbook_sheet_by_name (wb, name);
	g_free (name);
	return *sheet != NULL ? end : start;
}

static char const *
applix_rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
		       GnmConventions const *convention)
{
	char const *ptr = start, *tmp1, *tmp2;
	Workbook *wb = pp->wb;

	/* TODO : Does not handle external references */

	ptr = applix_sheetref_parse (start, &res->a.sheet, wb);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (*ptr == ':') ptr++;
	tmp1 = applix_col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (!tmp1)
		return start;
	tmp2 = applix_row_parse (tmp1, &res->a.row, &res->a.row_relative);
	if (!tmp2)
		return start;
	if (res->a.col_relative)
		res->a.col -= pp->eval.col;
	if (res->a.row_relative)
		res->a.row -= pp->eval.row;
	if (tmp2[0] != '.' || tmp2[1] != '.') {
		res->b = res->a;
		return tmp2;
	}

	start = tmp2;
	ptr = applix_sheetref_parse (start+2, &res->b.sheet, wb);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (*ptr == ':') ptr++;
	tmp1 = applix_col_parse (ptr, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		return start;
	tmp2 = applix_row_parse (tmp1, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		return start;
	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}

static unsigned char *
applix_get_line (ApplixReadState *state)
{
	unsigned char *ptr, *end, *buf;
	GString *line = g_string_new (NULL);
	gboolean first = TRUE;

	// Read line and continuation lines.
	while (NULL != (ptr = gsf_input_textline_ascii_gets (state->input))) {
		size_t len = strlen (ptr);
		// Clip at the state line length
		size_t uselen = MIN (len, state->line_len);

		if (first) {
			first = FALSE;
			g_string_append_len (line, ptr, uselen);
		} else if (uselen > 0) {
			// Drop initial space from continuation line
			g_string_append_len (line, ptr + 1, uselen - 1);
		}

		if (len < state->line_len)
			break;
	}

	if (line->len > state->buffer_size) {
		state->buffer_size = line->len;
		state->buffer = g_realloc (state->buffer, state->buffer_size + 1);
	}

	ptr = line->str;
	end = ptr + line->len;
	buf = state->buffer;

	// g_printerr ("Pre [%s]\n", ptr);

	while (ptr < end) {
		if (*ptr != '^') {
			*(buf++) = *(ptr++);
			continue;
		}

		if (ptr[1] == '^') {
			// An encoded carat
			*(buf++) = '^', ptr += 2;
			continue;
		}

		if (ptr[1] == '\0' || ptr[2] == '\0') {
			applix_parse_error (state, _("Missing characters for character encoding"));
			*(buf++) = *(ptr++);
		} else if (ptr[1] < 'a' || ptr[1] > 'p' ||
			   ptr[2] < 'a' || ptr[2] > 'p') {
			applix_parse_error (state, _("Invalid characters for encoding '%c%c'"),
					    ptr[1], ptr[2]);
			*(buf++) = *(ptr++);
		} else {
			guchar uc = ((ptr[1] - 'a') << 4) | (ptr[2] - 'a');
			gsize utf8_len;
			char *utf8buf = g_convert_with_iconv (&uc, 1, state->converter, NULL,
							      &utf8_len, NULL);
			memcpy (buf, utf8buf, utf8_len);
			buf += utf8_len;
			g_free (utf8buf);
			ptr += 3;
		}
	}

	if (line->len == 0) {
		g_string_free (line, TRUE);
		return NULL;
	}

	if (buf)
		*buf = 0;

	g_string_free (line, TRUE);

	//g_printerr ("Post: [%s]\n", state->buffer);
	return state->buffer;
}

static gboolean
applix_read_colormap (ApplixReadState *state)
{
	unsigned char *buffer, *pos, *iter, *end;
	int count;
	long numbers[6];


	while (NULL != (buffer = applix_get_line (state))) {

		if (!a_strncmp (buffer, "END COLORMAP"))
			return FALSE;

		iter = pos = buffer + strlen (buffer) - 1;
		for (count = 6; --count >= 0; pos = iter) {
			while (--iter > buffer && g_ascii_isdigit (*iter))
				;

			if (iter <= buffer || *iter != ' ')
				return TRUE;

			numbers[count] = au_strtol (iter + 1, &end);
			if (end != pos || numbers[count] < 0 || numbers[count] > 255)
				return TRUE;
		}
		if (numbers[0] != 0 || numbers[5] != 0)
			return TRUE;

		*pos = '\0';

		{
			int const c = numbers[1];
			int const m = numbers[2];
			int const y = numbers[3];
			int const k = numbers[4];
			guint8 r, g, b;

			/* From Shelf-2.1 /gui/colorcom.c:1330 */
			/* cmyk to rgb */
			r = 255 - MIN(255, c+k); /* red */
			g = 255 - MIN(255, m+k); /* green */
			b = 255 - MIN(255, y+k); /* blue */

			/* Store the result */
			g_ptr_array_add	(state->colors,
					 gnm_color_new_rgb8 (r, g, b));
#if 0
			g_printerr ("'%s' %ld %ld %ld %ld\n", buffer, numbers[1],
				    numbers[2], numbers[3], numbers[4]);
#endif
		}
	}

	return TRUE;
}

static gboolean
applix_read_typefaces (ApplixReadState *state)
{
	unsigned char *ptr;

	while (NULL != (ptr = applix_get_line (state))) {
		if (!a_strncmp (ptr, "END TYPEFACE TABLE"))
			return FALSE;
		g_ptr_array_add	(state->font_names, g_strdup (ptr));
	}

	return FALSE;
}

static GnmColor *
applix_get_color (ApplixReadState *state, char **buf)
{
	/* Skip 'FG' or 'BG' */
	char *start = *buf+2;
	int num = a_strtol (start, buf);

	if (start == *buf) {
		applix_parse_error (state, "Invalid color");
		return NULL;
	}

	if (num >= 0 && num < (int)state->colors->len)
		return style_color_ref (g_ptr_array_index(state->colors, num));

	return style_color_black ();
}

static int
applix_get_precision (char const *val)
{
	if ('0' <= *val && *val <= '9')
		return *val - '0';
	if (*val != 'f')
		g_warning ("APPLIX : unknow number format %c", *val);
	return 2;
}

static GnmStyle *
applix_parse_style (ApplixReadState *state, unsigned char **buffer)
{
	GnmStyle *style;
	char *start = *buffer, *tmp = start;
	gboolean is_protected = FALSE, is_invisible = FALSE;
	char const *format_prefix = NULL, *format_suffix = NULL;
	int font_id = 0; /* default */

	*buffer = NULL;
	if (*tmp == 'P') {
		is_protected = TRUE;
		tmp = ++start;
	}
	if (*tmp == 'I') {
		is_invisible = TRUE;
		tmp = ++start;
	}
	if ((is_protected || is_invisible)) {
		if (*tmp != ' ') {
			applix_parse_error (state, "Invalid format, protection problem");
			return NULL;
		}
		tmp = ++start;
	}

	if (*tmp != '(') {
		applix_parse_error (state, "Invalid format, missing '('");
		return NULL;
	}

	while (*(++tmp) && *tmp != ')')
		;

	if (tmp[0] != ')' || tmp[1] != ' ') {
		applix_parse_error (state, "Invalid format missing ')'");
		return NULL;
	}

	/* Look the descriptor string up in the hash of parsed styles */
	tmp[1] = '\0';
	style = g_hash_table_lookup (state->styles, start);
	if (style == NULL) {
		/* Parse the descriptor */
		char *sep = start;

		/* Allocate the new style */
		style = gnm_style_new_default ();

		gnm_style_set_contents_locked (style, is_protected);
		gnm_style_set_contents_hidden (style, is_invisible);

		if (sep[1] == '\'')
			sep += 2;
		else
			++sep;

		/* Formatting and alignment */
		for (; *sep && *sep != '|' && *sep != ')' ; ) {

			if (*sep == ',') {
				++sep;
				continue;
			}

			if (g_ascii_isdigit (*sep)) {
				GnmHAlign a;
				switch (*sep) {
				case '1' : a = GNM_HALIGN_LEFT; break;
				case '2' : a = GNM_HALIGN_RIGHT; break;
				case '3' : a = GNM_HALIGN_CENTER; break;
				case '4' : a = GNM_HALIGN_FILL; break;
				default :
					applix_parse_error (state, "Unknown horizontal alignment '%c'", *sep);
					return NULL;
				}
				gnm_style_set_align_h (style, a);
				++sep;
			} else if (*sep == 'V') {
				GnmVAlign a;
				switch (sep[1]) {
				case 'T' : a = GNM_VALIGN_TOP; break;
				case 'C' : a = GNM_VALIGN_CENTER; break;
				case 'B' : a = GNM_VALIGN_BOTTOM; break;
				default :
					applix_parse_error (state, "Unknown vertical alignment '%c'", *sep);
					return NULL;
				}
				gnm_style_set_align_v (style, a);
				sep += 2;
				break;
			} else {
				gboolean get_precision = FALSE;
				char const *format = NULL;

				switch (*sep) {
				case 'D' : {
					int id = 0;
					char *end;
					static char const * const date_formats[] = {
						/*  1 */ "mmmm d, yyyy",
						/*  2 */ "mmm d, yyyy",
						/*  3 */ "d mmm yy",
						/*  4 */ "mm/dd/yy",
						/*  5 */ "dd.mm.yy",
						/*  6 */ "yyyy-mm-dd",
						/*  7 */ "yy-mm-dd",
						/*  8 */ "yyyy mm dd",
						/*  9 */ "yy mm dd",
						/* 10 */ "yyyymmdd",
						/* 11 */ "yymmdd",
						/* 12 */ "dd/mm/yy",
						/* 13 */ "dd.mm.yyyy",
						/* 14 */ "mmm dd, yyyy",
						/* 15 */ "mmmm yyyy",
						/* 16 */ "mmm.yyyy"
					};

					/* General : do nothing */
					if (sep[1] == 'N') {
						sep += 2;
						break;
					}

					if (!g_ascii_isdigit (sep[1]) ||
					    (0 == (id = a_strtol (sep+1, &end))) ||
					    sep+1 == end ||
					    id < 1 || id > 16) {
						applix_parse_error (state, "Unknown format %d", id);
						return NULL;
					}
					format = date_formats[id - 1];
					sep = end;
					break;
				}

				case 'T' :
					switch (sep[1]) {
					case '0' : format = "hh:mm:ss AM/PM";	break;
					case '1' : format = "hh:mm AM/PM";	break;
					case '2' : format = "hh:mm:ss";		break;
					case '3' : format = "hh:mm";		break;
					default :
						applix_parse_error (state, "Unknown time format '%c'", sep[1]);
						return NULL;
					}
					sep += 2;
					break;

				case 'G' : /* general */
					gnm_style_set_format (style, go_format_general ());

					/* What is 'Gf' ? */
					if (sep[1] == 'f')
						sep += 2;
					else while (g_ascii_isdigit (*(++sep)))
						;
					break;

				case 'C' : /* currency or comma */
					/* comma 'CO' */
					if (sep[1] == 'O') {
						++sep;
						format_prefix = "#,##0";
					} else
						/* FIXME : what currency to use for differnt locales */
						format_prefix = "$ #,##0";

					format_suffix = "";
					get_precision = TRUE;
					break;

				case 'S' : /* scientific */
					format_suffix = "E+00";
					get_precision = TRUE;
					break;

				case 'P' : /* percentage */
					format_suffix = "%";
					get_precision = TRUE;
					break;

				case 'F' : /* fixed */
					get_precision = TRUE;
					break;

#if 0
				/* FIXME : Add these to gnumeric ? */
				case "GR0" : Graph ?  Seems like a truncated integer histogram
					     /* looks like crap, no need to support */

#endif
				case 'B' : if (sep[1] == '0') {
						   /* TODO : support this in gnumeric */
						   sep += 2;
						   break;
					   }
					   /* Fall through */
				default :
					applix_parse_error (state, "Unknown format '%c'", *sep);
					return NULL;
				}

				if (get_precision) {
					static char const *zeros = "000000000";
					char *tmp_format;
					char const *prec = "", *decimal = "";
					int n_prec = applix_get_precision (++sep);

					sep++;
					if (n_prec > 0) {
						prec = zeros + 9 - n_prec;
						decimal = ".";
					}

					if (!format_prefix)
						format_prefix = "0";
					tmp_format = g_strconcat (format_prefix, decimal, prec,
								  format_suffix, NULL);

					gnm_style_set_format_text (style, tmp_format);
					g_free (tmp_format);
				} else if (NULL != format)
					gnm_style_set_format_text (style, format);
			}
		}

		/* Font spec */
		for (++sep ; *sep && *sep != '|' && *sep != ')' ; ) {

			/* check for the 1 character modifiers */
			switch (*sep) {
			case 'B' :
				gnm_style_set_font_bold (style, TRUE);
				++sep;
				break;
			case 'I' :
				gnm_style_set_font_italic (style, TRUE);
				++sep;
				break;
			case 'U' :
				gnm_style_set_font_uline (style, UNDERLINE_SINGLE);
				++sep;
				break;
			case 'D' :
				gnm_style_set_font_uline (style, UNDERLINE_DOUBLE);
				++sep;
				break;
			case 'f' :
				if (sep[1] == 'g' ) {
					/* TODO : what is this ?? */
					sep += 2;
					break;
				}
				applix_parse_error (state, "Unknown font modifier 'f%c'", sep[1]);
				return NULL;

			case 'F' :
				if (sep[1] == 'G' ) {
					GnmColor *color = applix_get_color (state, &sep);
					if (color == NULL)
						return NULL;
					gnm_style_set_font_color (style, color);
					break;
				}
				applix_parse_error (state, "Unknown font modifier F%c", sep[1]);
				return NULL;

			case 'P' : {
				char *start = ++sep;
				double size = go_strtod (start, &sep);

				if (start != sep && size > 0.) {
					gnm_style_set_font_size (style, size / gnm_app_dpi_to_pixels ());
					break;
				}
				applix_parse_error (state, "Invalid font size '%s", start);
				return NULL;
			}

			case 'W' :
				if (sep[1] == 'T') {
					/* FIXME : What is WTO ?? */
					if (sep[2] == 'O') {
						sep +=3;
						break;
					}
					gnm_style_set_wrap_text (style, TRUE);
					sep +=2;
					break;
				}
				applix_parse_error (state, "Unknown font modifier W%c", sep[1]);
				return NULL;

			case 'T' :
				if (sep[1] == 'F') {
					/* be a font ID numbered from 0 */
					char *start = (sep += 2);

					font_id = a_strtol (start, &sep);
					if (start == sep || font_id < 0 || font_id >= (int)state->font_names->len) {
						applix_parse_error (state, "Unknown font index %s", start);
						font_id = 0;
					}
					break;
				}


			default :
				applix_parse_error (state, "Unknown font modifier");
				return NULL;
			}

			if (*sep == ',')
				++sep;
		}

		if (*sep != '|' && *sep != ')') {
			applix_parse_error (state, "Invalid font specification");
			return NULL;
		}

		if (font_id < (int)state->font_names->len)
			gnm_style_set_font_name (style, g_ptr_array_index (state->font_names, font_id));

		/* Background, pattern, and borders */
		for (++sep ; *sep && *sep != ')' ; ) {

			if (sep[0] == 'S' && sep[1] == 'H')  {
				/* A map from applix patten
				 * indicies to gnumeric.
				 */
				static int const map[] = { 0,
					1,  6, 5,  4,  3, 2, 24,
					24, 14, 13, 17, 16, 15, 11,
					19, 20, 21, 22, 23,
				};
				char *end;
				int num = a_strtol (sep += 2, &end);

				if (sep == end || 0 >= num || num >= (int)G_N_ELEMENTS (map)) {
					applix_parse_error (state, "Unknown pattern %s", sep);
					goto error;
				}

				num = map[num];
				gnm_style_set_pattern (style, num);
				sep = end;

				if (sep[0] == 'F' && sep[1] == 'G' ) {
					GnmColor *color = applix_get_color (state, &sep);
					if (color == NULL)
						goto error;
					gnm_style_set_pattern_color (style, color);
				}

				if (sep[0] == 'B' && sep[1] == 'G') {
					GnmColor *color = applix_get_color (state, &sep);
					if (color == NULL)
						goto error;
					gnm_style_set_back_color (style, color);
				}
			} else if (sep[0] == 'T' || sep[0] == 'B' || sep[0] == 'L' || sep[0] == 'R') {
				/* A map from applix border indicies to gnumeric. */
				static GnmStyleBorderType const map[] = {0,
					GNM_STYLE_BORDER_THIN,
					GNM_STYLE_BORDER_MEDIUM,
					GNM_STYLE_BORDER_THICK,
					GNM_STYLE_BORDER_DASHED,
					GNM_STYLE_BORDER_DOUBLE
				};

				GnmColor *color;
				GnmStyleElement const type =
					(sep[0] == 'T') ? MSTYLE_BORDER_TOP :
					(sep[0] == 'B') ? MSTYLE_BORDER_BOTTOM :
					(sep[0] == 'L') ? MSTYLE_BORDER_LEFT : MSTYLE_BORDER_RIGHT;
				GnmStyleBorderOrientation const orient = (sep[0] == 'T' || sep[0] == 'B')
					? GNM_STYLE_BORDER_HORIZONTAL : GNM_STYLE_BORDER_VERTICAL;
				char *end;
				int num = a_strtol (++sep, &end);

				if (sep == end || 0 >= num || num >= (int)G_N_ELEMENTS (map)) {
					applix_parse_error (state, "Unknown border style %s", sep);
					goto error;
				}
				sep = end;

				if (sep[0] == 'F' && sep[1] == 'G' ) {
					color = applix_get_color (state, &sep);
					if (color == NULL)
						goto error;
				} else
					color = style_color_black ();

				gnm_style_set_border (style, type,
						   gnm_style_border_fetch (map[num], color, orient));
			}

			if (*sep == ',')
				++sep;
			else if (*sep != ')') {
				applix_parse_error (state, "Invalid pattern, background, or border");
				goto error;
			}
		}

		if (*sep != ')') {
			applix_parse_error (state, "Invalid pattern or background");
			goto error;
		}

		/* Store the newly parsed style along with its descriptor */
		g_hash_table_insert (state->styles, g_strdup (start), style);
	}

	*buffer = tmp + 2;
	gnm_style_ref (style);
	return style;

error:
	if (style)
		gnm_style_unref (style);
	return NULL;
}

static gboolean
applix_read_attributes (ApplixReadState *state)
{
	int count = 0;
	unsigned char *ptr, *tmp;
	GnmStyle *style;

	while (NULL != (ptr = applix_get_line (state))) {
		if (!a_strncmp (ptr, "Attr Table End"))
			return FALSE;

		if (ptr[0] != '<')
			return applix_parse_error (state, "Invalid attribute");

		/* TODO : The first style seems to be a different format */
		if (count++) {
			tmp = ptr + 1;
			style = applix_parse_style (state, &tmp);
			if (style == NULL || *tmp != '>')
				return applix_parse_error (state, "Invalid attribute");
			g_ptr_array_add	(state->attrs, style);
		}
	}

	/* NOTREACHED */
	return FALSE;
}

static Sheet *
applix_fetch_sheet (ApplixReadState *state, char const *name)
{
	Sheet *sheet = workbook_sheet_by_name (state->wb, name);

	if (sheet == NULL) {
		int cols = APPLIX_SHEET_MAX_COLS;
		int rows = APPLIX_SHEET_MAX_ROWS;
		gnm_sheet_suggest_size (&cols, &rows);
		sheet = sheet_new (state->wb, name, cols, rows);
		workbook_sheet_attach (state->wb, sheet);
		g_object_set (sheet, "zoom-factor", state->zoom / 100.0, NULL);
		sheet_flag_recompute_spans (sheet);
	}

	return sheet;
}

static Sheet *
applix_parse_sheet (ApplixReadState *state, unsigned char **buffer,
		    char const separator)
{
	Sheet *sheet;

	/* Get sheet name */
	char *tmp = strchr (*buffer, separator);

	if (tmp == NULL) {
		applix_parse_error (state, "Invalid sheet name.");
		return NULL;
	}

	*tmp = '\0';
	sheet = applix_fetch_sheet (state, *buffer);
	*buffer = tmp+1;
	return sheet;
}

static char *
applix_parse_cellref (ApplixReadState *state, unsigned char *buffer,
		      Sheet **sheet, GnmCellPos *pos,
		      char const separator)
{
	*sheet = applix_parse_sheet (state, &buffer, separator);

	/* Get cell addr */
	if (*sheet) {
		buffer = (unsigned char *)applix_cellpos_parse
			(buffer, *sheet, pos, FALSE);
		if (buffer)
			return buffer;
	}

	*sheet = NULL;
	pos->col = pos->row = -1;
	return NULL;
}

static int
applix_height_to_pixels (int height)
{
	return height+4;
}
static int
applix_width_to_pixels (int width)
{
	return width*8 + 3;
}

static int
applix_read_current_view (ApplixReadState *state, unsigned char *buffer)
{
	/* What is this ? */
	unsigned char *ptr;
	while (NULL != (ptr = applix_get_line (state)))
	       if (!a_strncmp (ptr, "End View, Name: ~Current~"))
		       return 0;
	return -1;
}

static int
applix_read_view (ApplixReadState *state, unsigned char *buffer)
{
	Sheet *sheet = NULL;
	unsigned char *name = buffer + 19;
	unsigned char *tmp;
	gboolean ignore;

	tmp = strchr (name, ':');
	if (tmp == NULL)
		return 0;
	*tmp =  '\0';

	ignore = tmp[1] != '~';
	if (!ignore)
		state->sheet_order = g_slist_prepend (state->sheet_order,
			applix_fetch_sheet (state, name));

	while (NULL != (buffer = applix_get_line (state))) {
		if (!a_strncmp (buffer, "View End, Name: ~"))
			break;
		if (ignore)
			continue;

		if (!a_strncmp (buffer, "View Top Left: ")) {
			GnmCellPos pos;
			if (applix_parse_cellref (state, buffer+15, &sheet, &pos, ':') &&
			    valid_cellpos (sheet, &pos))
				gnm_sheet_view_set_initial_top_left (sheet_get_view (sheet, state->wb_view),
							 pos.col, pos.row);
		} else if (!a_strncmp (buffer, "View Open Cell: ")) {
			GnmCellPos pos;
			if (applix_parse_cellref (state, buffer+16, &sheet, &pos, ':') &&
			    valid_cellpos (sheet, &pos))
				sv_selection_set (sheet_get_view (sheet, state->wb_view),
						  &pos, pos.col, pos.row, pos.col, pos.row);
		} else if (!a_strncmp (buffer, "View Default Column Width ")) {
			char *ptr, *tmp = buffer + 26;
			int width = a_strtol (tmp, &ptr);
			if (tmp == ptr || width <= 0)
				return applix_parse_error (state, "Invalid default column width");

			sheet_col_set_default_size_pixels (sheet,
				applix_width_to_pixels (width));
		} else if (!a_strncmp (buffer, "View Default Row Height: ")) {
			char *ptr, *tmp = buffer + 25;
			int height = a_strtol (tmp, &ptr);
			if (tmp == ptr || height <= 0)
				return applix_parse_error (state, "Invalid default row height");

			/* height + one for the grid line */
			sheet_row_set_default_size_pixels (sheet,
				applix_height_to_pixels (height));
		} else if (!a_strncmp (buffer, "View Row Heights: ")) {
			char *ptr = buffer + 17;
			do {
				int row, height;
				char *tmp;

				row = a_strtol (tmp = ptr + 1, &ptr) - 1;
				if (tmp == ptr || row < 0 || *ptr != ':')
					return applix_parse_error (state, "Invalid row size row number");
				height = a_strtol (tmp = ptr + 1, &ptr);
				if (height >= 32768)
					height -= 32768;

				if (tmp == ptr || height <= 0)
					return applix_parse_error (state, "Invalid row size");

				/* These seem to assume
				 * top margin 2
				 * bottom margin 1
				 * size in pixels = val -32768 (sometimes ??)
				 */
				sheet_row_set_size_pixels (sheet, row,
							  applix_height_to_pixels (height),
							  TRUE);
			} while (ptr[0] == ' ' && g_ascii_isdigit (ptr[1]));
		} else if (!a_strncmp (buffer, "View Column Widths: ")) {
			char const *ptr = buffer + 19;
			char const *tmp;
			int col, width;
			unsigned char dummy;

			do {
				ptr = applix_col_parse (tmp = ptr + 1, &col, &dummy);
				if (!ptr || *ptr != ':')
					return applix_parse_error (state, "Invalid column");
				width = a_strtol (tmp = ptr + 1, (char **)&ptr);
				if (tmp == ptr || width <= 0)
					return applix_parse_error (state, "Invalid column size");

				/* These seem to assume
				 * pixels = 8*width + 3 for the grid lines and margins
				 */
				sheet_col_set_size_pixels (sheet, col,
							   applix_width_to_pixels (width),
							   TRUE);
			} while (ptr[0] == ' ' && g_ascii_isalpha (ptr[1]));
		}
	}

	return 0;
}

static int
applix_read_cells (ApplixReadState *state)
{
	Sheet *sheet;
	GnmStyle *style;
	GnmCell *cell;
	GnmCellPos pos;
	GnmParseError  perr;
	unsigned char content_type, *tmp, *ptr;

	while (NULL != (ptr = applix_get_line (state))) {
		gboolean const val_is_string = (ptr[0] != '\0' && ptr[1] == '\'');

	       if (!a_strncmp (ptr, "*END SPREADSHEETS"))
		       break;

		/* Parse formatting */
		style = applix_parse_style (state, &ptr);
		if (style == NULL)
			return -1;
		if (ptr == NULL) {
			gnm_style_unref (style);
			return -1;
		}

		/* Get cell */
		ptr = applix_parse_cellref (state, ptr, &sheet, &pos, '!');
		if (ptr == NULL) {
			gnm_style_unref (style);
			return applix_parse_error (state, "Expression did not specify target cell");
		}

		if (!valid_cellpos (sheet, &pos)) {
			gnm_style_unref (style);
			g_warning ("Ignoring sheet contents beyond allowed range.");
			continue;
		}

		cell = sheet_cell_fetch (sheet, pos.col, pos.row);

		/* Apply the formatting */
		sheet_style_set_pos (sheet, pos.col, pos.row, style);
		content_type = *ptr;
		switch (content_type) {
		case ';' : /* First of a shared formula */
		case '.' : { /* instance of a shared formula */
			GnmParsePos	 pos;
			GnmValue		*val = NULL;
			GnmRange		 r;
			char *expr_string;

			ptr = applix_parse_value (ptr+2, &expr_string);

			/* Just in case something failed */
			if (ptr == NULL)
				return -1;

			if (!val_is_string)
				/* Does it match any formats (use default date convention) */
				val = format_match (ptr, NULL, NULL);

			if (val == NULL)
				/* TODO : Could this happen ? */
				val = value_new_string (ptr);

#if 0
			g_printerr ("\'%s\'\n\'%s\'\n", ptr, expr_string);
#endif

			if (content_type == ';') {
				GnmExprTop const *texpr;
				gboolean	is_array = FALSE;

				if (*expr_string == '~') {
					Sheet *start_sheet, *end_sheet;
					tmp = applix_parse_cellref (state, expr_string+1, &start_sheet,
								    &r.start, ':');
					if (start_sheet == NULL || tmp == NULL || tmp[0] != '.' || tmp[1] != '.') {
						applix_parse_error (state, "Invalid array expression");
						continue;
					}

					tmp = applix_parse_cellref (state, tmp+2, &end_sheet,
								    &r.end, ':');
					if (end_sheet == NULL || tmp == NULL || tmp[0] != '~') {
						applix_parse_error (state, "Invalid array expression");
						continue;
					}

					if (start_sheet != end_sheet) {
						applix_parse_error (state, "3D array functions are not supported.");
						continue;
					}

					if (!valid_cellpos (start_sheet, &r.start) ||
					    !valid_cellpos (end_sheet, &r.end)) {
						g_warning ("Ignoring sheet contents beyond allowed range.");
						continue;
					}

					is_array = TRUE;
					expr_string = tmp+3; /* ~addr~<space><space>=expr */
				}

				/* We need to continue at all costs so that the
				 * rest of the sheet can be parsed. If we quit, then trailing
				 * 'Formula ' lines confuse the parser
				 */
				(void) parse_error_init(&perr);
				if (*expr_string != '=' && *expr_string != '+') {
					applix_parse_error (state, _("Expression did not start with '=' ? '%s'"),
								   expr_string);
					texpr = gnm_expr_top_new_constant (value_new_string (expr_string));
				} else
					texpr = gnm_expr_parse_str (expr_string+1,
						parse_pos_init_cell (&pos, cell),
								   GNM_EXPR_PARSE_DEFAULT,
								   state->convs,
								   &perr);

				if (texpr == NULL) {
					applix_parse_error (state, _("%s!%s : unable to parse '%s'\n     %s"),
								   sheet->name_quoted, cell_name (cell),
								   expr_string, perr.err->message);
					texpr = gnm_expr_top_new_constant (value_new_string (expr_string));
				} else if (is_array) {
					gnm_cell_set_array (sheet,
							    &r,
							    texpr);
					gnm_cell_assign_value (cell, val);
					/* Leak? */
				} else {
					gnm_cell_set_expr_and_value (cell, texpr, val, TRUE);
					/* Leak? */
				}

				if (!applix_get_line (state) ||
				    a_strncmp (state->buffer, "Formula: ")) {
					applix_parse_error (state, "Missing formula ID");
					continue;
				}

				ptr = state->buffer + 9;

				/* Store the newly parsed expression along with its descriptor */
				g_hash_table_insert (state->exprs,
						     g_strdup (ptr),
						     (gpointer)texpr);

				parse_error_free (&perr);
			} else {
				GnmExprTop const *texpr;
				char const *key = expr_string + strlen (expr_string);
				while (key > expr_string && !g_ascii_isspace (key[-1]))
					key--;
#if 0
				g_printerr ("Shared '%s'\n", expr_string);
#endif
				texpr = g_hash_table_lookup (state->exprs, key);
				gnm_cell_set_expr_and_value (cell, texpr, val, TRUE);
			}
			break;
		}

		case ':' : { /* simple value */
			GnmValue *val = NULL;

			ptr += 2;
#if 0
			g_printerr ("\"%s\" %d\n", ptr, val_is_string);
#endif
			/* Does it match any formats (use default date convention) */
			if (!val_is_string)
				val = format_match (ptr, NULL, NULL);
			if (val == NULL)
				val = value_new_string (ptr);

			if (gnm_cell_is_array (cell))
				gnm_cell_assign_value (cell, val);
			else
				gnm_cell_set_value (cell, val);
			break;
		}

		default :
			g_warning ("Unknown cell type '%c'", content_type);
		}
	}

	return 0;
}

static int
applix_read_row_list (ApplixReadState *state, unsigned char *ptr)
{
	unsigned char *tmp;
	GnmRange	r;
	Sheet *sheet = applix_parse_sheet (state, &ptr, ' ');

	if (ptr == NULL)
		return -1;
	if (*ptr != '!')
		return applix_parse_error (state, "Invalid row format");

	r.start.row = r.end.row = au_strtol (++ptr, &tmp) - 1;
	if (tmp == ptr || r.start.row < 0 || tmp[0] != ':' || tmp[1] != ' ')
		return applix_parse_error (state, "Invalid row format row number");

	++tmp;
	do {
		unsigned attr_index;

		r.start.col = au_strtol (ptr = tmp+1, &tmp);
		if (tmp == ptr || r.start.col < 0 || tmp[0] != '-')
			return applix_parse_error (state, "Invalid row format start col");
		r.end.col = au_strtol (ptr = tmp+1, &tmp);
		if (tmp == ptr || r.end.col < 0 || tmp[0] != ':')
			return applix_parse_error (state, "Invalid row format end col");
		attr_index = au_strtol (ptr = tmp+1, &tmp);
		if (tmp != ptr && attr_index >= 2 && attr_index < state->attrs->len+2) {
			GnmStyle *style = g_ptr_array_index(state->attrs, attr_index-2);
			gnm_style_ref (style);
			sheet_style_set_range (sheet, &r, style);
		} else if (attr_index != 1) /* TODO : What the hell is attr 1 ?? */
			return applix_parse_error (state, "Invalid row format attr index");

	/* Just for kicks they added a trailing space */
	} while (tmp[0] && g_ascii_isdigit (tmp[1]));

	return 0;
}

static gboolean
applix_read_sheet_table (ApplixReadState *state)
{
	unsigned char *ptr;
	unsigned char *std_name, *real_name;
	while (NULL != (ptr = applix_get_line (state))) {
	       if (!a_strncmp (ptr, "END SHEETS TABLE"))
		       return FALSE;

	       /* Sheet A: ~Foo~ */
	       std_name = ptr + 6;
	       ptr = strchr (std_name, ':');
	       if (ptr == NULL)
		       continue;
	       *ptr = '\0';

	       real_name = ptr + 3;
	       ptr = strchr (real_name, '~');
	       if (ptr == NULL)
		       continue;
	       *ptr = '\0';

	       state->std_names  = g_slist_prepend (state->std_names,
						    g_strdup (std_name));
	       state->real_names = g_slist_prepend (state->real_names,
						    g_strdup (real_name));
	}
	return TRUE;
}

static gboolean
applix_read_header_footer (ApplixReadState *state)
{
	unsigned char *ptr;
	while (NULL != (ptr = applix_get_line (state)))
	       if (!a_strncmp (ptr, "Headers And Footers End"))
		       return FALSE;
	return TRUE;
}

static gboolean
applix_read_absolute_name (ApplixReadState *state, char *buffer)
{
	char *end;
	GnmRangeRef ref;
	GnmParsePos pp;
	GnmExprTop const *texpr;

	/* .ABCDe. Coordinate: A:B2..A:C4 */
	/* Spec guarantees that there are no dots in the name */
	buffer = strchr (buffer, '.');
	if (buffer == NULL)
		return TRUE;
	end = strchr (++buffer, '.');
	if (end == NULL)
		return TRUE;
	*end = '\0';
	end = strchr (end + 1, ':');
	if (end == NULL)
		return TRUE;
	applix_rangeref_parse (&ref, end+2,
		parse_pos_init (&pp, state->wb, NULL, 0, 0),
		state->convs);
	ref.a.col_relative = ref.b.col_relative =
		ref.a.row_relative = ref.b.row_relative = FALSE;

	texpr = gnm_expr_top_new_constant
		(value_new_cellrange_unsafe (&ref.a, &ref.b));
	expr_name_add (&pp, buffer, texpr, NULL, TRUE, NULL);

	return FALSE;
}

static gboolean
applix_read_relative_name (ApplixReadState *state, char *buffer)
{
	int dummy;
	char *end;
	GnmRangeRef ref, flag;
	GnmParsePos pp;
	GnmExprTop const *texpr;

	/* .abcdE. tCol:0 tRow:0 tSheet:0 bCol:1 bRow:2 bSheet: 0 tColAbs:0 tRowAbs:0 tSheetAbs:1 bColAbs:0 bRowAbs:0 bSheetAbs:1 */
	/* Spec guarantees that there are no dots in the name */
	buffer = strchr (buffer, '.');
	if (buffer == NULL)
		return TRUE;
	end = strchr (++buffer, '.');
	if (end == NULL)
		return TRUE;
	*end = '\0';
	if (12 != sscanf (end + 2,
			  " tCol:%d tRow:%d tSheet:%d bCol:%d bRow:%d bSheet: %d tColAbs:%d tRowAbs:%d tSheetAbs:%d bColAbs:%d bRowAbs:%d bSheetAbs:%d",
			  &ref.a.col, &ref.a.row, &dummy, &ref.b.col, &ref.b.row, &dummy,
			  &flag.a.col, &flag.a.row, &dummy, &flag.b.col, &flag.b.row, &dummy))
		return TRUE;

	ref.a.col_relative = (flag.a.col == 0);
	ref.b.col_relative = (flag.b.col == 0);
	ref.a.row_relative = (flag.a.row == 0);
	ref.b.row_relative = (flag.b.row == 0);

	ref.a.sheet = ref.b.sheet = NULL;
	texpr = gnm_expr_top_new_constant
		(value_new_cellrange_unsafe (&ref.a, &ref.b));
	parse_pos_init (&pp, state->wb, NULL,
		MAX (-ref.a.col, 0), MAX (-ref.a.row, 0));
	expr_name_add (&pp, buffer, texpr, NULL, TRUE, NULL);

	return FALSE;
}

#define ABS_NAMED_RANGE	"Named Range, Name:"
#define REL_NAMED_RANGE	"Relative Named Range, Name:"

static int
applix_read_impl (ApplixReadState *state)
{
	Sheet *sheet;
	GnmCellPos pos;
	int ext_links = -1;
	unsigned char *real_name = NULL;
	char top_cell_addr[30] = "";
	char cur_cell_addr[30] = "";
	unsigned char *buffer;
	char default_text_format[128] = "";
	char default_number_format[128] = "";
	int def_col_width = -1;
	int win_width = -1;
	int win_height = -1;

	while (NULL != (buffer = applix_get_line (state))) {
		if (!a_strncmp (buffer, "*BEGIN SPREADSHEETS VERSION=")) {
			char encoding_buffer[32];
			int v0, v1;
			if (3 != sscanf (buffer, "*BEGIN SPREADSHEETS VERSION=%d/%d ENCODING=%31s",
					 &v0, &v1, encoding_buffer))
				return applix_parse_error (state, "Invalid header ");

			/* FIXME : Guess that version 400 is a minimum */
			if (v0 < 400)
				return applix_parse_error (state, "Versions < 4.0 are not supported");

			/* We only have a sample of '7BIT' right now */
			if (strcmp (encoding_buffer, "7BIT"))
				return applix_parse_error (state, "We only have samples of '7BIT' encoding, please send us this sample.");

		} else if (!a_strncmp (buffer, "Num ExtLinks:")) {
			if (1 != sscanf (buffer, "Num ExtLinks: %d", &ext_links))
				return applix_parse_error (state, "Missing number of external links");

		} else if (!a_strncmp (buffer, "Spreadsheet Dump Rev")) {
			int major_rev, minor_rev, len;
			if (3 != sscanf (buffer, "Spreadsheet Dump Rev %d.%d Line Length %d",
					 &major_rev, &minor_rev, &len))
				return applix_parse_error (state, "Missing dump revision");
			if (len < 0 || 65535 < len) /* magic sanity check */
				return applix_parse_error (state, "Invalid line length");
			state->line_len = len;

			d (0, g_printerr ("Applix load : Saved with revision %d.%d",
					  major_rev, minor_rev););
		} else if (!a_strncmp (buffer, "Current Doc Real Name:")) {
			g_free (real_name);
			real_name = NULL;  /* FIXME? g_strdup (buffer + 22); */

		} else if (!strcmp (buffer, "COLORMAP")) {
			if (applix_read_colormap (state))
				return applix_parse_error (state, "invalid colormap");

		} else if (!strcmp (buffer, "TYPEFACE TABLE")) {
			if (applix_read_typefaces (state))
				return applix_parse_error (state, "invalid typefaces");

		} else if (!strcmp (buffer, "Attr Table Start")) {
			if (applix_read_attributes (state))
				return applix_parse_error (state, "Invalid attribute table");

		} else if (!a_strncmp (buffer, "View, Name: ~Current~")) {
			if (0 != applix_read_current_view (state, buffer))
				return applix_parse_error (state, "Invalid view");

		} else if (!a_strncmp (buffer, "View Start, Name: ~")) {
			if (0 != applix_read_view (state, buffer))
				return applix_parse_error (state, "Invalid view");

		} else if (!a_strncmp (buffer, "Default Label Style")) {
			if (1 != sscanf (buffer, "Default Label Style %127s", default_text_format))
				return applix_parse_error (state, "invalid default label style");

		} else if (!a_strncmp (buffer, "Default Number Style")) {
			if (1 != sscanf (buffer, "Default Number Style %127s", default_number_format))
				return applix_parse_error (state, "invalid default number style");

		} else if (!a_strncmp (buffer, "Document Column Width:")) {
			if (1 != sscanf (buffer, "Document Column Width: %d", &def_col_width))
				return applix_parse_error (state, "invalid col width");

		} else if (!a_strncmp (buffer, "Percent Zoom Factor:")) {
			if (1 != sscanf (buffer, "Percent Zoom Factor: %d", &state->zoom) ||
			    state->zoom <= 10 || 500 <= state->zoom)
				return applix_parse_error (state, "invalid zoom");
		} else if (!a_strncmp (buffer, "Window Width:")) {
			if (1 != sscanf (buffer, "Window Width: %d", &win_width))
				return applix_parse_error (state, "invalid win width");
		} else if (!a_strncmp (buffer, "Window Height:")) {
			if (1 != sscanf (buffer, "Window Height: %d", &win_height))
				return applix_parse_error (state, "invalid win height");
		} else if (!a_strncmp (buffer, "Top Left:")) {
			if (1 != sscanf (buffer, "Top Left: %25s", top_cell_addr))
				return applix_parse_error (state, "invalid top left");
		} else if (!a_strncmp (buffer, "Open Cell:")) {
			if (1 != sscanf (buffer, "Open Cell: %25s", cur_cell_addr))
				return applix_parse_error (state, "invalid cur cell");
		} else if (!a_strncmp (buffer, "SHEETS TABLE")) {
			if (applix_read_sheet_table (state))
				return applix_parse_error (state, "sheet table");
		} else if (!a_strncmp (buffer, ABS_NAMED_RANGE)) {
			if (applix_read_absolute_name (state, buffer + sizeof (ABS_NAMED_RANGE)))
				return applix_parse_error (state, "Absolute named range");
		} else if (!a_strncmp (buffer, REL_NAMED_RANGE)) {
			if (applix_read_relative_name (state, buffer + sizeof (REL_NAMED_RANGE)))
				return applix_parse_error (state, "Relative named range");
		} else if (!a_strncmp (buffer, "Row List")) {
			if (applix_read_row_list (state, buffer + sizeof ("Row List")))
				return applix_parse_error (state, "row list");
		} else if (!a_strncmp (buffer, "Headers And Footers")) {
			if (applix_read_header_footer (state))
				return applix_parse_error (state, "headers and footers");

			break; /* BREAK OUT OF THE LOOP HERE */
		}
	}

	if (applix_read_cells (state))
		return -1;

	/* We only need the sheet, the visible cell, and edit pos are already set */
	if (applix_parse_cellref (state, cur_cell_addr, &sheet, &pos, ':') &&
	    valid_cellpos (sheet, &pos))
		wb_view_sheet_focus (state->wb_view, sheet);

	return 0;
}

static gboolean
cb_remove_texpr (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	gnm_expr_top_unref (value);
	return TRUE;
}
static gboolean
cb_remove_style (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	gnm_style_unref (value);
	return TRUE;
}

static GnmExpr const *
applix_func_map_in (GnmConventions const *conv, Workbook *scope,
		    char const *name, GnmExprList *args)
{
	static struct {
		char const *applix_name;
		char const *gnm_name;
	} const sc_func_renames[] = {
		{ "IPAYMT",	"IPMT" },
		{ "PAYMT",	"PMT" },
		{ "PPAYMT",	"PPMT" },
		{ NULL, NULL }
	};
	static GHashTable *namemap = NULL;

	GnmFunc  *f;
	char const *new_name;
	int i;

	if (NULL == namemap) {
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].applix_name; i++)
			g_hash_table_insert (namemap,
				(gchar *) sc_func_renames[i].applix_name,
				(gchar *) sc_func_renames[i].gnm_name);
	}

	if (NULL != namemap &&
	    NULL != (new_name = g_hash_table_lookup (namemap, name)))
		name = new_name;
	if (NULL == (f = gnm_func_lookup (name, scope)))
		f = gnm_func_add_placeholder (scope, name, "");
	return gnm_expr_new_funcall (f, args);
}

static GnmConventions *
applix_conventions_new (void)
{
	GnmConventions *conv = gnm_conventions_new ();

	conv->intersection_char		= 0;
	conv->accept_hash_logicals	= TRUE;
	conv->allow_absolute_sheet_references = TRUE;
	conv->range_sep_dotdot		= TRUE;
	conv->decimal_sep_dot = '.';
	conv->arg_sep = ',';
	conv->array_col_sep = ',';
	conv->array_row_sep = ';';
	conv->input.range_ref		= applix_rangeref_parse;
	conv->input.func		= applix_func_map_in;

	return conv;
}

void
applix_read (GOIOContext *io_context, WorkbookView *wb_view, GsfInput *src)
{
	int i;
	int res;
	ApplixReadState	state;
	GSList *ptr, *renamed_sheets;
	GnmLocale	*locale;

	locale = gnm_push_C_locale ();

	/* Init the state variable */
	state.input	  = (GsfInputTextline *)gsf_input_textline_new (src);
	state.parse_error = NULL;
	state.wb_view     = wb_view;
	state.wb          = wb_view_get_workbook (wb_view);
	state.exprs       = g_hash_table_new (&g_str_hash, &g_str_equal);
	state.styles      = g_hash_table_new (&g_str_hash, &g_str_equal);
	state.colors      = g_ptr_array_new ();
	state.attrs       = g_ptr_array_new ();
	state.font_names  = g_ptr_array_new ();
	state.buffer      = NULL;
	state.buffer_size = 0;
	state.line_len    = 80;
	state.sheet_order = NULL;
	state.std_names   = NULL;
	state.real_names  = NULL;
	state.convs       = applix_conventions_new ();
	state.converter   = g_iconv_open ("UTF-8", "ISO-8859-1");

	/* Actually read the workbook */
	res = applix_read_impl (&state);

	g_object_unref (state.input);
	g_free (state.buffer);

	state.sheet_order = g_slist_reverse (state.sheet_order);
	workbook_sheet_reorder (state.wb, state.sheet_order);
	g_slist_free (state.sheet_order);

	renamed_sheets = NULL;
	for (ptr = state.std_names; ptr != NULL ; ptr = ptr->next) {
		const char *name = ptr->data;
		Sheet *sheet = workbook_sheet_by_name (state.wb, name);
		int idx = sheet ? sheet->index_in_wb : -1;
		renamed_sheets = g_slist_prepend (renamed_sheets,
						  GINT_TO_POINTER (idx));
	}
	renamed_sheets = g_slist_reverse (renamed_sheets);
	workbook_sheet_rename (state.wb, renamed_sheets,
			       state.real_names,
			       GO_CMD_CONTEXT (io_context));
	g_slist_free (renamed_sheets);
	g_slist_free_full (state.std_names, g_free);
	g_slist_free_full (state.real_names, g_free);

	/* Release the shared expressions and styles */
	g_hash_table_foreach_remove (state.exprs, &cb_remove_texpr, NULL);
	g_hash_table_destroy (state.exprs);
	g_hash_table_foreach_remove (state.styles, &cb_remove_style, NULL);
	g_hash_table_destroy (state.styles);

	for (i = state.colors->len; --i >= 0 ; )
		style_color_unref (g_ptr_array_index (state.colors, i));
	g_ptr_array_free (state.colors, TRUE);

	for (i = state.attrs->len; --i >= 0 ; )
		gnm_style_unref (g_ptr_array_index(state.attrs, i));
	g_ptr_array_free (state.attrs, TRUE);

	for (i = state.font_names->len; --i >= 0 ; )
		g_free (g_ptr_array_index(state.font_names, i));
	g_ptr_array_free (state.font_names, TRUE);

	if (state.parse_error != NULL)
		go_io_error_info_set (io_context, state.parse_error);

	gnm_conventions_unref (state.convs);
	gsf_iconv_close (state.converter);
	gnm_pop_C_locale (locale);
}
