/*
 * stf-parse.c : Structured Text Format parser. (STF)
 *               A general purpose engine for parsing data
 *               in CSV and Fixed width format.
 *
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
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

#include <config.h>
#include <ctype.h>

#include "stf-parse.h"
#include "clipboard.h"

/* Some nice warning messages */
#define WARN_TOO_MANY_ROWS _("Too many rows in data to parse: %d")
#define WARN_TOO_MANY_COLS _("Too many columns in data to parse: %d")

/* Source_t struct, used for interchanging parsing information between the low level parse functions */
typedef struct {
	const char *position;  /* Indicates the current position within data */

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


/*******************************************************************************************************
 * STF PARSE OPTIONS : StfParseOptions related
 *******************************************************************************************************/

/**
 * stf_parse_options_new
 *
 * This will return a new StfParseOptions_t struct.
 * The struct should, after being used, freed with stf_parse_options_free.
 *
 * returns : a new StfParseOptions_t
 **/
StfParseOptions_t*
stf_parse_options_new (void)
{
	StfParseOptions_t* parseoptions = g_new0 (StfParseOptions_t, 1);

	parseoptions->parsetype   = PARSE_TYPE_NOTSET;
	parseoptions->terminator  = '\n';
	parseoptions->parselines  = -1;
	parseoptions->trim_spaces = (TRIM_TYPE_RIGHT | TRIM_TYPE_LEFT);

	parseoptions->splitpositions    = g_array_new (FALSE, FALSE, sizeof (int));
	parseoptions->oldsplitpositions = NULL;

	parseoptions->indicator_2x_is_single = TRUE;
	parseoptions->duplicates             = FALSE;

	return parseoptions;
}

/**
 * stf_parse_options_free
 * @parseoptions : a parse options struct
 *
 * will free @parseoptions, note that this will not free the splitpositions
 * member (GArray) of the struct, the caller is responsible to do that.
 *
 * returns : nothing
 **/
void
stf_parse_options_free (StfParseOptions_t *parseoptions)
{
	g_return_if_fail (parseoptions != NULL);

	g_array_free (parseoptions->splitpositions, TRUE);

	g_free (parseoptions);
}

/**
 * stf_parse_options_set_type
 * @parseoptions : a parse options struct
 * @parsetype : the import type you wish to do (PARSE_TYPE_CSV or PARSE_TYPE_FIXED)
 *
 * returns : nothing
 **/
void
stf_parse_options_set_type (StfParseOptions_t *parseoptions, StfParseType_t parsetype)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail ((parsetype == PARSE_TYPE_CSV || parsetype == PARSE_TYPE_FIXED));

	parseoptions->parsetype = parsetype;
}

/**
 * stf_parse_options_set_line_terminator
 * @parseoptions : a parse options struct
 * @terminator : a char indicating a line terminator
 *
 * This will set the line terminator, in both the Fixed width and CSV delimited importers
 * this indicates the end of a row. If you set this to '\0' the whole data will be treated
 * as on big line, note that '\n' (newlines) will not be parsed out if you set the
 * terminator to anything other than a newline
 *
 * returns : nothing
 **/
void
stf_parse_options_set_line_terminator (StfParseOptions_t *parseoptions, char terminator)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->terminator = terminator;
}

/**
 * stf_parse_options_set_lines_to_parse
 * @parseoptions : a parse options struct
 * @lines : number of lines to parse
 *
 * This actually forces the parser to stop after parsing @lines lines, if you set @lines
 * to -1, which is also the default, the parser will parse till it encounters a '\0'
 *
 * returns : nothing
 **/
void
stf_parse_options_set_lines_to_parse (StfParseOptions_t *parseoptions, int lines)
{
	g_return_if_fail (parseoptions != NULL);

	/* we'll convert this to an index by subtracting 1 */
	if (lines != -1)
		lines--;

	parseoptions->parselines = lines;
}

/**
 * stf_parse_options_set_trim_spaces:
 * @parseoptions: a parse options struct
 * @trim_spaces: whether you want to trim spaces or not
 *
 * If enabled will trim spaces in every parsed field on left and right
 * sides.
 **/
void
stf_parse_options_set_trim_spaces (StfParseOptions_t *parseoptions, StfTrimType_t trim_spaces)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->trim_spaces = trim_spaces;
}

/**
 * stf_parse_options_csv_set_separators
 * @parseoptions : an import options struct
 * @tab, @colon, @comma, @space, @custom : indicates the field separators
 *
 * NOTE : if @custom is set you should also set @parseoptions->customfieldseparator
 *
 * returns : nothing
 **/
void
stf_parse_options_csv_set_separators (StfParseOptions_t *parseoptions,
				       gboolean tab, gboolean colon,
				       gboolean comma, gboolean space,
				       gboolean semicolon, gboolean pipe,
				       gboolean slash, gboolean hyphen,
				       gboolean bang, gboolean custom)
{
	StfTextSeparator_t separators;

	g_return_if_fail (parseoptions != NULL);

	separators = 0;

	if (tab)
		separators |= TEXT_SEPARATOR_TAB;
	if (colon)
		separators |= TEXT_SEPARATOR_COLON;
	if (comma)
		separators |= TEXT_SEPARATOR_COMMA;
	if (space)
		separators |= TEXT_SEPARATOR_SPACE;
	if (semicolon)
		separators |= TEXT_SEPARATOR_SEMICOLON;
	if (pipe)
		separators |= TEXT_SEPARATOR_PIPE;
	if (slash)
		separators |= TEXT_SEPARATOR_SLASH;
	if (hyphen)
		separators |= TEXT_SEPARATOR_HYPHEN;
	if (bang)
		separators |= TEXT_SEPARATOR_BANG;
	if (custom)
		separators |= TEXT_SEPARATOR_CUSTOM;

	parseoptions->separators = separators;
}

/**
 * stf_parse_options_csv_set_customfieldseparator
 * @parseoptions : a parse options struct
 * @customfieldseparator : a char that will be used as custom field separator
 *
 * NOTE : This will only be used if TEXT_SEPARATOR_CUSTOM in @parseoptions->separators is also set
 *
 * returns : nothing
 **/
void
stf_parse_options_csv_set_customfieldseparator (StfParseOptions_t *parseoptions, char customfieldseparator)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->customfieldseparator = customfieldseparator;
}

/**
 * stf_parse_options_csv_set_stringindicator
 * @parseoptions : a parse options struct
 * @stringindicator : a char representing the string indicator
 *
 * returns : nothing
 **/
void
stf_parse_options_csv_set_stringindicator (StfParseOptions_t *parseoptions, char stringindicator)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (stringindicator != '\0');

	parseoptions->stringindicator = stringindicator;
}

/**
 * stf_parse_options_csv_set_indicator_2x_is_single :
 * @parseoptions : a parse options struct
 * @indic_2x : a boolean value indicating whether we want to see two
 * 		adjacent string indicators as a single string indicator
 * 		that is part of the cell, rather than a terminator.
 *
 * returns : nothing
 **/
void
stf_parse_options_csv_set_indicator_2x_is_single (StfParseOptions_t *parseoptions,
						  gboolean indic_2x)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->indicator_2x_is_single = indic_2x;
}

/**
 * stf_parse_options_csv_set_duplicates
 * @parseoptions : a parse options struct
 * @duplicates : a boolean value indicating whether we want to see two
 *               separators right behind each other as one
 *
 * returns : nothing
 **/
void
stf_parse_options_csv_set_duplicates (StfParseOptions_t *parseoptions, gboolean duplicates)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->duplicates = duplicates;
}

/**
 * stf_parse_options_fixed_splitpositions_clear
 * @parseoptions : a parse options struct
 *
 * This will clear the splitpositions (== points on which a line is split)
 *
 * returns : nothing
 **/
void
stf_parse_options_fixed_splitpositions_clear (StfParseOptions_t *parseoptions)
{
	g_return_if_fail (parseoptions != NULL);

	g_array_free (parseoptions->splitpositions, TRUE);
	parseoptions->splitpositions = g_array_new (FALSE, FALSE, sizeof (int));
}

/**
 * stf_parse_options_fixed_splitpositions_add
 * @parseoptions : a parse options struct
 * @position : a new position to split at
 *
 * @position will be added to the splitpositions, @position must be equal to
 * or greater than zero or -1 which means as much as "parse to end of line"
 *
 * returns : nothing
 **/
void
stf_parse_options_fixed_splitpositions_add (StfParseOptions_t *parseoptions, int position)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (position >= 0 || position == -1);

	g_array_append_val (parseoptions->splitpositions, position);
}


/**
 * stf_parse_options_valid
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
trim_spaces_inplace (char *field, const StfParseOptions_t *parseoptions)
{
	unsigned char *s = (unsigned char *)field;

	if (!s) return;

	if (isspace (*s) && parseoptions->trim_spaces & TRIM_TYPE_LEFT) {
		unsigned char *tmp = s;
		while (isspace (*tmp)) tmp++;
		strcpy (s, tmp);
	}

	if (parseoptions->trim_spaces & TRIM_TYPE_RIGHT) {
		int len = strlen (s);
		while (len && isspace (s[len - 1]))
			s[--len] = 0;
	}
}


/**
 * stf_parse_csv_is_separator
 * @character : pointer to the character to check
 * @parsetype : a bitwise orred field of what to see as separator and what not
 * @customfieldseparator : the custom field separator which will be examined if TEXT_SEPARATOR_CUSTOM is set
 *
 * returns : true if character is a separator, false otherwise
 **/
static inline gboolean
stf_parse_csv_is_separator (const char *character, StfParseType_t parsetype, char customfieldseparator)
{
	g_return_val_if_fail (character != NULL, FALSE);

	/* I have done this to slightly speed up the parsing, if we
	 * wouldn't do this, it had to go over the case statement below for
	 * each character, which clearly slows parsing down
	 */
	if (isalnum ((unsigned char)*character))
		return FALSE;

	switch (*character) {
	case ' '              : if (parsetype & TEXT_SEPARATOR_SPACE)     return TRUE; break;
	case '\t'             : if (parsetype & TEXT_SEPARATOR_TAB)       return TRUE; break;
	case ':'              : if (parsetype & TEXT_SEPARATOR_COLON)     return TRUE; break;
	case ','              : if (parsetype & TEXT_SEPARATOR_COMMA)     return TRUE; break;
	case ';'              : if (parsetype & TEXT_SEPARATOR_SEMICOLON) return TRUE; break;
	case '|'              : if (parsetype & TEXT_SEPARATOR_PIPE)      return TRUE; break;
	case '/'              : if (parsetype & TEXT_SEPARATOR_SLASH)     return TRUE; break;
	case '-'              : if (parsetype & TEXT_SEPARATOR_HYPHEN)    return TRUE; break;
	case '!'              : if (parsetype & TEXT_SEPARATOR_BANG)      return TRUE; break;
        }

	if ((parsetype & TEXT_SEPARATOR_CUSTOM) && (customfieldseparator == *character))
		return TRUE;

	return FALSE;
}

/**
 * stf_parse_csv_cell
 * @src : struct containing information/memory location of the input data
 * @parseoptions : struct containing separation customizations
 *
 * returns : a pointer to the parsed cell contents. (has to be freed by the calling routine)
 **/
static inline char*
stf_parse_csv_cell (Source_t *src, StfParseOptions_t *parseoptions)
{
	GString *res;
	const char *cur;
	gboolean isstring, sawstringterm;
	int len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;

	/* A leading quote is always a quote */
	if (*cur == parseoptions->stringindicator) {

		cur++;
		isstring = TRUE;
		sawstringterm = FALSE;
	} else {

		isstring = FALSE;
		sawstringterm = TRUE;
        }


	res = g_string_new ("");

	while (*cur && *cur != parseoptions->terminator) {

		if (!sawstringterm) {

			if (*cur == parseoptions->stringindicator) {
				/* two stringindicators in a row represent a
				 * single stringindicator character that does
				 * not terminate the cell
				 */
				cur++;
				if (*cur != parseoptions->stringindicator ||
				    !parseoptions->indicator_2x_is_single) {
					sawstringterm = TRUE;
					continue;
				}
			}
		} else {

			if (stf_parse_csv_is_separator (cur, parseoptions->separators, parseoptions->customfieldseparator)) {

				if (parseoptions->duplicates) {
					const char *nextcur = cur;
					nextcur++;
					if (stf_parse_csv_is_separator (nextcur, parseoptions->separators, parseoptions->customfieldseparator)) {
						cur = nextcur;
						continue;
					}
				}
				break;
			}
		}

		g_string_append_c (res, *cur);

		len++;
		cur++;
	}

	/* Only skip over cell terminators, not line terminators or terminating nulls*/
	if (*cur != parseoptions->terminator && *cur)
		cur++;

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
 * stf_parse_csv_line
 * @src : a struct containing information on the source data
 * @parseoptions : a struct containing parsing options
 *
 * This will parse one line from the current @src->position.
 * It will return a GSList with the cell contents as strings.
 * NOTE :
 * 1) The calling routine is responsible for freeing the string in the GSList
 * 2) The calling routine is responsible for freeing the list itself.
 *
 * returns : a list with char*'s (contains the cells reversed)
 **/
static GSList*
stf_parse_csv_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GSList *list = NULL;
	GSList *listend = NULL;
	char *field;
	int col = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	while (*src->position && *src->position != parseoptions->terminator) {
		field = stf_parse_csv_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);

		if (list != NULL) {

			listend = g_slist_append (listend, field)->next;
		} else {

			list = g_slist_append (list, field);
			listend = list;
		}

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			return list;
		}
	}

	return list;
}

/**
 * stf_parse_fixed_cell
 * @src : a struct containing information on the source data
 * @parseoptions : a struct containing parsing options
 *
 * returns : a pointer to the parsed cell contents. (has to be freed by the calling routine)
 **/
static inline char *
stf_parse_fixed_cell (Source_t *src, StfParseOptions_t *parseoptions)
{
	GString *res;
	const char *cur;
	int splitval;
	int len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;

	res = g_string_new ("");
	if (src->splitpos < my_garray_len (parseoptions->splitpositions))
		splitval = (int) g_array_index (parseoptions->splitpositions, int, src->splitpos);
	else
		splitval = -1;

	while (*cur && *cur != parseoptions->terminator  && splitval != src->linepos) {
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
 * stf_parse_fixed_line
 * @src : a struct containing information on the source data
 * @parseoptions : a struct containing parsing options
 *
 * This will parse one line from the current @src->position.
 * It will return a GSList with the cell contents as strings.
 * NOTE :
 * 1) The calling routine is responsible for freeing the string in the GSList
 * 2) The calling routine is responsible for freeing the list itself.
 *
 * returns : a list with char*'s
 **/
static GSList*
stf_parse_fixed_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GSList *list = NULL;
	GSList *listend = NULL;
	int col = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	src->linepos = 0;
	src->splitpos = 0;

	while (*src->position && *src->position != parseoptions->terminator) {
		char *field;
		field = stf_parse_fixed_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);

		if (list != NULL) {
			listend = g_slist_append (listend, field)->next;
		} else {
			list = g_slist_append (list, field);
			listend = list;
		}

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			return list;
		}

		src->splitpos++;
	}

	return list;
}

/**
 * stf_parse_general
 * @parseoptions : a parseoptions struct
 * @data : the data to parse
 *
 * The GSList that is returned contains smaller GSLists.
 * The format is more or less :
 *
 * GSList (Top-level)
 *  |------>GSList (Sub)
 *  |------>GSList (Sub)
 *  |------>GSList (Sub)
 *
 * Every Sub-GSList represents a parsed line and contains
 * strings as data. if a Sub-GSList is NULL this means the line
 * was empty.
 * NOTE : The calling routine has to free *a lot* of stuff by
 *        itself :
 *        1) The data of each Sub-GSList item (with g_free) these are all strings.
 *        2) The sub-GSList's themselves (with g_slist_free).
 *        3) The top level GSList.
 *
 * returns : a GSList with Sub-GSList's containing celldata.
 **/
GSList*
stf_parse_general (StfParseOptions_t *parseoptions, const char *data)
{
	GSList *celllist = NULL;
	GSList *list = NULL;
	GSList *listend = NULL;
	Source_t src;
	int row;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (stf_parse_options_valid (parseoptions), NULL);

	src.position = data;
	row = 0;

	while (TRUE) {
		if (parseoptions->parsetype == PARSE_TYPE_CSV)
			celllist = stf_parse_csv_line (&src, parseoptions);
		else
			celllist = stf_parse_fixed_line (&src, parseoptions);

		if (list != NULL) {

			listend = g_slist_append (listend, celllist)->next;
		} else {

			list = g_slist_append (list, celllist);
			listend = list;
		}

		if (++row >= SHEET_MAX_ROWS) {

				g_warning (WARN_TOO_MANY_ROWS, row);
				return NULL;
		}

		if (parseoptions->parselines != -1)
			if (row > parseoptions->parselines)
				break;

		if (*src.position == '\0')
			break;

		src.position++;
	}

	return list;
}

/**
 * stf_parse_get_rowcount
 * @parseoptions : a parse options struct
 * @data : the data
 *
 * This will retrieve the number of rows @data consists of.
 * It will always return the right number of rows.
 *
 * returns : number of rows in @data
 **/
int
stf_parse_get_rowcount (StfParseOptions_t *parseoptions, const char *data)
{
	const char *iterator;
	int rowcount = 0;
	gboolean last_row_empty = TRUE;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	iterator = data;

	while (*iterator) {
		if (*iterator == parseoptions->terminator)
			rowcount++, last_row_empty = TRUE;
		else
			last_row_empty = FALSE;

		if (parseoptions->parselines != -1)
			if (rowcount > parseoptions->parselines)
				break;

		iterator++;
	}

	return last_row_empty ? rowcount : rowcount + 1;
}

/**
 * stf_parse_get_colcount
 * @parseoptions : a parse options struct
 * @data : the data
 *
 * This will retrieve the number of columns @data consists of.
 * It will always return the right number of columns wether you
 * are doing a fixed or csv parse.
 *
 * returns : number of columns in @data
 **/
int
stf_parse_get_colcount (StfParseOptions_t *parseoptions, const char *data)
{
	int colcount = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	if (parseoptions->parsetype == PARSE_TYPE_CSV) {
		const char *iterator = data;
		int tempcount = 0;

		while (1) {

			if (*iterator == parseoptions->terminator || *iterator == '\0') {

				if (tempcount > colcount)
					colcount = tempcount;

				tempcount = 0;

				if (*iterator == '\0')
					break;
			}

			if (stf_parse_csv_is_separator (iterator, parseoptions->separators, parseoptions->customfieldseparator)) {

				if (parseoptions->duplicates) {
					const char *nextiterator = iterator;

					nextiterator++;
					if (stf_parse_csv_is_separator (nextiterator, parseoptions->separators, parseoptions->customfieldseparator)) {

						iterator = nextiterator;
						continue;
					}
				}

				tempcount++;
			}

			iterator++;
		}
	} else {
		colcount = my_garray_len (parseoptions->splitpositions) - 1;
	}


	return colcount;
}

/**
 * stf_parse_get_longest_row_width
 * @parseoptions : a parse options struct
 * @data : the data
 *
 * Returns the largest number of characters found in a line/row
 *
 * returns : the longest row width.
 **/
int
stf_parse_get_longest_row_width (StfParseOptions_t *parseoptions, const char *data)
{
	const char *iterator;
	int len = 0;
	int longest = 0;
	int row = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	iterator = data;
	while (*iterator) {

		if (*iterator == parseoptions->terminator
		    || iterator[1] == '\0') {

			if (len > longest)
				longest = len;

			len = 0;
			row++;
		}

		iterator++;
		len++;

		if (parseoptions->parselines != -1)
			if (row > parseoptions->parselines)
				break;
	}

	return longest - 1;
}
/**
 * stf_parse_get_colwidth
 * @parseoptions : a parse options struct
 * @data : the data
 * @index : the index of the column you wish to know the width of
 *
 * Will determine the width of column @index in @data given the
 * parsing rules as specified in @parseoptions
 *
 * returns : the width, in characters, of the column @index.
 **/
int
stf_parse_get_colwidth (StfParseOptions_t *parseoptions, const char *data, int index)
{
	int width = 0;

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	if (parseoptions->parsetype == PARSE_TYPE_CSV) {
		const char *iterator = data;
		int col = 0, colwidth = 0;

		while (1) {

			if (stf_parse_csv_is_separator (iterator, parseoptions->separators, parseoptions->customfieldseparator) || *iterator == parseoptions->terminator || *iterator == '\0') {

				if (*iterator != 0 && parseoptions->duplicates) {
					const char *nextiterator = iterator;

					nextiterator++;
					if (stf_parse_csv_is_separator (nextiterator, parseoptions->separators, parseoptions->customfieldseparator)) {

						iterator = nextiterator;
						continue;
					}
				}

				if (col == index) {

					if (colwidth > width)
						width = colwidth;
				}


				if (*iterator == parseoptions->terminator)
					col = 0;
				else
					col++;

				colwidth = -1;
			}

			if (*iterator == '\0')
				break;

			colwidth++;
			iterator++;
		}
	} else {
		int colstart, colend;

		if (index - 1 < 0)
			colstart = 0;
		else
			colstart = (int) g_array_index (parseoptions->splitpositions, int, index - 1);

		colend = (int) g_array_index (parseoptions->splitpositions, int, index);
		if (colend == -1) {
			const char *iterator = data;
			int len = 0, templen = 0;

			while (*iterator) {

				if (*iterator == parseoptions->terminator) {

					if (templen > len)
						len = templen;

					templen = 0;
				}

				templen++;
				iterator++;
			}

			colend = len;
		}

		width = colend - colstart;
	}

	return width;

}

/**
 * stf_parse_convert_to_unix
 * @data : the char buffer to convert
 *
 * This function will convert the @data into
 * unix line-terminated format. this means that CRLF (windows) will be converted to LF
 * and CR (Macintosh) to LF.
 * In addition to that form feed (\F) characters will be removed.
 * NOTE: This will not resize the buffer
 *
 * returns: TRUE on success, FALSE otherwise.
 **/
gboolean
stf_parse_convert_to_unix (char *data)
{
	const char *iterator = data;
	char *dest = data;

	if (!iterator)
		return FALSE;

	while (*iterator) {
		if (*iterator == '\r') {
			*dest++ = '\n';
			iterator++;
			if (*iterator == '\n')
				iterator++;
			continue;
		} else if (*iterator == '\f') {
			iterator++;
			continue;
		}

		*dest++ = *iterator++;
	}
	*dest = '\0';

	return TRUE;
}

/**
 * stf_is_valid_data
 * @data : the data to validate
 *
 * returns weather the input data is valid to import.
 * (meaning it checks weather it is text only)
 *
 * returns : NULL if valid, a pointer to the invalid character otherwise
 **/
char *
stf_parse_is_valid_data (char *data)
{
	unsigned char *iterator = data;

	for (; *iterator; iterator++)
		if (!isprint (*iterator) && !isspace (*iterator))
			return (char *)iterator;

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
 **/
void
stf_parse_options_fixed_autodiscover (StfParseOptions_t *parseoptions, int data_lines, const char *data)
{
	const char *iterator = data;
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

		while (*iterator && *iterator != parseoptions->terminator) {

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
				while (*iterator && *iterator != parseoptions->terminator) {

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

				while (*iterator && *iterator != parseoptions->terminator) {


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
				 * If the beginning of the column is zero than this
				 * is an exclusive case and the end also needs to be removed
				 * because there is no preceding column
				 */
				if (begin == 0)
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

Sheet*
stf_parse_sheet (StfParseOptions_t *parseoptions, const char *data, Sheet *sheet)
{
	GSList *list;
	GSList *list_start;
	GSList *sublist;
	int row = 0, col;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	list = stf_parse_general (parseoptions, data);
	list_start = list;

	while (list) {

		sublist = list->data;
		col = 0;

		while (sublist) {

			if (sublist->data) {
				Cell *newcell = sheet_cell_new (sheet, col, row);
				char *celldata = sublist->data;

				/* The '@' character appears to be somewhat magic, so
				 * if the line starts with an '@' character we have to prepend an '=' char and qoutes
				 */
				if (celldata[0] == '@') {
					char *tmp;

					tmp = g_strdup_printf ("=\"%s\"", celldata);

					g_free (celldata);
					sublist->data = tmp;
				}

				cell_set_text (newcell, sublist->data);

				g_free (sublist->data);
			}

			sublist = g_slist_next (sublist);
			col++;
		}

		g_slist_free (list->data);

		list = g_slist_next (list);
		row++;
	}

	g_slist_free (list_start);

	return sheet;
}

CellRegion*
stf_parse_region (StfParseOptions_t *parseoptions, const char *data)
{
	CellRegion *cr;
	GSList *list;
	GSList *list_start;
	GSList *sublist;
	CellCopyList *content = NULL;
	int row = 0, col = 0, colhigh = 0;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	list = stf_parse_general (parseoptions, data);
	list_start = list;

	while (list) {

		sublist = list->data;
		col = 0;

		while (sublist) {

			if (sublist->data) {
				CellCopy *ccopy;
				char *celldata = sublist->data;

				/* The '@' character appears to be somewhat magic, so
				 * if the line starts with an '@' character we have to prepend an '=' char and qoutes
				 */
				if (celldata[0] == '@') {
					char *tmp;

					tmp = g_strdup_printf ("=\"%s\"", celldata);

					g_free (celldata);
					sublist->data = tmp;
				}

				ccopy = g_new (CellCopy, 1);
				ccopy->type = CELL_COPY_TYPE_TEXT;
				ccopy->col_offset = col;
				ccopy->row_offset = row;
				ccopy->u.text = sublist->data;

				content = g_list_prepend (content, ccopy);

				/* we don't free sublist->data, simply because CellCopy will do this */
			}

			sublist = g_slist_next (sublist);
			col++;
		}

		g_slist_free (list->data);

		if (col > colhigh)
			colhigh = col;

		list = g_slist_next (list);
		row++;
	}

	g_slist_free (list_start);

	cr = cellregion_new (NULL);
	cr->content = content;
	cr->cols   = (colhigh > 0) ? colhigh : 1;
	cr->rows   = row;

	return cr;
}
