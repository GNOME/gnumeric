/*
 * sstest.c: Test code for Gnumeric
 *
 * Copyright (C) 2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include <goffice/goffice.h>
#include "command-context-stderr.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "parse-util.h"
#include "expr-name.h"
#include "expr.h"
#include "search.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "func.h"
#include "parse-util.h"
#include "sheet-object-cell-comment.h"
#include "mathfunc.h"
#include "gnm-random.h"
#include "sf-dpq.h"
#include "sf-gamma.h"
#include "rangefunc.h"

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>
#include <errno.h>

static gboolean sstest_show_version = FALSE;

static GOptionEntry const sstest_options [] = {
	{
		"version", 'V',
		0, G_OPTION_ARG_NONE, &sstest_show_version,
		N_("Display program version"),
		NULL
	},

	{ NULL }
};

static void
mark_test_start (const char *name)
{
	g_printerr ("-----------------------------------------------------------------------------\nStart: %s\n-----------------------------------------------------------------------------\n\n", name);
}

static void
mark_test_end (const char *name)
{
	g_printerr ("End: %s\n\n", name);
}

static void
cb_collect_names (G_GNUC_UNUSED const char *name, GnmNamedExpr *nexpr, GSList **names)
{
	*names = g_slist_prepend (*names, nexpr);
}

static GnmCell *
fetch_cell (Sheet *sheet, const char *where)
{
	GnmCellPos cp;
	gboolean ok = cellpos_parse (where,
				     gnm_sheet_get_size (sheet),
				     &cp, TRUE) != NULL;
	g_return_val_if_fail (ok, NULL);
	return sheet_cell_fetch (sheet, cp.col, cp.row);
}

static void
set_cell (Sheet *sheet, const char *where, const char *what)
{
	GnmCell *cell = fetch_cell (sheet, where);
	if (cell)
		gnm_cell_set_text (cell, what);
}

static void
dump_sheet (Sheet *sheet, const char *header)
{
	GPtrArray *cells = sheet_cells (sheet, NULL);
	unsigned ui;

	if (header)
		g_printerr ("# %s\n", header);
	for (ui = 0; ui < cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (cells, ui);
		char *txt = gnm_cell_get_entered_text (cell);
		g_printerr ("%s: %s\n",
			    cellpos_as_string (&cell->pos), txt);
		g_free (txt);
	}
	g_ptr_array_free (cells, TRUE);
}


static void
dump_names (Workbook *wb)
{
	GSList *l, *names = NULL;

	workbook_foreach_name (wb, FALSE, (GHFunc)cb_collect_names, &names);
	names = g_slist_sort (names, (GCompareFunc)expr_name_cmp_by_name);

	g_printerr ("Dumping names...\n");
	for (l = names; l; l = l->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmConventionsOut out;

		out.accum = g_string_new (NULL);
		out.pp = &nexpr->pos;
		out.convs = gnm_conventions_default;

		g_string_append (out.accum, "Scope=");
		if (out.pp->sheet)
			g_string_append (out.accum, out.pp->sheet->name_quoted);
		else
			g_string_append (out.accum, "Global");

		g_string_append (out.accum, " Name=");
		go_strescape (out.accum, expr_name_name (nexpr));

		g_string_append (out.accum, " Expr=");
		gnm_expr_top_as_gstring (nexpr->texpr, &out);

		g_printerr ("%s\n", out.accum->str);
		g_string_free (out.accum, TRUE);
	}
	g_printerr ("Dumping names... Done\n");

	g_slist_free (names);
}

static void
define_name (const char *name, const char *expr_txt, gpointer scope)
{
	GnmParsePos pos;
	GnmExprTop const *texpr;
	GnmNamedExpr const *nexpr;
	GnmConventions const *convs;

	if (IS_SHEET (scope)) {
		parse_pos_init_sheet (&pos, scope);
		convs = sheet_get_conventions (pos.sheet);
	} else {
		parse_pos_init (&pos, WORKBOOK (scope), NULL, 0, 0);
		convs = gnm_conventions_default;
	}

	texpr = gnm_expr_parse_str (expr_txt, &pos,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs, NULL);
	if (!texpr) {
		g_printerr ("Failed to parse %s for name %s\n",
			    expr_txt, name);
		return;
	}

	nexpr = expr_name_add (&pos, name, texpr, NULL, TRUE, NULL);
	if (!nexpr)
		g_printerr ("Failed to add name %s\n", name);
}

static void
test_insdel_rowcol_names (void)
{
	Workbook *wb;
	Sheet *sheet1,*sheet2;
	const char *test_name = "test_insdel_rowcol_names";
	GOUndo *undo;
	int i;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	sheet2 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

	define_name ("Print_Area", "Sheet1!$A$1:$IV$65536", sheet1);
	define_name ("Print_Area", "Sheet2!$A$1:$IV$65536", sheet2);

	define_name ("NAMEGA1", "A1", wb);
	define_name ("NAMEG2", "$A$14+Sheet1!$A$14+Sheet2!$A$14", wb);

	define_name ("NAMEA1", "A1", sheet1);
	define_name ("NAMEA2", "A2", sheet1);
	define_name ("NAMEA1ABS", "$A$1", sheet1);
	define_name ("NAMEA2ABS", "$A$2", sheet1);

	dump_names (wb);

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %s on %s\n",
			    col_name (i), sheet1->name_unquoted);
		sheet_insert_cols (sheet1, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %s on %s\n",
			    col_name (i), sheet2->name_unquoted);
		sheet_insert_cols (sheet2, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to delete column %s on %s\n",
			    col_name (i), sheet1->name_unquoted);
		sheet_delete_cols (sheet1, i, 1, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	g_object_unref (wb);

	mark_test_end (test_name);
}

/*-------------------------------------------------------------------------- */

static void
test_insert_delete (void)
{
	const char *test_name = "test_insert_delete";
	Workbook *wb;
	Sheet *sheet1;
	int i;
	GOUndo *u = NULL, *u1;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	set_cell (sheet1, "B2", "=D4+1");
	set_cell (sheet1, "D2", "=if(TRUE,B2,2)");

	dump_sheet (sheet1, "Init");

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to insert column before %s\n",
			    col_name (i));
		sheet_insert_cols (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to insert row before %s\n",
			    row_name (i));
		sheet_insert_rows (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	go_undo_undo (u);
	g_object_unref (u);
	u = NULL;
	dump_sheet (sheet1, "Undo the lot");

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to delete column %s\n",
			    col_name (i));
		sheet_delete_cols (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to delete row %s\n",
			    row_name (i));
		sheet_delete_rows (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	go_undo_undo (u);
	g_object_unref (u);
	u = NULL;
	dump_sheet (sheet1, "Undo the lot");

	g_object_unref (wb);

	mark_test_end (test_name);
}

/*-------------------------------------------------------------------------- */

static void
test_func_help (void)
{
	const char *test_name = "test_func_help";
	int res;

	mark_test_start (test_name);

	res = gnm_func_sanity_check ();
	g_printerr ("Result = %d\n", res);

	mark_test_end (test_name);
}

/*-------------------------------------------------------------------------- */

static int
test_strtol_ok (const char *s, long l, size_t expected_len)
{
	long l2;
	char *end;
	int save_errno;

	l2 = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != l2) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_noconv (const char *s)
{
	long l;
	char *end;
	int save_errno;

	l = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != 0) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_overflow (const char *s, gboolean pos)
{
	long l;
	char *end;
	int save_errno;
	size_t expected_len = strlen (s);

	l = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != (pos ? LONG_MAX : LONG_MIN)) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != ERANGE) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_reverse (long l)
{
	char buffer[4*sizeof(l) + 4];
	int res = 0;

	sprintf(buffer, "%ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, " %ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, "\xc2\xa0\n\t%ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, " \t%ldx", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer) - 1);

	return res;
}

static int
test_strtod_ok (const char *s, double d, size_t expected_len)
{
	gnm_float d2;
	char *end;
	int save_errno;

	d2 = gnm_utf8_strto (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (d != d2) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static void
test_nonascii_numbers (void)
{
	const char *test_name = "test_nonascii_numbers";
	int res = 0;

	mark_test_start (test_name);

	res |= test_strtol_reverse (0);
	res |= test_strtol_reverse (1);
	res |= test_strtol_reverse (-1);
	res |= test_strtol_reverse (LONG_MIN);
	res |= test_strtol_reverse (LONG_MIN + 1);
	res |= test_strtol_reverse (LONG_MAX - 1);

	res |= test_strtol_ok ("\xef\xbc\x8d\xef\xbc\x91", -1, 6);
	res |= test_strtol_ok ("\xc2\xa0+1", 1, 4);

	res |= test_strtol_ok ("000000000000000000000000000000", 0, 30);

	res |= test_strtol_noconv ("");
	res |= test_strtol_noconv (" ");
	res |= test_strtol_noconv (" +");
	res |= test_strtol_noconv (" -");
	res |= test_strtol_noconv (" .00");
	res |= test_strtol_noconv (" e0");
	res |= test_strtol_noconv ("--0");
	res |= test_strtol_noconv ("+-0");
	res |= test_strtol_noconv ("+ 0");
	res |= test_strtol_noconv ("- 0");

	{
		char buffer[4 * sizeof (long) + 2];

		sprintf (buffer, "-%lu", 1 + (unsigned long)LONG_MIN);
		res |= test_strtol_overflow (buffer, FALSE);
		sprintf (buffer, "-%lu", 10 + (unsigned long)LONG_MIN);
		res |= test_strtol_overflow (buffer, FALSE);

		sprintf (buffer, "%lu", 1 + (unsigned long)LONG_MAX);
		res |= test_strtol_overflow (buffer, TRUE);
		sprintf (buffer, "%lu", 10 + (unsigned long)LONG_MAX);
		res |= test_strtol_overflow (buffer, TRUE);
	}

	/* -------------------- */

	res |= test_strtod_ok ("0", 0, 1);
	res |= test_strtod_ok ("1", 1, 1);
	res |= test_strtod_ok ("-1", -1, 2);
	res |= test_strtod_ok ("+1", 1, 2);
	res |= test_strtod_ok (" +1", 1, 3);
	res |= test_strtod_ok ("\xc2\xa0+1", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1x", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e+", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e+0", 1, 7);
	res |= test_strtod_ok ("-1e1", -10, 4);
	res |= test_strtod_ok ("100e-2", 1, 6);
	res |= test_strtod_ok ("100e+2", 10000, 6);
	res |= test_strtod_ok ("1x0p0", 1, 1);
	res |= test_strtod_ok ("+inf", gnm_pinf, 4);
	res |= test_strtod_ok ("-inf", gnm_ninf, 4);
	res |= test_strtod_ok ("1.25", 1.25, 4);
	res |= test_strtod_ok ("1.25e1", 12.5, 6);
	res |= test_strtod_ok ("12.5e-1", 1.25, 7);

	g_printerr ("Result = %d\n", res);

	mark_test_end (test_name);
}

/*-------------------------------------------------------------------------- */

static void
define_cell (Sheet *sheet, int c, int r, const char *expr)
{
	GnmCell *cell = sheet_cell_fetch (sheet, c, r);
	sheet_cell_set_text (cell, expr, NULL);
}

#define GET_PROB(i_) ((i_) <= 0 ? 0 : ((i_) >= nf ? 1 : probs[(i_)]))

static gboolean
rand_fractile_test (gnm_float const *vals, int N, int nf,
		    gnm_float const *fractiles, gnm_float const *probs)
{
	gnm_float f = 1.0 / nf;
	int *fractilecount = g_new (int, nf + 1);
	int *expected = g_new (int, nf + 1);
	int i;
	gboolean ok = TRUE;
	gboolean debug = TRUE;

	if (debug) {
		g_printerr ("Bin upper limit:");
		for (i = 1; i <= nf; i++) {
			gnm_float U = (i == nf) ? gnm_pinf : fractiles[i];
			g_printerr ("%s%" GNM_FORMAT_g,
				    (i == 1) ? " " : ", ",
				    U);
		}
		g_printerr (".\n");
	}

	if (debug && probs) {
		g_printerr ("Cumulative probabilities:");
		for (i = 1; i <= nf; i++)
			g_printerr ("%s%.1" GNM_FORMAT_f "%%",
				    (i == 1) ? " " : ", ", 100 * GET_PROB (i));
		g_printerr (".\n");
	}

	for (i = 1; i < nf - 1; i++) {
		if (!(fractiles[i] <= fractiles[i + 1])) {
			g_printerr ("Severe fractile ordering problem.\n");
			return FALSE;
		}

		if (probs && !(probs[i] <= probs[i + 1])) {
			g_printerr ("Severe cumulative probabilities ordering problem.\n");
			return FALSE;
		}
	}
	if (probs && (probs[1] < 0 || probs[nf - 1] > 1)) {
		g_printerr ("Severe cumulative probabilities range problem.\n");
		return FALSE;
	}

	for (i = 0; i <= nf; i++)
		fractilecount[i] = 0;

	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		int j;
		for (j = 1; j < nf; j++)
			if (r <= fractiles[j])
				break;
		fractilecount[j]++;
	}
	g_printerr ("Fractile counts:");
	for (i = 1; i <= nf; i++)
		g_printerr ("%s%d", (i == 1) ? " " : ", ", fractilecount[i]);
	g_printerr (".\n");

	if (probs) {
		g_printerr ("Expected counts:");
		for (i = 1; i <= nf; i++) {
			gnm_float p = GET_PROB (i) - GET_PROB (i-1);
			expected[i] = gnm_floor (p * N + 0.5);
			g_printerr ("%s%d", (i == 1) ? " " : ", ", expected[i]);
		}
		g_printerr (".\n");
	} else {
		gnm_float T = f * N;
		g_printerr ("Expected count in each fractile: %.10" GNM_FORMAT_g "\n", T);
		for (i = 0; i <= nf; i++)
			expected[i] = T;
	}

	for (i = 1; i <= nf; i++) {
		gnm_float T = expected[i];
		if (!(gnm_abs (fractilecount[i] - T) <= 3 * gnm_sqrt (T))) {
			g_printerr ("Fractile test failure for bin %d.\n", i);
			ok = FALSE;
		}
	}

	g_free (fractilecount);
	g_free (expected);

	return ok;
}

#undef GET_PROB

static gnm_float *
test_random_1 (int N, const char *expr,
	       gnm_float *mean, gnm_float *var,
	       gnm_float *skew, gnm_float *kurt)
{
	Workbook *wb = workbook_new ();
	Sheet *sheet;
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;
	int cols = 2, rows = N;

	g_printerr ("Testing %s\n", expr);

	gnm_sheet_suggest_size (&cols, &rows);
	sheet = workbook_sheet_add (wb, -1, cols, rows);

	for (i = 0; i < N; i++)
		define_cell (sheet, 0, i, expr);

	s = g_strdup_printf ("=average(a1:a%d)", N);
	define_cell (sheet, 1, 0, s);
	g_free (s);

	s = g_strdup_printf ("=var(a1:a%d)", N);
	define_cell (sheet, 1, 1, s);
	g_free (s);

	s = g_strdup_printf ("=skew(a1:a%d)", N);
	define_cell (sheet, 1, 2, s);
	g_free (s);

	s = g_strdup_printf ("=kurt(a1:a%d)", N);
	define_cell (sheet, 1, 3, s);
	g_free (s);

	/* Force recalc of all dirty cells even in manual mode.  */
	workbook_recalc (sheet->workbook);

	for (i = 0; i < N; i++)
		res[i] = value_get_as_float (sheet_cell_get (sheet, 0, i)->value);
	*mean = value_get_as_float (sheet_cell_get (sheet, 1, 0)->value);
	g_printerr ("Mean: %.10" GNM_FORMAT_g "\n", *mean);

	*var = value_get_as_float (sheet_cell_get (sheet, 1, 1)->value);
	g_printerr ("Var: %.10" GNM_FORMAT_g "\n", *var);

	*skew = value_get_as_float (sheet_cell_get (sheet, 1, 2)->value);
	g_printerr ("Skew: %.10" GNM_FORMAT_g "\n", *skew);

	*kurt = value_get_as_float (sheet_cell_get (sheet, 1, 3)->value);
	g_printerr ("Kurt: %.10" GNM_FORMAT_g "\n", *kurt);

	g_object_unref (wb);
	return res;
}

static gnm_float *
test_random_normality (int N, const char *expr,
		       gnm_float *mean, gnm_float *var,
		       gnm_float *adtest, gnm_float *cvmtest,
		       gnm_float *lkstest, gnm_float *sftest)
{
	Workbook *wb = workbook_new ();
	Sheet *sheet;
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;
	int cols = 2, rows = N;

	g_printerr ("Testing %s\n", expr);

	gnm_sheet_suggest_size (&cols, &rows);
	sheet = workbook_sheet_add (wb, -1, cols, rows);

	for (i = 0; i < N; i++)
		define_cell (sheet, 0, i, expr);

	s = g_strdup_printf ("=average(a1:a%d)", N);
	define_cell (sheet, 1, 0, s);
	g_free (s);

	s = g_strdup_printf ("=var(a1:a%d)", N);
	define_cell (sheet, 1, 1, s);
	g_free (s);

	s = g_strdup_printf ("=adtest(a1:a%d)", N);
	define_cell (sheet, 1, 2, s);
	g_free (s);

	s = g_strdup_printf ("=cvmtest(a1:a%d)", N);
	define_cell (sheet, 1, 3, s);
	g_free (s);

	s = g_strdup_printf ("=lkstest(a1:a%d)", N);
	define_cell (sheet, 1, 4, s);
	g_free (s);

	s = g_strdup_printf ("=sftest(a1:a%d)", N > 5000 ? 5000 : N);
	define_cell (sheet, 1, 5, s);
	g_free (s);

	/* Force recalc of all dirty cells even in manual mode.  */
	workbook_recalc (sheet->workbook);

	for (i = 0; i < N; i++)
		res[i] = value_get_as_float (sheet_cell_get (sheet, 0, i)->value);
	*mean = value_get_as_float (sheet_cell_get (sheet, 1, 0)->value);
	g_printerr ("Mean: %.10" GNM_FORMAT_g "\n", *mean);

	*var = value_get_as_float (sheet_cell_get (sheet, 1, 1)->value);
	g_printerr ("Var: %.10" GNM_FORMAT_g "\n", *var);

	*adtest = value_get_as_float (sheet_cell_get (sheet, 1, 2)->value);
	g_printerr ("ADTest: %.10" GNM_FORMAT_g "\n", *adtest);

	*cvmtest = value_get_as_float (sheet_cell_get (sheet, 1, 3)->value);
	g_printerr ("CVMTest: %.10" GNM_FORMAT_g "\n", *cvmtest);

	*lkstest = value_get_as_float (sheet_cell_get (sheet, 1, 4)->value);
	g_printerr ("LKSTest: %.10" GNM_FORMAT_g "\n", *lkstest);

	*sftest = value_get_as_float (sheet_cell_get (sheet, 1, 5)->value);
	g_printerr ("SFTest: %.10" GNM_FORMAT_g "\n", *sftest);

	g_object_unref (wb);
	return res;
}

static void
test_random_rand (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	int i;
	gboolean ok;
	gnm_float T;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	vals = test_random_1 (N, "=RAND()", &mean, &var, &skew, &kurt);
	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r < 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = 0.5;
	if (gnm_abs (mean - T) > 0.01) {
		g_printerr ("Mean failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = 1.0 / 12;
	if (gnm_abs (var - T) > 0.01) {
		g_printerr ("Var failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = 0;
	if (gnm_abs (skew - T) > 0.05) {
		g_printerr ("Skew failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = -6.0 / 5;
	if (gnm_abs (kurt - T) > 0.05) {
		g_printerr ("Kurt failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = i / (double)nf;
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randuniform (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lsign = (random_01 () > 0.75 ? 1 : -1);
	gnm_float param_l = lsign * gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 4)));
	gnm_float param_h = param_l + gnm_floor (1 / (0.0001 + gnm_pow (random_01 () / 2, 4)));
	gnm_float n = param_h - param_l;
	gnm_float mean_target = (param_l + param_h) / 2;
	gnm_float var_target = (n * n) / 12;
	gnm_float skew_target = 0;
	gnm_float kurt_target = -6 / 5.0;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDUNIFORM(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")", param_l, param_h);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= param_l && r < param_h)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = param_l + n * i / (double)nf;
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbernoulli (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	int i;
	gboolean ok;
	gnm_float p = 0.3;
	gnm_float q = 1 - p;
	char *expr;
	gnm_float T;

	expr = g_strdup_printf ("=RANDBERNOULLI(%.10" GNM_FORMAT_g ")", p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r == 0 || r == 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = p;
	if (gnm_abs (mean - p) > 0.01) {
		g_printerr ("Mean failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = p * (1 - p);
	if (gnm_abs (var - T) > 0.01) {
		g_printerr ("Var failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = (q - p) / gnm_sqrt (p * q);
	if (gnm_abs (skew - T) > 0.05) {
		g_printerr ("Skew failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = (1 - 6 * p * q) / (p * q);
	if (gnm_abs (kurt - T) > 0.10) {
		g_printerr ("Kurt failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randdiscrete (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	int i;
	gboolean ok;
	gnm_float mean_target = 13;
	gnm_float var_target = 156;
	gnm_float skew_target = 0.6748;
	gnm_float kurt_target = -0.9057;
	char *expr;
	gnm_float T;

	expr = g_strdup_printf ("=RANDDISCRETE({0;1;4;9;16;25;36})");
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= 36 && gnm_sqrt (r) == gnm_floor (gnm_sqrt (r)))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randnorm (int N)
{
	gnm_float mean, var, adtest, cvmtest, lkstest, sftest;
	gnm_float mean_target = 0, var_target = 1;
	gnm_float *vals;
	gboolean ok;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDNORM(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")",
				mean_target, var_target);
	vals = test_random_normality (N, expr, &mean, &var, &adtest, &cvmtest, &lkstest, &sftest);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!gnm_finite (r)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	if (gnm_abs (mean - T) > 0.02) {
		g_printerr ("Mean failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = var_target;
	if (gnm_abs (var - T) > 0.02) {
		g_printerr ("Var failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qnorm (i / (double)nf, mean_target, gnm_sqrt (var_target), TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (adtest < 0.05) {
		g_printerr ("Anderson Darling Test rejected [%.10" GNM_FORMAT_g "]\n", adtest);
		ok = FALSE;
	}
	if (cvmtest < 0.05) {
		g_printerr ("Cramér-von Mises Test rejected [%.10" GNM_FORMAT_g "]\n", cvmtest);
		ok = FALSE;
	}
	if (lkstest < 0.01) {
		g_printerr ("Lilliefors (Kolmogorov-Smirnov) Test rejected [%.10" GNM_FORMAT_g "]\n",
			    lkstest);
		ok = FALSE;
	}
	if (sftest < 0.05) {
		g_printerr ("Shapiro-Francia Test rejected [%.10" GNM_FORMAT_g "]\n", sftest);
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randsnorm (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float alpha = 5;
	gnm_float delta = alpha/gnm_sqrt(1+alpha*alpha);
	gnm_float mean_target = delta * gnm_sqrt (2/M_PIgnum);
	gnm_float var_target = 1-mean_target*mean_target;
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDSNORM(%.10" GNM_FORMAT_g ")", alpha);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!gnm_finite (r)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	if (gnm_abs (mean - T) > 0.01) {
		g_printerr ("Mean failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = var_target;
	if (gnm_abs (var - T) > 0.01) {
		g_printerr ("Var failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = mean_target/gnm_sqrt(var_target);
	T = T*T*T*(4-M_PIgnum)/2;
	if (gnm_abs (skew - T) > 0.05) {
		g_printerr ("Skew failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}
	T = 2*(M_PIgnum - 3)*mean_target*mean_target*mean_target*mean_target/(var_target*var_target);
	if (gnm_abs (kurt - T) > 0.15) {
		g_printerr ("Kurt failure [%.10" GNM_FORMAT_g "]\n", T);
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randexp (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_l = 1 / (0.0001 + gnm_pow (random_01 () / 2, 4));
	gnm_float mean_target = param_l;
	gnm_float var_target = mean_target * mean_target;
	gnm_float skew_target = 2;
	gnm_float kurt_target = 6;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDEXP(%.10" GNM_FORMAT_g ")", param_l);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qexp (i / (double)nf, param_l, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randgamma (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_shape = gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 6)));
	gnm_float param_scale = 0.001 + gnm_pow (random_01 (), 4) * 1000;
	gnm_float mean_target = param_shape * param_scale;
	gnm_float var_target = mean_target * param_scale;
	gnm_float skew_target = 2 / gnm_sqrt (param_shape);
	gnm_float kurt_target = 6 / param_shape;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDGAMMA(%.0" GNM_FORMAT_f ",%.10" GNM_FORMAT_g ")", param_shape, param_scale);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r > 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qgamma (i / (double)nf, param_shape, param_scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbeta (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_a = gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 6)));	gnm_float param_b = gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 6)));
	gnm_float s = param_a + param_b;
	gnm_float mean_target = param_a / s;
	gnm_float var_target = mean_target * param_b / (s * (s + 1));
	gnm_float skew_target =
		(2 * (param_b - param_a) * gnm_sqrt (s + 1))/
		((s + 2) * gnm_sqrt (param_a * param_b));
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDBETA(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")", param_a, param_b);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qbeta (i / (double)nf, param_a, param_b, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randtdist (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = 0;
	gnm_float var_target = param_df > 2 ? param_df / (param_df - 2) : gnm_nan;
	gnm_float skew_target = param_df > 3 ? 0 : gnm_nan;
	gnm_float kurt_target = param_df > 4 ? 6 / (param_df - 4) : gnm_nan;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDTDIST(%.0" GNM_FORMAT_f ")", param_df);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qt (i / (double)nf, param_df, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randfdist (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df1 = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 6)));
	gnm_float param_df2 = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = param_df2 > 2 ? param_df2 / (param_df2 - 2) : gnm_nan;
	gnm_float var_target = param_df2 > 4
		? (2 * param_df2 * param_df2 * (param_df1 + param_df2 - 2) /
		   (param_df1 * (param_df2 - 2) * (param_df2 - 2) * (param_df2 - 4)))
		: gnm_nan;
	gnm_float skew_target = gnm_nan; /* Complicated */
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDFDIST(%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_df1, param_df2);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qf (i / (double)nf, param_df1, param_df2, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randchisq (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = param_df;
	gnm_float var_target = param_df * 2;
	gnm_float skew_target = gnm_sqrt (8 / param_df);
	gnm_float kurt_target = 12 / param_df;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDCHISQ(%.10" GNM_FORMAT_g ")", param_df);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qchisq (i / (double)nf, param_df, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randcauchy (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_scale = 0.001 + gnm_pow (random_01 (), 4) * 1000;
	gnm_float mean_target = gnm_nan;
	gnm_float var_target = gnm_nan;
	gnm_float skew_target = gnm_nan;
	gnm_float kurt_target = gnm_nan;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	/*
	 * The distribution has no mean, no variance, no skew, and no kurtosis.
	 * The support is all reals.
	 */

	expr = g_strdup_printf ("=RANDCAUCHY(%.10" GNM_FORMAT_g ")", param_scale);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qcauchy (i / (double)nf, 0.0, param_scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbinom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float param_trials = gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 4)));
	gnm_float mean_target = param_trials * param_p;
	gnm_float var_target = mean_target * (1 - param_p);
	gnm_float skew_target = (1 - 2 * param_p) / gnm_sqrt (var_target);
	gnm_float kurt_target = (1 - 6 * param_p * (1 - param_p)) / var_target;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDBINOM(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ")", param_p, param_trials);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= param_trials && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qbinom (i / (double)nf, param_trials, param_p, TRUE, FALSE);
		probs[i] = pbinom (fractiles[i], param_trials, param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randnegbinom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float param_fails = gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 4)));
	/* Warning: these differ from Wikipedia by swapping p and 1-p.  */
	gnm_float mean_target = param_fails * (1 - param_p) / param_p;
	gnm_float var_target = mean_target / param_p;
	gnm_float skew_target = (2 - param_p) / gnm_sqrt (param_fails * (1 - param_p));
	gnm_float kurt_target = 6 / param_fails + 1 / var_target;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDNEGBINOM(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ")", param_p, param_fails);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qnbinom (i / (double)nf, param_fails, param_p, TRUE, FALSE);
		probs[i] = pnbinom (fractiles[i], param_fails, param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randhyperg (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_nr = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 4)));
	gnm_float param_nb = gnm_floor (1 / (0.01 + gnm_pow (random_01 (), 4)));
	gnm_float s = param_nr + param_nb;
	gnm_float param_n = gnm_floor (random_01 () * (s + 1));
	gnm_float mean_target = param_n * param_nr / s;
	gnm_float var_target = s > 1
		? mean_target * (param_nb / s) * (s - param_n) / (s - 1)
		: 0;
	gnm_float skew_target = gnm_nan; /* Complicated */
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDHYPERG(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_nr, param_nb, param_n);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= param_n && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) &&
	    !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qhyper (i / (double)nf, param_nr, param_nb, param_n, TRUE, FALSE);
		probs[i] = phyper (fractiles[i], param_nr, param_nb, param_n, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbetween (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lsign = (random_01 () > 0.75 ? 1 : -1);
	gnm_float param_l = lsign * gnm_floor (1 / (0.0001 + gnm_pow (random_01 (), 4)));
	gnm_float param_h = param_l + gnm_floor (1 / (0.0001 + gnm_pow (random_01 () / 2, 4)));
	gnm_float n = param_h - param_l + 1;
	gnm_float mean_target = (param_l + param_h) / 2;
	gnm_float var_target = (n * n - 1) / 12;
	gnm_float skew_target = 0;
	gnm_float kurt_target = (n * n + 1) / (n * n - 1) * -6 / 5;
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDBETWEEN(%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_l, param_h);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= param_l && r <= param_h && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randpoisson (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_l = 1 / (0.0001 + gnm_pow (random_01 () / 2, 4));
	gnm_float mean_target = param_l;
	gnm_float var_target = param_l;
	gnm_float skew_target = 1 / gnm_sqrt (param_l);
	gnm_float kurt_target = 1 / param_l;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDPOISSON(%.10" GNM_FORMAT_g ")", param_l);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qpois (i / (double)nf, param_l, TRUE, FALSE);
		probs[i] = ppois (fractiles[i], param_l, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

/*
 * Note: this geometric distribution is the only with support {0,1,2,...}
 */
static void
test_random_randgeom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float mean_target = (1 - param_p) / param_p;
	gnm_float var_target = (1 - param_p) / (param_p * param_p);
	gnm_float skew_target = (2 - param_p) / gnm_sqrt (1 - param_p);
	gnm_float kurt_target = 6 + (param_p * param_p) / (1 - param_p);
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDGEOM(%.10" GNM_FORMAT_g ")", param_p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qgeom (i / (double)nf, param_p, TRUE, FALSE);
		probs[i] = pgeom (fractiles[i], param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randlog (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float p = param_p;
	gnm_float l1mp = gnm_log1p (-p);
	gnm_float mean_target = -p / (1 - p) / l1mp;
	gnm_float var_target = -(p * (p + l1mp)) / gnm_pow ((1 - p) * l1mp, 2);
	/* See http://mathworld.wolfram.com/Log-SeriesDistribution.html */
	gnm_float skew_target =
		-l1mp *
		(2 * p * p + 3 * p * l1mp + (1 + p) * l1mp * l1mp) /
		(l1mp * (p + l1mp) * gnm_sqrt (-p * (p + l1mp)));
	gnm_float kurt_target =
		-(6 * p * p * p +
		  12 * p * p * l1mp +
		  p * (7 + 4 * p) * l1mp * l1mp +
		  (1 + 4 * p + p * p) * l1mp * l1mp * l1mp) /
		(p * gnm_pow (p + l1mp, 2));
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDLOG(%.10" GNM_FORMAT_g ")", param_p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 1 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randweibull (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float shape = 1 / (0.0001 + gnm_pow (random_01 () / 2, 2));
	gnm_float scale = 2 * random_01 ();
	gnm_float mean_target = scale * gnm_gamma (1 + 1 / shape);
	gnm_float var_target = scale * scale  *
		(gnm_gamma (1 + 2 / shape) -
		 gnm_pow (gnm_gamma (1 + 1 / shape), 2));
	/* See https://en.wikipedia.org/wiki/Weibull_distribution */
	gnm_float skew_target =
		(gnm_gamma (1 + 3 / shape) * gnm_pow (scale, 3) -
		 3 * mean_target * var_target -
		 gnm_pow (mean_target, 3)) /
		gnm_pow (var_target, 1.5);
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDWEIBULL(%.10" GNM_FORMAT_f ",%.10" GNM_FORMAT_f ")", scale, shape);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qweibull (i / (double)nf, shape, scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randlognorm (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lm = (random_01() - 0.5) / (0.1 + gnm_pow (random_01 () / 2, 2));
	gnm_float ls = 1 / (1 + gnm_pow (random_01 () / 2, 2));
	gnm_float mean_target = gnm_exp (lm + ls * ls / 2);
	gnm_float var_target = gnm_expm1 (ls * ls) * (mean_target * mean_target);
	/* See https://en.wikipedia.org/wiki/Log-normal_distribution */
	gnm_float skew_target = (gnm_exp (ls * ls) + 2) *
		gnm_sqrt (gnm_expm1 (ls * ls));
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDLOGNORM(%.10" GNM_FORMAT_f ",%.10" GNM_FORMAT_f ")", lm, ls);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= gnm_pinf)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure.\n");
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qlnorm (i / (double)nf, lm, ls, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random (void)
{
	const char *test_name = "test_random";
	const int N = 20000;
	const int High_N = 200000;
	const char *single = g_getenv ("SSTEST_RANDOM");

	mark_test_start (test_name);

#define CHECK1(NAME,C) \
	do { if (!single || strcmp(single,#NAME) == 0) test_random_ ## NAME (C); } while (0)

	/* Continuous */
	CHECK1 (rand, N);
	CHECK1 (randuniform, N);
	CHECK1 (randbeta, N);
	CHECK1 (randcauchy, N);
	CHECK1 (randchisq, N);
	CHECK1 (randexp, N);
	CHECK1 (randfdist, N);
	CHECK1 (randgamma, N);
	CHECK1 (randlog, N);
	CHECK1 (randlognorm, N);
	CHECK1 (randnorm, High_N);
	CHECK1 (randsnorm, High_N);
	CHECK1 (randtdist, N);
	CHECK1 (randweibull, N);
#if 0
	CHECK1 (randexppow, N);
	CHECK1 (randgumbel, N);
	CHECK1 (randlandau, N);
	CHECK1 (randlaplace, N);
	CHECK1 (randlevy, N);
	CHECK1 (randlogistic, N);
	CHECK1 (randnormtail, N);
	CHECK1 (randpareto, N);
	CHECK1 (randrayleigh, N);
	CHECK1 (randrayleightail, N);
	CHECK1 (randstdist, N);
#endif

	/* Discrete */
	CHECK1 (randbernoulli, N);
	CHECK1 (randbetween, N);
	CHECK1 (randbinom, N);
	CHECK1 (randdiscrete, N);
	CHECK1 (randgeom, High_N);
	CHECK1 (randhyperg, High_N);
	CHECK1 (randnegbinom, High_N);
	CHECK1 (randpoisson, High_N);

#undef CHECK1

	mark_test_end (test_name);
}

/*-------------------------------------------------------------------------- */

#define MAYBE_DO(name) if (strcmp (testname, "all") != 0 && strcmp (testname, (name)) != 0) { } else

int
main (int argc, char const **argv)
{
	GOErrorInfo	*plugin_errs;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	const char *testname;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("[testname]"));
	g_option_context_add_main_entries (ocontext, sstest_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, g_get_prgname ());
		g_error_free (error);
		return 1;
	}

	if (sstest_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	gnm_init ();

	cc = gnm_cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}

	testname = argv[1];
	if (!testname) testname = "all";

	/* ---------------------------------------- */

	MAYBE_DO ("test_insdel_rowcol_names") test_insdel_rowcol_names ();
	MAYBE_DO ("test_insert_delete") test_insert_delete ();
	MAYBE_DO ("test_func_help") test_func_help ();
	MAYBE_DO ("test_nonascii_numbers") test_nonascii_numbers ();
	MAYBE_DO ("test_random") test_random ();

	/* ---------------------------------------- */

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}
