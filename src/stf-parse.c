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
	parseoptions->terminator  = '\n';
	parseoptions->parselines  = -1;
	parseoptions->trim_spaces = (TRIM_TYPE_RIGHT | TRIM_TYPE_LEFT);

	parseoptions->splitpositions    = g_array_new (FALSE, FALSE, sizeof (int));

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

	g_free (parseoptions);
}

void
stf_parse_options_set_type (StfParseOptions_t *parseoptions, StfParseType_t const parsetype)
{
	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (parsetype == PARSE_TYPE_CSV || parsetype == PARSE_TYPE_FIXED);

	parseoptions->parsetype = parsetype;
}

/**
 * stf_parse_options_set_line_terminator:
 *
 * This will set the line terminator, in both the Fixed width and CSV delimited importers
 * this indicates the end of a row. If you set this to '\0' the whole data will be treated
 * as on big line, note that '\n' (newlines) will not be parsed out if you set the
 * terminator to anything other than a newline
 **/
void
stf_parse_options_set_line_terminator (StfParseOptions_t *parseoptions, char const terminator)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->terminator = terminator;
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
		parseoptions->parselines = lines - 1; /* Convert to index */
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
stf_parse_options_csv_set_stringindicator (StfParseOptions_t *parseoptions, char const stringindicator)
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
			int cnt;
			int const len = strlen (s);

			/* Don't compare past the end of the buffer! */
			for (r = character, cnt = 0; cnt < len; cnt++, r++)
				if (*r == '\0')
					break;
			
			if ((cnt == len) && (strncmp (character, s, len) == 0))
				return character + len;
		}
	}

	if (chr) {
		char const *s;
		
		for (s = chr; *s != '\0'; s++) {
			if (*character == *s) {
				return character + 1;
				break;
			}
		}
	}

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
	GString *res;
	char const *cur;
	gboolean sawstringterm;
	int len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;

	/* A leading quote is always a quote */
	if (*cur == parseoptions->stringindicator) {
		cur++;
		sawstringterm = FALSE;
	} else
		sawstringterm = TRUE;

	res = g_string_new ("");

	while (*cur != '\0') {
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
			char const *s;
			
			if (*cur == parseoptions->terminator)
				break;

			if ((s = stf_parse_csv_is_separator (cur, parseoptions->sep.chr, parseoptions->sep.str))) {
				char const *r;
				
				if (parseoptions->duplicates) {
					if ((r = stf_parse_csv_is_separator (s, parseoptions->sep.chr, parseoptions->sep.str))) {
						cur = r;
						continue;
					}
				}
				cur = s;
				break;
			}
		}

		g_string_append_c (res, *cur);

		len++;
		cur++;
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
 * stf_parse_csv_line:
 *
 * This will parse one line from the current @src->position.
 * It will return a GList with the cell contents as strings.
 * NOTE :
 * 1) The calling routine is responsible for freeing the string in the GList
 * 2) The calling routine is responsible for freeing the list itself.
 *
 * returns : a list with char*'s (contains the cells reversed)
 **/
static GList *
stf_parse_csv_line (Source_t *src, StfParseOptions_t *parseoptions)
{
	GList *list = NULL;
	int col = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	while (*src->position != '\0' && *src->position != parseoptions->terminator) {
		char *field = stf_parse_csv_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);
		list = g_list_append (list, field);

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			return list;
		}
	}

	return list;
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

	res = g_string_new ("");
	if (src->splitpos < my_garray_len (parseoptions->splitpositions))
		splitval = (int) g_array_index (parseoptions->splitpositions, int, src->splitpos);
	else
		splitval = -1;

	while (*cur != '\0' && *cur != parseoptions->terminator && splitval != src->linepos) {
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

	while (*src->position != '\0' && *src->position != parseoptions->terminator) {
		char *field = stf_parse_fixed_cell (src, parseoptions);

		trim_spaces_inplace (field, parseoptions);
		list = g_list_append (list, field);

		if (++col >= SHEET_MAX_COLS) {
			g_warning (WARN_TOO_MANY_COLS, col);
			return list;
		}

		src->splitpos++;
	}

	return list;
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

	src.position = data;
	row = 0;

	while (*src.position != '\0') {
		GList *r = parseoptions->parsetype == PARSE_TYPE_CSV
			? stf_parse_csv_line (&src, parseoptions)
			: stf_parse_fixed_line (&src, parseoptions);
			
		if (++row >= SHEET_MAX_ROWS) {
				g_warning (WARN_TOO_MANY_ROWS, row);
				return NULL;
		}
		
		if (parseoptions->parselines != -1)
			if (row > parseoptions->parselines)
				break;

		l = g_list_append (l, r);
		src.position++;
	}

	return l;
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

	g_return_val_if_fail (parseoptions != NULL, 0);
	g_return_val_if_fail (data != NULL, 0);

	for (s = data; *s != '\0'; s++) {
		if (*s == parseoptions->terminator) {
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
			
			if (*iterator == parseoptions->terminator) {
				if (tempcount > colcount)
					colcount = tempcount;
				tempcount = 0;
			}

			if ((s = stf_parse_csv_is_separator (iterator, parseoptions->sep.chr, parseoptions->sep.str))) {
			
				if (parseoptions->duplicates) {
					char const *r;

					if ((r = stf_parse_csv_is_separator (s, parseoptions->sep.chr, parseoptions->sep.str))) {
						iterator = r;
						continue;
					}
				}
				
				iterator = s;
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
		if (*s == parseoptions->terminator || s[1] == '\0') {
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

				if (parseoptions->duplicates) {
					char const *r;

					if ((r = stf_parse_csv_is_separator (s, parseoptions->sep.chr, parseoptions->sep.str))) {
						iterator = r;
						continue;
					}
				}

				if (col == index && colwidth > width)
					width = colwidth;

				colwidth = 0;
				iterator = s;
				col++;
			} else {
				colwidth++;
				if (*iterator == parseoptions->terminator) {
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
				if (*s == parseoptions->terminator) {
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
 * returns: TRUE on success, FALSE otherwise.
 **/
gboolean
stf_parse_convert_to_unix (char *data)
{
	char *s;
	char *d;

	if (!data)
		return FALSE;

	for (s = d = data; *s != '\0'; s++, d++) {
		if (*s == '\r') {
			*d++ = '\n';
			s++;
			if (*s == '\n')
				s++;
		} else if (*s == '\f') {
			s++;
		} else
			*d = *s;
	}
	*d = '\0';

	return TRUE;
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
stf_parse_is_valid_data (char const *data)
{
	unsigned char const *s;

	for (s = data; *s != '\0'; s++)
		if (!isprint (*s) && !isspace (*s))
			return (char *)s;

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

Sheet *
stf_parse_sheet (StfParseOptions_t *parseoptions, char const *data, Sheet *sheet)
{
	GList *res, *l;
	int row;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	res = stf_parse_general (parseoptions, data);
	for (row = 0, l = res; l != NULL; l = l->next, row++) {
		GList *mres = l->data;
		GList *m;
		int col;

		for (col = 0, m = mres; m != NULL; m = m->next, col++) {
			char *text = m->data;
			
			if (text) {
				Cell *newcell = sheet_cell_new (sheet, col, row);

				/* The '@' character appears to be somewhat magic, so
				 * if the line starts with an '@' character we have to prepend an '=' char and quotes
				 */
				if (text[0] == '@') {
					char *tmp = g_strdup_printf ("=\"%s\"", text);

					g_free (text);
					text = tmp;
				}
				
				cell_set_text (newcell, text);
				g_free (text);
			}
		}
		
		g_list_free (mres);
	}
	
	g_list_free (res);
	return sheet;
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
