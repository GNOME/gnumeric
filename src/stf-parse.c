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

#include <ctype.h>
#ifdef HAVE_WCTYPE_H
#include <wctype.h>
#endif
#include <stdlib.h>

#define WARN_TOO_MANY_ROWS _("Too many rows in data to parse: %d")
#define WARN_TOO_MANY_COLS _("Too many columns in data to parse: %d")

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
			
static inline gint
compare_terminator (char const *s, StfParseOptions_t *parseoptions)
{
	GSList *term;
	for (term = parseoptions->terminator; term; term = term->next) 
		if (comp_term (s, (gchar const*)(term->data)))
			return strlen ((gchar const*)(term->data));
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

	parseoptions->terminator  = g_create_slist (g_strdup("\r\n"), g_strdup("\n"), g_strdup("\r"), NULL);
	
	parseoptions->parselines  = -1;
	parseoptions->trim_spaces = (TRIM_TYPE_RIGHT | TRIM_TYPE_LEFT);

	parseoptions->splitpositions    = g_array_new (FALSE, FALSE, sizeof (int));

	parseoptions->stringindicator        = '"';
	parseoptions->indicator_2x_is_single = TRUE;
	parseoptions->duplicates             = FALSE;

	parseoptions->sep.str = NULL;
	parseoptions->sep.chr = NULL;

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
	glong la = g_utf8_strlen (a, -1);
	glong lb = g_utf8_strlen (a, -1);
	
	if (la > lb)
		return -1;
	if (la == lb)
		return 0;
	return 1;
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

	GNM_SLIST_PREPEND(parseoptions->terminator, (gpointer)terminator);
	GNM_SLIST_SORT(parseoptions->terminator, (GCompareFunc)long_string_first);
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

	if (in_list)
		GNM_SLIST_REMOVE(parseoptions->terminator, in_list->data);	
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
}



/**
 * stf_parse_options_set_lines_to_parse:
 *
 * This forces the parser to stop after parsing @lines lines, if you set @lines
 * to -1, which is the default, the parser will parse until it encounters a '\0'
 **/
void
stf_parse_options_set_lines_to_parse (StfParseOptions_t *parseoptions, int const lines)
{
	g_return_if_fail (parseoptions != NULL);

	if (lines != -1)
		parseoptions->parselines = lines;
	else
		parseoptions->parselines = -1;
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
	GSList const *l = NULL;
	GSList *r;

	g_return_if_fail (parseoptions != NULL);

	if (parseoptions->sep.chr)
		g_free (parseoptions->sep.chr);
	parseoptions->sep.chr = g_strdup (character);

	if (parseoptions->sep.str) {
		for (l = parseoptions->sep.str; l != NULL; l = l->next)
			g_free ((char *) l->data);
		g_slist_free (parseoptions->sep.str);
	}

	for (r = NULL, l = string; l != NULL; l = l->next)
		r = g_slist_prepend (r, g_strdup (l->data));
	parseoptions->sep.str = g_slist_reverse (r);
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
 *               separators right behind eachother as one
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

static inline void
trim_spaces_inplace (char *field, StfParseOptions_t const *parseoptions)
{
	char *s = field;

	if (!s) return;

	if (isspace ((unsigned char)*s) && parseoptions->trim_spaces & TRIM_TYPE_LEFT) {
		char *tmp = s;
		while (isspace ((unsigned char)*tmp)) tmp++;
		strcpy (s, tmp);
	}

	if (parseoptions->trim_spaces & TRIM_TYPE_RIGHT) {
		int len = strlen (s);
		while (len && isspace ((unsigned char)(s[len - 1])))
			s[--len] = 0;
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
	g_return_val_if_fail (!chr || g_utf8_validate (chr, -1, NULL), NULL);

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
	
	printf ("stf_parse_csv_cell: >%s<\n", res->str);
	
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
 * It will return a GList with the cell contents as strings.
 * NOTE :
 * 1) The calling routine is responsible for freeing the strings in the GList
 * 2) The calling routine is responsible for freeing the list itself.
 *
 * returns : a list with char*'s
 **/
static GList *
stf_parse_csv_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GList *list = NULL;
	int col = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	while (*src->position != '\0' && !compare_terminator (src->position, parseoptions)) {
		char *field = stf_parse_csv_cell (src, parseoptions);
		if (parseoptions->duplicates)
			stf_parse_eat_separators (src, parseoptions);
		
		trim_spaces_inplace (field, parseoptions);
		list = g_list_prepend (list, field);

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			break;
		}
	}

	return g_list_reverse (list);
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
		g_string_append_c (res, *cur);

		src->linepos++;
	        cur++;
		len++;
	}

	src->position = cur;

	if (len != 0) {
		char *tmp = res->str;
		g_string_free (res, FALSE);
		return tmp;
	}

	g_string_free (res, TRUE);
	return NULL;
}

/**
 * stf_parse_fixed_line:
 *
 * This will parse one line from the current @src->position.
 * It will return a GList with the cell contents as strings.
 * NOTE :
 * 1) The calling routine is responsible for freeing the string in the GList
 * 2) The calling routine is responsible for freeing the list itself.
 *
 * returns : a list with char*'s
 **/
static GList *
stf_parse_fixed_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GList *list = NULL;
	int col = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	src->linepos = 0;
	src->splitpos = 0;

	while (*src->position != '\0' && !compare_terminator (src->position, parseoptions)) {
		char *field = stf_parse_fixed_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);
		list = g_list_prepend (list, field);

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			break;
		}

		src->splitpos++;
	}

	return g_list_reverse (list);
}

/**
 * stf_parse_general:
 *
 * The GList that is returned contains smaller GLists.
 * The format is more or less :
 *
 * GList (Top-level)
 *  |------>GList (Sub)
 *  |------>GList (Sub)
 *  |------>GList (Sub)
 *
 * Every Sub-GList represents a parsed line and contains
 * strings as data. if a Sub-GList is NULL this means the line
 * was empty.
 * NOTE : The calling routine has to free *a lot* of stuff by
 *        itself :
 *        1) The data of each Sub-GList item (with g_free) these are all strings.
 *        2) The sub-GList's themselves (with g_slist_free).
 *        3) The top level GList.
 *
 * returns : a GList with Sub-GList's containing celldata.
 **/
GList *
stf_parse_general (StfParseOptions_t *parseoptions, char const *data)
{
	GList *l = NULL;
	Source_t src;
	int row;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (stf_parse_options_valid (parseoptions), NULL);
	g_return_val_if_fail (g_utf8_validate (data, -1, NULL), NULL);
	
	src.position = data;
	row = 0;

	while (*src.position != '\0') {
		GList *r;

		if (++row >= SHEET_MAX_ROWS) {
				g_warning (WARN_TOO_MANY_ROWS, row);
				break;
		}

		if (parseoptions->parselines != -1)
			if (row > parseoptions->parselines)
				break;

		r = parseoptions->parsetype == PARSE_TYPE_CSV
			? stf_parse_csv_line (&src, parseoptions)
			: stf_parse_fixed_line (&src, parseoptions);
		l = g_list_prepend (l, r);

		src.position++;
	}

	return g_list_reverse (l);
}

/**
 * stf_parse_get_rowcount:
 *
 * returns : number of rows in @data
 **/
int
stf_parse_get_rowcount (StfParseOptions_t *parseoptions, char const *data)
{
	char const *s;
	int rowcount = 0;
	gboolean last_row_empty = TRUE;
	StfTokenType_t ttype;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	for (s = data; s && *s != '\0'; 
	     s = stf_parse_next_token(s, parseoptions, &ttype)) {
		if (ttype == STF_TOKEN_TERMINATOR) {
			rowcount++;
			last_row_empty = TRUE;
		} else
			last_row_empty = FALSE;

		if (parseoptions->parselines != -1)
			if (rowcount > parseoptions->parselines)
				break;
	}

	return last_row_empty ? rowcount : rowcount + 1;
}

/**
 * stf_parse_get_colcount:
 *
 * returns : number of columns in @data
 **/
int
stf_parse_get_colcount (StfParseOptions_t *parseoptions, char const *data)
{
	int colcount = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	if (parseoptions->parsetype == PARSE_TYPE_CSV) {
		char const *iterator = data;
		int tempcount = 0;

		while (*iterator != '\0') {
			char const *s;

			if (compare_terminator (iterator, parseoptions)) {
				if (tempcount > colcount)
					colcount = tempcount;
				tempcount = 0;
			}

			if ((s = stf_parse_csv_is_separator (iterator, parseoptions->sep.chr, parseoptions->sep.str))) {
				iterator = s;
				if (parseoptions->duplicates) {
					if (stf_parse_csv_is_separator (s, parseoptions->sep.chr, parseoptions->sep.str))
						continue;
				}

				tempcount++;
			} else
				iterator++;
		}
		if (tempcount > colcount)
			colcount = tempcount;
	} else
		colcount = my_garray_len (parseoptions->splitpositions) - 1;

	return colcount;
}

/**
 * stf_parse_get_longest_row_width:
 *
 * Returns the largest number of characters found in a line/row
 **/
int
stf_parse_get_longest_row_width (StfParseOptions_t *parseoptions, char const *data)
{
	char const *s;
	int len = 0;
	int longest = 0;
	int row = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	for (s = data; *s != '\0'; s++) {
		if (compare_terminator (s, parseoptions) || s[1] == '\0') {
			if (len > longest)
				longest = len;
			len = 0;
			row++;
		} else
			len++;

		if (parseoptions->parselines != -1)
			if (row > parseoptions->parselines)
				break;
	}

	return longest;
}

/**
 * stf_parse_get_colwidth:
 *
 * Will determine the width of column @index in @data given the
 * parsing rules as specified in @parseoptions
 **/
int
stf_parse_get_colwidth (StfParseOptions_t *parseoptions, char const *data, int const index)
{
	int width = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	if (parseoptions->parsetype == PARSE_TYPE_CSV) {
		char const *iterator = data;
		int col = 0, colwidth = 0;

		while (*iterator != '\0') {
			char const *s;

			if ((s = stf_parse_csv_is_separator (iterator, parseoptions->sep.chr, parseoptions->sep.str))) {

				iterator = s;
				if (parseoptions->duplicates) {
					if (stf_parse_csv_is_separator (s, parseoptions->sep.chr, parseoptions->sep.str))
						continue;
				}

				if (col == index && colwidth > width)
					width = colwidth;

				colwidth = 0;
				col++;
			} else {
				colwidth++;
				if (compare_terminator (iterator, parseoptions)) {
					if (col == index && colwidth > width)
						width = colwidth;
					col = 0;
					colwidth = 0;
				}
				iterator++;
			}
		}
		if (col == index && colwidth > width)
			width = colwidth;
	} else {
		int colstart = index - 1 < 0
			? 0
			: (int) g_array_index (parseoptions->splitpositions, int, index - 1);
		int colend = (int) g_array_index (parseoptions->splitpositions, int, index);

		if (colend == -1) {
			const char *s;
			int len = 0, templen = 0;

			for (s = data; *s != '\0'; s++) {
				if (compare_terminator (s, parseoptions)) {
					if (templen > len)
						len = templen;
					templen = 0;
				} else
					templen++;
			}

			colend = len;
		}

		width = colend - colstart;
	}

	return width;

}

/**
 * stf_parse_convert_to_unix:
 *
 * This function will convert the @data into
 * unix line-terminated format. this means that CRLF (windows) will be converted to LF
 * and CR (Macintosh) to LF.
 * In addition to that form feed (\F) characters will be removed.
 * NOTE: This will not resize the buffer
 *
 * returns: number of characters in the clean buffer or -1 on failure
 **/
int
stf_parse_convert_to_unix (char *data)
{
	char *s, *d;

	if (!data)
		return -1;

	for (s = d = data; *s != '\0';) {
		if (*s == '\r') {
			*d++ = '\n';
			s++;
			if (*s == '\n')
				s++;
			continue;
		} else if (*s == '\f') {
			s++;
			continue;
		}

		*d++ = *s++;
	}
	*d = '\0';

	return d - data;
}

/**
 * stf_is_valid_data
 *
 * returns wether the input data is valid to import.
 * (meaning it checks wether it is text only)
 *
 * returns : NULL if valid, a pointer to the invalid character otherwise
 **/
char const *
stf_parse_is_valid_data (char const *data, int buf_len)
{
	char const *s, *end;
#ifdef HAVE_WCTYPE_H
	wchar_t wstr;
	int len;
#endif

	end = data + buf_len;
	for (s = data; s < end;) {
#ifdef HAVE_WCTYPE_H
		len = mblen(s, MB_CUR_MAX);
		if (len == -1)
			return (char *)s;
		if (len > 1) {
			if (mbstowcs (&wstr, s, 1) == 1 &&
			    !iswprint (wstr) && !iswspace (wstr))
				return (char *)s;
			s += len;
		} else
#endif
		{
			if (!isprint ((unsigned char)*s) && !isspace ((unsigned char)*s))
				return (char *)s;
			s++;
		}
	}

	return NULL;
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
stf_parse_options_fixed_autodiscover (StfParseOptions_t *parseoptions, int const data_lines, char const *data)
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
	while (*iterator) {
		gboolean begin_recorded = FALSE;
		AutoDiscovery_t *disc = NULL;
		int position = 0;

		while (*iterator && !compare_terminator (iterator, parseoptions)) {

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
		if (*iterator)
			iterator++;

		if (position != 0)
			effective_lines++;

		lines++;

		if (lines >= data_lines)
			break;
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
			while (*iterator) {
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

				if (lines >= data_lines)
					break;
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
			while (*iterator) {
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

				if (lines >= data_lines)
					break;
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
stf_parse_sheet (StfParseOptions_t *parseoptions, char const *data, Sheet *sheet,
		 int start_col, int start_row)
{
	StyleFormat *fmt;
	Value *v;
	GList *res, *l, *mres, *m;
	char  *text;
	int col, row;
	GnmDateConventions const *date_conv;

	g_return_val_if_fail (parseoptions != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	date_conv = workbook_date_conv (sheet->workbook);
	res = stf_parse_general (parseoptions, data);
	for (row = start_row, l = res; l != NULL; l = l->next, row++) {
		mres = l->data;

		/* format is the same for the entire column */
		fmt = mstyle_get_format (sheet_style_get (sheet, start_col, row));

		for (col = start_col, m = mres; m != NULL; m = m->next, col++) {
			text = m->data;
			if (text) {
				v = format_match (text, fmt, date_conv);
				if (v == NULL)
					v = value_new_string_nocopy (text);
				else
					g_free (text);
				cell_set_value (sheet_cell_fetch (sheet, col, row), v);
			}
		}

		g_list_free (mres);
	}

	g_list_free (res);
	return TRUE;
}

CellRegion *
stf_parse_region (StfParseOptions_t *parseoptions, char const *data)
{
	CellRegion *cr;
	GList *res, *l;
	CellCopyList *content = NULL;
	int row, colhigh = 0;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	res = stf_parse_general (parseoptions, data);
	for (row = 0, l = res; l != NULL; l = l->next, row++) {
		GList *mres = l->data;
		GList *m;
		int col;

		for (col = 0, m = mres; m != NULL; m = m->next, col++) {
			char *text = m->data;

			if (text) {
				CellCopy *ccopy;

#warning FIXME
				/************************
				 * AAARRRGGGGG
				 * This is bogus
				 * none of this should be at this level.
				 * we need the user selected formats
				 * which are currently stuck down in the render info ??
				 * All we really need at this level is the set of values.
				 * See stf_parse_sheet
				 **/

				/* The '@' character appears to be somewhat magic, so
				 * if the line starts with an '@' character we have to prepend an '=' char and quotes
				 */
				if (text[0] == '@') {
					char *tmp;

					tmp = g_strdup_printf ("=\"%s\"", text);

					g_free (text);
					text = tmp;
				}

				ccopy = g_new (CellCopy, 1);
				ccopy->type = CELL_COPY_TYPE_TEXT;
				ccopy->col_offset = col;
				ccopy->row_offset = row;
				ccopy->u.text = text; /* No need to free this here */
				ccopy->comment = NULL;

				content = g_list_prepend (content, ccopy);
			}
		}
		if (col > colhigh)
			colhigh = col;
		g_list_free (mres);
	}

	g_list_free (res);

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
	g_return_val_if_fail (g_utf8_validate (data, -1, NULL), NULL);
	
	quote= parseoptions->stringindicator;
	
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



