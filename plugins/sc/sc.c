/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from sylk.c
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>
#include <goffice/app/io-context.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/go-glib-extras.h>
#include "sheet-style.h"
#include "workbook-view.h"
#include "workbook.h"
#include "parse-util.h"
#include "value.h"
#include <goffice/app/file.h>
#include "cell.h"
#include "style.h"
#include "sheet.h"
#include "expr.h"
#include "func.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-input-textline.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean sc_file_probe (GOFileOpener const *fo, GsfInput *input,
			FileProbeLevel pl);
void sc_file_open (GOFileOpener const *fo, IOContext *io_context,
                   WorkbookView *wb_view, GsfInput *input);

typedef struct {
	GsfInputTextline *textline;
	Sheet            *sheet;
	GIConv            converter;
	GnmConventions	 *convs;
} ScParseState;

typedef enum {
	LABEL,
	LEFTSTRING,
	RIGHTSTRING
} sc_string_cmd_t;


/* we can't use cellpos_parse b/c it doesn't support 0 bases (A0, B0, ...) */
static gboolean
sc_cellname_to_coords (char const *cellname, GnmCellPos *pos)
{
	int mult;

	g_return_val_if_fail (cellname, FALSE);

	if (!cellname || !*cellname || !g_ascii_isalpha (*cellname))
		goto err_out;

	mult = g_ascii_toupper (*cellname) - 'A';
	if (mult < 0 || mult > 25)
		goto err_out;

	cellname++;

	if (g_ascii_isalpha (*cellname)) {
		int ofs = g_ascii_toupper (*cellname) - 'A';
		if (ofs < 0 || ofs > 25)
			goto err_out;
		pos->col = ((mult + 1) * 26) + ofs;
		cellname++;
	}

	else {
		pos->col = mult;
	}

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


static void
sc_parse_coord (char const **strdata, GnmCellPos *pos)
{
	char const *s = *strdata, *eq;
	int len = strlen (s);
	char tmpstr[16];
	size_t tmplen;

	g_return_if_fail (strdata);

	eq = strstr (s, " = ");
	if (!eq)
		return;

	tmplen = eq - s;
	if (tmplen >= sizeof (tmpstr))
		return;

	memcpy (tmpstr, s, tmplen);
	tmpstr [tmplen] = 0;

	if (!sc_cellname_to_coords (tmpstr, pos))
		return;

	g_return_if_fail (pos->col >= 0);
	g_return_if_fail (pos->row >= 0);

	if ((eq - s + 1 + 3) > len)
		return;

	*strdata = eq + 3;
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

static gboolean
sc_parse_label (ScParseState *state, char const *cmd, char const *str,
		GnmCellPos const *pos)
{
	GnmCell *cell;
	char *s = NULL, *tmpout;
	char const *tmpstr;
	gboolean result = FALSE;

	g_return_val_if_fail (str, FALSE);

	if (*str != '"')
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

	cell = sheet_cell_fetch (state->sheet, pos->col, pos->row);
	if (!cell)
		goto err_out;

	gnm_cell_set_text (cell, s);

	if (strcmp (cmd, "leftstring") == 0)
		set_h_align (state->sheet, pos, HALIGN_LEFT);
	else if (strcmp (cmd, "rightstring") == 0)
		set_h_align (state->sheet, pos, HALIGN_RIGHT);
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
sc_parse_cell_name_list (Sheet *sheet, char const *cell_name_str,
		         int *error_flag)
{
        char     *buf;
	GSList   *cells = NULL;
	GnmCell     *cell;
	GnmCellPos   pos;
	int      i, n;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
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

			cell = sheet_cell_fetch (sheet, pos.col, pos.row);
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
sc_row_parse (char const *str, int *res, unsigned char *relative)
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
	    0 <= row && row  < gnm_sheet_get_max_rows (NULL)) {
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

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	res->a.sheet = NULL;
	tmp1 = col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (!tmp1)
		return start;
	tmp2 = sc_row_parse (tmp1, &res->a.row, &res->a.row_relative);
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
	tmp1 = col_parse (start+1, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		return start;
	tmp2 = sc_row_parse (tmp1, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		return start;
	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}


static gboolean
sc_parse_let (ScParseState *state, char const *cmd, char const *str,
	      GnmCellPos const *pos)
{
	GnmExprTop const *texpr;
	GnmCell *cell;
	GnmParsePos pp;
	GnmValue const *v;
	const char *p1;
	gboolean infunc;
	GString *exprstr;

	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);

	cell = sheet_cell_fetch (state->sheet, pos->col, pos->row);
	if (!cell)
		return FALSE;

	exprstr = g_string_sized_new (500);
	infunc = FALSE;
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

	texpr = gnm_expr_parse_str (exprstr->str,
			parse_pos_init_cell (&pp, cell),
			GNM_EXPR_PARSE_DEFAULT,
			state->convs, NULL);
	g_string_free (exprstr, TRUE);
	if (!texpr) {
		g_warning ("cannot parse cmd='%s', str='%s', col=%d, row=%d.",
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


typedef struct {
	char const *name;
	int namelen;
	gboolean (*handler) (ScParseState *state, char const *name,
			     char const *str, GnmCellPos const *pos);
	gboolean have_coord;
} sc_cmd_t;

static sc_cmd_t const sc_cmd_list[] = {
	{ "leftstring", 10,	sc_parse_label,	TRUE },
	{ "rightstring", 11,	sc_parse_label,	TRUE },
	{ "label", 5,		sc_parse_label,	TRUE },
	{ "let", 3,		sc_parse_let,	TRUE },
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

			if (cmd->have_coord)
				sc_parse_coord (&strdata, &pos);

			cmd->handler (state, cmd->name, strdata, &pos);
			return TRUE;
		}
	}

#if 1
	g_warning ("sc importer: unhandled directive: '%-.*s'",
		   cmdlen, buf);
#endif

	return TRUE;
}


static ErrorInfo *
sc_parse_sheet (ScParseState *state)
{
	unsigned char *data;
	while ((data = gsf_input_textline_ascii_gets (state->textline)) != NULL) {
		char *utf8data;

		g_strchomp (data);
		utf8data = g_convert_with_iconv (data, -1, state->converter,
						 NULL, NULL, NULL);

		if (g_ascii_isalpha (*data) && !sc_parse_line (state, utf8data)) {
			g_free (utf8data);
			return error_info_new_str (_("Error parsing line"));
		}

		g_free (utf8data);
	}

	return NULL;
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
		f = gnm_func_add_placeholder (scope, name, "", TRUE);
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


void
sc_file_open (GOFileOpener const *fo, IOContext *io_context,
              WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	char      *name;
	ErrorInfo *error;
	ScParseState state;

	wb    = wb_view_get_workbook (wb_view);
	name  = workbook_sheet_get_free_name (wb, "SC", FALSE, TRUE);
	state.sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, state.sheet);

	/* This should probably come from import dialog.  */
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");

	state.convs = sc_conventions ();
	state.textline = (GsfInputTextline *) gsf_input_textline_new (input);
	error = sc_parse_sheet (&state);
	if (error != NULL) {
		workbook_sheet_delete (state.sheet);
		gnumeric_io_error_info_set (io_context, error);
	}
	g_object_unref (G_OBJECT (state.textline));
	g_iconv_close (state.converter);
	gnm_conventions_free (state.convs);
}


static guint8 const signature[] =
"# This data file was generated by the Spreadsheet Calculator.";

gboolean
sc_file_probe (GOFileOpener const *fo, GsfInput *input,
	       FileProbeLevel pl)
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
