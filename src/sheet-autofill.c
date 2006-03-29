/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-autofill.c: Provides the autofill features
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org), 1998
 *   Jody Goldberg (jody@gnome.org), 1999-2006
 *   Morten Welinder (terra@gnome.org), 2006
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "sheet-autofill.h"

#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "sheet-style.h"
#include "expr.h"
#include "expr-impl.h"
#include "gnm-format.h"
#include "gnm-datetime.h"
#include "mstyle.h"
#include "ranges.h"
#include "sheet-merge.h"
#include <goffice/utils/go-glib-extras.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */

static char *month_names_long[12 + 1];
static char *month_names_short[12 + 1];
static char *weekday_names_long[7 + 1];
static char *weekday_names_short[7 + 1];

void
autofill_init (void)
{
	GDateMonth m;
	GDateWeekday wd;

	for (m = 1; m <= 12; m++) {
		month_names_long[m - 1] = go_date_month_name (m, FALSE);
		month_names_short[m - 1] = go_date_month_name (m, TRUE);
	}
	for (wd = 1; wd <= 7; wd++) {
		weekday_names_long[wd - 1] = go_date_weekday_name (wd, FALSE);
		weekday_names_short[wd - 1] = go_date_weekday_name (wd, TRUE);
	}
}

void
autofill_shutdown (void)
{
	GDateMonth m;
	GDateWeekday wd;

	for (m = 1; m <= 12; m++) {
		g_free (month_names_long[m - 1]);
		g_free (month_names_short[m - 1]);
	}
	for (wd = 1; wd <= 7; wd++) {
		g_free (weekday_names_long[wd - 1]);
		g_free (weekday_names_short[wd - 1]);
	}
}

/* ------------------------------------------------------------------------- */

typedef enum {
	AFS_INCOMPLETE,
	AFS_READY,
	AFS_ERROR
} AutoFillerStatus;

typedef struct _AutoFiller AutoFiller;

struct _AutoFiller {
	AutoFillerStatus status;
	int priority;

	void (*finalize) (AutoFiller *af);

	/* Given a new cell, adapt filler to that.  */
	void (*teach_cell) (AutoFiller *af, const GnmCell *cell, int n);

	/* Set cell to the value of the nth sequence member.  */
	void (*set_cell) (AutoFiller *af, GnmCell *cell, int n);

	/* Hint of what will be the nth element.  */
	char * (*hint) (AutoFiller *af, int n);
};

static void
af_finalize (AutoFiller *af)
{
	g_free (af);
}

/* ------------------------------------------------------------------------- */
/*
 * Arithmetic sequences:
 *
 * 1, 2, 3, ...
 * 1, 3, 5, ....
 * 1-Jan-2009, 2-Jan-2009, 3-Jan-2009, ...
 * 1-Jan-2009, 8-Jan-2009, 15-Jan-2009, ...
 * 00:00, 00:30, 01:00, ...
 */

typedef struct {
	AutoFiller filler;

	gboolean singleton;  /* Missing step becomes 1.  */
	gnm_float base, step;
	GOFormat *format;
} AutoFillerArithmetic;

static void
afa_finalize (AutoFiller *af)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	if (afa->format)
		go_format_unref (afa->format);
	af_finalize (af);
}

static void
afa_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	GnmValue *value = cell ? cell->value : NULL;
	gnm_float f;

	if (value == NULL ||
	    cell_has_expr (cell) ||
	    !VALUE_IS_NUMBER (value) ||
	    VALUE_TYPE (value) == VALUE_BOOLEAN) {
		af->status = AFS_ERROR;
		return;
	}

	f = value_get_as_float (value);

	switch (n) {
	case 0:
		afa->base = f;
		if (afa->singleton) {
			afa->step = 1;
			af->status = AFS_READY;
		}
		if (VALUE_FMT (value))
			afa->format = go_format_ref (VALUE_FMT (value));
		break;
	case 1:
		afa->step = f - afa->base;
		af->status = AFS_READY;
		break;
	default: {
		gnm_float step2 = f - (afa->base + n * afa->step);
		gnm_float err = (afa->step - step2) /
			(gnm_abs (afa->step) + gnm_abs (step2));
		/* Be fairly lenient: */
		if (err > (n + 64) * GNM_EPSILON) {
			af->status = AFS_ERROR;
			return;
		}
	}
	}
}

static void
afa_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	gnm_float f = afa->base + n * afa->step;
	cell_set_value (cell, value_new_float (f));
}

static char *
afa_hint (AutoFiller *af, int n)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	if (af->status == AFS_READY) {
		gnm_float f = afa->base + n * afa->step;
		return g_strdup_printf ("%" GNM_FORMAT_g, f);
	}
	return NULL;
}

static AutoFiller *
auto_filler_arithmetic (gboolean singleton)
{
	AutoFillerArithmetic *res = g_new (AutoFillerArithmetic, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = 100;
	res->filler.finalize = afa_finalize;
	res->filler.teach_cell = afa_teach_cell;
	res->filler.set_cell = afa_set_cell;
	res->filler.hint = afa_hint;
	res->format = NULL;
	res->singleton = singleton;

	return &res->filler;
}

/* ------------------------------------------------------------------------- */
/*
 * Arithmetic sequences:
 *
 * "Foo 1", "Foo 2", "Foo 3", ...
 * "1 Bar", "3 Bar", "5 Bar", ...
 * "Fall '99", "Fall '00", "Fall '01", ...
 */

typedef struct {
	AutoFiller filler;

	gboolean singleton;  /* Missing step becomes 1.  */
	gnm_float base, step;
	GString *prefix;
	GString *suffix;
	gboolean fixed_length;
	gsize numlen;
	gnm_float p10;
} AutoFillerNumberString;

static void
afns_finalize (AutoFiller *af)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	g_string_free (afns->prefix, TRUE);
	g_string_free (afns->suffix, TRUE);
	af_finalize (af);
}

static void
afns_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	GnmValue *value = cell ? cell->value : NULL;
	const char *s;

	if (value == NULL ||
	    cell_has_expr (cell) ||
	    !VALUE_IS_STRING (value)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	s = value_peek_string (value);

	if (n == 0) {
		gsize pl;
		char *end;

		for (pl = 0; s[pl]; pl++) {
			if (g_ascii_isdigit (s[pl]))
				break;
			if (!afns->fixed_length &&
			    (s[pl] == '+' || s[pl] == '-') &&
			    g_ascii_isdigit (s[pl + 1]))
				break;
		}
		if (s[pl] == 0)
			goto bad;
		g_string_append_len (afns->prefix, s, pl);
		errno = 0;
		afns->base = strtol (s + pl, &end, 10);
		if (errno)
			goto bad;
		afns->numlen = end - (s + pl);
		afns->p10 = gnm_pow10 (afns->numlen);
		g_string_append (afns->suffix, end);

		if (afns->singleton) {
			afns->step = 1;
			af->status = AFS_READY;
		}
	} else {
		gsize slen = strlen (s);
		char *end;
		gnm_float val;
		const char *s2 = s + afns->prefix->len;

		if (slen <= afns->prefix->len + afns->suffix->len)
			goto bad;
		if (memcmp (s, afns->prefix->str, afns->prefix->len))
			goto bad;
		if (memcmp (s + slen - afns->suffix->len,
			    afns->suffix->str,
			    afns->suffix->len))
			goto bad;
		if (g_ascii_isspace (*s2))
			goto bad;

		errno = 0;
		if (afns->fixed_length) {
			val = strtoul (s2, &end, 10);
			if (afns->numlen != (gsize)(end - s2))
				goto bad;
		} else {
			/*
			 * Verify no leading zero so the fixed-length
			 * version gets a chance.
			 */
			const char *s3 = s2;
			if (!g_ascii_isdigit (*s3))
				s3++;
			if (s3[0] == '0' && g_ascii_isdigit (s3[1]))
				goto bad;
			val = strtol (s2, &end, 10);
		}
		if (errno || end != s + slen - afns->suffix->len)
			goto bad;

		if (n == 1) {
			afns->step = val - afns->base;
			if (afns->fixed_length && afns->step < 0)
				afns->step += afns->p10;
			af->status = AFS_READY;
		} else {
			gnm_float f = afns->base + n * afns->step;
			if (afns->fixed_length)
				f = gnm_fmod (f, afns->p10);
			if (gnm_abs (f - val) > 0.5)
				goto bad;
		}
	}
}

static char *
afns_compute (AutoFillerNumberString *afns, int n)
{
	gnm_float f = afns->base + n * afns->step;
	if (afns->fixed_length) {
		f = gnm_fmod (f, afns->p10);
		return g_strdup_printf ("%-.*s%0*.0" GNM_FORMAT_f "%s",
					(int)afns->prefix->len,
					afns->prefix->str,
					(int)afns->numlen, f,
					afns->suffix->str);
	} else {
		return g_strdup_printf ("%-.*s%.0" GNM_FORMAT_f "%s",
					(int)afns->prefix->len,
					afns->prefix->str,
					f,
					afns->suffix->str);
	}
}

static void
afns_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	char *s = afns_compute (afns, n);
	cell_set_value (cell, value_new_string_nocopy (s));
}

static char *
afns_hint (AutoFiller *af, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	if (af->status == AFS_READY)
		return afns_compute (afns, n);
	else
		return NULL;
}

static AutoFiller *
auto_filler_number_string (gboolean singleton, gboolean fixed_length)
{
	AutoFillerNumberString *res = g_new (AutoFillerNumberString, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = fixed_length ? 9 : 10;
	res->filler.finalize = afns_finalize;
	res->filler.teach_cell = afns_teach_cell;
	res->filler.set_cell = afns_set_cell;
	res->filler.hint = afns_hint;
	res->singleton = singleton;
	res->fixed_length = fixed_length;
	res->prefix = g_string_new (NULL);
	res->suffix = g_string_new (NULL);

	return &res->filler;
}

/* ------------------------------------------------------------------------- */
/*
 * Month sequences:
 *
 * 1-Jan-2009, 1-Feb-2009, 3-Mar-2009, ...
 * 31-Jan-2009, 28-Feb-2009, 31-Mar-2009, ...
 * 1-Jan-2009, 1-Jan-2010, 1-Jan-2011, ...
 */

typedef struct {
	AutoFiller filler;

	GODateConventions const *dateconv;
	GDate base;
	GOFormat *format;
	int nmonths;
	gboolean end_of_month;
} AutoFillerMonth;

static void
afm_finalize (AutoFiller *af)
{
	AutoFillerMonth *afm = (AutoFillerMonth *)af;
	if (afm->format)
		go_format_unref (afm->format);
	af_finalize (af);
}

static void
afm_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerMonth *afm = (AutoFillerMonth *)af;
	GnmValue *value = cell ? cell->value : NULL;
	GDate d;
	const GOFormat *sf;

	if (value == NULL || cell_has_expr (cell)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	sf = cell_get_format (cell);
	if (VALUE_IS_NUMBER (value) && sf->family != GO_FORMAT_DATE)
		goto bad;

	if (!datetime_value_to_g (&d, value, afm->dateconv))
		goto bad;

	if (!g_date_is_last_of_month (&d))
		afm->end_of_month = FALSE;

	if (n == 0) {
		if (VALUE_FMT (value))
			afm->format = go_format_ref (VALUE_FMT (value));
		afm->base = d;
	} else {
		int year = g_date_get_year (&d);
		int month = g_date_get_month (&d);
		int day = g_date_get_day (&d);
		int nmonths;

		if (day != g_date_get_day (&afm->base) &&
		    day != g_date_get_days_in_month (month, year))
			goto bad;

		nmonths = 12 * (year - g_date_get_year (&afm->base)) +
			(month - g_date_get_month (&afm->base));
		if (n == 1)
			afm->nmonths = nmonths;
		else if (nmonths != afm->nmonths * n)
			goto bad;

		af->status = AFS_READY;
	}
}

static void
afm_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerMonth *afm = (AutoFillerMonth *)af;
	GDate d = afm->base;
	GnmValue *v;

	if (afm->nmonths >= 0)
		g_date_add_months (&d, n * afm->nmonths);
	else
		g_date_subtract_months (&d, n * afm->nmonths);

	if (!g_date_valid (&d) || g_date_get_year (&d) > 9999) {
		GnmEvalPos ep;
		eval_pos_init_cell (&ep, cell);
		cell_set_value (cell, value_new_error_VALUE (&ep));
		return;
	}

	if (afm->end_of_month) {
		int year = g_date_get_year (&d);
		int month = g_date_get_month (&d);
		g_date_set_day (&d, g_date_get_days_in_month (month, year));
	}

	v = value_new_int (datetime_g_to_serial (&d, afm->dateconv));
	if (afm->format)
		value_set_fmt (v, afm->format);
	cell_set_value (cell, v);
}

static char *
afm_hint (AutoFiller *af, int n)
{
	return NULL;
}

static AutoFiller *
auto_filler_month (GODateConventions const *dateconv)
{
	AutoFillerMonth *res = g_new (AutoFillerMonth, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = 200;
	res->filler.finalize = afm_finalize;
	res->filler.teach_cell = afm_teach_cell;
	res->filler.set_cell = afm_set_cell;
	res->filler.hint = afm_hint;
	res->format = NULL;
	res->dateconv = dateconv;

	return &res->filler;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	AutoFiller filler;

	char **list;
	int base, size;
	gboolean with_number;
	GString *sep;
	gnm_float base_number;
} AutoFillerList;

static void
afl_finalize (AutoFiller *af)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	g_string_free (afl->sep, TRUE);
	af_finalize (af);
}

static void
afl_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	GnmValue *value = cell ? cell->value : NULL;
	const char *s;

	if (value == NULL ||
	    cell_has_expr (cell) ||
	    !VALUE_IS_STRING (value)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	s = value_peek_string (value);

	if (n == 0) {
		gsize elen = 0;

		for (afl->base = 0; afl->base < afl->size; afl->base++) {
			const char *e = afl->list[afl->base];
			elen = strlen (e);
			/* This isn't UTF-8 pretty.  */
			if (strncmp (s, e, elen) == 0)
				break;
		}
		if (afl->base == afl->size)
			goto bad;

		if (afl->with_number) {
			const char *s2 = s + elen;
			char *end;

			while (*s2 && !g_ascii_isdigit (*s2)) {
				g_string_append_c (afl->sep, *s2);
				s2++;
			}

			errno = 0;
			afl->base_number = strtoul (s2, &end, 10);
			if (*s2 == 0 || errno || *end != 0)
				goto bad;
		} else {
			if (s[elen] != 0)
				goto bad;
		}
	} else {
		const char *e = afl->list[(afl->base + n) % afl->size];
		gsize elen = strlen (e);
		/* This isn't UTF-8 pretty.  */
		if (strncmp (s, e, elen))
			goto bad;

		if (afl->with_number) {
			const char *s2 = s + elen;
			char *end;
			gnm_float f = afl->base_number +
				(afl->base + n) / afl->size;

			if (memcmp (s2, afl->sep->str, afl->sep->len))
				goto bad;
			s2 += afl->sep->len;

			errno = 0;
			if (*s2 == 0 ||
			    strtoul (s2, &end, 10) != f ||
			    errno ||
			    *end != 0)
				goto bad;
		} else {
			if (s[elen] != 0)
				goto bad;
		}

		af->status = AFS_READY;
	}
}

static void
afl_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	int n2 = afl->base + n;
	GnmValue *val;
	GString *res = g_string_new (afl->list[n2 % afl->size]);
	if (afl->with_number) {
		gnm_float f = afl->base_number + n2 / afl->size;
		go_string_append_gstring (res, afl->sep);
		g_string_append_printf (res, "%.0" GNM_FORMAT_f, f);
	}

	val = value_new_string_nocopy (g_string_free (res, FALSE));
	cell_set_value (cell, val);
}

static char *
afl_hint (AutoFiller *af, int n)
{
	return NULL;
}

static AutoFiller *
auto_filler_list (char **list, int prio, gboolean with_number)
{
	AutoFillerList *res = g_new (AutoFillerList, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = prio;
	res->filler.finalize = afl_finalize;
	res->filler.teach_cell = afl_teach_cell;
	res->filler.set_cell = afl_set_cell;
	res->filler.hint = afl_hint;
	res->list = list;
	res->size = g_strv_length (list);
	res->with_number = with_number;
	res->sep = g_string_new (NULL);

	return &res->filler;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	AutoFiller filler;

	int size;
	GnmCellPos last;
	const GnmCell ** cells;
} AutoFillerCopy;

static void
afc_finalize (AutoFiller *af)
{
	AutoFillerCopy *afe = (AutoFillerCopy *)af;
	g_free (afe->cells);
	af_finalize (af);
}

static void
afc_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerCopy *afe = (AutoFillerCopy *)af;
	afe->cells[n] = cell;
	if (n == afe->size - 1) {
		/* This actually includes the all-empty case.  */
		af->status = AFS_READY;
	}
}

static void
afc_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerCopy *afe = (AutoFillerCopy *)af;
	const GnmCell *src = afe->cells[n % afe->size];
	if (src && cell_has_expr (src)) {
		GnmExprRewriteInfo rwinfo;
		GnmExprRelocateInfo *rinfo = &rwinfo.u.relocate;
		GnmExprTop const *texpr;
		GnmExprTop const *src_texpr = src->base.texpr;
		GnmExprOp oper = GNM_EXPR_GET_OPER (src_texpr->expr);

		/* Arrays are always assigned fully at the corner.  */
		if (oper == GNM_EXPR_OP_ARRAY_ELEM)
			return;

		rwinfo.rw_type = GNM_EXPR_REWRITE_EXPR;
		rinfo->target_sheet = rinfo->origin_sheet = NULL;
		rinfo->col_offset = rinfo->row_offset = 0;
		rinfo->origin.start = rinfo->origin.end = cell->pos;
		eval_pos_init_cell (&rinfo->pos, cell);

		texpr = gnm_expr_top_rewrite (src_texpr, &rwinfo);

		/* Clip arrays that are only partially copied.  */
		if (oper == GNM_EXPR_OP_ARRAY_CORNER) {
                        GnmExpr const *aexpr;
			GnmExprArrayCorner const *array =
				&src_texpr->expr->array_corner;
			guint limit_x = afe->last.col - cell->pos.col + 1;
			guint limit_y = afe->last.row - cell->pos.row + 1;
                        unsigned cols = MIN (limit_x, array->cols);
                        unsigned rows = MIN (limit_y, array->rows);

                        if (texpr) {
                                aexpr = gnm_expr_copy (texpr->expr->array_corner.expr);
                                gnm_expr_top_unref (texpr);
                        } else
                                aexpr = gnm_expr_copy (array->expr);

			cell_set_array_formula
				(cell->base.sheet,
				 cell->pos.col, cell->pos.row,
				 cell->pos.col + (cols - 1),
				 cell->pos.row + (rows - 1),
				 gnm_expr_top_new (aexpr));
		} else if (texpr) {
			cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		} else
			cell_set_expr (cell, src_texpr);
	} else if (src) {
		cell_set_value (cell, value_dup (src->value));
	} else
		sheet_cell_remove (cell->base.sheet, cell, TRUE, TRUE);
}

static char *
afc_hint (AutoFiller *af, int n)
{
	return NULL;
}

static AutoFiller *
auto_filler_copy (int size, guint last_col, guint last_row)
{
	AutoFillerCopy *res = g_new (AutoFillerCopy, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = 1;
	res->filler.finalize = afc_finalize;
	res->filler.teach_cell = afc_teach_cell;
	res->filler.set_cell = afc_set_cell;
	res->filler.hint = afc_hint;
	res->size = size;
	res->last.col = last_col;
	res->last.row = last_row;
	res->cells = g_new0 (const GnmCell *, size);

	return &res->filler;
}

/* ------------------------------------------------------------------------- */

/*
 * (base_col,base_row): start of source area.
 * (col_inc,row_inc): direction of fill.
 * count_max: size of source+fill area in direction of fill.
 * region_size: size of source area in direction of fill.
 * (last_col,last_row): last cell of entire area being filled.
 */

static void
sheet_autofill_dir (Sheet *sheet, gboolean singleton,
		    int base_col, int base_row,
		    int region_size,
		    int count_max,
		    int col_inc, int row_inc,
		    int last_col, int last_row)
{
	GList *fillers = NULL;
	GList *f;
	int i;
	AutoFiller *af = NULL;
	GnmStyle **styles;
	GODateConventions const *dateconv =
		workbook_date_conv (sheet->workbook);

	if (count_max <= 0 || region_size <= 0)
		return;

	fillers = g_list_prepend
		(fillers, auto_filler_arithmetic (singleton));
	fillers = g_list_prepend
		(fillers, auto_filler_number_string (singleton, TRUE));
	fillers = g_list_prepend
		(fillers, auto_filler_number_string (singleton, FALSE));
	fillers = g_list_prepend
		(fillers, auto_filler_month (dateconv));
	fillers = g_list_prepend
		(fillers, auto_filler_copy (region_size, last_col, last_row));
	fillers = g_list_prepend
		(fillers, auto_filler_list (month_names_long, 61, TRUE));
	fillers = g_list_prepend
		(fillers, auto_filler_list (month_names_short, 51, TRUE));
	fillers = g_list_prepend
		(fillers, auto_filler_list (month_names_long, 61, FALSE));
	fillers = g_list_prepend
		(fillers, auto_filler_list (month_names_short, 51, FALSE));
	fillers = g_list_prepend
		(fillers, auto_filler_list (weekday_names_long, 60, FALSE));
	fillers = g_list_prepend
		(fillers, auto_filler_list (weekday_names_short, 50, FALSE));

	styles = g_new (GnmStyle *, region_size);

	for (i = 0; i < region_size; i++) {
		int col = base_col + i * col_inc;
		int row = base_row + i * row_inc;
		GnmCell *cell = sheet_cell_get (sheet, col, row);
		GList *f = fillers;

		while (f) {
			AutoFiller *af = f->data;
			GList *next = f->next;

			af->teach_cell (af, cell, i);

			if (af->status == AFS_ERROR) {
				fillers = g_list_delete_link (fillers, f);
				af->finalize (af);
			}

			f = next;
		}

		styles[i] = sheet_style_get (sheet, col, row);
		gnm_style_ref (styles[i]);
	}

	/* Find the best filler that's ready.  */
	for (f = fillers; f; f = f->next) {
		AutoFiller *af1 = f->data;
		if (af1->status == AFS_READY &&
		    (af == NULL || af1->priority > af->priority)) {
			af = af1;
		}
	}

	for (; af && i < count_max; i++) {
		int col = base_col + i * col_inc;
		int row = base_row + i * row_inc;
		int j = i % region_size;
		GnmCell *cell = sheet_cell_fetch (sheet, col, row);
		af->set_cell (af, cell, i);

		gnm_style_ref (styles[j]);
		sheet_style_set_pos (sheet, col, row, styles[j]);
	}

	while (fillers) {
		AutoFiller *af = fillers->data;
		fillers = g_list_delete_link (fillers, fillers);
		af->finalize (af);
	}

	for (i = 0; i < region_size; i++)
		gnm_style_unref (styles[i]);
	g_free (styles);
}

/**
 * sheet_autofill :
 *
 * An internal routine to autofill a region.  It does NOT
 * queue a recalc, flag a status update, or regen spans.
 */
void
sheet_autofill (Sheet *sheet, gboolean singleton,
		int base_col, int base_row,
		int w, int h,
		int end_col, int end_row)
{
	int series;
	int right_col = MAX (base_col, end_col);
	int bottom_row = MAX (base_row, end_row);

	g_return_if_fail (IS_SHEET (sheet));

	if (base_col > end_col || base_row > end_row) {
		if (base_col != end_col + w - 1) {
			/* LEFT */
			for (series = 0; series < h; series++)
				sheet_autofill_dir (sheet, singleton,
					base_col, base_row - series,
					w, ABS (base_col - (end_col - 1)),
					-1, 0,
					right_col, bottom_row);
		} else {
			/* UP */
			for (series = 0; series < w; series++)
				sheet_autofill_dir (sheet, singleton,
					base_col - series, base_row,
					h, ABS (base_row - (end_row - 1)),
					0, -1,
					right_col, bottom_row);
		}
	} else {
		if (end_col != base_col + w - 1) {
			/* RIGHT */
			for (series = 0; series < h; series++)
				sheet_autofill_dir (sheet, singleton,
					base_col, base_row + series,
					w, ABS (base_col - (end_col + 1)),
					1, 0,
					right_col, bottom_row);
		} else {
			/* DOWN */
			for (series = 0; series < w; series++)
				sheet_autofill_dir (sheet, singleton,
					base_col + series, base_row,
					h, ABS (base_row - (end_row + 1)),
					0, 1,
					right_col, bottom_row);
		}
	}
}
