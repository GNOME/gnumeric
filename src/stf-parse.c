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
 * Copyright (C) 2003,2008-2009 Morten Welinder <terra@gnome.org>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <stf-parse.h>
#include <stf-export.h>

#include <workbook.h>
#include <cell.h>
#include <sheet.h>
#include <expr.h>
#include <clipboard.h>
#include <sheet-style.h>
#include <value.h>
#include <mstyle.h>
#include <number-match.h>
#include <gutils.h>
#include <parse-util.h>
#include <number-match.h>
#include <gnm-format.h>
#include <ranges.h>
#include <goffice/goffice.h>

#include <stdlib.h>
#include <locale.h>
#include <string.h>

#define SETUP_LOCALE_SWITCH char *oldlocale = NULL

#define START_LOCALE_SWITCH if (parseoptions->locale) {\
oldlocale = g_strdup(go_setlocale (LC_ALL, NULL)); \
go_setlocale(LC_ALL, parseoptions->locale);}

#define END_LOCALE_SWITCH if (oldlocale) {\
go_setlocale(LC_ALL, oldlocale);\
g_free (oldlocale);}

/* Source_t struct, used for interchanging parsing information between the low level parse functions */
typedef struct {
	GStringChunk *chunk;
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
my_garray_len (GArray const *a)
{
	return (int)a->len;
}

static char *
my_utf8_strchr (const char *p, gunichar uc)
{
	return uc < 0x7f ? strchr (p, uc) : g_utf8_strchr (p, -1, uc);
}

static int
compare_terminator (char const *s, StfParseOptions_t *parseoptions)
{
	guchar const *us = (guchar const *)s;
	GSList *l;

	if (*us > parseoptions->compiled_terminator.max ||
	    *us < parseoptions->compiled_terminator.min)
		return 0;

	for (l = parseoptions->terminator; l; l = l->next) {
		char const *term = l->data;
		char const *d = s;

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

static void
gnm_g_string_free (GString *s)
{
	if (s) g_string_free (s, TRUE);
}


/**
 * stf_parse_options_new:
 *
 * This will return a new StfParseOptions_t struct.
 * The struct should, after being used, freed with stf_parse_options_free.
 **/
static StfParseOptions_t *
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

	parseoptions->splitpositions = NULL;
	stf_parse_options_fixed_splitpositions_clear (parseoptions);

	parseoptions->stringindicator = '"';
	parseoptions->indicator_2x_is_single = TRUE;
	parseoptions->sep.duplicates = FALSE;
	parseoptions->trim_seps = FALSE;

	parseoptions->sep.str = NULL;
	parseoptions->sep.chr = NULL;

	parseoptions->col_autofit_array = NULL;
	parseoptions->col_import_array = NULL;
	parseoptions->col_import_array_len = 0;
	parseoptions->formats = g_ptr_array_new_with_free_func ((GDestroyNotify)go_format_unref);
	parseoptions->formats_decimal = g_ptr_array_new_with_free_func ((GDestroyNotify)gnm_g_string_free);
	parseoptions->formats_thousand = g_ptr_array_new_with_free_func ((GDestroyNotify)gnm_g_string_free);
	parseoptions->formats_curr = g_ptr_array_new_with_free_func ((GDestroyNotify)gnm_g_string_free);

	parseoptions->cols_exceeded = FALSE;
	parseoptions->rows_exceeded = FALSE;
	parseoptions->ref_count = 1;

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

	if (parseoptions->ref_count-- > 1)
		return;

	g_free (parseoptions->col_import_array);
	g_free (parseoptions->col_autofit_array);
	g_free (parseoptions->locale);
	g_free (parseoptions->sep.chr);

	if (parseoptions->sep.str) {
		GSList *l;

		for (l = parseoptions->sep.str; l != NULL; l = l->next)
			g_free ((char *) l->data);
		g_slist_free (parseoptions->sep.str);
	}

	g_array_free (parseoptions->splitpositions, TRUE);

	stf_parse_options_clear_line_terminator (parseoptions);

	g_ptr_array_free (parseoptions->formats, TRUE);
	g_ptr_array_free (parseoptions->formats_decimal, TRUE);
	g_ptr_array_free (parseoptions->formats_thousand, TRUE);
	g_ptr_array_free (parseoptions->formats_curr, TRUE);

	g_free (parseoptions);
}

static StfParseOptions_t *
stf_parse_options_ref (StfParseOptions_t *parseoptions)
{
	parseoptions->ref_count++;
	return parseoptions;
}

GType
stf_parse_options_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("StfParseOptions_t",
			 (GBoxedCopyFunc)stf_parse_options_ref,
			 (GBoxedFreeFunc)stf_parse_options_free);
	}
	return t;
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

	parseoptions->terminator =
		g_slist_sort (parseoptions->terminator,
			      (GCompareFunc)long_string_first);
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

	GO_SLIST_PREPEND (parseoptions->terminator, g_strdup (terminator));
	compile_terminators (parseoptions);
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

	g_slist_free_full (parseoptions->terminator, g_free);
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
 * @parseoptions: #StfParseOptions_t
 * @character:
 * @seps: (element-type utf8): the separators to be used
 *
 * A copy is made of the parameters.
 **/
void
stf_parse_options_csv_set_separators (StfParseOptions_t *parseoptions,
				      char const *character,
				      GSList const *seps)
{
	g_return_if_fail (parseoptions != NULL);

	g_free (parseoptions->sep.chr);
	parseoptions->sep.chr = g_strdup (character);

	g_slist_free_full (parseoptions->sep.str, g_free);
	parseoptions->sep.str =
		g_slist_copy_deep ((GSList *)seps, (GCopyFunc)g_strdup, NULL);
}

void
stf_parse_options_csv_set_stringindicator (StfParseOptions_t *parseoptions, gunichar const stringindicator)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->stringindicator = stringindicator;
}

/**
 * stf_parse_options_csv_set_indicator_2x_is_single:
 * @indic_2x: a boolean value indicating whether we want to see two
 *		adjacent string indicators as a single string indicator
 *		that is part of the cell, rather than a terminator.
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
 * @parseoptions:
 * @duplicates: a boolean value indicating whether we want to see two
 *               separators right behind each other as one
 **/
void
stf_parse_options_csv_set_duplicates (StfParseOptions_t *parseoptions, gboolean const duplicates)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->sep.duplicates = duplicates;
}

/**
 * stf_parse_options_csv_set_trim_seps:
 * @trim_seps: a boolean value indicating whether we want to ignore
 *               separators at the beginning of lines
 **/
void
stf_parse_options_csv_set_trim_seps (StfParseOptions_t *parseoptions, gboolean const trim_seps)
{
	g_return_if_fail (parseoptions != NULL);

	parseoptions->trim_seps = trim_seps;
}

/**
 * stf_parse_options_fixed_splitpositions_clear:
 *
 * This will clear the splitpositions (== points on which a line is split)
 **/
void
stf_parse_options_fixed_splitpositions_clear (StfParseOptions_t *parseoptions)
{
	int minus_one = -1;
	g_return_if_fail (parseoptions != NULL);

	if (parseoptions->splitpositions)
		g_array_free (parseoptions->splitpositions, TRUE);
	parseoptions->splitpositions = g_array_new (FALSE, FALSE, sizeof (int));

	g_array_append_val (parseoptions->splitpositions, minus_one);
}

/**
 * stf_parse_options_fixed_splitpositions_add:
 *
 * @position will be added to the splitpositions.
 **/
void
stf_parse_options_fixed_splitpositions_add (StfParseOptions_t *parseoptions, int position)
{
	unsigned int ui;

	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (position >= 0);

	for (ui = 0; ui < parseoptions->splitpositions->len - 1; ui++) {
		int here = g_array_index (parseoptions->splitpositions, int, ui);
		if (position == here)
			return;
		if (position < here)
			break;
	}

	g_array_insert_val (parseoptions->splitpositions, ui, position);
}

void
stf_parse_options_fixed_splitpositions_remove (StfParseOptions_t *parseoptions, int position)
{
	unsigned int ui;

	g_return_if_fail (parseoptions != NULL);
	g_return_if_fail (position >= 0);

	for (ui = 0; ui < parseoptions->splitpositions->len - 1; ui++) {
		int here = g_array_index (parseoptions->splitpositions, int, ui);
		if (position == here)
			g_array_remove_index (parseoptions->splitpositions, ui);
		if (position <= here)
			return;
	}
}

int
stf_parse_options_fixed_splitpositions_count (StfParseOptions_t *parseoptions)
{
	return parseoptions->splitpositions->len;
}

int
stf_parse_options_fixed_splitpositions_nth (StfParseOptions_t *parseoptions, int n)
{
	return g_array_index (parseoptions->splitpositions, int, n);
}


/**
 * stf_parse_options_valid:
 * @parseoptions: an import options struct
 *
 * Checks if @parseoptions is correctly filled
 *
 * Returns: %TRUE if it is correctly filled, %FALSE otherwise.
 **/
static gboolean
stf_parse_options_valid (StfParseOptions_t *parseoptions)
{
	g_return_val_if_fail (parseoptions != NULL, FALSE);

	if (parseoptions->parsetype == PARSE_TYPE_FIXED) {
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
			memmove (field, s, 1 + strlen (s));
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
 * Returns: %NULL if @character is not a separator, a pointer to the character
 * after the separator otherwise.
 **/
static char const *
stf_parse_csv_is_separator (char const *character, char const *chr, GSList const *str)
{
	g_return_val_if_fail (character != NULL, NULL);

	if (*character == 0)
		return NULL;

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

	if (chr && my_utf8_strchr (chr, g_utf8_get_char (character)))
		return g_utf8_next_char(character);

	return NULL;
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


typedef enum {
	STF_CELL_ERROR,
	STF_CELL_EOF,
	STF_CELL_EOL,
	STF_CELL_FIELD_NO_SEP,
	STF_CELL_FIELD_SEP
} StfParseCellRes;

static StfParseCellRes
stf_parse_csv_cell (GString *text, Source_t *src, StfParseOptions_t *parseoptions)
{
	char const *cur;
	gboolean saw_sep = FALSE;

	g_return_val_if_fail (src != NULL, STF_CELL_ERROR);
	g_return_val_if_fail (parseoptions != NULL, STF_CELL_ERROR);

	cur = src->position;
	g_return_val_if_fail (cur != NULL, STF_CELL_ERROR);

	/* Skip whitespace, but stop at line terminators.  */
	while (1) {
		int term_len;

		if (*cur == 0) {
			src->position = cur;
			return STF_CELL_EOF;
		}

		term_len = compare_terminator (cur, parseoptions);
		if (term_len) {
			src->position = cur + term_len;
			return STF_CELL_EOL;
		}

		if ((parseoptions->trim_spaces & TRIM_TYPE_LEFT) == 0)
			break;

		if (stf_parse_csv_is_separator (cur, parseoptions->sep.chr,
						parseoptions->sep.str))
			break;

		if (!g_unichar_isspace (g_utf8_get_char (cur)))
			break;
		cur = g_utf8_next_char (cur);
	}

	if (parseoptions->stringindicator != 0 &&
	    g_utf8_get_char (cur) == parseoptions->stringindicator) {
		cur = g_utf8_next_char (cur);
		while (*cur) {
			gunichar uc = g_utf8_get_char (cur);
			cur = g_utf8_next_char (cur);

			if (uc == parseoptions->stringindicator) {
				if (parseoptions->indicator_2x_is_single &&
				    g_utf8_get_char (cur) == parseoptions->stringindicator)
					cur = g_utf8_next_char (cur);
				else {
					/* "field content"dropped-garbage,  */
					while (*cur && !compare_terminator (cur, parseoptions)) {
						char const *post = stf_parse_csv_is_separator
							(cur, parseoptions->sep.chr, parseoptions->sep.str);
						if (post) {
							cur = post;
							saw_sep = TRUE;
							break;
						}
						cur = g_utf8_next_char (cur);
					}
					break;
				}
			}

			g_string_append_unichar (text, uc);
		}

		/* We silently allow a missing terminating quote.  */
	} else {
		/* Unquoted field.  */

		while (*cur && !compare_terminator (cur, parseoptions)) {

			char const *post = stf_parse_csv_is_separator
				(cur, parseoptions->sep.chr, parseoptions->sep.str);
			if (post) {
				cur = post;
				saw_sep = TRUE;
				break;
			}

			g_string_append_unichar (text, g_utf8_get_char (cur));
			cur = g_utf8_next_char (cur);
		}

		if (parseoptions->trim_spaces & TRIM_TYPE_RIGHT) {
			while (text->len) {
				const char *last = g_utf8_prev_char (text->str + text->len);
				if (!g_unichar_isspace (g_utf8_get_char (last)))
					break;
				g_string_truncate (text, last - text->str);
			}
		}
	}

	src->position = cur;

	if (saw_sep && parseoptions->sep.duplicates)
		stf_parse_eat_separators (src, parseoptions);

	return saw_sep ? STF_CELL_FIELD_SEP : STF_CELL_FIELD_NO_SEP;
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
	gboolean cont = FALSE;
	GString *text;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	line = g_ptr_array_new ();
	if (parseoptions->trim_seps)
		stf_parse_eat_separators (src, parseoptions);

	text = g_string_sized_new (30);

	while (1) {
		char *ctext;
		StfParseCellRes res =
			stf_parse_csv_cell (text, src, parseoptions);
		trim_spaces_inplace (text->str, parseoptions);
		ctext = g_string_chunk_insert_len (src->chunk,
						   text->str, text->len);
		g_string_truncate (text, 0);

		switch (res) {
		case STF_CELL_FIELD_NO_SEP:
			g_ptr_array_add (line, ctext);
			cont = FALSE;
			break;

		case STF_CELL_FIELD_SEP:
			g_ptr_array_add (line, ctext);
			cont = TRUE;  /* Make sure we see one more field.  */
			break;

		default:
			if (cont)
				g_ptr_array_add (line, ctext);
			g_string_free (text, TRUE);
			return line;
		}
	}
}

/**
 * stf_parse_fixed_cell:
 *
 * returns a pointer to the parsed cell contents.
 **/
static char *
stf_parse_fixed_cell (Source_t *src, StfParseOptions_t *parseoptions)
{
	char *res;
	char const *cur;
	int splitval;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (parseoptions != NULL, NULL);

	cur = src->position;

	if (src->splitpos < my_garray_len (parseoptions->splitpositions))
		splitval = (int) g_array_index (parseoptions->splitpositions, int, src->splitpos);
	else
		splitval = -1;

	while (*cur != 0 && !compare_terminator (cur, parseoptions) && splitval != src->linepos) {
		src->linepos++;
		cur = g_utf8_next_char (cur);
	}

	res = g_string_chunk_insert_len (src->chunk,
					 src->position,
					 cur - src->position);

	src->position = cur;

	return res;
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

	while (line->len < parseoptions->splitpositions->len)
		g_ptr_array_add (line, g_strdup (""));

	return line;
}

/**
 * stf_parse_general_free: (skip)
 */
void
stf_parse_general_free (GPtrArray *lines)
{
	unsigned lineno;
	for (lineno = 0; lineno < lines->len; lineno++) {
		GPtrArray *line = g_ptr_array_index (lines, lineno);
		/* Fields are not freed here.  */
		if (line)
			g_ptr_array_free (line, TRUE);
	}
	g_ptr_array_free (lines, TRUE);
}


/**
 * stf_parse_general: (skip)
 *
 * Returns: (transfer full): a GPtrArray of lines, where each line is itself a
 * GPtrArray of strings.
 *
 * The caller must free this entire structure, for example by calling
 * stf_parse_general_free.
 **/
GPtrArray *
stf_parse_general (StfParseOptions_t *parseoptions,
		   GStringChunk *lines_chunk,
		   char const *data, char const *data_end)
{
	GPtrArray *lines;
	Source_t src;
	int row;
	char const *valid_end = data_end;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data_end != NULL, NULL);
	g_return_val_if_fail (stf_parse_options_valid (parseoptions), NULL);
	g_return_val_if_fail (g_utf8_validate (data, data_end-data, &valid_end), NULL);

	src.chunk = lines_chunk;
	src.position = data;
	row = 0;

	if ((data_end-data >= 3) && !strncmp(src.position, "\xEF\xBB\xBF", 3)) {
		/* Skip over byte-order mark */
		src.position += 3;
	}

	lines = g_ptr_array_new ();
	while (*src.position != '\0' && src.position < data_end) {
		GPtrArray *line;

		if (row == GNM_MAX_ROWS) {
			parseoptions->rows_exceeded = TRUE;
			break;
		}

		line = parseoptions->parsetype == PARSE_TYPE_CSV
			? stf_parse_csv_line (&src, parseoptions)
			: stf_parse_fixed_line (&src, parseoptions);

		g_ptr_array_add (lines, line);
		if (parseoptions->parsetype != PARSE_TYPE_CSV)
			src.position += compare_terminator (src.position, parseoptions);
		row++;
	}

	return lines;
}

/**
 * stf_parse_lines: (skip)
 * @parseoptions: #StfParseOptions_t
 * @lines_chunk:
 * @data:
 * @maxlines:
 * @with_lineno:
 *
 * Returns: (transfer full): a GPtrArray of lines, where each line is itself a
 * GPtrArray of strings.
 *
 * The caller must free this entire structure, for example by calling
 * stf_parse_general_free.
 **/
GPtrArray *
stf_parse_lines (StfParseOptions_t *parseoptions,
		 GStringChunk *lines_chunk,
		 char const *data,
		 int maxlines, gboolean with_lineno)
{
	GPtrArray *lines;
	int lineno = 1;

	g_return_val_if_fail (data != NULL, NULL);

	lines = g_ptr_array_new ();
	while (*data) {
		char const *data0 = data;
		GPtrArray *line = g_ptr_array_new ();

		if (with_lineno) {
			char buf[4 * sizeof (int)];
			sprintf (buf, "%d", lineno);
			g_ptr_array_add (line,
					 g_string_chunk_insert (lines_chunk, buf));
		}

		while (1) {
			int termlen = compare_terminator (data, parseoptions);
			if (termlen > 0 || *data == 0) {
				g_ptr_array_add (line,
						 g_string_chunk_insert_len (lines_chunk,
									    data0,
									    data - data0));
				data += termlen;
				break;
			} else
				data = g_utf8_next_char (data);
		}

		g_ptr_array_add (lines, line);

		lineno++;
		if (lineno >= maxlines)
			break;
	}
	return lines;
}

char const *
stf_parse_find_line (StfParseOptions_t *parseoptions,
		     char const *data,
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
 * stf_parse_options_fixed_autodiscover:
 * @parseoptions: a Parse options struct.
 * @data: The actual data.
 * @data_end: data end.
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
	 * Kewl stuff:
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
		 * Try to find columns that look like:
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

/*
 * This is more or less as gnm_cell_set_text, except...
 * 1. Unknown names are not allowed.
 * 2. Only '=' can start an expression.
 */

static void
stf_cell_set_text (GnmCell *cell, char const *text)
{
	GnmExprTop const *texpr;
	GnmValue *val;
	GOFormat const *fmt = gnm_cell_get_format (cell);
	const GODateConventions *date_conv = sheet_date_conv (cell->base.sheet);

	if (!go_format_is_text (fmt) && *text == '=' && text[1] != 0) {
		GnmExprParseFlags flags =
			GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID;
		const char *expr_start = text + 1;
		GnmParsePos pos;
		val = NULL;
		parse_pos_init_cell (&pos, cell);
		texpr = gnm_expr_parse_str (expr_start, &pos, flags,
					    NULL, NULL);
	} else {
		texpr = NULL;
		val = format_match (text, fmt, date_conv);
	}

	if (!val && !texpr)
		val = value_new_string (text);

	if (val)
		gnm_cell_set_value (cell, val);
	else {
		gnm_cell_set_expr (cell, texpr);
		gnm_expr_top_unref (texpr);
	}
}

static void
stf_read_remember_settings (Workbook *book, StfParseOptions_t *po)
{
	if (po->parsetype == PARSE_TYPE_CSV) {
		GnmStfExport *stfe = gnm_stf_get_stfe (G_OBJECT (book));
		char quote[6];
		int length = g_unichar_to_utf8 (po->stringindicator, quote);
		if (length > 5) {
			quote[0] = '"';
			quote[1] = '\0';
		} else quote[length] = '\0';

		g_object_set (G_OBJECT (stfe), "separator", po->sep.chr, "quote", &quote, NULL);

		if ((po->terminator != NULL) &&  (po->terminator->data != NULL))
			g_object_set (G_OBJECT (stfe), "eol", po->terminator->data, NULL);
	}
}

gboolean
stf_parse_sheet (StfParseOptions_t *parseoptions,
		 char const *data, char const *data_end,
		 Sheet *sheet, int start_col, int start_row)
{
	int row;
	unsigned int lrow;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	gboolean result = TRUE;
	int col;
	unsigned int lcol;
	size_t nformats;

	SETUP_LOCALE_SWITCH;

	g_return_val_if_fail (parseoptions != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (!data_end)
		data_end = data + strlen (data);

	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_general (parseoptions, lines_chunk, data, data_end);
	if (lines == NULL)
		result = FALSE;

	col = start_col;
	nformats = parseoptions->formats->len;
	for (lcol = 0; lcol < nformats; lcol++) {
		GOFormat const *fmt = g_ptr_array_index (parseoptions->formats, lcol);
		GnmStyle *mstyle;
		gboolean want_col =
			(parseoptions->col_import_array == NULL ||
			 parseoptions->col_import_array_len <= lcol ||
			 parseoptions->col_import_array[lcol]);
		if (!want_col || col >= gnm_sheet_get_max_cols (sheet))
			continue;

		if (fmt && !go_format_is_general (fmt)) {
			GnmRange r;
			int end_row = MIN (start_row + (int)lines->len - 1,
					   gnm_sheet_get_last_row (sheet));

			range_init (&r, col, start_row, col, end_row);
			mstyle = gnm_style_new ();
			gnm_style_set_format (mstyle, fmt);
			sheet_apply_style (sheet, &r, mstyle);
		}
		col++;
	}

	START_LOCALE_SWITCH;
	for (row = start_row, lrow = 0;
	     result && lrow < lines->len;
	     row++, lrow++) {
		GPtrArray *line;

		if (row >= gnm_sheet_get_max_rows (sheet)) {
			if (!parseoptions->rows_exceeded) {
				/* FIXME: What locale?  */
				g_warning (_("There are more rows of data than "
					     "there is room for in the sheet.  Extra "
					     "rows will be ignored."));
				parseoptions->rows_exceeded = TRUE;
			}
			break;
		}

		col = start_col;
		line = g_ptr_array_index (lines, lrow);

		for (lcol = 0; lcol < line->len; lcol++) {
			GOFormat const *fmt = lcol < nformats
				? g_ptr_array_index (parseoptions->formats, lcol)
				: go_format_general ();
			char const *text = g_ptr_array_index (line, lcol);
			gboolean want_col =
				(parseoptions->col_import_array == NULL ||
				 parseoptions->col_import_array_len <= lcol ||
				 parseoptions->col_import_array[lcol]);
			if (!want_col)
				continue;

			if (col >= gnm_sheet_get_max_cols (sheet)) {
				if (!parseoptions->cols_exceeded) {
					/* FIXME: What locale?  */
					g_warning (_("There are more columns of data than "
						     "there is room for in the sheet.  Extra "
						     "columns will be ignored."));
					parseoptions->cols_exceeded = TRUE;
				}
				break;
			}
			if (text && *text) {
				GnmCell *cell = sheet_cell_fetch (sheet, col, row);
				if (!go_format_is_text (fmt) &&
				    text[0] != '=' && text[0] != '\'' &&
				    lcol < parseoptions->formats_decimal->len &&
				    g_ptr_array_index (parseoptions->formats_decimal, lcol)) {
					GOFormatFamily fam;
					GnmValue *v = format_match_decimal_number_with_locale
						(text, &fam,
						 g_ptr_array_index (parseoptions->formats_curr, lcol),
						 g_ptr_array_index (parseoptions->formats_thousand, lcol),
						 g_ptr_array_index (parseoptions->formats_decimal, lcol));
					if (!v)
						v = value_new_string (text);
					sheet_cell_set_value (cell, v);
				} else {
					stf_cell_set_text (cell, text);
				}
			}
			col++;
		}

		g_ptr_array_index (lines, lrow) = NULL;
		g_ptr_array_free (line, TRUE);
	}
	END_LOCALE_SWITCH;

	for (lcol = 0, col = start_col;
	     lcol < parseoptions->col_import_array_len  && col < gnm_sheet_get_max_cols (sheet);
	     lcol++) {
		if (parseoptions->col_import_array == NULL ||
		    parseoptions->col_import_array_len <= lcol ||
		    parseoptions->col_import_array[lcol]) {
			if (parseoptions->col_autofit_array == NULL ||
			    parseoptions->col_autofit_array[lcol]) {
				ColRowIndexList *list = colrow_get_index_list (col, col, NULL);
				ColRowStateGroup  *state = colrow_set_sizes (sheet, TRUE, list, -1, 0, -1);
				colrow_index_list_destroy (list);
				g_slist_free (state);
			}
			col++;
		}
	}

	g_string_chunk_free (lines_chunk);
	if (lines)
		stf_parse_general_free (lines);
	if (result)
		stf_read_remember_settings (sheet->workbook, parseoptions);
	return result;
}

GnmCellRegion *
stf_parse_region (StfParseOptions_t *parseoptions, char const *data, char const *data_end,
		  Workbook const *wb)
{
	static GODateConventions const default_conv = {FALSE};
	GODateConventions const *date_conv = wb ? workbook_date_conv (wb) : &default_conv;

	GnmCellRegion *cr;
	unsigned int row, colhigh = 0;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	size_t nformats;

	SETUP_LOCALE_SWITCH;

	g_return_val_if_fail (parseoptions != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	START_LOCALE_SWITCH;

	cr = gnm_cell_region_new (NULL);

	if (!data_end)
		data_end = data + strlen (data);
	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_general (parseoptions, lines_chunk, data, data_end);
	nformats = parseoptions->formats->len;
	for (row = 0; row < lines->len; row++) {
		GPtrArray *line = g_ptr_array_index (lines, row);
		unsigned int col, targetcol = 0;
		for (col = 0; col < line->len; col++) {
			if (parseoptions->col_import_array == NULL ||
			    parseoptions->col_import_array_len <= col ||
			    parseoptions->col_import_array[col]) {
				const char *text = g_ptr_array_index (line, col);
				if (text) {
					GOFormat *fmt = NULL;
					GnmValue *v;
					GnmCellCopy *cc;

					if (col < nformats)
						fmt = g_ptr_array_index (parseoptions->formats, col);
					v = format_match (text, fmt, date_conv);
					if (!v)
						v = value_new_string (text);

					cc = gnm_cell_copy_new (cr, targetcol, row);
					cc->val  = v;
					cc->texpr = NULL;
					targetcol++;
					if (targetcol > colhigh)
						colhigh = targetcol;
				}
			}
		}
	}
	stf_parse_general_free (lines);
	g_string_chunk_free (lines_chunk);

	END_LOCALE_SWITCH;

	cr->cols    = (colhigh > 0) ? colhigh : 1;
	cr->rows    = row;

	return cr;
}

static int
int_sort (void const *a, void const *b)
{
	return *(int const *)a - *(int const *)b;
}

static int
count_character (GPtrArray *lines, gunichar c, double quantile)
{
	int *counts, res;
	unsigned int lno, cno;

	if (lines->len == 0)
		return 0;

	counts = g_new (int, lines->len);
	for (lno = cno = 0; lno < lines->len; lno++) {
		int count = 0;
		GPtrArray *boxline = g_ptr_array_index (lines, lno);
		char const *line = g_ptr_array_index (boxline, 0);

		/* Ignore empty lines.  */
		if (*line == 0)
			continue;

		while (*line) {
			if (g_utf8_get_char (line) == c)
				count++;
			line = g_utf8_next_char (line);
		}

		counts[cno++] = count;
	}

	if (cno == 0)
		res = 0;
	else {
		unsigned int qi = (unsigned int)ceil (quantile * cno);
		qsort (counts, cno, sizeof (counts[0]), int_sort);
		if (qi == cno)
			qi--;
		res = counts[qi];
	}

	g_free (counts);

	return res;
}

static void
dump_guessed_options (const StfParseOptions_t *res)
{
	GSList *l;
	char ubuffer[6 + 1];
	unsigned ui;

	g_printerr ("Guessed format:\n");
	switch (res->parsetype) {
	case PARSE_TYPE_CSV:
		g_printerr ("  type = sep\n");
		g_printerr ("  separator = %s\n",
			    res->sep.chr ? res->sep.chr : "(none)");
		g_printerr ("    see two as one = %s\n",
			    res->sep.duplicates ? "yes" : "no");
		break;
	case PARSE_TYPE_FIXED:
		g_printerr ("  type = sep\n");
		break;
	default:
		;
	}
	g_printerr ("  trim space = %d\n", res->trim_spaces);

	ubuffer[g_unichar_to_utf8 (res->stringindicator, ubuffer)] = 0;
	g_printerr ("  string indicator = %s\n", ubuffer);
	g_printerr ("    see two as one = %s\n",
		    res->indicator_2x_is_single ? "yes" : "no");

	g_printerr ("  line terminators =");
	for (l = res->terminator; l; l = l->next) {
		const char *t = l->data;
		if (strcmp (t, "\n") == 0)
			g_printerr (" unix");
		else if (strcmp (t, "\r") == 0)
			g_printerr (" mac");
		else if (strcmp (t, "\r\n") == 0)
			g_printerr (" dos");
		else
			g_printerr (" other");
	}
	g_printerr ("\n");

	for (ui = 0; ui < res->formats->len; ui++) {
		GOFormat const *fmt = g_ptr_array_index (res->formats, ui);
		const GString *decimal = ui < res->formats_decimal->len
			? g_ptr_array_index (res->formats_decimal, ui)
			: NULL;
		const GString *thousand = ui < res->formats_thousand->len
			? g_ptr_array_index (res->formats_thousand, ui)
			: NULL;

		g_printerr ("  fmt.%d = %s\n", ui, go_format_as_XL (fmt));
		if (decimal)
			g_printerr ("  fmt.%d.dec = %s\n", ui, decimal->str);
		if (thousand)
			g_printerr ("  fmt.%d.thou = %s\n", ui, thousand->str);
	}
}

/**
 * stf_parse_options_guess:
 * @data: the input data.
 *
 * Returns: (transfer full): the guessed options.
 **/
StfParseOptions_t *
stf_parse_options_guess (char const *data)
{
	StfParseOptions_t *res;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	int tabcount;
	int sepcount;
	gunichar sepchar = go_locale_get_arg_sep ();

	g_return_val_if_fail (data != NULL, NULL);

	res = stf_parse_options_new ();
	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_lines (res, lines_chunk, data, 1000, FALSE);

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
		    count_character (lines, (c = go_locale_get_col_sep ()), 0.5) > 0 ||
		    count_character (lines, (c = ':'), 0.5) > 0 ||
		    count_character (lines, (c = ','), 0.5) > 0 ||
		    count_character (lines, (c = ';'), 0.5) > 0 ||
		    count_character (lines, (c = '|'), 0.5) > 0 ||
		    count_character (lines, (c = '!'), 0.5) > 0 ||
		    count_character (lines, (c = ' '), 0.5) > 0) {
			char sep[7];
			sep[g_unichar_to_utf8 (c, sep)] = 0;
			if (c == ' ')
				strcat (sep, "\t");
			stf_parse_options_csv_set_separators (res, sep, NULL);
		}
	}

	// For now, always separated:
	stf_parse_options_set_type (res, PARSE_TYPE_CSV);

	switch (res->parsetype) {
	case PARSE_TYPE_CSV: {
		gboolean dups =
			res->sep.chr &&
			strchr (res->sep.chr, ' ') != NULL;
		gboolean trim =
			res->sep.chr &&
			strchr (res->sep.chr, ' ') != NULL;

		stf_parse_options_set_trim_spaces (res, TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT);
		stf_parse_options_csv_set_indicator_2x_is_single (res, TRUE);
		stf_parse_options_csv_set_duplicates (res, dups);
		stf_parse_options_csv_set_trim_seps (res, trim);

		stf_parse_options_csv_set_stringindicator (res, '"');
		break;
	}

	case PARSE_TYPE_FIXED:
		break;

	default:
		g_assert_not_reached ();
	}

	stf_parse_general_free (lines);
	g_string_chunk_free (lines_chunk);

	stf_parse_options_guess_formats (res, data);

	if (gnm_debug_flag ("stf"))
		dump_guessed_options (res);

	return res;
}

/**
 * stf_parse_options_guess_csv:
 * @data: the CSV input data.
 *
 * Returns: (transfer full): the guessed options.
 **/
StfParseOptions_t *
stf_parse_options_guess_csv (char const *data)
{
	StfParseOptions_t *res;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	char *sep = NULL;
	char const *quoteline = NULL;
	int pass;
	gunichar stringind = '"';

	g_return_val_if_fail (data != NULL, NULL);

	res = stf_parse_options_new ();
	stf_parse_options_set_type (res, PARSE_TYPE_CSV);
	stf_parse_options_set_trim_spaces (res, TRIM_TYPE_NEVER);
	stf_parse_options_csv_set_indicator_2x_is_single (res, TRUE);
	stf_parse_options_csv_set_duplicates (res, FALSE);
	stf_parse_options_csv_set_trim_seps (res, FALSE);
	stf_parse_options_csv_set_stringindicator (res, stringind);

	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_lines (res, lines_chunk, data, 1000, FALSE);

	// Find a line containing a quote; skip first line unless it is
	// the only one.  Prefer a line with the quote first.
	//
	// Pass 1: look for initial quote, but not on first line
	// Pass 2: look for initial quote on first line
	// Pass 3: look for quote anywhere in any line
	for (pass = 1; !quoteline && pass <= 3; pass++) {
		size_t lno;
		size_t lstart = (pass == 1 ? 1 : 0);
		size_t lend = (pass == 2 ? 1 : -1);

		for (lno = lstart;
		     !quoteline && lno < MIN (lend, lines->len);
		     lno++) {
			GPtrArray *boxline = g_ptr_array_index (lines, lno);
			const char *line = g_ptr_array_index (boxline, 0);
			switch (pass) {
			case 1:
			case 2:
				if (g_utf8_get_char (line) == stringind)
					quoteline = line;
				break;
			case 3:
				if (my_utf8_strchr (line, stringind))
					quoteline = line;
				break;
			}
		}
	}

	if (quoteline) {
		const char *p0 = my_utf8_strchr (quoteline, stringind);
		const char *p = p0;
		gboolean inquote;

		if (gnm_debug_flag ("stf"))
			g_printerr ("quoteline = [%s]\n", quoteline);

		p = g_utf8_next_char (p);
		inquote = TRUE;
		while (inquote) {
			gunichar c = g_utf8_get_char (p);
			if (c == stringind) {
				p = g_utf8_next_char (p);
				if (g_utf8_get_char (p) == stringind)
					p = g_utf8_next_char (p);
				else
					inquote = FALSE;
			} else if (c == 0)
				break;
			else
				p = g_utf8_next_char (p);
		}

		if (*p) p = g_utf8_next_char (p);
		while (*p && g_unichar_isspace (g_utf8_get_char (p)))
			p = g_utf8_next_char (p);
		if (*p && g_utf8_get_char (p) != stringind &&
		    g_unichar_ispunct (g_utf8_get_char (p))) {
			// Use the character after the quote.
			sep = g_strndup (p, g_utf8_next_char (p) - p);
		} else {
			/* Try to use character before the quote.  */
			while (p0 > quoteline && !sep) {
				gunichar uc;
				p = p0;
				p0 = g_utf8_prev_char (p0);
				uc = g_utf8_get_char (p0);
				if (g_unichar_ispunct (uc) &&
				    uc != stringind)
					sep = g_strndup (p0, p - p0);
			}
		}
	}

	if (!sep)
		sep = g_strdup (",");
	stf_parse_options_csv_set_separators (res, sep, NULL);
	g_free (sep);

	stf_parse_general_free (lines);
	g_string_chunk_free (lines_chunk);

	stf_parse_options_guess_formats (res, data);

	if (gnm_debug_flag ("stf"))
		dump_guessed_options (res);

	return res;
}

typedef enum {
	STF_GUESS_DATE_DMY = 1,
	STF_GUESS_DATE_MDY = 2,
	STF_GUESS_DATE_YMD = 4,

	STF_GUESS_NUMBER_DEC_POINT = 0x10,
	STF_GUESS_NUMBER_DEC_COMMA = 0x20,
	STF_GUESS_NUMBER_DEC_EITHER = 0x30,

	STF_GUESS_ALL = 0x37
} StfGuessFormats;

static void
do_check_date (const char *data, StfGuessFormats flag,
	       gboolean mbd, gboolean ybm,
	       unsigned *possible,
	       GODateConventions const *date_conv)
{
	GnmValue *v;
	gboolean has_all, this_mbd, this_ybm;
	int imbd;
	GOFormat const *fmt;

	if (!(*possible & flag))
		return;

	v = format_match_datetime (data, date_conv, mbd, TRUE, FALSE);
	if (!v || !VALUE_FMT (v))
		goto fail;

	fmt = VALUE_FMT (v);
	has_all = (go_format_has_year (fmt) &&
		   go_format_has_month (fmt) &&
		   go_format_has_day (fmt));
	if (!has_all) {
		// See #401
		// Avoid detecting 1.9999 is a date.
		goto fail;
	}

	imbd = go_format_month_before_day (fmt);
	this_mbd = (imbd >= 1);
	this_ybm = (imbd == 2);
	if (mbd != this_mbd || ybm != this_ybm)
		goto fail;

	goto done;

fail:
	*possible &= ~flag;
done:
	value_release (v);
}


static void
do_check_number (const char *data, StfGuessFormats flag,
		 const GString *dec, const GString *thousand, const GString *curr,
		 unsigned *possible, int *decimals)
{
	GnmValue *v;
	GOFormatFamily family;
	const char *pthou;

	if (!(*possible & flag))
		return;

	v = format_match_decimal_number_with_locale (data, &family, curr, thousand, dec);
	if (!v)
		goto fail;

	if (*decimals != -2) {
		const char *pdec = strstr (data, dec->str);
		int this_decimals = 0;
		if (pdec) {
			pdec += dec->len;
			while (g_ascii_isdigit (*pdec)) {
				pdec++;
				this_decimals++;
			}
		}
		if (*decimals == -1)
			*decimals = this_decimals;
		else if (*decimals != this_decimals)
			*decimals = -2;
	}

	pthou = strstr (data, thousand->str);
	if (pthou) {
		const char *p;
		int digits = 0, nonzero_digits = 0;
		for (p = data; p < pthou; p = g_utf8_next_char (p)) {
			if (g_unichar_isdigit (g_utf8_get_char (p))) {
				digits++;
				if (*p != '0')
					nonzero_digits++;
			}
		}
		// "-.222" implies that "." is not a thousands separator.
		// "0.222" implies that "." is not a thousands separator.
		// "12345,555" implies that "," is not a thousands separator.
		if (nonzero_digits == 0 || digits > 3)
			goto fail;
	}

	goto done;

fail:
	*possible &= ~flag;
done:
	value_release (v);
}


/**
 * stf_parse_options_guess_formats:
 * @data: the CSV input data.
 *
 * This function attempts to recognize data formats on a column-by-column
 * basis under the assumption that the data in a text file will generally
 * use the same data formats.
 *
 * This is useful because not all values give sufficient information by
 * themselves to tell what format the data is in.  For example, "1/2/2000"
 * is likely to be a date in year 2000, but it is not clear if it is in
 * January or February.  If another value in the same column is "31/1/1999"
 * then it is likely that the former date was in February.
 *
 * Likewise, a value of "123,456" could mean either 1.23456e5 or 1.23456e2.
 * A later value of "111,200.22" would clear up the confusion.
 *
 **/
void
stf_parse_options_guess_formats (StfParseOptions_t *po, char const *data)
{
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	unsigned lno, col, colcount, sline;
	GODateConventions const *date_conv = go_date_conv_from_str ("Lotus:1900");
	GString *s_comma = g_string_new (",");
	GString *s_dot = g_string_new (".");
	GString *s_dollar = g_string_new ("$");
	gboolean debug = gnm_debug_flag ("stf");

	g_ptr_array_set_size (po->formats, 0);
	g_ptr_array_set_size (po->formats_decimal, 0);
	g_ptr_array_set_size (po->formats_thousand, 0);
	g_ptr_array_set_size (po->formats_curr, 0);

	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_general (po, lines_chunk, data, data + strlen (data));

	colcount = 0;
	for (lno = 0; lno < lines->len; lno++) {
		GPtrArray *line = g_ptr_array_index (lines, lno);
		colcount = MAX (colcount, line->len);
	}

	// Ignore first line unless it is the only one
	sline = MIN ((int)lines->len - 1, 1);

	g_ptr_array_set_size (po->formats, colcount);
	g_ptr_array_set_size (po->formats_decimal, colcount);
	g_ptr_array_set_size (po->formats_thousand, colcount);
	g_ptr_array_set_size (po->formats_curr, colcount);
	for (col = 0; col < colcount; col++) {
		unsigned possible = STF_GUESS_ALL;
		GOFormat *fmt = NULL;
		gboolean seen_dot = FALSE;
		gboolean seen_comma = FALSE;
		int decimals_if_point = -1; // -1: unset; -2: inconsistent; >=0: count
		int decimals_if_comma = -1; // -1: unset; -2: inconsistent; >=0: count

		for (lno = sline; possible && lno < lines->len; lno++) {
			GPtrArray *line = g_ptr_array_index (lines, lno);
			const char *data = col < line->len ? g_ptr_array_index (line, col) : "";
			unsigned prev_possible = possible;
			gunichar c0 = g_utf8_get_char (data);

			if (c0 == 0 || c0 == '\'' || c0 == '=')
				continue;

			do_check_date (data, STF_GUESS_DATE_DMY, FALSE, FALSE, &possible, date_conv);
			do_check_date (data, STF_GUESS_DATE_MDY, TRUE, FALSE, &possible, date_conv);
			do_check_date (data, STF_GUESS_DATE_YMD, TRUE, TRUE, &possible, date_conv);

			if ((possible & STF_GUESS_NUMBER_DEC_EITHER) == STF_GUESS_NUMBER_DEC_EITHER) {
				const char *pdot = strstr (data, s_dot->str);
				const char *pcomma = strstr (data, s_comma->str);
				if (pdot && pcomma) {
					// Both -- last one is the decimal separator
					if (pdot > pcomma)
						possible &= ~STF_GUESS_NUMBER_DEC_COMMA;
					else
						possible &= ~STF_GUESS_NUMBER_DEC_POINT;
				} else if (pdot && strstr (pdot + s_dot->len, s_dot->str)) {
					// Two dots so they are thousands separators
					possible &= ~STF_GUESS_NUMBER_DEC_POINT;
				} else if (pcomma && strstr (pcomma + s_comma->len, s_comma->str)) {
					// Two commas so they are thousands separators
					possible &= ~STF_GUESS_NUMBER_DEC_COMMA;
				}

				seen_dot = seen_dot || (pdot != 0);
				seen_comma = seen_comma || (pcomma != 0);
			}
			do_check_number (data, STF_GUESS_NUMBER_DEC_POINT,
					 s_dot, s_comma, s_dollar,
					 &possible, &decimals_if_point);
			do_check_number (data, STF_GUESS_NUMBER_DEC_COMMA,
					 s_comma, s_dot, s_dollar,
					 &possible, &decimals_if_comma);

			if (possible != prev_possible && debug)
				g_printerr ("col=%d; after [%s] possible=0x%x\n", col, data, possible);
		}

		if ((possible & STF_GUESS_NUMBER_DEC_EITHER) == STF_GUESS_NUMBER_DEC_EITHER) {
			if (!seen_dot && !seen_comma) {
				// It doesn't matter what the separators are
				possible &= ~STF_GUESS_NUMBER_DEC_COMMA;
			} else if (seen_dot && !seen_comma) {
				// Hope for the best
				possible &= ~STF_GUESS_NUMBER_DEC_COMMA;
			} else if (seen_comma && !seen_dot) {
				// Hope for the best
				possible &= ~STF_GUESS_NUMBER_DEC_EITHER;
			}
		}

		switch (possible) {
		case STF_GUESS_DATE_DMY:
			fmt = go_format_new_from_XL ("d-mmm-yyyy");
			break;
		case STF_GUESS_DATE_MDY:
			fmt = go_format_new_from_XL ("m/d/yyyy");
			break;
		case STF_GUESS_DATE_YMD:
			fmt = go_format_new_from_XL ("yyyy-mm-dd");
			break;
		case STF_GUESS_NUMBER_DEC_POINT:
			g_ptr_array_index (po->formats_decimal, col) = g_string_new (".");
			g_ptr_array_index (po->formats_thousand, col) = g_string_new (",");
			g_ptr_array_index (po->formats_curr, col) = g_string_new (s_dollar->str);
			if (decimals_if_point > 0) {
				// Don't set format if decimals is zero
				GString *fmt_str = g_string_new (NULL);
				go_format_generate_number_str (fmt_str, 1, decimals_if_point, seen_comma, FALSE, FALSE, "", "");
				fmt = go_format_new_from_XL (fmt_str->str);
				g_string_free (fmt_str, TRUE);
			}
			break;
		case STF_GUESS_NUMBER_DEC_COMMA:
			g_ptr_array_index (po->formats_decimal, col) = g_string_new (",");
			g_ptr_array_index (po->formats_thousand, col) = g_string_new (".");
			g_ptr_array_index (po->formats_curr, col) = g_string_new (s_dollar->str);
			if (decimals_if_comma > 0) {
				// Don't set format if decimals is zero
				GString *fmt_str = g_string_new (NULL);
				go_format_generate_number_str (fmt_str, 1, decimals_if_comma, seen_dot, FALSE, FALSE, "", "");
				fmt = go_format_new_from_XL (fmt_str->str);
				g_string_free (fmt_str, TRUE);
			}
			break;
		default:
			break;
		}

		if (!fmt)
			fmt = go_format_ref (go_format_general ());
		g_ptr_array_index (po->formats, col) = fmt;
	}

	stf_parse_general_free (lines);
	g_string_chunk_free (lines_chunk);

	g_string_free (s_dot, TRUE);
	g_string_free (s_comma, TRUE);
	g_string_free (s_dollar, TRUE);
}
