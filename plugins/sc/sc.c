/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from sylk.c
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "io-context.h"
#include "error-info.h"
#include "workbook-view.h"
#include "workbook.h"
#include "parse-util.h"
#include "value.h"
#include "file.h"
#include "cell.h"
#include "style.h"
#include "sheet.h"
#include "expr.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-input-textline.h>
#include <string.h>
#include <math.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean sc_file_probe (GnumFileOpener const *fo, GsfInput *input,
			FileProbeLevel pl);
void sc_file_open (GnumFileOpener const *fo, IOContext *io_context,
                   WorkbookView *wb_view, GsfInput *input);

typedef enum {
	LABEL,
	LEFTSTRING,
	RIGHTSTRING
} sc_string_cmd_t;


/* we can't use cellpos_parse b/c it doesn't support 0 bases (A0, B0, ...) */
static gboolean
sc_cellname_to_coords (const char *cellname, int *col, int *row)
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
sc_parse_coord (const char **strdata, int *col, int *row)
{
	const char *s = *strdata, *eq;
	int len = strlen (s);
	char tmpstr [16];

	g_return_if_fail (strdata);
	g_return_if_fail (col);
	g_return_if_fail (row);

	eq = strstr (s, " = ");
	if (!eq)
		return;

	memcpy (tmpstr, s, eq - s);
	tmpstr [eq - s] = 0;

	if (!sc_cellname_to_coords (tmpstr, col, row))
		return;

	g_assert (*col >= 0);
	g_assert (*row >= 0);

	if ((eq - s + 1 + 3) > len)
		return;

	*strdata = eq + 3;
}


static gboolean
sc_parse_label (Sheet *sheet, const char *cmd, const char *str, int col, int row)
{
	Cell *cell;
	char *s = NULL, *tmpout;
	const char *tmpstr;
	gboolean result = FALSE;
	sc_string_cmd_t cmdtype;

	g_return_val_if_fail (sheet, FALSE);
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

	cell = sheet_cell_fetch (sheet, col, row);
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
		MStyle *mstyle;

		mstyle = cell_get_mstyle (cell);
		if (!mstyle)
			goto err_out;

		if (cmdtype == LEFTSTRING)
			mstyle_set_align_h (mstyle, HALIGN_LEFT);
		else
			mstyle_set_align_h (mstyle, HALIGN_RIGHT);
	}

	result = TRUE;
	/* fall through */

err_out:
	if (s) g_free (s);
	return result;
}


#if 0
static GSList *
sc_parse_cell_name_list (Sheet *sheet, const char *cell_name_str,
		         int *error_flag)
{
        char     *buf;
	GSList   *cells = NULL;
	Cell     *cell;
	CellPos   pos;
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
sc_rangeref_parse (RangeRef *res, char const *start, ParsePos const *pp)
{
	/* This is a hack.  We still cannot handle sc's row 0.  */
	const char *end = rangeref_parse (res, start, pp);
	if (end != start) {
		res->a.row++;
		res->b.row++;
	}
	return end;
}

static gboolean
sc_parse_let (Sheet *sheet, const char *cmd, const char *str, int col, int row)
{
	const GnmExpr *tree;
	Cell *cell;
	ParsePos pos;
	const Value *v;

	g_return_val_if_fail (sheet, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	cell = sheet_cell_fetch (sheet, col, row);
	if (!cell)
		return FALSE;

	/* FIXME FIXME FIXME sc/xspread rows start at A0 not A1.  we must
	 * go through and fixup each row number in each cell reference */
	tree = gnm_expr_parse_str (str,
				   parse_pos_init_cell (&pos, cell),
				   GNM_EXPR_PARSE_DEFAULT,
				   &sc_rangeref_parse,
				   NULL);

	if (!tree) {
		g_warning ("cannot parse cmd='%s', str='%s', col=%d, row=%d.",
			   cmd, str, col, row);
		goto out;
	}

	v = gnm_expr_get_constant (tree);
	if (v && VALUE_IS_NUMBER (v)) {
		cell_set_value (cell, value_duplicate (v));
	} else {
		cell_set_expr (cell, tree);
	}

out:
	if (tree) gnm_expr_unref (tree); 
	return TRUE;
}


typedef struct {
	const char *name;
	int namelen;
	gboolean (*handler) (Sheet *sheet, const char *name,
			     const char *str, int col, int row);
	unsigned have_coord : 1;
} sc_cmd_t;


static const sc_cmd_t sc_cmd_list[] = {
	{ "leftstring", 10, sc_parse_label, 1 },
	{ "rightstring", 11, sc_parse_label, 1 },
	{ "label", 5, sc_parse_label, 1 },
	{ "let", 3, sc_parse_let, 1 },
	{ NULL, 0, NULL, 0 },
};


static gboolean
sc_parse_line (Sheet *sheet, char *buf)
{
	const char *space;
	int i, cmdlen;
	const sc_cmd_t *cmd;

	g_return_val_if_fail (sheet, FALSE);
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
			const char *strdata = space;

			if (cmd->have_coord)
				sc_parse_coord (&strdata, &col, &row);

			cmd->handler (sheet, cmd->name, strdata, col, row);
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
sc_parse_sheet (GsfInputTextline *input, Sheet *sheet, GIConv ic)
{
	unsigned char *data;
	while ((data = gsf_input_textline_ascii_gets (input)) != NULL) {
		char *utf8data;

		g_strchomp (data);
		utf8data = g_convert_with_iconv (data, -1, ic, NULL, NULL, NULL);

		if (g_ascii_isalpha (*data) && !sc_parse_line (sheet, utf8data)) {
			g_free (utf8data);
			return error_info_new_str (_("Error parsing line"));
		}

		g_free (utf8data);
	}

	return NULL;
}

void
sc_file_open (GnumFileOpener const *fo, IOContext *io_context,
              WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	char      *name;
	ErrorInfo *error;
	Sheet	  *sheet;
	GsfInputTextline *textline;
	GIConv    ic;

	wb    = wb_view_workbook (wb_view);
	name  = workbook_sheet_get_free_name (wb, "SC", FALSE, TRUE);
	sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, sheet, NULL);

	/* This should probably come from import dialog.  */
	ic = g_iconv_open ("UTF-8", "ISO-8859-1");

	textline = gsf_input_textline_new (input);
	error = sc_parse_sheet (textline, sheet, ic);
	if (error != NULL) {
		workbook_sheet_detach (wb, sheet);
		gnumeric_io_error_info_set (io_context, error);
	}
	g_object_unref (G_OBJECT (textline));
	g_iconv_close (ic);
}

static guint8 const signature[] =
"# This data file was generated by the Spreadsheet Calculator.";

gboolean
sc_file_probe (GnumFileOpener const *fo, GsfInput *input,
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
