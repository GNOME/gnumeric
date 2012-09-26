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
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet1->name_unquoted);
		sheet_insert_cols (sheet1, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet2->name_unquoted);
		sheet_insert_cols (sheet2, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to delete column %d on %s\n",
			    i, sheet1->name_unquoted);
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

static gnm_float *
test_random_1 (int N, const char *expr,
	       gnm_float *mean, gnm_float *var,
	       gnm_float *skew, gnm_float *kurt)
{
	Workbook *wb = workbook_new ();
	Sheet *sheet = workbook_sheet_add
		(wb, -1, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;

	g_printerr ("Testing %s\n", expr);

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
	Sheet *sheet = workbook_sheet_add
		(wb, -1, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;

	g_printerr ("Testing %s\n", expr);

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

	vals = test_random_1 (N, "=RAND()", &mean, &var, &skew, &kurt);
	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}
	g_free (vals);

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
	if (ok)
		g_printerr ("OK\n");
	g_printerr ("\n");
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
	g_free (vals);

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

	expr = g_strdup_printf ("=RANDNORM(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")",
				mean_target, var_target);
	vals = test_random_normality (N, expr, &mean, &var, &adtest, &cvmtest, &lkstest, &sftest);
	g_free (expr);
	g_free (vals);

	ok = TRUE;

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

	expr = g_strdup_printf ("=RANDSNORM(%.10" GNM_FORMAT_g ")", alpha);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);
	g_free (vals);

	ok = TRUE;

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
}

static void
test_random (void)
{
	const char *test_name = "test_random";
	const int N = 20000;
	const int High_N = 65000;

	mark_test_start (test_name);
	test_random_rand (N);
        test_random_randbernoulli (N);
        test_random_randnorm (High_N);
        test_random_randsnorm (High_N);
#if 0
        test_random_randbeta (N);
        test_random_randbetween (N);
        test_random_randbinom (N);
        test_random_randcauchy (N);
        test_random_randchisq (N);
	test_random_randdiscrete (N);
	test_random_randexp (N);
        test_random_randexppow (N);
        test_random_randfdist (N);
        test_random_randgamma (N);
        test_random_randnormtail (N);
        test_random_randgeom (N);
        test_random_randgumbel (N);
        test_random_randhyperg (N);
        test_random_randlandau (N);
        test_random_randlaplace (N);
        test_random_randlevy (N);
        test_random_randlog (N);
        test_random_randlogistic (N);
        test_random_randlognorm (N);
        test_random_randnegbinom (N);
        test_random_randpareto (N);
        test_random_randpoisson (N);
        test_random_randrayleigh (N);
        test_random_randrayleightail (N);
        test_random_randstdist (N);
        test_random_randtdist (N);
        test_random_randuniform (N);
        test_random_randweibull (N);
#endif

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

	cc = cmd_context_stderr_new ();
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
	MAYBE_DO ("test_func_help") test_func_help ();
	MAYBE_DO ("test_nonascii_numbers") test_nonascii_numbers ();
	MAYBE_DO ("test_random") test_random ();

	/* ---------------------------------------- */

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}
