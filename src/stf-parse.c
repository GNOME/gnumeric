/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * stf-parse.c : Structured Text Format parser. (STF)
 *               A general purpose engine for parsing data
 *               in CSV and Fixed width format.
 *
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * Copyright (C) 2003 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "stf-parse.h"

#include "workbook.h"
#include "clipboard.h"
#include "sheet-style.h"
#include "value.h"
#include "mstyle.h"
#include "number-match.h"
#include "gutils.h"
#include "parse-util.h"
#include "format.h"
#include "datetime.h"

#include <stdlib.h>
#include <locale.h>

#define SETUP_LOCALE_SWITCH char *oldlocale = NULL

#define START_LOCALE_SWITCH if (parseoptions->locale) {\
oldlocale = g_strdup(gnumeric_setlocale (LC_ALL, NULL)); \
gnumeric_setlocale(LC_ALL, parseoptions->locale);}

#define END_LOCALE_SWITCH if (oldlocale) {\
gnumeric_setlocale(LC_ALL, oldlocale);\
g_free (oldlocale);}

#define WARN_TOO_MANY_ROWS _("Too many rows in data to parse: %d")

/* Source_t struct, used for interchanging parsing information between the low level parse functions */
typedef struct {
	char const *position;  /* Indicates the current position within data */

	/* Used internally for fixed width parsing */
	int splitpos;          /* Indicates current position in splitpositions array */
	int linepos;           /* Position on the current line */
} Source_t;

/* Struct used for autodiscovery */
typedef struct {
	int start;
	int stop;
} AutoDiscovery_t;

/*
 * Some silly dude make the length field an unsigned int.  C just does
 * not deal very well with that.
 */
static inline int
my_garray_len (const GArray *a)
{
	return (int)a->len;
}

static inline int
my_gptrarray_len (const GPtrArray *a)
{
	return (int)a->len;
}

static inline gboolean
comp_term (gchar const *s, gchar const *term)
{
	gchar const *this, *si = s;

	for (this = term; *term; term++, si++)
		if (*term != *si)
			return FALSE;
	return TRUE;
}

static int
compare_terminator (char const *s, StfParseOptions_t *parseoptions)
{
	const guchar *us = (const guchar *)s;
	GSList *l;

	if (*us > parseoptions->compiled_terminator.max ||
	    *us < parseoptions->compiled_terminator.min)
		return 0;

	for (l = parseoptions->terminator; l; l = l->next) {
		const char *term = l->data;
		const char *d = s;

		while (*term) {
			if (*d != *term)
				goto next;
			term++;
			d++;
		}
		return d - s;

	next:
		;
	}
	return 0;
}


/*******************************************************************************************************
 * STF PARSE OPTIONS : StfParseOptions related
 *******************************************************************************************************/

/**
 * stf_parse_options_new:
 *
 * This will return a new StfParseOptions_t struct.
 * The struct should, after being used, freed with stf_parse_options_free.
 **/
StfParseOptions_t *
stf_parse_options_new (void)
{
	StfParseOptions_t* parseoptions = g_new0 (StfParseOptions_t, 1);

	parseoptions->parsetype   = PARSE_TYPE_NOTSET;

	parseoptions->terminator  = NULL;
	stf_parse_options_add_line_terminator (parseoptions, "\r\n");
	stf_parse_options_add_line_terminator (parseoptions, "\n");
	stf_parse_options_add_line_terminator (parseoptions, "\r");

	parseoptions->trim_spaces = (TRIM_TYPE_RIGHT | TRIM_TYPE_LEFT);
	parseoptions->locale = NULL;

	parseoptions->splitpositions    = g_array_new (FALSE, FALSE, sizeof (int));

	parseoptions->stringindicator        = '"';
	parseoptions->indicator_2x_is_single = TRUE;
	parseoptions->duplicates             = FALSE;

	parseoptions->sep.str = NULL;
	parseoptions->sep.chr = NULL;

	parseoptions->col_import_array = NULL;
	parseoptions->formats = NULL;

	return parseoptions;
}

/**
 * stf_parse_options_free:
 *
 * will free @parseoptions, note that this will not free the splitpositions
 * member (GArray) of the struct, the caller is responsible for that.
 **/
void
stf_parse_options_free (StfParseOptions_t *parseoptions)
{
	g_return_if_fail (parseoptions != NULL);
	
	if (parseoptions->col_import_array)
		g_free (parseoptions->col_import_array);
	if (parseoptions->locale)
		g_free (parseoptions->locale);
	if (parseoptions->sep.chr)
		g_free (parseoptions->sep.chr);
	if (parseoptions->sep.str) {
		GSList *l;

		for (l = parseoptions->sep.str; l != NULL; l = l->next)
			g_free ((char *) l->data);
		g_slist_free (parseoptions->sep.str);
	}

	g_array_free (parseoptions->splitpositions, TRUE);

	stf_parse_options_clear_line_terminator (parseoptions);

	if (parseoptions->formats) {
		unsigned int ui;
		GPtrArray *formats = parseoptions->formats;

		for (ui = 0; ui < formats->len; ui++) {
			StyleFormat *sf = g_ptr_array_index (formats, ui);
			style_format_unref (sf);
		}
		g_ptr_array_free (formats, TRUE);
		parseoptions->formats = NULL;
	}

	g_free (parseoptions);
}

void
stf_parse_options_set_type (StfParseOptions_t *parseoptions, StfParseType_t const parsetype)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (parsetype == PARSE_TYPE_CSV || parsetype == PARSE_TYPE_FIXED);

	parseoptions->parsetype = parsetype;
}

static gint
long_string_first (gchar const *a, gchar const *b)
{
	/* This actually is UTF-8 safe.  */
	return strlen (b) - strlen (a);
}

static void
compile_terminators (StfParseOptions_t *parseoptions)
{
	GSList *l;
	GNM_SLIST_SORT (parseoptions->terminator, (GCompareFunc)long_string_first);

	parseoptions->compiled_terminator.min = 255;
	parseoptions->compiled_terminator.max = 0;
	for (l = parseoptions->terminator; l; l = l->next) {
		const guchar *term = l->data;
		parseoptions->compiled_terminator.min =
			MIN (parseoptions->compiled_terminator.min, *term);
		parseoptions->compiled_terminator.max =
			MAX (parseoptions->compiled_terminator.max, *term);
	}
}

/**
 * stf_parse_options_add_line_terminator:
 *
 * This will add to the line terminators, in both the Fixed width and CSV delimited importers
 * this indicates the end of a row.
 *
 **/
void
stf_parse_options_add_line_terminator (StfParseOptions_t *parseoptions, char const *terminator)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (terminator != NULL && *terminator != 0);

	GNM_SLIST_PREPEND (parseoptions->terminator, g_strdup (terminator));
	compile_terminators (parseoptions);
}

/**
 * stf_parse_options_remove_line_terminator:
 *
 * This will remove from the line terminators, in both the Fixed width and CSV delimited importers
 * this indicates the end of a row.
 *
 **/
void
stf_parse_options_remove_line_terminator (StfParseOptions_t *parseoptions, char const *terminator)
{
	GSList*    in_list;

	g_return_if_fail (parseoptions != NULL);

	in_list = g_slist_find_custom (parseoptions->terminator, terminator, g_str_compare);

	if (in_list) {
		char *s = in_list->data;
		GNM_SLIST_REMOVE (parseoptions->terminator, in_list->data);
		g_free (s);
		compile_terminators (parseoptions);
	}
}

/**
 * stf_parse_options_clear_line_terminator:
 *
 * This will clear the line terminator, in both the Fixed width and CSV delimited importers
 * this indicates the end of a row.
 *
 **/
void
stf_parse_options_clear_line_terminator (StfParseOptions_t *parseoptions)
{
	g_return_if_fail (parseoptions != NULL);

	g_slist_free_custom (parseoptions->terminator, g_free);
	parseoptions->terminator = NULL;
	compile_terminators (parseoptions);
}

/**
 * stf_parse_options_set_trim_spaces:
 *
 * If enabled will trim spaces in every parsed field on left and/or right
 * sides.
 **/
void
stf_parse_options_set_trim_spaces (StfParseOptions_t *parseoptions, StfTrimType_t const trim_spaces)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->trim_spaces = trim_spaces;
}

/**
 * stf_parse_options_csv_set_separators:
 *
 * A copy is made of the parameters.
 **/
void
stf_parse_options_csv_set_separators (StfParseOptions_t *parseoptions, char const *character,
				      GSList const *string)
{
	g_return_if_fail (parseoptions != NULL);

	g_free (parseoptions->sep.chr);
	parseoptions->sep.chr = g_strdup (character);

	g_slist_free_custom (parseoptions->sep.str, g_free);
	parseoptions->sep.str = g_slist_map (string, (GnmMapFunc)g_strdup);
}

void
stf_parse_options_csv_set_stringindicator (StfParseOptions_t *parseoptions, gunichar const stringindicator)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (stringindicator != '\0');

	parseoptions->stringindicator = stringindicator;
}

/**
 * stf_parse_options_csv_set_indicator_2x_is_single:
 * @indic_2x : a boolean value indicating whether we want to see two
 * 		adjacent string indicators as a single string indicator
 * 		that is part of the cell, rather than a terminator.
 **/
void
stf_parse_options_csv_set_indicator_2x_is_single (StfParseOptions_t *parseoptions,
						  gboolean const indic_2x)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->indicator_2x_is_single = indic_2x;
}

/**
 * stf_parse_options_csv_set_duplicates:
 * @duplicates : a boolean value indicating whether we want to see two
 *               separators right behind each other as one
 **/
void
stf_parse_options_csv_set_duplicates (StfParseOptions_t *parseoptions, gboolean const duplicates)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->duplicates = duplicates;
}

/**
 * stf_parse_options_fixed_splitpositions_clear:
 *
 * This will clear the splitpositions (== points on which a line is split)
 **/
void
stf_parse_options_fixed_splitpositions_clear (StfParseOptions_t *parseoptions)
{
	g_return_if_fail (parseoptions != NULL);

	g_array_free (parseoptions->splitpositions, TRUE);
	parseoptions->splitpositions = g_array_new (FALSE, FALSE, sizeof (int));
}

/**
 * stf_parse_options_fixed_splitpositions_add:
 *
 * @position will be added to the splitpositions, @position must be equal to
 * or greater than zero or -1 which means as much as "parse to end of line"
 **/
void
stf_parse_options_fixed_splitpositions_add (StfParseOptions_t *parseoptions, int const position)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (position >= 0 || position == -1);

	g_array_append_val (parseoptions->splitpositions, position);
}


/**
 * stf_parse_options_valid:
 * @parseoptions : an import options struct
 *
 * Checks if @parseoptions is correctly filled
 *
 * returns : TRUE if it is correctly filled, FALSE otherwise.
 **/
static gboolean
stf_parse_options_valid (StfParseOptions_t *parseoptions)
{
	g_return_val_if_fail (parseoptions != NULL, FALSE);

	if (parseoptions->parsetype == PARSE_TYPE_CSV) {
		if (parseoptions->stringindicator == '\0') {
			g_warning ("STF: Cannot have \\0 as string indicator");
			return FALSE;
		}

	} else if (parseoptions->parsetype == PARSE_TYPE_FIXED) {
		if (!parseoptions->splitpositions) {
			g_warning ("STF: No splitpositions in struct");
			return FALSE;
		}
	}

	return TRUE;
}

/*******************************************************************************************************
 * STF PARSE : The actual routines that do the 'trick'
 *******************************************************************************************************/

static void
trim_spaces_inplace (char *field, StfParseOptions_t const *parseoptions)
{
	if (!field) return;

	if (parseoptions->trim_spaces & TRIM_TYPE_LEFT) {
		char *s = field;

		while (g_unichar_isspace (g_utf8_get_char (s)))
			s = g_utf8_next_char (s);

		if (s != field)
			strcpy (field, s);
	}

	if (parseoptions->trim_spaces & TRIM_TYPE_RIGHT) {
		char *s = field + strlen (field);

		while (field != s) {
			s = g_utf8_prev_char (s);
			if (!g_unichar_isspace (g_utf8_get_char (s)))
				break;
			*s = 0;
		}
	}
}

/**
 * stf_parse_csv_is_separator:
 *
 * returns NULL if @character is not a separator, a pointer to the character
 * after the separator otherwise.
 **/
static inline char const *
stf_parse_csv_is_separator (char const *character, char const *chr, GSList const *str)
{
	g_return_val_if_fail (character != NULL, NULL);

	if (str) {
		GSList const *l;

		for (l = str; l != NULL; l = l->next) {
			char const *s = l->data;
			char const *r;
			glong cnt;
			glong const len = g_utf8_strlen (s, -1);

			/* Don't compare past the end of the buffer! */
			for (r = character, cnt = 0; cnt < len; cnt++, r = g_utf8_next_char (r))
				if (*r == '\0')
					break;

			if ((cnt == len) && (memcmp (character, s, len) == 0))
				return g_utf8_offset_to_pointer (character, len);
		}
	}

	if (chr && g_utf8_strchr (chr, -1,
				  g_utf8_get_char (character)))
		return g_utf8_next_char(character);

	return NULL;
}

/**
 * stf_parse_csv_cell:
 *
 * returns a pointer to the parsed cell contents. (has to be freed by the calling routine)
 **/
static inline char *
stf_parse_csv_cell (Source_t *src, StfParseOptions_t *parseoptions)
{
	char const *cur;
	char const *next = NULL;
	GString *res;
	StfTokenType_t ttype;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;
	g_return_val_if_fail (cur != NULL, NULL);

	res = g_string_sized_new (30);

	while (cur && *cur) {
		char const *here, *there;

		next = stf_parse_next_token (cur, parseoptions, &ttype);
		here = cur;
		there = next;
		switch (ttype) {
		case STF_TOKEN_STRING:
			there = g_utf8_find_prev_char (here, there);
			/* break */   /* fall through */
		case STF_TOKEN_STRING_INC:
			here = g_utf8_find_next_char (here, there);
			/* break */   /* fall through */
		case STF_TOKEN_CHAR:
			if (here && there)
				res = g_string_append_len (res, here, there - here);
			break;
		case STF_TOKEN_SEPARATOR:
			cur = next;
			goto cellfinished;
		case STF_TOKEN_TERMINATOR:
			goto cellfinished;
		case STF_TOKEN_UNDEF:
			g_warning ("Undefined stf token type encountered!");
			break;
		}
		cur = next;
	}

 cellfinished:
	src->position = cur;

	if (parseoptions->indicator_2x_is_single) {
		gboolean second = TRUE;
		gunichar quote = parseoptions->stringindicator;
		int len = res->len;
		char *found;

		while ((found = g_utf8_strrchr (res->str, len, quote))) {
			len = found - res->str;
			if (second) {
				res = g_string_erase (res, len, g_utf8_next_char(found)-found);
				second = FALSE;
			} else
				second = TRUE;
		}
	}
	return g_string_free (res, FALSE);
}

/*
 * stf_parse_eat_separators:
 *
 * skip over leading separators
 *
 */

static void
stf_parse_eat_separators (Source_t *src, StfParseOptions_t *parseoptions)
{
	char const *cur, *next;

	g_return_if_fail (src != NULL);
        g_return_if_fail (parseoptions != NULL);

	cur = src->position;

	if (*cur == '\0' || compare_terminator (cur, parseoptions))
		return;
	while ((next = stf_parse_csv_is_separator (cur, parseoptions->sep.chr, parseoptions->sep.str)))
		cur = next;
	src->position = cur;
	return;
}


/**
 * stf_parse_csv_line:
 *
 * This will parse one line from the current @src->position.
 * NOTE: The calling routine is responsible for freeing the result.
 *
 * returns : a GPtrArray of char*'s
 **/
static GPtrArray *
stf_parse_csv_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GPtrArray *line;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	line = g_ptr_array_new ();
	while (*src->position != '\0' && !compare_terminator (src->position, parseoptions)) {
		char *field = stf_parse_csv_cell (src, parseoptions);
		if (parseoptions->duplicates)
			stf_parse_eat_separators (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);
		g_ptr_array_add (line, field);
	
	}

	return line;
}

/**
 * stf_parse_fixed_cell:
 *
 * returns a pointer to the parsed cell contents. (has to be freed by the calling routine)
 **/
static inline char *
stf_parse_fixed_cell (Source_t *src, StfParseOptions_t *parseoptions)
{
	GString *res;
	char const *cur;
	int splitval;
	int len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;

	res = g_string_new (NULL);
	if (src->splitpos < my_garray_len (parseoptions->splitpositions))
		splitval = (int) g_array_index (parseoptions->splitpositions, int, src->splitpos);
	else
		splitval = -1;

	while (*cur != '\0' && !compare_terminator (cur, parseoptions) && splitval != src->linepos) {
		g_string_append_unichar (res, g_utf8_get_char (cur));

		src->linepos++;
	        cur = g_utf8_next_char (cur);
		len++;
	}

	src->position = cur;

	if (len != 0)
		return g_string_free (res, FALSE);

	g_string_free (res, TRUE);
	return NULL;
}

/**
 * stf_parse_fixed_line:
 *
 * This will parse one line from the current @src->position.
 * It will return a GPtrArray with the cell contents as strings.

 * NOTE: The calling routine is responsible for freeing result.
 **/
static GPtrArray *
stf_parse_fixed_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GPtrArray *line;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	src->linepos = 0;
	src->splitpos = 0;

	line = g_ptr_array_new ();
	while (*src->position != '\0' && !compare_terminator (src->position, parseoptions)) {
		char *field = stf_parse_fixed_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);
		g_ptr_array_add (line, field);

		src->splitpos++;
	}

	return line;
}

void
stf_parse_general_free (GPtrArray *lines)
{
	unsigned lineno;
	for (lineno = 0; lineno < lines->len; lineno++) {
		GPtrArray *line = g_ptr_array_index (lines, lineno);
		unsigned field;
		for (field = 0; field < line->len; field++) {
			char *text = g_ptr_array_index (line, field);
			g_free (text);
		}
		g_ptr_array_free (line, TRUE);
	}
	g_ptr_array_free (lines, TRUE);
}


/**
 * stf_parse_general:
 *
 * Returns a GPtrArray of lines, where each line is itself a
 * GPtrArray of strings.
 *
 * The caller must free this entire structure, for example by calling
 * stf_parse_general_free.
 **/
GPtrArray *
stf_parse_general (StfParseOptions_t *parseoptions,
		   char const *data, char const *data_end)
{
	GPtrArray *lines;
	Source_t src;
	int row;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data_end != NULL, NULL);
	g_return_val_if_fail (stf_parse_options_valid (parseoptions), NULL);
	g_return_val_if_fail (g_utf8_validate (data, -1, NULL), NULL);

	src.position = data;
	row = 0;

	lines = g_ptr_array_new ();
	while (*src.position != '\0' && src.position < data_end) {
		GPtrArray *line;

		if (++row >= SHEET_MAX_ROWS) {
				g_warning (WARN_TOO_MANY_ROWS, row);
				break;
		}

		line = parseoptions->parsetype == PARSE_TYPE_CSV
			? stf_parse_csv_line (&src, parseoptions)
			: stf_parse_fixed_line (&src, parseoptions);

		g_ptr_array_add (lines, line);
		src.position += compare_terminator (src.position, parseoptions);
	}

	return lines;
}

GPtrArray *
stf_parse_lines (StfParseOptions_t *parseoptions, const char *data, gboolean with_lineno)
{
	GPtrArray *lines;
	int lineno = 1;

	g_return_val_if_fail (data != NULL, NULL);

	lines = g_ptr_array_new ();
	while (*data) {
		const char *data0 = data;
		GPtrArray *line = g_ptr_array_new ();

		if (with_lineno)
			g_ptr_array_add (line, g_strdup_printf ("%d", lineno++));

		while (1) {
			int termlen = compare_terminator (data, parseoptions);
			if (termlen > 0 || *data == 0) {
				g_ptr_array_add (line, g_strndup (data0, data - data0));
				data += termlen;
				break;
			} else
				data = g_utf8_next_char (data);
		}

		g_ptr_array_add (lines, line);
	}
	return lines;
}

const char *
stf_parse_find_line (StfParseOptions_t *parseoptions,
		     const char *data,
		     int line)
{
	while (line > 0) {
		int termlen = compare_terminator (data, parseoptions);
		if (termlen > 0) {
			data += termlen;
			line--;
		} else if (*data == 0) {
			return data;
		} else {
			data = g_utf8_next_char (data);
		}
	}
	return data;
}


/**
 * stf_parse_get_longest_row_width:
 *
 * Returns the largest number of characters found in a line/row
 **/
int
stf_parse_get_longest_row_width (StfParseOptions_t *parseoptions,
				 char const *data, char const *data_end)
{
	char const *s;
	int len = 0;
	int longest = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);
	g_return_val_if_fail (data_end != NULL, 0);

	for (s = data; *s && s < data_end; ) {
		int termlen = compare_terminator (s, parseoptions);
		if (termlen > 0) {
			longest = MAX (len, longest);
			len = 0;
			s += termlen;
		} else {
			len++;
			s = g_utf8_next_char (s);
		}
	}
	longest = MAX (len, longest);

	return longest;
}


/**
 * stf_parse_options_fixed_autodiscover:
 * @parseoptions: a Parse options struct.
 * @data_lines : The number of lines to look at in @data.
 * @data : The actual data.
 *
 * Automatically try to discover columns in the text to be parsed.
 * We ignore empty lines (only containing parseoptions->terminator)
 *
 * FIXME: This is so extremely ugly that I am too tired to rewrite it right now.
 *        Think hard of a better more flexible solution...
 **/
void
stf_parse_options_fixed_autodiscover (StfParseOptions_t *parseoptions,
				      char const *data, char const *data_end)
{
	char const *iterator = data;
	GSList *list = NULL;
	GSList *list_start = NULL;
	int lines = 0;
	int effective_lines = 0;
	int max_line_length = 0;
	int *line_begin_hits = NULL;
	int *line_end_hits = NULL;
	int i;

	stf_parse_options_fixed_splitpositions_clear (parseoptions);

	/*
	 * First take a look at all possible white space combinations
	 */
	while (*iterator && iterator < data_end) {
		gboolean begin_recorded = FALSE;
		AutoDiscovery_t *disc = NULL;
		int position = 0;
		int termlen = 0;

		while (*iterator && (termlen = compare_terminator (iterator, parseoptions)) == 0) {
			if (!begin_recorded && *iterator == ' ') {
				disc = g_new0 (AutoDiscovery_t, 1);

				disc->start = position;

				begin_recorded = TRUE;
			} else if (begin_recorded && *iterator != ' ') {
				disc->stop = position;
				list = g_slist_prepend (list, disc);

				begin_recorded = FALSE;
				disc = NULL;
			}

			position++;
			iterator++;
		}

		if (position > max_line_length)
			max_line_length = position;

		/*
		 * If there are excess spaces at the end of
		 * the line : ignore them
		 */
		if (disc)
			g_free (disc);

		/*
		 * Hop over the terminator
		 */
		iterator += termlen;

		if (position != 0)
			effective_lines++;

		lines++;
	}

	list       = g_slist_reverse (list);
	list_start = list;

	/*
	 * Kewl stuff :
	 * Look at the number of hits at each line position
	 * if the number of hits equals the number of lines
	 * we can be pretty sure this is the start or end
	 * of a column, we filter out empty columns
	 * later
	 */
	line_begin_hits = g_new0 (int, max_line_length + 1);
	line_end_hits   = g_new0 (int, max_line_length + 1);

	while (list) {
		AutoDiscovery_t *disc = list->data;

		line_begin_hits[disc->start]++;
		line_end_hits[disc->stop]++;

		g_free (disc);

		list = g_slist_next (list);
	}
	g_slist_free (list_start);

	for (i = 0; i < max_line_length + 1; i++)
		if (line_begin_hits[i] == effective_lines || line_end_hits[i] == effective_lines)
			stf_parse_options_fixed_splitpositions_add (parseoptions, i);

	/*
	 * Do some corrections to the initial columns
	 * detected here, we obviously don't need to
	 * do this if there are no columns at all.
	 */
	if (my_garray_len (parseoptions->splitpositions) > 0) {
		/*
		 * Try to find columns that look like :
		 *
		 * Example     100
		 * Example2      9
		 *
		 * (In other words : Columns with left & right justification with
		 *  a minimum of 2 spaces in the middle)
		 * Split these columns in 2
		 */

		for (i = 0; i < my_garray_len (parseoptions->splitpositions) - 1; i++) {
			int begin = g_array_index (parseoptions->splitpositions, int, i);
			int end   = g_array_index (parseoptions->splitpositions, int, i + 1);
			int num_spaces   = -1;
			int spaces_start = 0;
			gboolean right_aligned = TRUE;
			gboolean left_aligned  = TRUE;
			gboolean has_2_spaces  = TRUE;

			iterator = data;
			lines = 0;
			while (*iterator && iterator < data_end) {
				gboolean trigger = FALSE;
				gboolean space_trigger = FALSE;
				int pos = 0;

				num_spaces   = -1;
				spaces_start = 0;
				while (*iterator && !compare_terminator (iterator, parseoptions)) {
					if (pos == begin) {
						if (*iterator == ' ')
							left_aligned = FALSE;

						trigger = TRUE;
					} else if (pos == end - 1) {
						if (*iterator == ' ')
							right_aligned = FALSE;

						trigger = FALSE;
					}

					if (trigger || pos == end - 1) {
						if (!space_trigger && *iterator == ' ') {
							space_trigger = TRUE;
							spaces_start = pos;
						} else if (space_trigger && *iterator != ' ') {
							space_trigger = FALSE;
							num_spaces = pos - spaces_start;
						}
					}

					iterator++;
					pos++;
				}

				if (num_spaces < 2)
					has_2_spaces = FALSE;

				if (*iterator)
					iterator++;

				lines++;
			}

			/*
			 * If this column meets all the criteria
			 * split it into two at the last measured
			 * spaces_start + num_spaces
			 */
			if (has_2_spaces && right_aligned && left_aligned) {
				int val = (((spaces_start + num_spaces) - spaces_start) / 2) + spaces_start;

				g_array_insert_val (parseoptions->splitpositions, i + 1, val);

				/*
				 * Skip over the inserted column
				 */
				i++;
			}
		}

		/*
		 * Remove empty columns here if needed
		 */
		for (i = 0; i < my_garray_len (parseoptions->splitpositions) - 1; i++) {
			int begin = g_array_index (parseoptions->splitpositions, int, i);
			int end = g_array_index (parseoptions->splitpositions, int, i + 1);
			gboolean only_spaces = TRUE;

			iterator = data;
			lines = 0;
			while (*iterator && iterator < data_end) {
				gboolean trigger = FALSE;
				int pos = 0;

				while (*iterator && !compare_terminator (iterator, parseoptions)) {
					if (pos == begin)
						trigger = TRUE;
					else if (pos == end)
						trigger = FALSE;

					if (trigger) {
						if (*iterator != ' ')
							only_spaces = FALSE;
					}

					iterator++;
					pos++;
				}

				if (*iterator)
					iterator++;

				lines++;
			}

			/*
			 * The column only contains spaces
			 * remove it
			 */
			if (only_spaces) {
				g_array_remove_index (parseoptions->splitpositions, i);

				/*
				 * We HAVE to make sure that the next column (end) also
				 * gets checked out. If we don't decrease "i" here, we
				 * will skip over it as the indexes shift down after
				 * the removal
				 */
				i--;
			}
		}
	}

	g_free (line_begin_hits);
	g_free (line_end_hits);
}

/*******************************************************************************************************
 * STF PARSE HL: high-level functions that dump the raw data returned by the low-level parsing
 *               functions into something meaningful (== application specific)
 *******************************************************************************************************/

gboolean
stf_parse_sheet (StfParseOptions_t *parseoptions,
		 char const *data, char const *data_end,
		 Sheet *sheet, int start_col, int start_row)
{
	int row;
	unsigned int lrow;
	GnmDateConventions const *date_conv;
	GPtrArray *lines;
	SETUP_LOCALE_SWITCH;

	g_return_val_if_fail (parseoptions != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	START_LOCALE_SWITCH;

	date_conv = workbook_date_conv (sheet->workbook);

	if (!data_end)
		data_end = data + strlen (data);
	lines = stf_parse_general (parseoptions, data, data_end);
	for (row = start_row, lrow = 0; lrow < lines->len ; row++, lrow++) {
		unsigned int lcol, lcol_target = 0;
		GPtrArray *line = g_ptr_array_index (lines, lrow);

		for (lcol = 0; lcol < line->len; lcol++) {
			if (parseoptions->col_import_array[lcol]) {
				char *text = g_ptr_array_index (line, lcol);
				if (text) {
					Value *v;
					StyleFormat *fmt = mstyle_get_format 
						(sheet_style_get (sheet,
								  start_col + lcol_target, 
								  row));
					v = format_match (text, fmt, date_conv);
					if (v == NULL) {
						v = value_new_string_nocopy (text);
						g_ptr_array_index (line, lcol) = NULL;
					}
					cell_set_value (sheet_cell_fetch 
							(sheet, start_col + lcol_target, row), v);
				}
				lcol_target++;
			}
		}
	}

	stf_parse_general_free (lines);
	END_LOCALE_SWITCH;
	return TRUE;
}

CellRegion *
stf_parse_region (StfParseOptions_t *parseoptions, char const *data, char const *data_end)
{
	CellRegion *cr;
	CellCopyList *content = NULL;
	unsigned int row, colhigh = 0;
	GPtrArray *lines;
	SETUP_LOCALE_SWITCH;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	START_LOCALE_SWITCH;

	if (!data_end)
		data_end = data + strlen (data);
	lines = stf_parse_general (parseoptions, data, data_end);
	for (row = 0; row < lines->len; row++) {
		GPtrArray *line = g_ptr_array_index (lines, row);
		unsigned int col, targetcol = 0;
#warning FIXME: We should not just assume the 1900 convention 
		GnmDateConventions date_conv = {FALSE};

		for (col = 0; col < line->len; col++) {
			if (parseoptions->col_import_array[col]) {
				char *text = g_ptr_array_index (line, col);

				if (text) {
					CellCopy *ccopy;
					Value *v;
					StyleFormat *fmt = g_ptr_array_index 
						(parseoptions->formats, col);

					v = format_match (text, fmt, &date_conv);
					if (v == NULL) {
						v = value_new_string_nocopy (text);
						g_ptr_array_index (line, col) = NULL;
					}

					ccopy = g_new (CellCopy, 1);
					ccopy->type = CELL_COPY_TYPE_CELL;
					ccopy->col_offset = targetcol;
					ccopy->row_offset = row;
					ccopy->u.cell = cell_new ();
					cell_set_value(ccopy->u.cell, v);
					ccopy->comment = NULL;

					content = g_list_prepend (content, ccopy);

					if (targetcol > colhigh)
						colhigh = targetcol;

					targetcol++;
				}
			}
		}
	}
	stf_parse_general_free (lines);

	END_LOCALE_SWITCH;

	cr = cellregion_new (NULL);
	cr->content = content;
	cr->cols    = (colhigh > 0) ? colhigh : 1;
	cr->rows    = row;

	return cr;
}

/*
 * stf_parse_next_token: find the next token. A token is a single character or
 *                       a sequence of quoted characters
 * @data      : string to consider (guaranteed to be utf-8)
 * @quote     : quote character
 * @adj_escaped : if true then 2 adjacent quote characters represent one
 *                literal quote character
 *
 * returns the next token or NULL if there are no more tokens
 */

char const*
stf_parse_next_token (char const *data, StfParseOptions_t *parseoptions, StfTokenType_t *tokentype)
{
	char const *character;
	StfTokenType_t ttype;
	gunichar quote;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (*data != '\0', NULL);
#if 0
	/* This is *nuts*.  Have the caller validate the whole string.  */
	g_return_val_if_fail (g_utf8_validate (data, -1, NULL), NULL);
#endif

	quote = parseoptions->stringindicator;

	ttype = STF_TOKEN_CHAR;
	character = g_utf8_find_next_char (data, NULL);
	if (g_utf8_get_char (data) == quote) {
		gboolean adj_escaped = parseoptions->indicator_2x_is_single;

		ttype = STF_TOKEN_STRING_INC;
		while (character && *character) {
			if (g_utf8_get_char (character) == quote) {
				character = g_utf8_find_next_char (character, NULL);
				if (!adj_escaped ||
				    g_utf8_get_char(character) != quote) {
					ttype = STF_TOKEN_STRING;
					break;
				}
			}
			character = g_utf8_find_next_char (character, NULL);
		}
	} else {
		gint term_length = compare_terminator (data, parseoptions);
		if (term_length) {
			ttype = STF_TOKEN_TERMINATOR;
			character = data + term_length;
		} else {
			char const *after_sep = stf_parse_csv_is_separator
				(data, parseoptions->sep.chr, parseoptions->sep.str);
			if (after_sep) {
				character = after_sep;
				ttype = STF_TOKEN_SEPARATOR;
			}
		}
	}

	if (tokentype)
		*tokentype = ttype;
	return character;
}

static int
int_sort (const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}


static int
count_character (GPtrArray *lines, gunichar c, double quantile)
{
	int *counts, res;
	unsigned int ui;

	if (lines->len == 0)
		return 0;

	counts = g_new (int, lines->len);
	for (ui = 0; ui < lines->len; ui++) {
		int count = 0;
		GPtrArray *boxline = g_ptr_array_index (lines, ui);
		const char *line = g_ptr_array_index (boxline, 0);

		while (*line) {
			if (g_utf8_get_char (line) == c)
				count++;
			line = g_utf8_next_char (line);
		}

		counts[ui] = count;
	}

	qsort (counts, lines->len, sizeof (counts[0]), int_sort);
	ui = (unsigned int)ceil (quantile * lines->len);
	if (ui == lines->len)
		ui--;
	res = counts[ui];

	g_free (counts);

	return res;
}


StfParseOptions_t *
stf_parse_options_guess (const char *data)
{
	StfParseOptions_t *res;
	GPtrArray *lines;
	int tabcount;
	int sepcount;
	gunichar sepchar = format_get_arg_sep ();

	g_return_val_if_fail (data != NULL, NULL);

	res = stf_parse_options_new ();
	lines = stf_parse_lines (res, data, FALSE);

	tabcount = count_character (lines, '\t', 0.2);
	sepcount = count_character (lines, sepchar, 0.2);

	/* At least one tab per line and enough to separate every
	   would-be sepchars.  */
	if (tabcount >= 1 && tabcount >= sepcount - 1)
		stf_parse_options_csv_set_separators (res, "\t", NULL);
	else {
		gunichar c;

		/*
		 * Try a few more or less likely characters and pick the first
		 * one that occurs on at least half the lines.
		 *
		 * The order is mostly random, although ' ' and '!' which
		 * could very easily occur in text are put last.
		 */
		if (count_character (lines, (c = sepchar), 0.5) > 0 ||
		    count_character (lines, (c = format_get_col_sep ()), 0.5) > 0 ||
		    count_character (lines, (c = ':'), 0.5) > 0 ||
		    count_character (lines, (c = ';'), 0.5) > 0 ||
		    count_character (lines, (c = '|'), 0.5) > 0 ||
		    count_character (lines, (c = '!'), 0.5) > 0 ||
		    count_character (lines, (c = ' '), 0.5) > 0) {
			char sep[7];
			sep[g_unichar_to_utf8 (c, sep)] = 0;
			stf_parse_options_csv_set_separators (res, sep, NULL);
		}
	}

	if (1) {
		/* Separated */

		stf_parse_options_set_type (res, PARSE_TYPE_CSV);
		stf_parse_options_set_trim_spaces (res, TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT);
		stf_parse_options_csv_set_indicator_2x_is_single (res, TRUE);
		stf_parse_options_csv_set_duplicates (res, FALSE);

		stf_parse_options_csv_set_stringindicator (res, '"');
	} else {
		/* Fixed-width */
	}

	stf_parse_general_free (lines);

	return res;
}
