/* vim: set sw=8: */

/*
 * applix.c : Routines to read applix version 4 spreadsheets.
 *
 * I have no docs or specs for this format that are useful.
 * This is a guess based on some sample sheets.
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnome/libgnome.h>
#include "applix.h"
#include "application.h"
#include "expr.h"
#include "value.h"
#include "sheet.h"
#include "number-match.h"
#include "cell.h"
#include "parse-util.h"
#include "sheet-style.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "selection.h"
#include "position.h"
#include "ranges.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "error-info.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

static int debug_applix_read = 1;

typedef struct {
	FILE          *file;
	ErrorInfo     *parse_error;
	WorkbookView  *wb_view;
	Workbook      *wb;
	GHashTable    *exprs, *styles;
	GPtrArray     *colours;
	GPtrArray     *attrs;
	GPtrArray     *font_names;

	char *buffer;
	int buffer_size;
	int line_len;
	int zoom;
} ApplixReadState;

/* The maximum numer of character potentially involved in a new line */
#define MAX_END_OF_LINE_SLOP	16

static int
applix_parse_error (ApplixReadState *state, char const *msg)
{
	if (state->parse_error == NULL) {
		state->parse_error = error_info_new_str (
		                     _("Parse error while reading Applix file."));
	}
	error_info_add_details (state->parse_error,
	                        error_info_new_str (msg));
	return -1;
}

/**
 * applix_parse_value : Parse applix's optionally quoted values.
 *
 * @follow : A pointer to a char * that is adjusted to point 2 chars AFTER the
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

gboolean
applix_read_header (FILE *file)
{
	char encoding_buffer[32];
	int v0, v1, matches;

	matches  = fscanf (file, "*BEGIN SPREADSHEETS VERSION=%d/%d ENCODING=%31s\n",
			   &v0, &v1, encoding_buffer);

	if (matches != 3)
		return FALSE;

	/* FIXME : Guess that version 400 is a minimum */
	if (v0 < 400)
		return FALSE;

	/* We only have a sample of '7BIT' right now */
	return strcmp (encoding_buffer, "7BIT") == 0;
}
static gboolean
applix_read_colormap (ApplixReadState *state)
{
	char buffer[128];

	if (NULL == fgets (buffer, sizeof(buffer), state->file))
		return TRUE;

	if (strncmp (buffer, "COLORMAP", 8))
		return TRUE;

	while (1) {
		int count;
		char *pos, * iter;
		long numbers[6];

		if (NULL == fgets (buffer, sizeof(buffer), state->file))
			return TRUE;

		if (!strncmp (buffer, "END COLORMAP", 12))
			return FALSE;

		pos = buffer + strlen(buffer) - 2;

		g_return_val_if_fail (pos >= buffer, TRUE);

		iter = pos;
		for (count = 6; --count >= 0; pos = iter) {
			char *end;
			while (--iter > buffer && isdigit ((unsigned char)*iter))
				;

			if (iter <= buffer || *iter != ' ')
				return TRUE;

			numbers[count] = strtol (iter+1, &end, 10);
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
			g_ptr_array_add	(state->colours,
					 style_color_new_i8 (r, g, b));
#if 0
			printf ("'%s' %ld %ld %ld %ld\n", buffer, numbers[1],
				numbers[2], numbers[3], numbers[4]);
#endif
		}
	}

	/* NOTREACHED */
	return TRUE;
}

static gboolean
applix_read_typefaces (ApplixReadState *state)
{
	char buffer[128];

	if (NULL == fgets (buffer, sizeof(buffer), state->file))
		return TRUE;

	if (strncmp (buffer, "TYPEFACE TABLE", 14))
		return TRUE;

looper :
	if (NULL == fgets (buffer, sizeof(buffer), state->file))
		return TRUE;
	if (strncmp (buffer, "END TYPEFACE TABLE", 18)) {
		char *ptr = buffer;
		while (*ptr && *ptr != '\n' && *ptr != '\r')
			++ptr;
		*ptr = '\0';
		g_ptr_array_add	(state->font_names, g_strdup (buffer));
		goto looper;
	}

	return FALSE;
}

static StyleColor *
applix_get_colour (ApplixReadState *state, char **buf)
{
	/* Skip 'FG' or 'BG' */
	char *start = *buf+2;
	int num = strtol (start, buf, 10);

	if (start == *buf) {
		(void) applix_parse_error (state, "Invalid colour");
		return NULL;
	}

	if (num >= 0 && num < (int)state->colours->len)
		return style_color_ref (g_ptr_array_index(state->colours, num));

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

static MStyle *
applix_parse_style (ApplixReadState *state, char **buffer)
{
	MStyle *style;
	char *start = *buffer, *tmp = start;
	gboolean is_protected = FALSE, is_invisible = FALSE;
	char const *format_prefix = NULL, *format_suffix = NULL;

	*buffer = NULL;
	if (*tmp == 'P') {
		is_protected = TRUE;
		tmp = ++start;
	}
	if (*tmp == 'I') {
		is_invisible = TRUE;
		tmp = ++start;
	}
	/* TODO : Handle these flags */
	if ((is_protected || is_invisible)) {
		if (*tmp != ' ') {
			(void) applix_parse_error (state, "Invalid format, protection problem");
			return NULL;
		}
		tmp = ++start;
	}

	if (*tmp != '(') {
		(void) applix_parse_error (state, "Invalid format, missing '('");
		return NULL;
	}

	while (*(++tmp) && *tmp != ')')
		;

	if (tmp[0] != ')' || tmp[1] != ' ') {
		(void) applix_parse_error (state, "Invalid format missing ')'");
		return NULL;
	}

	/* Look the descriptor string up in the hash of parsed styles */
	tmp[1] = '\0';
	style = g_hash_table_lookup (state->styles, start);
	if (style == NULL) {
		/* Parse the descriptor */
		char *sep = start;

		/* Allocate the new style */
		style = mstyle_new_default ();

		if (sep[1] == '\'')
			sep += 2;
		else
			++sep;

		/* Formating and alignment */
		for (; *sep && *sep != '|' && *sep != ')' ; ) {

			if (*sep == ',') {
				++sep;
				continue;
			}

			if (isdigit ((unsigned char)*sep)) {
				StyleHAlignFlags a;
				switch (*sep) {
				case '1' : a = HALIGN_LEFT; break;
				case '2' : a = HALIGN_RIGHT; break;
				case '3' : a = HALIGN_CENTER; break;
				case '4' : a = HALIGN_FILL; break;
				default :
					(void) applix_parse_error (state, "Unknown horizontal alignment");
					return NULL;
				};
				mstyle_set_align_h (style, a);
				++sep;
			} else if (*sep == 'V') {
				StyleVAlignFlags a;
				switch (sep[1]) {
				case 'T' : a = VALIGN_TOP; break;
				case 'C' : a = VALIGN_CENTER; break;
				case 'B' : a = VALIGN_BOTTOM; break;
				default :
					(void) applix_parse_error (state, "Unknown vertical alignment");
					return NULL;
				};
				mstyle_set_align_v (style, a);
				sep += 2;
				break;
			} else {
				char *format = NULL;
				gboolean needs_free = FALSE;
				switch (*sep) {
				case 'D' :
				{
					int id = 0;
					char *end;
					static char * const date_formats[] = {
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

					if (!isdigit ((unsigned char)sep[1]) ||
					    (0 == (id = strtol (sep+1, &end, 10))) ||
					    sep+1 == end ||
					    id < 1 || id > 16)
						(void) applix_parse_error (state, "Unknown format");

					format = date_formats[id-1];
					sep = end;
					break;
				}
				case 'T' :
				{
					switch (sep[1]) {
					case '0' : format = "hh:mm:ss AM/PM";	break;
					case '1' : format = "hh:mm AM/PM";	break;
					case '2' : format = "hh:mm:ss";		break;
					case '3' : format = "hh:mm";		break;
					default :
						(void) applix_parse_error (state, "Unknown time format");
						return NULL;
					};
					sep += 2;
					break;
				}
				case 'G' : /* general */
					mstyle_set_format_text (style, "General");

					/* What is 'Gf' ? */
					if (sep[1] == 'f')
						sep += 2;
					else while (isdigit (*(unsigned char *)(++sep)))
						;
					break;

				case 'C' : /* currency or comma */
					/* comma 'CO' */
					if (sep[1] == 'O') {
						++sep;
						format_prefix = "#,###";
					} else
						/* FIXME : what currency to use for differnt locales */
						format_prefix = "$ #,###";

					format_suffix = "";

				case 'S' : /* scientific */
					if (!format_suffix)
						format_suffix = "E+00";
				case 'P' : /* percentage */
					if (!format_suffix)
						format_suffix = "%";

				case 'F' : /* fixed */
				{
					static char const *zeros = "000000000";
					char *format;
					char const *prec = "", *decimal = "";
					int n_prec = applix_get_precision (++sep);

					sep++;
					if (n_prec > 0) {
						prec = zeros + 9 - n_prec;
						decimal = ".";
					}

					if (!format_prefix)
						format_prefix = "0";
					format = g_strconcat (format_prefix, decimal, prec,
							      format_suffix, NULL);

					mstyle_set_format_text (style, format);
					g_free (format);
					break;
				}

#if 0
				/* FIXME : Add these to gnumeric ? */
				case 'GR0' : Graph ?  Seems like a truncated integer histogram
					     /* looks like crap, no need to support */

#endif
				case 'B' : if (sep[1] == '0') {
						   /* TODO : support this in gnumeric */
						   sep += 2;
						   break;
					   }
					   /* Fall through */
				default :
					(void) applix_parse_error (state, "Unknown format");
					return NULL;
				};
				if (format)
					mstyle_set_format_text (style, format);
				if (needs_free)
					g_free (format);
			}
		}

		/* Font spec */
		for (++sep ; *sep && *sep != '|' && *sep != ')' ; ) {

			/* check for the 1 character modifiers */
			switch (*sep) {
			case 'B' :
				mstyle_set_font_bold (style, TRUE);
				++sep;
				break;
			case 'I' :
				mstyle_set_font_italic (style, TRUE);
				++sep;
				break;
			case 'U' :
				mstyle_set_font_uline (style, TRUE);
				++sep;
				break;
			case 'f' :
				if (sep[1] == 'g' ) {
					/* TODO : what is this ?? */
					sep += 2;
					break;
				};
				(void) applix_parse_error (state, "Unknown font modifier");
				return NULL;

			case 'F' :
				if (sep[1] == 'G' ) {
					StyleColor *color = applix_get_colour (state, &sep);
					if (color == NULL)
						return NULL;
					mstyle_set_color (style, MSTYLE_COLOR_FORE, color);
					break;
				}
				(void) applix_parse_error (state, "Unknown font modifier");
				return NULL;

			case 'P' : {
				char *start = ++sep;
				double size = strtod (start, &sep);

				if (start != sep && size > 0.) {
					mstyle_set_font_size (style, size / application_dpi_to_pixels ());
					break;
				}
				(void) applix_parse_error (state, "Invalid font size");
				return NULL;
			}

			case 'W' :
				if (sep[1] == 'T') {
					/* FIXME : What is WTO ?? */
					if (sep[2] == 'O') {
						sep +=3;
						break;
					}
					mstyle_set_wrap_text (style, TRUE);
					sep +=2;
					break;
				}
				(void) applix_parse_error (state, "Unknown font modifier");
				return NULL;

			case 'T' :
				if (sep[1] == 'F') {
					/* Ok this should be a font ID (I assume numbered from 0) */
					char *start = (sep += 2);
					int font_id = strtol (start, &sep, 10);

					if (start == sep || font_id < 1 || font_id > (int)state->font_names->len)
						(void) applix_parse_error (state, "Unknown font modifier");
					else {
						char const *name = g_ptr_array_index (state->font_names, font_id-1);
						mstyle_set_font_name (style, name);
					}
					break;
				}


			default :
				(void) applix_parse_error (state, "Unknown font modifier");
				return NULL;
			};

			if (*sep == ',')
				++sep;
		}

		if (*sep != '|' && *sep != ')') {
			(void) applix_parse_error (state, "Unknown font modifier");
			return NULL;
		}

		/* Background, pattern, and borders */
		for (++sep ; *sep && *sep != ')' ; ) {

			if (sep[0] == 'S' && sep[1] == 'H')  {
				/* A map from applix patten
				 * indicies to gnumeric.
				 */
				static int const map[] = { 0,
					1,  6, 5,  4,  3, 2, 25,
					24, 14, 13, 17, 16, 15, 11,
					19, 20, 21, 22, 23,
				};
				char *end;
				int num = strtol (sep += 2, &end, 10);

				if (sep == end || 0 >= num || num >= (int)(sizeof(map)/sizeof(int))) {
					(void) applix_parse_error (state, "Unknown pattern");
					return NULL;
				}

				num = map[num];
				mstyle_set_pattern (style, num);
				sep = end;

				if (sep[0] == 'F' && sep[1] == 'G' ) {
					StyleColor *color = applix_get_colour (state, &sep);
					if (color == NULL)
						return NULL;
					mstyle_set_color (style, MSTYLE_COLOR_PATTERN, color);
				}

				if (sep[0] == 'B' && sep[1] == 'G') {
					StyleColor *color = applix_get_colour (state, &sep);
					if (color == NULL)
						return NULL;
					mstyle_set_color (style, MSTYLE_COLOR_BACK, color);
				}
			} else if (sep[0] == 'T' || sep[0] == 'B' || sep[0] == 'L' || sep[0] == 'R') {
				/* A map from applix border indicies to gnumeric. */
				static StyleBorderType const map[] = {0,
					STYLE_BORDER_THIN,
					STYLE_BORDER_MEDIUM,
					STYLE_BORDER_THICK,
					STYLE_BORDER_DASHED,
					STYLE_BORDER_DOUBLE
				};

				StyleColor *color;
				MStyleElementType const type =
					(sep[0] == 'T') ? MSTYLE_BORDER_TOP :
					(sep[0] == 'B') ? MSTYLE_BORDER_BOTTOM :
					(sep[0] == 'L') ? MSTYLE_BORDER_LEFT : MSTYLE_BORDER_RIGHT;
				StyleBorderOrientation const orient = (sep[0] == 'T' || sep[0] == 'B')
					? STYLE_BORDER_HORIZONTAL : STYLE_BORDER_VERTICAL;
				char *end;
				int num = strtol (++sep, &end, 10);

				if (sep == end || 0 >= num || num >= (int)(sizeof(map)/sizeof(int))) {
					(void) applix_parse_error (state, "Unknown border style");
					return NULL;
				}
				sep = end;

				if (sep[0] == 'F' && sep[1] == 'G' ) {
					color = applix_get_colour (state, &sep);
					if (color == NULL)
						return NULL;
				} else
					color = style_color_black ();

				mstyle_set_border (style, type,
						   style_border_fetch (map[num], color, orient));
			}

			if (*sep == ',')
				++sep;
			else if (*sep != ')') {
				(void) applix_parse_error (state, "Unknown pattern, background, or border");
				return NULL;
			}
		}

		if (*sep != ')') {
			(void) applix_parse_error (state, "Unknown pattern or background");
			return NULL;
		}

		/* Store the newly parsed style along with its descriptor */
		g_hash_table_insert (state->styles, g_strdup (start), style);
	}

	g_return_val_if_fail (style != NULL, NULL);

	*buffer = tmp + 2;
	mstyle_ref (style);
	return style;
}

static int
applix_read_attributes (ApplixReadState *state)
{
	char buffer[128];
	int count = 0;

	if (NULL == fgets (buffer, sizeof(buffer), state->file) ||
	    strcmp (buffer, "Attr Table Start\n"))
		return applix_parse_error (state, "Invalid attribute table");

	while (1) {
		char *tmp = buffer+1;
		MStyle *style;
		if (NULL == fgets (buffer, sizeof(buffer), state->file))
			return applix_parse_error (state, "Invalid attribute");

		if (!strncmp (buffer, "Attr Table End", 14))
			return 0;

		if (buffer[0] != '<')
			return applix_parse_error (state, "Invalid attribute");

		/* TODO : The first style seems to be a different format */
		if (count++) {
			style = applix_parse_style (state, &tmp);
			if (style == NULL || *tmp != '>')
				return applix_parse_error (state, "Invalid attribute");
			g_ptr_array_add	(state->attrs, style);
		}
	}

	/* NOTREACHED */
	return 0;
}

static Sheet *
applix_get_sheet (ApplixReadState *state, char **buffer,
		  char const separator)
{
	Sheet *sheet;

	/* Get sheet name */
	char *tmp = strchr (*buffer, separator);

	if (tmp == NULL) {
		(void) applix_parse_error (state, "Invalid sheet name.");
		return NULL;
	}

	*tmp = '\0';
	sheet = workbook_sheet_by_name (state->wb, *buffer);
	if (sheet == NULL) {
		sheet = sheet_new (state->wb, *buffer);
		workbook_sheet_attach (state->wb, sheet, NULL);
		sheet_set_zoom_factor (sheet, (double )(state->zoom) / 100., FALSE, FALSE);
	}

	*buffer = tmp+1;
	return sheet;
}

static char *
applix_parse_cellref (ApplixReadState *state, char *buffer,
		      Sheet **sheet, int *col, int *row,
		      char const separator)
{
	int len;

	*sheet = applix_get_sheet (state, &buffer, separator);

	/* Get cell addr */
	if (*sheet && parse_cell_name (buffer, col, row, FALSE, &len))
		return buffer + len;

	*sheet = NULL;
	*col = *row = -1;
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
applix_read_view (ApplixReadState *state, char *name)
{
	char buffer[128];
	Sheet *sheet;

	do {
		if (NULL == fgets (buffer, sizeof(buffer), state->file))
			return TRUE;

		if (!strncmp ("View Top Left: ", buffer, 15)) {
			int col, row;
			if (applix_parse_cellref (state, buffer+15, &sheet, &col, &row, ':'))
				sheet_set_initial_top_left (sheet, col, row);
		} else if (!strncmp ("View Open Cell: ", buffer, 16)) {
			int col, row;
			if (applix_parse_cellref (state, buffer+16, &sheet, &col, &row, ':'))
				sheet_selection_set (sheet, col, row, col, row, col, row);
		} else if (!strncmp ("View Default Column Width ", buffer, 26)) {
			char *ptr, *tmp = buffer + 26;
			int width = strtol (tmp, &ptr, 10);
			if (tmp == ptr || width <= 0)
				return applix_parse_error (state, "Invalid default column width");

			sheet_col_set_default_size_pixels (sheet,
							   applix_width_to_pixels (width));
		} else if (!strncmp ("View Default Row Height: ", buffer, 25)) {
			char *ptr, *tmp = buffer + 25;
			int height = strtol (tmp, &ptr, 10);
			if (tmp == ptr || height <= 0)
				return applix_parse_error (state, "Invalid default row height");

			/* height + one for the grid line */
			sheet_row_set_default_size_pixels (sheet,
							   applix_height_to_pixels (height));
		} else if (!strncmp (buffer, "View Row Heights: ", 18)) {
			char *ptr = buffer + 17;
			do {
				int row, height;
				char *tmp;

				row = strtol (tmp = ptr + 1, &ptr, 10) - 1;
				if (tmp == ptr || row < 0 || *ptr != ':')
					return applix_parse_error (state, "Invalid row size row number");
				height = strtol (tmp = ptr + 1, &ptr, 10);
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
			} while (ptr[0] == ' ' && isdigit ((unsigned char) ptr[1]));
		} else if (!strncmp (buffer, "View Column Widths: ", 20)) {
			char *ptr = buffer + 19;
			do {
				int col, width;
				char *tmp;

				col = parse_col_name (tmp = ptr + 1, (char const **)&ptr);
				if (tmp == ptr || col < 0 || *ptr != ':')
					return applix_parse_error (state, "Invalid column");
				width = strtol (tmp = ptr + 1, &ptr, 10);
				if (tmp == ptr || width <= 0)
					return applix_parse_error (state, "Invalid column size");

				/* These seem to assume
				 * pixels = 8*width + 3 for the grid lines and margins
				 */
				sheet_col_set_size_pixels (sheet, col,
							   applix_width_to_pixels (width),
							   TRUE);
			} while (ptr[0] == ' ' && isalpha ((unsigned char) ptr[1]));
		}
	} while (strncmp (buffer, "View End, Name: ~", 17));

	return 0;
}

static int
applix_read_views (ApplixReadState *state)
{
	char buffer[128];

	/* Ignore current view */
	do {
		if (NULL == fgets (buffer, sizeof(buffer), state->file))
			return -1;
	} while (strncmp (buffer, "End View, Name: ~Current~", 25));

loop :
	if (NULL == fgets (buffer, sizeof(buffer), state->file))
		return TRUE;

	if (!strncmp (buffer, "View Start, Name: ~", 19)) {
		char *name = buffer + 19;
		int len = strlen (name);

		g_return_val_if_fail (name[len-1] == '\n', -1);
		g_return_val_if_fail (name[len-2] == '~', -1);

		/* I wish there were some docs on this, names sometimes appear to be
		 * SHEET_LETTER:name~\n and sometimes
		 * SHEET_LETTER:name:~\n and sometimes
		 */
		if (name[len-3] == ':')
			name[len-3] = '\0';
		else
			name[len-2] = '\0';

		applix_read_view (state, name);
		goto loop;
	}
	return 0;
}

static gboolean
applix_get_line (ApplixReadState *state)
{
	char saved_char = '\0';
	char *ptr = state->buffer;
	int len = state->buffer_size;

loop :
	/* Read line length plus room for end of line characters */
	if (NULL == fgets(ptr, len, state->file))
		return FALSE;

	if ((int)strlen (ptr) > state->line_len) {
		/* Clip at the state line length */
		len -= state->line_len;
		if (len < 0) {
			int offset = ptr - state->buffer;
			len += state->line_len;
			state->buffer = g_renew (gchar, state->buffer, len);
			ptr = state->buffer + offset;
		}
		/* the extension lines have a leading space.
		 * To avoid reading the moving the line we copy over the last
		 * character of the preceding line and replace it later.
		 */
		if (saved_char)
			*ptr = saved_char;
		ptr += state->line_len-1;
		saved_char = *ptr;
		goto loop;
	}
	if (saved_char)
		*ptr = saved_char;
	return TRUE;
}

static int
applix_read_cells (ApplixReadState *state)
{
	Sheet *sheet;
	int col, row;

	while (applix_get_line (state) && strncmp (state->buffer, "*END SPREADSHEETS", 17)) {
		Cell *cell;
		char content_type, *tmp, *ptr = state->buffer;
		gboolean const val_is_string = (state->buffer[1] == '\'');

		/* Parse formatting */
		MStyle *style = applix_parse_style (state, &ptr);

		/* Error reported above */
		if (style == NULL)
			return -1;
		if (ptr == NULL) {
			mstyle_unref (style);
			return -1;
		}

		/* Get cell */
		ptr = applix_parse_cellref (state, ptr, &sheet, &col, &row, '!');
		if (ptr == NULL) {
			mstyle_unref (style);
			return applix_parse_error (state, "Expression did not specify target cell");
		}
		cell = sheet_cell_fetch (sheet, col, row);

		/* Apply the formating */
		sheet_style_set_pos (sheet, col, row, style);
		content_type = *ptr;
		switch (content_type) {
		case ';' : /* First of a shared formula */
		case '.' : { /* instance of a shared formula */
			ParsePos	 pos;
			ExprTree	*expr;
			Value		*val = NULL;
			Range		 r;
			char *expr_string;

			ptr = applix_parse_value (ptr+2, &expr_string);

			/* Just in case something failed */
			if (ptr == NULL)
				return -1;

			if (!val_is_string)
				/* Does it match any formats */
				val = format_match (ptr, NULL);

			if (val == NULL)
				/* TODO : Could this happen ? */
				val = value_new_string (ptr);

			tmp = expr_string + strlen (expr_string) - 1;
			*tmp = '\0';
#if 0
			printf ("\'%s\'\n\'%s\'\n", ptr, expr_string);
#endif

			if (content_type == ';') {
				gboolean	is_array = FALSE;

				if (*expr_string == '~') {
					Sheet *start_sheet, *end_sheet;
					tmp = applix_parse_cellref (state, expr_string+1, &start_sheet,
								    &r.start.col, &r.start.row, ':');
					if (start_sheet == NULL || tmp == NULL || tmp[0] != '.' || tmp[1] != '.') {
						(void) applix_parse_error (state, "Invalid array expression");
						continue;
					}

					tmp = applix_parse_cellref (state, tmp+2, &end_sheet,
								    &r.end.col, &r.end.row, ':');
					if (end_sheet == NULL || tmp == NULL || tmp[0] != '~') {
						(void) applix_parse_error (state, "Invalid array expression");
						continue;
					}

					if (start_sheet != end_sheet) {
						(void) applix_parse_error (state, "3D array functions are not supported.");
						continue;
					}

					is_array = TRUE;
					expr_string = tmp+3; /* ~addr~<space><space>=expr */
				}

				if (*expr_string != '=' && *expr_string != '+') {
					(void) applix_parse_error (state, "Expression did not start with '=' ?");
					continue;
				}

				expr = expr_parse_str (expr_string+1,
					parse_pos_init_cell (&pos, cell),
					GNM_PARSER_USE_APPLIX_REFERENCE_CONVENTIONS |
					GNM_PARSER_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC,
					NULL);
				if (expr == NULL) {
					(void) applix_parse_error (state, "Invalid expression");
					continue;
				}

				if (is_array) {
					expr_tree_ref (expr);
					cell_set_array_formula (sheet,
								r.start.col, r.start.row,
								r.end.col, r.end.row,
								expr);
					cell_assign_value (cell, val);
				} else
					cell_set_expr_and_value (cell, expr, val, TRUE);

				if (!applix_get_line (state) ||
				    strncmp (state->buffer, "Formula: ", 9)) {
					(void) applix_parse_error (state, "Missing forumula ID");
					continue;
				}

				ptr = state->buffer + 9;
				tmp = ptr + strlen (ptr) - 1;
				*tmp = '\0';

				/* Store the newly parsed expresion along with its descriptor */
				g_hash_table_insert (state->exprs, g_strdup (ptr), expr);
			} else {
#if 0
				printf ("shared '%s'\n", expr_string);
#endif
				expr = g_hash_table_lookup (state->exprs, expr_string);
				cell_set_expr_and_value (cell, expr, val, TRUE);
			}
			break;
		}

		case ':' : { /* simple value */
			Value *val = NULL;

			ptr += 2;
			tmp = ptr + strlen (ptr) - 1;
			*tmp = '\0';
#if 0
			printf ("\"%s\" %d\n", ptr, val_is_string);
#endif
			/* Does it match any formats */
			if (!val_is_string)
				val = format_match (ptr, NULL);
			if (val == NULL)
				val = value_new_string (ptr);

			if (cell_is_array (cell))
				cell_assign_value (cell, val);
			else
				cell_set_value (cell, val);
			break;
		}

		default :
			g_warning ("Unknown cell type '%c'", content_type);
		};
	}

	return 0;
}

static int
applix_read_impl (ApplixReadState *state)
{
	Sheet *sheet;
	int col, row;
	int ext_links;
	int major_rev, minor_rev;
	char real_name[128];
	int dummy, res;
	char dummy_c;
	char top_cell_addr[30], cur_cell_addr[30];
	char buffer[128];
	char default_text_format[128];
	char default_number_format[128];
	int def_col_width, win_width, win_height;
	gboolean valid_header = applix_read_header (state->file);

	g_return_val_if_fail (valid_header, -1);

	if (1 != fscanf (state->file, "Num ExtLinks: %d\n", &ext_links))
		return applix_parse_error (state, "Missing number of external links");

	if (3 != fscanf (state->file, "Spreadsheet Dump Rev %d.%d Line Length %d\n",
			 &major_rev, &minor_rev, &state->line_len))
		return applix_parse_error (state, "Missing dump revision");
	else if (state->line_len < 0 || 65535 < state->line_len) /* magic sanity check */
		return applix_parse_error (state, "Invalid line length");

	state->buffer_size = 2 * state->line_len + MAX_END_OF_LINE_SLOP;
	state->buffer = g_new (gchar, state->buffer_size);

	/* It appears some docs do not have this */
	if (1 != fscanf (state->file, "Current Doc Real Name: %127s\n", real_name)) {
		strncpy (real_name, "Unspecified", sizeof (real_name)-1);
		ungetc ((unsigned char)'C', state->file); /* restore the leading 'C' */
	}

#ifndef NO_APPLIX_DEBUG
	if (debug_applix_read > 0)
		printf ("Applix load '%s' : Saved with revision %d.%d\n",
			real_name, major_rev, minor_rev);
#endif
	if (applix_read_colormap (state))
		return applix_parse_error (state, "invalid colormap");
	if (applix_read_typefaces (state))
		return applix_parse_error (state, "invalid typefaces");
	if (0 != (res = applix_read_attributes (state)))
		return res;

	if (1 != fscanf (state->file, "No Auto Start Rt: %d\n", &dummy))
		return applix_parse_error (state, "invalid auto start");

	if (1 != fscanf (state->file, "No Auto Link Update: %d\n", &dummy))
		return applix_parse_error (state, "invalid auto link");

	if (1 != fscanf (state->file, "No Auto Start OD: %d\n", &dummy))
		return applix_parse_error (state, "invalid auto start OD");

	if (1 != fscanf (state->file, "No Auto Start Rt Insert: %d\n", &dummy))
		return applix_parse_error (state, "invalid auto start insert");

	if (0 != fscanf (state->file, "Date Numbers Standard\n"))
		return applix_parse_error (state, "invalid date num");

	if (0 != fscanf (state->file, "Calculation Automatic\n"))
		return applix_parse_error (state, "invalid auto calc");

	if (1 != fscanf (state->file, "Minimal Recalc: %d\n", &dummy))
		return applix_parse_error (state, "invalid minimal recalc");

	if (1 != fscanf (state->file, "Optimal Calc: %d\n", &dummy))
		return applix_parse_error (state, "invalid optimal calc");

	if (1 != fscanf (state->file, "Calculation Style %c\n", &dummy_c))
		return applix_parse_error (state, "invalid calc style");

	if (1 != fscanf (state->file, "Calc Background Enabled: %d\n", &dummy))
		return applix_parse_error (state, "invalid enabled background calc");

	if (1 != fscanf (state->file, "Calc On Display: %d\n", &dummy))
		return applix_parse_error (state, "invalid calc on display");

	if (1 != fscanf (state->file, "Calc Visible Cells Only:%d\n", &dummy))
		return applix_parse_error (state, "invalid calc visi");

	if (1 != fscanf (state->file, "Calc RtInsert On Display: %d\n", &dummy))
		return applix_parse_error (state, "invalid rtinsert on disp");

	if (1 != fscanf (state->file, "Calc Type Conversion: %d\n", &dummy))
		return applix_parse_error (state, "invalid type conv");

	if (1 != fscanf (state->file, "Calc Before Save: %d\n", &dummy))
		return applix_parse_error (state, "invalid calc before save");

	if (1 != fscanf (state->file, "Bottom Right Corner %25s\n", buffer))
		return applix_parse_error (state, "invalid bottom right");

	if (1 != fscanf (state->file, "Default Label Style %s\n", default_text_format))
		return applix_parse_error (state, "invalid default label style");

	if (1 != fscanf (state->file, "Default Number Style %s\n", default_number_format))
		return applix_parse_error (state, "invalid default number style");

	if (NULL == fgets (buffer, sizeof(buffer), state->file) ||
	    strncmp (buffer, "Symbols ", 8))
		return applix_parse_error (state, "invalid symbols ??");

	if (1 != fscanf (state->file, "Document Column Width: %d\n", &def_col_width))
		return applix_parse_error (state, "invalid col width");

	if (1 != fscanf (state->file, "Percent Zoom Factor: %d\n", &state->zoom) ||
	    state->zoom <= 10 || 500 <= state->zoom)
		return applix_parse_error (state, "invalid zoom");

	if (1 != fscanf (state->file, "Window Width: %d\n", &win_width))
		return applix_parse_error (state, "invalid win width");

	if (1 != fscanf (state->file, "Window Height: %d\n", &win_height))
		return applix_parse_error (state, "invalid win height");

	if (1 != fscanf (state->file, "Window X Pos: %d\n", &dummy))
		return applix_parse_error (state, "invalid win x");

	if (1 != fscanf (state->file, "Window Y Pos: %d\n", &dummy))
		return applix_parse_error (state, "invalid win y");

	if (1 != fscanf (state->file, "Top Left: %25s\n", top_cell_addr))
		return applix_parse_error (state, "invalid top left");

	if (1 != fscanf (state->file, "Open Cell: %25s\n", cur_cell_addr))
		return applix_parse_error (state, "invalid cur cell");
	/* TODO : find the rest of the selection */

	if (1 != fscanf (state->file, "Charting Preference: %s\n", buffer))
		return applix_parse_error (state, "invalid chart pref");

	if (0 != applix_read_views (state))
		return -1;

	while (NULL != fgets (buffer, sizeof(buffer), state->file) &&
	       strncmp (buffer, "Headers And Footers End", 23)) {

		if (!strncmp (buffer, "Row List ", 9)) {
			char *tmp, *ptr = buffer + 9;
			Range	r;

			Sheet *sheet = applix_get_sheet (state, &ptr, ' ');
			if (ptr == NULL)
				return -1;
			if (*ptr != '!')
				return applix_parse_error (state, "Invalid row format");

			r.start.row = r.end.row = strtol (++ptr, &tmp, 10) - 1;
			if (tmp == ptr || r.start.row < 0 || tmp[0] != ':' || tmp[1] != ' ')
				return applix_parse_error (state, "Invalid row format row number");

			++tmp;
			do {
				unsigned attr_index;

				r.start.col = strtol (ptr = tmp+1, &tmp, 10);
				if (tmp == ptr || r.start.col < 0 || tmp[0] != '-')
					return applix_parse_error (state, "Invalid row format start col");
				r.end.col = strtol (ptr = tmp+1, &tmp, 10);
				if (tmp == ptr || r.end.col < 0 || tmp[0] != ':')
					return applix_parse_error (state, "Invalid row format end col");
				attr_index = strtol (ptr = tmp+1, &tmp, 10);
				if (tmp != ptr && attr_index >= 2 && attr_index < state->attrs->len+2) {
					MStyle *style = g_ptr_array_index(state->attrs, attr_index-2);
					mstyle_ref (style);
					sheet_style_set_range (sheet, &r, style);
				} else if (attr_index != 1) /* TODO : What the hell is attr 1 ?? */
					return applix_parse_error (state, "Invalid row format attr index");

			/* Just for kicks they added a trailing space */
			} while (tmp[0] && isdigit ((unsigned char)tmp[1]));
		}

		/* FIXME : Can we really just ignore all of this ? */
	}

	if (!applix_read_cells (state)) {
	}

	/* We only need the sheet, the visible cell, and edit pos are already set */
	if (applix_parse_cellref (state, cur_cell_addr, &sheet, &col, &row, ':'))
		wb_view_sheet_focus (state->wb_view, sheet);

	return 0;
}

static gboolean
cb_remove_expr (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	expr_tree_unref (value);
	return TRUE;
}
static gboolean
cb_remove_style (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	mstyle_unref (value);
	return TRUE;
}

void
applix_read (IOContext *io_context, WorkbookView *wb_view, FILE *file)
{
	int i;
	int res;
	ApplixReadState	state;

	/* Init the state variable */
	state.file        = file;
	state.parse_error = NULL;
	state.wb_view     = wb_view;
	state.wb          = wb_view_workbook (wb_view);
	state.exprs       = g_hash_table_new (&g_int_hash, &g_int_equal);
	state.styles      = g_hash_table_new (&g_str_hash, &g_str_equal);
	state.colours     = g_ptr_array_new ();
	state.attrs       = g_ptr_array_new ();
	state.font_names  = g_ptr_array_new ();
	state.buffer      = NULL;

	/* Actualy read the workbook */
	res = applix_read_impl (&state);

	if (state.buffer)
		g_free (state.buffer);

	/* Release the shared expressions and styles */
	g_hash_table_foreach_remove (state.exprs, &cb_remove_expr, NULL);
	g_hash_table_destroy (state.exprs);
	g_hash_table_foreach_remove (state.styles, &cb_remove_style, NULL);
	g_hash_table_destroy (state.styles);

	for (i = state.colours->len; --i >= 0 ; )
		style_color_unref (g_ptr_array_index(state.colours, i));
	g_ptr_array_free (state.colours, TRUE);

	for (i = state.attrs->len; --i >= 0 ; )
		mstyle_unref (g_ptr_array_index(state.attrs, i));
	g_ptr_array_free (state.attrs, TRUE);

	for (i = state.font_names->len; --i >= 0 ; )
		g_free (g_ptr_array_index(state.font_names, i));
	g_ptr_array_free (state.font_names, TRUE);

	if (state.parse_error != NULL)
		gnumeric_io_error_info_set (io_context, state.parse_error);
}
