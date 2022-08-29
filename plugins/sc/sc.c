/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 * Copyright (C) 2010 Andreas J. Guelzow <aguelzow@pyrshep.ca> All Rights Reserved
 *
 * With some code from sylk.c
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <string.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <sheet-style.h>
#include <workbook-view.h>
#include <workbook.h>
#include <parse-util.h>
#include <value.h>
#include <cell.h>
#include <ranges.h>
#include <style.h>
#include <sheet.h>
#include <expr.h>
#include <expr-name.h>
#include <func.h>
#include <sheet-view.h>
#include <selection.h>
#include <rendered-value.h>
#include <style-font.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-input-textline.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean sc_file_probe (GOFileOpener const *fo, GsfInput *input,
			GOFileProbeLevel pl);
void sc_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                   WorkbookView *wb_view, GsfInput *input);

typedef struct {
	GsfInputTextline *textline;
	Sheet            *sheet;
	GIConv            converter;
	GnmConventions	 *convs;
	GOIOContext	 *context;	/* The IOcontext managing things */
	char             *last_error;
	GArray           *precision;
	GPtrArray        *formats;
} ScParseState;

typedef enum {
	LABEL,
	LEFTSTRING,
	RIGHTSTRING
} sc_string_cmd_t;


static GOErrorInfo *sc_go_error_info_new_vprintf (GOSeverity severity,
					  char const *msg_format, ...)
	G_GNUC_PRINTF (2, 3);

static GOErrorInfo *
sc_go_error_info_new_vprintf (GOSeverity severity,
			      char const *msg_format, ...)
{
	va_list args;
	GOErrorInfo *ei;

	va_start (args, msg_format);
	ei = go_error_info_new_vprintf (severity, msg_format, args);
	va_end (args);

	return ei;
}

static gboolean sc_warning (ScParseState *state, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
sc_warning (ScParseState *state, char const *fmt, ...)
{
	char *msg;
	char *detail;
	va_list args;

	va_start (args, fmt);
	detail = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->sheet))
		msg = g_strdup_printf (_("On worksheet %s:"),state->sheet->name_quoted);
	else
		msg = g_strdup (_("General SC import error"));

	if (0 != go_str_compare (msg, state->last_error)) {
		GOErrorInfo *ei = sc_go_error_info_new_vprintf
			(GO_WARNING, "%s", msg);

		go_io_error_info_set (state->context, ei);
		g_free (state->last_error);
		state->last_error = msg;
	} else
		g_free (msg);

	go_error_info_add_details
		(state->context->info->data,
		 sc_go_error_info_new_vprintf (GO_WARNING, "%s", detail));

	g_free (detail);

	return FALSE; /* convenience */
}

static gboolean
enlarge (ScParseState *state, int col, int row)
{
	GnmSheetSize const *size = gnm_sheet_get_size (state->sheet);
	gboolean err = FALSE;

	if (col >= size->max_cols
	    || row >= size->max_rows) {
		GOUndo   * goundo;
		int cols_needed = (col >= size->max_cols) ? col + 1
			: size->max_cols;
		int rows_needed = (row >= size->max_rows) ? row + 1
			: size->max_rows;
		gnm_sheet_suggest_size (&cols_needed, &rows_needed);

		goundo = gnm_sheet_resize
			(state->sheet, cols_needed, rows_needed, NULL, &err);
		if (goundo) g_object_unref (goundo);
	}

	return err;
}


static GnmCell  *
sc_sheet_cell_fetch (ScParseState *state, int col, int row)
{
	gboolean err = enlarge (state, col, row);

	if (err) {
		sc_warning (state, _("The cell in row %i and column %i is beyond "
				     "Gnumeric's maximum sheet size."),
			   row, col);

		return NULL;
	} else
		return sheet_cell_fetch (state->sheet, col, row);
}

static gint
sc_colname_to_coords (char const *colname, gint *m)
{
	int mult;
	int digits = 1;

	g_return_val_if_fail (colname, 0);

	if (!colname || !*colname || !g_ascii_isalpha (*colname))
		return 0;

	mult = g_ascii_toupper (*colname) - 'A';
	if (mult < 0 || mult > 25)
		return 0;

	colname++;

	if (g_ascii_isalpha (*colname)) {
		int ofs = g_ascii_toupper (*colname) - 'A';
		if (ofs < 0 || ofs > 25)
			return 0;
		mult = ((mult + 1) * 26) + ofs;
		digits++;
	}

	*m = mult;

	return digits;
}


/* we can't use cellpos_parse b/c it doesn't support 0 bases (A0, B0, ...) */
static gboolean
sc_cellname_to_coords (char const *cellname, GnmCellPos *pos)
{
	int mult, digits;

	g_return_val_if_fail (cellname, FALSE);

	digits = sc_colname_to_coords (cellname, &mult);
	if (digits == 0)
		goto err_out;

	pos->col = mult;
	cellname += digits;

	/* XXX need to replace this block with strtol+error checking */
	if (1) {
		if (!g_ascii_isdigit (*cellname))
			goto err_out;

		pos->row = atoi (cellname);
	}

	g_return_val_if_fail (pos->col > -1, FALSE);
	g_return_val_if_fail (pos->row > -1, FALSE);
	return TRUE;

err_out:
	pos->col = pos->row = -1;
	return FALSE;
}



static gboolean
sc_parse_coord_real (ScParseState *state, char const *strdata, GnmCellPos *pos,
		     size_t tmplen)
{
	char *tmpstr;
	GnmNamedExpr *nexpr;
	GnmParsePos pp;
	GnmValue *v;

	g_return_val_if_fail (strdata, FALSE);

	tmpstr = g_strndup (strdata, tmplen);

	/* It ought to be a cellref.  */
	if (sc_cellname_to_coords (tmpstr, pos)) {
		g_return_val_if_fail (pos->col >= 0, FALSE);
		g_return_val_if_fail (pos->row >= 0, FALSE);
		g_free (tmpstr);
		return TRUE;
	}

	/* But it could be a named expression of the same kind.  */

	parse_pos_init (&pp, NULL, state->sheet, 0, 0);
	nexpr = expr_name_lookup (&pp, tmpstr);
	if (nexpr && (v = gnm_expr_top_get_range (nexpr->texpr))) {
		if (VALUE_IS_CELLRANGE (v)) {
			GnmEvalPos ep;
			const GnmCellRef *cr = &v->v_range.cell.a;

			eval_pos_init_sheet (&ep, state->sheet);
			pos->col = gnm_cellref_get_col (cr, &ep);
			pos->row = gnm_cellref_get_row (cr, &ep);
			value_release (v);
			g_free (tmpstr);
			return TRUE;
		}

		value_release (v);
	}
	g_free (tmpstr);
	return FALSE;
}

static gboolean
sc_parse_coord (ScParseState *state, char const **strdata, GnmCellPos *pos)
{
	char const *s, *eq;
	gboolean res;

	g_return_val_if_fail (strdata, FALSE);
	g_return_val_if_fail (*strdata, FALSE);

	s = *strdata;

	eq = strstr (s, " = ");
	if (!eq)
		return FALSE;

	res = sc_parse_coord_real (state, s, pos, eq - s);

	if (res) {
		if ((eq - s + 1 + 3) > (int) strlen (s))
			res = FALSE;
		else
			*strdata = eq + 3;
	}
	return res;
}

static void
set_h_align (Sheet *sheet, GnmCellPos const *pos, GnmHAlign ha)
{
	GnmRange r;
	GnmStyle *style = gnm_style_new ();
	gnm_style_set_align_h (style, ha);
	r.start = r.end = *pos;
	sheet_style_apply_range	(sheet, &r, style);
}

static void
sc_parse_set_handle_option (ScParseState *state, char const *option)
{
	if (g_str_has_prefix (option, "iterations=")) {
		int it = atoi (option + 11);
		if (it > 0) {
			workbook_iteration_enabled (state->sheet->workbook, TRUE);
			workbook_iteration_max_number (state->sheet->workbook, it);
		}
	} else if (g_str_has_prefix (option, "autocalc"))
		workbook_set_recalcmode	(state->sheet->workbook, TRUE);
	else if (g_str_has_prefix (option, "!autocalc"))
		workbook_set_recalcmode (state->sheet->workbook, FALSE);
}

static gboolean
sc_parse_set (ScParseState *state, char const *cmd, char const *str,
	      GnmCellPos const *cpos)
{
	gchar** options = g_strsplit (str, " ", -1), **tmp;

	if (options != NULL)
		for (tmp = options; *tmp != NULL; tmp++)
			sc_parse_set_handle_option (state, *tmp);
	g_strfreev(options);

	/* Most of these settings are not applicable to Gnumeric */
	return TRUE;
}

static gboolean
sc_parse_goto (ScParseState *state, char const *cmd, char const *str,
		GnmCellPos const *cpos)
{
	GnmCellPos pos = { -1, -1 };
	gboolean res;

	res = sc_parse_coord_real (state, str, &pos, strlen (str));
	if (!res)
		return FALSE;

	SHEET_FOREACH_VIEW(state->sheet, sv,
			   sv_selection_set
			   (sv, &pos, pos.col, pos.row, pos.col, pos.row););

	return TRUE;
}

static gboolean
sc_parse_format_definition (ScParseState *state, char const *cmd, char const *str)
{
	sc_warning (state, "Ignoring column format definition: %s", str);
	return TRUE;
}

static void
sc_parse_format_set_width (ScParseState *state, int len, int col_from, int col_to)
{
	GnmFont *style_font;
	int width;
	int col;
	GnmStyle *mstyle;
	gboolean err;

	if (len < 1)
		return;

	err = enlarge (state, col_to, 0);
	if (err) {
		sc_warning (state, _("The sheet is wider than "
				     "Gnumeric can handle."));
		return;
	}

	mstyle = gnm_style_new_default ();
	style_font = gnm_style_get_font
		(mstyle, state->sheet->rendered_values->context);
	width = PANGO_PIXELS (len * style_font->go.metrics->avg_digit_width) + 4;
	gnm_style_unref (mstyle);

	for (col = col_from; col <= col_to; col++)
		sheet_col_set_size_pixels (state->sheet, col, width, TRUE);
}

static void
sc_parse_format_free_precision (ScParseState *state)
{
	if (state->precision != NULL)
		g_array_free (state->precision, TRUE);
}

static int
sc_parse_format_get_precision (ScParseState *state, int col)
{
	if (state->precision != NULL &&
	    col < (int)state->precision->len) {
		return (g_array_index(state->precision, int, col) - 1 );
	} else return -1;
}

static void
sc_parse_format_save_precision (ScParseState *state, int precision,
				int col_from, int col_to)
{
	int col;

	if (state->precision == NULL)
		state->precision = g_array_new (FALSE, TRUE, sizeof (int));

	if (!(col_to < (int)state->precision->len))
		state->precision = g_array_set_size (state->precision, col_to + 1);

	for (col = col_from; col <= col_to; col++)
		g_array_index(state->precision, int, col) = precision + 1;
}

static char *
sc_parse_format_apply_precision (ScParseState *state, char *format, int col)
{
	if (strchr (format, '&')) {
		GString* str = g_string_new (format);
		char *amp = str->str;
		int off = 0;

		g_free (format);
		while (NULL != (amp = strchr (str->str + off, '&'))) {
			off = amp - str->str + 1;
			if (amp == str->str || *(amp - 1) != '\\') {
				int p = sc_parse_format_get_precision (state, col);
				int i;
				if (p == -1) {
					p = 0;
					sc_warning (state, _("Encountered precision dependent format without set precision."));
				}
				off--;
				g_string_erase (str, off, 1);
				for (i = 0; i < p; i++)
					g_string_insert_c (str, off, '0');
			}

		}
		format = g_string_free (str, FALSE);
	}
	return format;
}

static void
sc_parse_format_set_type (ScParseState *state, int type, int col_from, int col_to)
{
	char const *o_format = type >= 0 && (size_t)type < state->formats->len
		? g_ptr_array_index(state->formats, type)
		: NULL;
	int col;

	if (o_format == NULL) {
		sc_warning (state, _("Column format %i is undefined."), type);
		return;
	}
	for (col = col_from; col <= col_to; col++) {
		char *fmt = g_strdup (o_format);
		GOFormat *gfmt;
		GnmStyle *style;
		GnmRange range;
		range_init_cols (&range, state->sheet, col, col);
		fmt = sc_parse_format_apply_precision (state, fmt, col);
		gfmt = go_format_new_from_XL (fmt);
		style = gnm_style_new_default ();
		gnm_style_set_format (style, gfmt);
		sheet_style_apply_range (state->sheet, &range, style);
		/* gnm_style_unref (style); reference has been absorbed */
		go_format_unref (gfmt);
		g_free (fmt);
	}

}

static gboolean
sc_parse_format (ScParseState *state, char const *cmd, char const *str,
		 GnmCellPos const *cpos)
{
	char const *s = str;
	int col_from = -1, col_to = -1, d;
	int len = 0, precision = 0, format_type = 0;

	if (g_ascii_isdigit ((gchar) *str))
		return sc_parse_format_definition (state, cmd, str);

	d = sc_colname_to_coords (s, &col_from);

	if (d == 0)
		goto cannotparse;

	s += d;
	if (*s == ':') {
		s++;
		d = sc_colname_to_coords (s, &col_to);
		if (d == 0)
			goto cannotparse;
		s += d;
	} else
		col_to= col_from;
	while (*s == ' ')
		s++;

	d = sscanf(s, "%i %i %i", &len, &precision, &format_type);

	if (d != 3)
		goto cannotparse;

	if (len > 0)
		sc_parse_format_set_width (state, len, col_from, col_to);
	sc_parse_format_save_precision (state, precision, col_from, col_to);
	sc_parse_format_set_type (state, format_type, col_from, col_to);

	return TRUE;
 cannotparse:
		sc_warning (state, "Unable to parse: %s %s", cmd, str);
	return FALSE;
}

static gboolean
sc_parse_fmt (ScParseState *state, char const *cmd, char const *str,
		GnmCellPos const *cpos)
{
	char const *s = str, *space;
	char *fmt;
	gboolean res;
	GOFormat *gfmt;
	GnmStyle *style;
	GnmCellPos pos = { -1, -1 };

	space = strstr (s, "\"");
	space--;
	if (!space)
		return FALSE;

	res = sc_parse_coord_real (state, s, &pos, space - s);
	if (!res)
		return FALSE;
	s = space + 2;
	space = strstr (s, "\"");
	if (!space)
		return FALSE;
	fmt = g_strndup (s, space - s);
	fmt = sc_parse_format_apply_precision (state, fmt, pos.col);
	gfmt = go_format_new_from_XL (fmt);
	style = gnm_style_new_default ();
	gnm_style_set_format (style, gfmt);

	sheet_style_apply_pos (state->sheet, pos.col, pos.row, style);
	/* gnm_style_unref (style); reference has been absorbed */
	go_format_unref (gfmt);
	g_free (fmt);

	return TRUE;
}

static gboolean
sc_parse_label (ScParseState *state, char const *cmd, char const *str,
		GnmCellPos const *pos)
{
	GnmCell *cell;
	char *s = NULL, *tmpout;
	char const *tmpstr;
	gboolean result = FALSE;

	g_return_val_if_fail (str, FALSE);

	if (*str != '"' || str[1] == 0)
		goto err_out;

	s = tmpout = g_strdup (str);
	if (!s)
		goto err_out;

	tmpstr = str + 1; /* skip leading " */
	while (*tmpstr) {
		if (*tmpstr != '\\') {
			*tmpout = *tmpstr;
			tmpout++;
		}
		tmpstr++;
	}
	if (*(tmpstr - 1) != '"')
		goto err_out;
	tmpout--;
	*tmpout = 0;

	cell = sc_sheet_cell_fetch (state, pos->col, pos->row);
	if (!cell)
		goto err_out;

	gnm_cell_set_value (cell, value_new_string (s));

	if (strcmp (cmd, "leftstring") == 0)
		set_h_align (state->sheet, pos, GNM_HALIGN_LEFT);
	else if (strcmp (cmd, "rightstring") == 0)
		set_h_align (state->sheet, pos, GNM_HALIGN_RIGHT);
#if 0
	else
		cmdtype = LABEL;
#endif

	result = TRUE;
	/* fall through */

 err_out:
	g_free (s);
	return result;
}


#if 0
static GSList *
sc_parse_cell_name_list (ScParseState *state, char const *cell_name_str,
		         int *error_flag)
{
        char     *buf;
	GSList   *cells = NULL;
	GnmCell     *cell;
	GnmCellPos   pos;
	int      i, n;

	g_return_val_if_fail (state->sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (state->sheet), NULL);
	g_return_val_if_fail (cell_name_str != NULL, NULL);
	g_return_val_if_fail (error_flag != NULL, NULL);

	buf = g_malloc (strlen (cell_name_str) + 1);
	for (i = n = 0; cell_name_str[i]; i++) {

	        if ((cell_name_str [i] == ',') ||
		    (!cell_name_str [i])){
		        buf [n] = '\0';

			if (!cellpos_parse (buf, &pos)){
			        *error_flag = 1;
				g_free (buf);
				g_slist_free (cells);
				return NULL;
			}

			cell = sc_sheet_cell_fetch (state, pos.col, pos.row);
			if (cell != NULL)
				cells = g_slist_append (cells, (gpointer) cell);
			n = 0;
		} else
		        buf [n++] = cell_name_str [i];
	}

	*error_flag = 0;
	g_free (buf);
	return cells;
}
#endif


static char const *
sc_row_parse (char const *str, Sheet *sheet, int *res, unsigned char *relative)
{
	char const *end, *ptr = str;
	long int row;

	if (!(*relative = (*ptr != '$')))
		ptr++;

	if (*ptr < '0' || *ptr > '9')
		return NULL;

	/*
	 * Do not allow letters after the row number.  If we did, then
	 * the name "K3P" would lex as the reference K3 followed by the
	 * name "P".
	 */
	row = strtol (ptr, (char **)&end, 10);
	if (ptr != end &&
	    !g_unichar_isalnum (g_utf8_get_char (end)) && *end != '_' &&
	    0 <= row && row < gnm_sheet_get_max_rows (sheet)) {
		*res = row;
		return end;
	} else
		return NULL;
}


static char const *
sc_rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
		   G_GNUC_UNUSED GnmConventions const *convs)
{
	char const *ptr = start, *tmp1, *tmp2;
	GnmSheetSize const *ss;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	ss = gnm_sheet_get_size (pp->sheet);

	res->a.sheet = NULL;
	tmp1 = col_parse (ptr, ss, &res->a.col, &res->a.col_relative);
	if (!tmp1)
		return start;
	tmp2 = sc_row_parse (tmp1, pp->sheet, &res->a.row, &res->a.row_relative);
	if (!tmp2)
		return start;
	if (res->a.col_relative)
		res->a.col -= pp->eval.col;
	if (res->a.row_relative)
		res->a.row -= pp->eval.row;

	/* prepare as if it's a singleton, in case we want to fall back */
	res->b = res->a;
	if (*tmp2 != ':')
		return tmp2;

	start = tmp2;
	tmp1 = col_parse (start+1, ss, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		return start;
	tmp2 = sc_row_parse (tmp1, pp->sheet, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		return start;
	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}

static GnmExprTop const *
sc_parse_expr (ScParseState *state, const char *str, GnmParsePos *pp)
{
	GnmExprTop const *texpr;
	const char *p1;
	gboolean infunc = FALSE;
	GString *exprstr;

	exprstr = g_string_sized_new (500);
	for (p1 = str; *p1; p1++) {
		char c = *p1;
		if (infunc) {
			infunc = g_ascii_isalpha (c);
			if (!infunc && *p1 != '(')
				g_string_append_len (exprstr, "()", 2);
			g_string_append_c (exprstr, c);
		} else if (*p1 == '@')
			infunc = TRUE;
		else
			g_string_append_c (exprstr, c);
	}
	if (infunc)
		g_string_append_len (exprstr, "()", 2);

	texpr = gnm_expr_parse_str (exprstr->str, pp,
				    GNM_EXPR_PARSE_DEFAULT,
				    state->convs, NULL);
	g_string_free (exprstr, TRUE);

	return texpr;
}


static gboolean
sc_parse_let (ScParseState *state, char const *cmd, char const *str,
	      GnmCellPos const *pos)
{
	GnmExprTop const *texpr;
	GnmCell *cell;
	GnmParsePos pp;
	GnmValue const *v;

	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);

	cell = sc_sheet_cell_fetch (state, pos->col, pos->row);
	if (!cell)
		return FALSE;

	texpr = sc_parse_expr (state, str,
			       parse_pos_init_cell (&pp, cell));

	if (!texpr) {
		sc_warning (state, _("Unable to parse cmd='%s', str='%s', col=%d, row=%d."),
			   cmd, str, pos->col, pos->row);
		return TRUE;
	}

	v = gnm_expr_top_get_constant (texpr);
	if (v && VALUE_IS_NUMBER (v)) {
		gnm_cell_set_value (cell, value_dup (v));
	} else {
		gnm_cell_set_expr (cell, texpr);
		cell_queue_recalc (cell);
	}

	if (texpr) gnm_expr_top_unref (texpr);
	return TRUE;
}

static gboolean
sc_parse_define (ScParseState *state, char const *cmd, char const *str,
		 GnmCellPos const *dummy_pos)
{
	GnmParsePos pp;
	GString *name = g_string_new (NULL);
	char *errstr = NULL;
	GnmNamedExpr *nexpr;
	gboolean res = FALSE;
	GnmExprTop const *texpr;

	str = go_strunescape (name, str);
	if (!str)
		goto out;
	while (g_ascii_isspace (*str))
		str++;
	texpr = sc_parse_expr (state, str,
			       parse_pos_init (&pp, NULL, state->sheet, 0, 0));
	if (!texpr) {
		sc_warning (state, "Unable to parse cmd='%s', str='%s'.", cmd, str);
		goto out;
	}

	nexpr = expr_name_add (&pp, name->str, texpr, &errstr, TRUE, NULL);
	if (!nexpr)
		goto out;

	res = TRUE;

out:
	g_string_free (name, TRUE);
	g_free (errstr);
	return res;
}

typedef struct {
	char const *name;
	int namelen;
	gboolean (*handler) (ScParseState *state, char const *name,
			     char const *str, GnmCellPos const *pos);
	gboolean have_coord;
} sc_cmd_t;

static sc_cmd_t const sc_cmd_list[] = {
	{ "leftstring", 10,	sc_parse_label,	 TRUE },
	{ "rightstring", 11,	sc_parse_label,	 TRUE },
	{ "label", 5,		sc_parse_label,	 TRUE },
	{ "let", 3,		sc_parse_let,	 TRUE },
	{ "define", 6,          sc_parse_define, FALSE },
	{ "fmt", 3,             sc_parse_fmt,    FALSE },
	{ "format", 6,          sc_parse_format, FALSE },
	{ "set", 3,             sc_parse_set,    FALSE },
	{ "goto", 4,            sc_parse_goto,   FALSE },
	{ NULL, 0, NULL, 0 },
};


static gboolean
sc_parse_line (ScParseState *state, char *buf)
{
	char const *space;
	int i, cmdlen;
	sc_cmd_t const *cmd;

	g_return_val_if_fail (state, FALSE);
	g_return_val_if_fail (state->sheet, FALSE);
	g_return_val_if_fail (buf, FALSE);

	for (space = buf; g_ascii_isalnum (*space) || *space == '_'; space++)
		; /* Nothing */
	if (*space == 0)
		return TRUE;
	cmdlen = space - buf;
	while (*space == ' ')
		space++;

	for (i = 0 ; sc_cmd_list[i].name != NULL ; ++i) {
		cmd = &sc_cmd_list [i];
		if (cmd->namelen == cmdlen &&
		    strncmp (cmd->name, buf, cmdlen) == 0) {
			GnmCellPos pos = { -1, -1 };
			char const *strdata = space;

			if (cmd->have_coord) {
				if (!sc_parse_coord (state, &strdata, &pos)) {
					sc_warning (state, "Cannot parse %s\n",
						    buf);
					return FALSE;
				}
			}

			cmd->handler (state, cmd->name, strdata, &pos);
			return TRUE;
		}
	}

	sc_warning (state, "Unhandled directive: '%-.*s'",
		   cmdlen, buf);
	return TRUE;
}


static GOErrorInfo *
sc_parse_sheet (ScParseState *state)
{
	unsigned char *data;
	GOErrorInfo *res = NULL;

	while ((data = gsf_input_textline_ascii_gets (state->textline)) != NULL) {
		char *utf8data;

		g_strchomp (data);
		utf8data = g_convert_with_iconv (data, -1, state->converter,
						 NULL, NULL, NULL);

		if (g_ascii_isalpha (*data) && !sc_parse_line (state, utf8data)) {
			if (!res)
				res = go_error_info_new_str
					(_("Error parsing line"));
		}

		g_free (utf8data);
	}

	return res;
}

static GnmExpr const *
sc_func_map_in (GnmConventions const *conv, Workbook *scope,
		char const *name, GnmExprList *args)
{
	static struct {
		char const *sc_name;
		char const *gnm_name;
	} const sc_func_renames[] = {
		{ "AVG",    "AVERAGE" },
		{ "DTR",    "RADIANS" },
		{ "FABS",   "ABS" },
		{ "COLS",   "COLUMNS" },
		{ "AVG",    "AVERAGE" },
		{ "POW",    "POWER" },
		{ "PROD",   "PRODUCT" },
		{ "RND",    "ROUND" },
		{ "RTD",    "DEGREES" },
		{ "STDDEV", "STDEV" },
		{ "STON",   "INT" },
		{ "SUBSTR", "MID" },
		{ NULL, NULL }
	};
	static GHashTable *namemap = NULL;

	GnmFunc  *f;
	char const *new_name;
	int i;

	if (NULL == namemap) {
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].sc_name; i++)
			g_hash_table_insert (namemap,
				(gchar *) sc_func_renames[i].sc_name,
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
sc_conventions (void)
{
	GnmConventions *conv = gnm_conventions_new ();

	conv->decimal_sep_dot		= TRUE;
	conv->range_sep_colon		= TRUE;
	conv->input.range_ref		= sc_rangeref_parse;
	conv->input.func		= sc_func_map_in;

	return conv;
}

static void
sc_format_free (gpointer data,  gpointer user_data)
{
	g_free (data);
}

void
sc_file_open (GOFileOpener const *fo, GOIOContext *io_context,
              WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	char      *name;
	GOErrorInfo *error;
	ScParseState state;

	wb = wb_view_get_workbook (wb_view);
	name = workbook_sheet_get_free_name (wb, "SC", FALSE, TRUE);
	state.sheet = sheet_new (wb, name, 256, 65536);
	g_free (name);
	workbook_sheet_attach (wb, state.sheet);

	/* This should probably come from import dialog.  */
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");

	state.convs = sc_conventions ();
	state.context = io_context;
	state.last_error = NULL;
	state.precision = NULL;
	state.formats = g_ptr_array_sized_new (10);
	g_ptr_array_add (state.formats, g_strdup ("#.&")); /* 0 */
	g_ptr_array_add (state.formats, g_strdup ("0.&E+00")); /* 1 */
	g_ptr_array_add (state.formats, g_strdup ("##0.&E+00")); /* 2 */
	g_ptr_array_add (state.formats, g_strdup ("[$-f8f2]m/d/yy")); /* 3 */
	g_ptr_array_add (state.formats, g_strdup ("[$-f800]dddd, mmmm dd, yyyy")); /* 4 */
	g_ptr_array_set_size (state.formats, 10);

	state.textline = (GsfInputTextline *) gsf_input_textline_new (input);
	error = sc_parse_sheet (&state);
	if (error != NULL) {
		workbook_sheet_delete (state.sheet);
		go_io_error_info_set (io_context, error);
	}
	g_object_unref (state.textline);
	g_iconv_close (state.converter);
	gnm_conventions_unref (state.convs);
	g_free (state.last_error);
	sc_parse_format_free_precision (&state);

	/*In glib 2.22 or later we could use g_ptr_array_set_free_func */
	g_ptr_array_foreach (state.formats, (GFunc) sc_format_free, NULL);
	g_ptr_array_unref (state.formats);
}


static guint8 const signature[] =
"# This data file was generated by the Spreadsheet Calculator.";

gboolean
sc_file_probe (GOFileOpener const *fo, GsfInput *input,
	       GOFileProbeLevel pl)
{
	char const *header = NULL;

	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, sizeof (signature)-1, NULL);
	return header != NULL &&
	    memcmp (header, signature, sizeof (signature)-1) == 0;
}


/*
 * http://www.thule.no/haynie/cpumods/a2620/docs/commrc.sc.txt:
 * format B 20 2
 *
 * http://www.mcs.kent.edu/system/documentation/xspread/demo_func
 * format A 15 2 0
 * goto C7
 *
 */
