/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from sylk.c
 */

#include <config.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <gnome.h>
#include "plugin.h"
#include "gnumeric.h"
#include "file.h"
#include "utils.h"

#define arraysize(x)     (sizeof(x)/sizeof(*(x)))


typedef struct {
	/* input data */
	FILE *f;

	/* gnumeric sheet */
	Sheet *sheet;
} sc_file_state_t;


typedef enum {
	LABEL,
	LEFTSTRING,
	RIGHTSTRING,
} sc_string_cmd_t;


/* we can't use parse_cell_name b/c it doesn't support 0 bases (A0, B0, ...) */
static gboolean
sc_cellname_to_coords (const char *cellname, int *col, int *row)
{
	int mult;

	g_return_val_if_fail (cellname, FALSE);
	g_return_val_if_fail (col, FALSE);
	g_return_val_if_fail (row, FALSE);

	if (!cellname || !*cellname || !isalpha(*cellname))
		goto err_out;

	mult = toupper(*cellname) - 'A';
	if (mult < 0 || mult > 25)
		goto err_out;

	cellname++;

	if (isalpha(*cellname)) {
		int ofs = toupper(*cellname) - 'A';
		if (ofs < 0 || ofs > 25)
			goto err_out;
		*col = (mult * 26) + ofs;
		cellname++;
	}

	else {
		*col = mult;
	}

	/* XXX need to replace this block with strtol+error checking */
	if (1) {
		if (!isdigit(*cellname))
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
	size_t len = strlen (s);
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
sc_parse_label (sc_file_state_t *src, const char *cmd, const char *str, int col, int row)
{
	Cell *cell;
	char *s = NULL, *tmpout;
	const char *tmpstr;
	gboolean result = FALSE;
	sc_string_cmd_t cmdtype;

	g_return_val_if_fail (src, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	if (!src || !str || *str != '"' || col == -1 || row == -1)
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

	cell = sheet_cell_fetch (src->sheet, col, row);
	if (!cell)
		goto err_out;

	cell_set_text_simple (cell, s);

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

		mstyle_unref (mstyle);
	}

	result = TRUE;
	/* fall through */

err_out:
	if (s) g_free (s);
	return result;
}


#if SC_EXPR_PARSE_WORKS
static GSList *
sc_parse_cell_name_list (Sheet *sheet, const char *cell_name_str,
		         int *error_flag)
{
        char     *buf;
	GSList   *cells = NULL;
	Cell     *cell;
	int      i, n, col, row;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (cell_name_str != NULL, NULL);
	g_return_val_if_fail (error_flag != NULL, NULL);

	buf = g_malloc (strlen (cell_name_str) + 1);
	for (i = n = 0; cell_name_str[i]; i++) {

	        if ((cell_name_str [i] == ',') ||
		    (!cell_name_str [i])){
		        buf [n] = '\0';

			if (!parse_cell_name (buf, &col, &row)){
			        *error_flag = 1;
				free (buf);
				g_slist_free (cells);
				return NULL;
			}

			cell = sheet_cell_fetch (sheet, col, row);
			cells = g_slist_append (cells, (gpointer) cell);
			n = 0;
		} else
		        buf [n++] = cell_name_str [i];
	}

	*error_flag = 0;
	free (buf);
	return cells;
}


static gboolean
sc_parse_let_expr (sc_file_state_t *src, const char *cmd, const char *str, int col, int row)
{
	char *error = NULL;
	ExprTree *tree;
	Cell *cell;

	g_return_val_if_fail (src, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	tree = expr_parse_string (str, NULL, NULL, &error);
	if (!tree) {
		g_warning ("cannot parse cmd='%s',str='%s',col=%d,row=%d\n",
			   cmd, str, col, row);
		goto out;
	}

	/* FIXME FIXME FIXME sc/xspread rows start at A0 not A1.  we must
	 * go through and fixup each row number in each cell reference */

	cell = sheet_cell_fetch (src->sheet, col, row);
	if (!cell)
		return FALSE;

	cell_set_formula_tree_simple (cell, tree);

	/* fall through */

out:
	if (tree) expr_tree_unref (tree); /* XXX correct? */
	return TRUE;
}
#endif


static gboolean
sc_parse_let (sc_file_state_t *src, const char *cmd, const char *str, int col, int row)
{
	Cell *cell;
	Value *v;

	g_return_val_if_fail (src, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	g_return_val_if_fail (str, FALSE);
	g_return_val_if_fail (col >= 0, FALSE);
	g_return_val_if_fail (row >= 0, FALSE);

	if (!*str)
		return FALSE;

	/* it's an expression not a simple value, handle elsewhere */
	if (*str == '@')
#if SC_EXPR_PARSE_WORKS
		return sc_parse_let_expr (src, cmd, str, col, row);
#else
		return TRUE;
#endif

	cell = sheet_cell_fetch (src->sheet, col, row);
	if (!cell)
		return FALSE;

	v = value_new_float (atof(str));
	if (!v)
		return FALSE;

	cell_set_value_simple (cell, v);

	return TRUE;
}


typedef struct {
	const char *name;
	size_t namelen;
	gboolean (*handler) (sc_file_state_t *src, const char *name,
			     const char *str, int col, int row);
	unsigned have_coord : 1;
} sc_cmd_t;


static const sc_cmd_t sc_cmd_list[] = {
	{ "leftstring", 10, sc_parse_label, 1 },
	{ "rightstring", 11, sc_parse_label, 1 },
	{ "label", 5, sc_parse_label, 1 },
	{ "let", 3, sc_parse_let, 1 },
};


static gboolean
sc_parse_line (sc_file_state_t *src, char *buf)
{
	const char *space;
	size_t i, cmdlen;
	const sc_cmd_t *cmd;

	g_return_val_if_fail (src, FALSE);
	g_return_val_if_fail (buf, FALSE);

	space = strchr (buf, ' ');
	if (!space)
		return TRUE;
	cmdlen = space - buf;

	for (i = arraysize (sc_cmd_list) - 1; i >= 0; i--) {
		cmd = &sc_cmd_list [i];
		if (cmd->namelen == cmdlen &&
		    strncmp (cmd->name, buf, cmdlen) == 0) {
			int col = -1, row = -1;
			const char *strdata;

			strdata = space + 1;

			if (cmd->have_coord)
				sc_parse_coord (&strdata, &col, &row);

			cmd->handler (src, cmd->name, strdata, col, row);
			return TRUE;
		}
	}

#if 0
	fprintf (stderr, "unhandled directive: '%s'\n", buf);
#endif

	return TRUE;
}


static char *
sc_parse_sheet (sc_file_state_t *src)
{
	char buf [BUFSIZ];

	g_return_val_if_fail (src, FALSE);
	g_return_val_if_fail (src->f, FALSE);

	while (fgets (buf, sizeof (buf), src->f) != NULL) {
		g_strchomp (buf);
		if (isalpha (buf [0]) && !sc_parse_line (src, buf))
			return g_strdup ("Error parsing line");
	}

	if (ferror (src->f))
		return "";

	return NULL;
}


static char *
sc_read_workbook (Workbook *book, const char *filename)
{
	/*
	 * TODO : When there is a reasonable error reporting
	 * mechanism use it and put all the error code back
	 */
	sc_file_state_t src;
	char *name;
	char *result;
	FILE *f;

	g_return_val_if_fail (book, "");
	g_return_val_if_fail (filename, "");
	g_return_val_if_fail (*filename, "");

	f = fopen (filename, "r");
	if (!f)
		return g_strdup (g_strerror(errno));

	name = g_strdup_printf (_("Imported %s"), g_basename (filename));

	memset (&src, 0, sizeof (src));
	src.f	  = f;
	src.sheet = sheet_new (book, name);

	workbook_attach_sheet (book, src.sheet);
	g_free (name);

	result = sc_parse_sheet (&src);

	fclose(f);

	return result;
}


static int
sc_can_unload (PluginData *pd)
{
	g_return_val_if_fail (pd, TRUE);

	/* We can always unload */
	return TRUE;
}


static void
sc_cleanup_plugin (PluginData *pd)
{
	g_return_if_fail (pd);

	file_format_unregister_open (NULL, sc_read_workbook);
	g_free (pd->title);
}


PluginInitResult
init_plugin (CommandContext *context, PluginData * pd)
{
	const char *desc = _("SC/xspread file import");

	g_return_val_if_fail (pd, -1);

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	file_format_register_open (1, desc, NULL, sc_read_workbook);

	pd->can_unload     = sc_can_unload;
	pd->cleanup_plugin = sc_cleanup_plugin;
	pd->title = g_strdup (desc);

	return PLUGIN_OK;
}

