/*
 * GNU Oleo input filter for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * partially based on the Lotus-123 code,
 * partially based on actual Oleo code.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "oleo.h"

#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>
#include <expr.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <ranges.h>
#include <number-match.h>
#include <func.h>

#include <gsf/gsf-input-textline.h>

#include <string.h>
#include <stdlib.h>

#define OLEO_DEBUG 0


typedef struct {
	GnmConventions	 *convs;
	GnmParsePos	  pp;

	GsfInputTextline *textline;
	GIConv            converter;
} OleoReader;

static void
append_zeros (GString *s, int n)
{
	if (n > 0) {
		size_t oldlen = s->len;
		g_string_set_size (s, oldlen + n);
		memset (s->str+oldlen, '0', n);
	}
}

/* adapted from Oleo */
static long
astol (char **ptr)
{
	char *end;
	long res;

	res = strtol (*ptr, &end, 10);
	*ptr = end;

	return res;
}


static void
oleo_set_style (OleoReader *state, GnmStyle *style)
{
	/* sheet_style_set_range absorbs our reference */
	gnm_style_ref (style);
	sheet_style_set_pos (state->pp.sheet,
		state->pp.eval.col, state->pp.eval.row, style);
}

static GnmExprTop const *
oleo_parse_formula (OleoReader *state, char const *expr_str)
{
	GnmParseError error;
	GnmExprTop const *texpr = gnm_expr_parse_str (expr_str,
		&state->pp, GNM_EXPR_PARSE_DEFAULT,
		state->convs, parse_error_init (&error));

	if (error.err != NULL) {
		g_warning ("%s \"%s\" at %s!%s.",  error.err->message, expr_str,
			   state->pp.sheet->name_unquoted,
			   cell_coord_name (state->pp.eval.col, state->pp.eval.row));
	}
	parse_error_free (&error);

	return texpr;
}

static void
oleo_parse_cell (OleoReader *state, guint8 *str, GnmStyle *style)
{
	GnmCell *cell;
	GnmExprTop const *texpr = NULL;
	char *ptr = str + 1, *cval = NULL, *formula = NULL;

	while (*ptr) {
		int quotes = 0;
		if (*ptr != ';') {
#if OLEO_DEBUG > 0
			g_warning ("ptr: %s.", ptr);
#endif
			break;
		}
		*ptr++ = '\0';
		switch (*ptr++) {
		case 'c' : state->pp.eval.col = astol (&ptr) - 1; break;
		case 'r' : state->pp.eval.row = astol (&ptr) - 1; break;
		case 'K' :
			cval = ptr;
			quotes = 0;
			while (*ptr && (*ptr != ';' || quotes > 0))
				if (*ptr++ == '"')
					quotes = !quotes;
			break;

		case 'E' :
			formula = ptr;
			while (*ptr && *ptr != ';')
				ptr++;
			break;

		default:
#if OLEO_DEBUG > 0
			g_warning ("oleo: Don't know how to deal with C; '%c'.",
				   *ptr);
#endif
			ptr = (char *)""; /* I wish C had multilevel break */
			break;
		}

		if (!*ptr)
			break;
	}

	cell = sheet_cell_fetch (state->pp.sheet,
		state->pp.eval.col, state->pp.eval.row);

	if (formula != NULL)
		texpr = oleo_parse_formula (state, formula);

	if (cval != NULL) {
		GnmValue *val = format_match_simple (cval);

		if (val == NULL) {
			char *last = cval + strlen (cval) - 1;
			if (*cval == '"' && *last == '"') {
				*last = 0;
				val = value_new_string (cval + 1);
			} else
				val = value_new_string (cval);
		}

		if (texpr != NULL)
			gnm_cell_set_expr_and_value (cell, texpr, val, TRUE);
		else
			gnm_cell_set_value (cell, val);

		if (style != NULL)
			oleo_set_style (state, style);

	} else {
#if OLEO_DEBUG > 0
		g_warning ("oleo: cval is NULL.");
#endif
		/* We can still store the expression, even if the value is missing */
		if (texpr != NULL)
			gnm_cell_set_expr (cell, texpr);
	}
	if (texpr)
		gnm_expr_top_unref (texpr);
}

/* NOTE : We don't care too much about formatting as such, but we need to
 * parse the command as it may update current row/column */
static void
oleo_parse_style (OleoReader *state, guint8 *str, GnmStyle **res)
{
	char *ptr = str + 1;
	GnmStyle *style = gnm_style_new_default ();
	GString  *fmt_string = g_string_new (NULL);

	while (*ptr) {
		char c = *ptr++;

		switch (c) {
		case 'c' : state->pp.eval.col = astol (&ptr) - 1; break;
		case 'r' : state->pp.eval.row = astol (&ptr) - 1; break;
		case 'F': case 'G':
			c = *ptr++;

			g_string_truncate (fmt_string, 0);
			g_string_append_c (fmt_string, '0');
			if (g_ascii_isdigit (*ptr))
				append_zeros (fmt_string, astol (&ptr));
			switch (c) {
			case 'F':
				break;
			case '%':
				g_string_append_c (fmt_string, '%');
				break;
			default: /* Unknown format type... */
				g_string_truncate (fmt_string, 0);
			}
			break;
		case 'L':
			gnm_style_set_align_h (style, GNM_HALIGN_LEFT);
			break;
		case 'R':
			gnm_style_set_align_h (style, GNM_HALIGN_RIGHT);
		}
	}
	if (fmt_string->len)
		gnm_style_set_format_text (style, fmt_string->str);
	g_string_free (fmt_string, TRUE);

	if (*res)
		gnm_style_unref (*res);
	*res = style;
}

static Sheet *
oleo_new_sheet (Workbook *wb, int idx)
{
	char  *sheet_name = g_strdup_printf (_("Sheet%d"), idx);
	Sheet *sheet = sheet_new (wb, sheet_name, 256, 65536);
	g_free (sheet_name);
	workbook_sheet_attach (wb, sheet);

	/* Ensure that things get rendered and spanned */
	sheet_flag_recompute_spans (sheet);
	return sheet;
}

static GnmConventions *
oleo_conventions_new (void)
{
	GnmConventions *convs = gnm_conventions_new ();

	convs->decimal_sep_dot	 = TRUE;
	convs->intersection_char = 0;
	convs->r1c1_addresses	 = TRUE;

	return convs;
}

void
oleo_read (GOIOContext *io_context, Workbook *wb, GsfInput *input)
{
	int sheetidx = 0;
	GnmStyle *style = NULL;
	guint8 *line;
	OleoReader state;

	state.convs = oleo_conventions_new ();
	parse_pos_init (&state.pp,
		wb, oleo_new_sheet (wb, ++sheetidx), 0, 0);

	/* Does this need to come from the import dialog ? */
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");
	state.textline  = (GsfInputTextline *) gsf_input_textline_new (input);

	while (NULL != (line = gsf_input_textline_ascii_gets (state.textline))) {
		char *utf8line =
			g_convert_with_iconv (line, -1, state.converter, NULL, NULL, NULL);

		switch (utf8line[0]) {
		case '#': /* Comment */
			break;

		case 'C': oleo_parse_cell (&state, utf8line, style);
			break;

		case 'F': oleo_parse_style (&state, utf8line, &style);
			break;

		default: /* unknown */
#if OLEO_DEBUG > 0
			g_warning ("oleo: Don't know how to deal with %c.",
				   line[0]);
#endif
			break;
		}

		g_free (utf8line);
	}

	if (style)
		gnm_style_unref (style);

	g_iconv_close (state.converter);
	gnm_conventions_unref (state.convs);
	g_object_unref (state.textline);
}
