/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from sylk.c
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>
#include <goffice/app/io-context.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/go-glib-extras.h>
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
	GsfInputTextline   *textline;
	Sheet              *sheet;
	GIConv             converter;
	GnmExprConventions *exprconv;
} ScParseState;


typedef enum {
	LABEL,
	LEFTSTRING,
	RIGHTSTRING
} sc_string_cmd_t;


/* we can't use cellpos_parse b/c it doesn't support 0 bases (A0, B0, ...) */
static gboolean
sc_cellname_to_coords (char const *cellname, int *col, int *row)
{
	int mult;

	g_return_val_if_fail (cellname, FALSE);
	g_return_val_if_fail (col, FALSE);
	g_return_val_if_fail (row, FALSE);

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
		*col = ((mult + 1) * 26) + ofs;
		cellname++;
	}

	else {
		*col = mult;
	}

	/* XXX need to replace this block with strtol+error checking */
	if (1) {
		if (!g_ascii_isdigit (*cellname))
			goto err_out;

		*row = atoi (cellname);
	}

	g_return_val_if_fail (*col > -1, FALSE);
	g_return_val_if_fail (*row > -1, FALSE);
	return TRUE;

err_out:
	*col = *row = -1;
	return FALSE;
}


static void
sc_parse_coord (char const **strdata, int *col, int *row)
{
	char const *s = *strdata, *eq;
	int len = strlen (s);
	char tmpstr[16];
	size_t tmplen;

	g_return_if_fail (strdata);
	g_return_if_fail (col);
	g_return_if_fail (row);

	eq = strstr (s, " = ");
	if (!eq)
		return;

	tmplen = eq - s;
	if (tmplen >= sizeof (tmpstr))
		return;

	memcpy (tmpstr, s, tmplen);
	tmpstr [tmplen] = 0;

	if (!sc_cellname_to_coords (tmpstr, col, row))
		return;

	g_assert (*col >= 0);
	g_assert (*row >= 0);

	if ((eq - s + 1 + 3) > len)
		return;

	*strdata = eq + 3;
}


static gboolean
sc_parse_label (ScParseState *state, char const *cmd, char const *str, int col, int row)
{
	GnmCell *cell;
	char *s = NULL, *tmpout;
	char const *tmpstr;
	gboolean result = FALSE;
	sc_string_cmd_t cmdtype;

	g_return_val_if_fail (state, FALSE);
	g_return_val_if_fail (state->sheet, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	if (!str || *str != '"' || col == -1 || row == -1)
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

	cell = sheet_cell_fetch (state->sheet, col, row);
	if (!cell)
		goto err_out;

	cell_set_text (cell, s);

	if (strcmp (cmd, "leftstring") == 0)
		cmdtype = LEFTSTRING;
	else if (strcmp (cmd, "rightstring") == 0)
		cmdtype = RIGHTSTRING;
	else
		cmdtype = LABEL;

	if (cmdtype == LEFTSTRING || cmdtype == RIGHTSTRING) {
		GnmStyle *mstyle;

		mstyle = cell_get_mstyle (cell);
		if (!mstyle)
			goto err_out;

		if (cmdtype == LEFTSTRING)
			gnm_style_set_align_h (mstyle, HALIGN_LEFT);
		else
			gnm_style_set_align_h (mstyle, HALIGN_RIGHT);
	}

	result = TRUE;
	/* fall through */

err_out:
	if (s) g_free (s);
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
	    0 <= row && row  < SHEET_MAX_ROWS) {
		*res = row;
		return end;
	} else
		return NULL;
}


static char const *
sc_rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
			G_GNUC_UNUSED GnmExprConventions const *convs)
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
sc_parse_let (ScParseState *state, char const *cmd, char const *str, int col, int row)
{
	GnmExpr const *tree;
	GnmCell *cell;
	GnmParsePos pos;
	GnmValue const *v;
	char *str2, *p1, *p2;

	g_return_val_if_fail (state, FALSE);
	g_return_val_if_fail (state->sheet, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	cell = sheet_cell_fetch (state->sheet, col, row);
	if (!cell)
		return FALSE;

	str2 = g_malloc0 (strlen (str) + 1);
	for (p1 = (char *) str, p2 = str2; *p1; p1++)
		if (*p1 != '@')
			*p2++ = *p1;
	tree = gnm_expr_parse_str (str2,
				   parse_pos_init_cell (&pos, cell),
				   GNM_EXPR_PARSE_DEFAULT,
				   state->exprconv,
				   NULL);
	g_free (str2);
	if (!tree) {
		g_warning ("cannot parse cmd='%s', str='%s', col=%d, row=%d.",
			   cmd, str, col, row);
		goto out;
	}

	v = gnm_expr_get_constant (tree);
	if (v && VALUE_IS_NUMBER (v)) {
		cell_set_value (cell, value_dup (v));
	} else {
		cell_set_expr (cell, tree);
		cell_queue_recalc (cell);
	}

out:
	if (tree) gnm_expr_unref (tree); 
	return TRUE;
}


typedef struct {
	char const *name;
	int namelen;
	gboolean (*handler) (ScParseState *state, char const *name,
			     char const *str, int col, int row);
	unsigned have_coord : 1;
} sc_cmd_t;


static sc_cmd_t const sc_cmd_list[] = {
	{ "leftstring", 10, sc_parse_label, 1 },
	{ "rightstring", 11, sc_parse_label, 1 },
	{ "label", 5, sc_parse_label, 1 },
	{ "let", 3, sc_parse_let, 1 },
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
			int col = -1, row = -1;
			char const *strdata = space;

			if (cmd->have_coord)
				sc_parse_coord (&strdata, &col, &row);

			cmd->handler (state, cmd->name, 
				      strdata, col, row);
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

static struct {
	char const *scname;
	char const *gnumericname;
} const simple_renames[] = {
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


static GnmExpr const *
function_renamer (char const *name,
		  GnmExprList *args,
		  GnmExprConventions *convs)
{
	Workbook *wb = NULL;
	int i;
	GnmFunc *f;

	for (i = 0; simple_renames[i].scname; i++)
		if (strcasecmp (name, simple_renames[i].scname) == 0) {
			name = simple_renames[i].gnumericname;
			break;
		}

	f = gnm_func_lookup (name, wb);
	if (f)
		return gnm_expr_new_funcall (f, args);

	return gnm_func_placeholder_factory (name, args, convs);
}


static GnmExprConventions *
sc_conventions (void)
{
	GnmExprConventions *convs = gnm_expr_conventions_new ();
	int i;

	convs->decimal_sep_dot = TRUE;
	convs->ref_parser = sc_rangeref_parse;
	convs->range_sep_colon = TRUE;
	convs->sheet_sep_exclamation	= TRUE;
	convs->dots_in_names		= TRUE;
	convs->unknown_function_handler = gnm_func_placeholder_factory;
	convs->function_rewriter_hash =
		g_hash_table_new (go_ascii_strcase_hash, 
				  go_ascii_strcase_equal);
	for (i = 0; simple_renames[i].scname; i++)
		g_hash_table_insert (convs->function_rewriter_hash,
				     (gchar *) simple_renames[i].scname,
				     function_renamer);

	return convs;
}


void
sc_file_open (GOFileOpener const *fo, IOContext *io_context,
              WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	char      *name;
	ErrorInfo *error;
	ScParseState state;

	wb    = wb_view_workbook (wb_view);
	name  = workbook_sheet_get_free_name (wb, "SC", FALSE, TRUE);
	state.sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, state.sheet);

	/* This should probably come from import dialog.  */
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");
	
	state.exprconv = sc_conventions ();
	state.textline = (GsfInputTextline *) gsf_input_textline_new (input);
	error = sc_parse_sheet (&state);
	if (error != NULL) {
		workbook_sheet_delete (state.sheet);
		gnumeric_io_error_info_set (io_context, error);
	}
	g_object_unref (G_OBJECT (state.textline));
	g_iconv_close (state.converter);
	gnm_expr_conventions_free (state.exprconv);
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


