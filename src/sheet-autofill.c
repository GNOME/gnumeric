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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "sheet-autofill.h"

#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "sheet-style.h"
#include "expr.h"
#include "expr-impl.h"
#include "gnm-datetime.h"
#include "mstyle.h"
#include "ranges.h"
#include "sheet-merge.h"
#include "gnm-format.h"
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-format.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */

static char *month_names_long[12 + 1];
static char *month_names_short[12 + 1];
static char *weekday_names_long[7 + 1];
static char *weekday_names_short[7 + 1];
static char *quarters[4 + 1];
static gboolean has_quarters;

void
gnm_autofill_init (void)
{
	GDateMonth m;
	GDateWeekday wd;
	char const *qtemplate;

	for (m = 1; m <= 12; m++) {
		month_names_long[m - 1] = go_date_month_name (m, FALSE);
		month_names_short[m - 1] = go_date_month_name (m, TRUE);
	}
	for (wd = 1; wd <= 7; wd++) {
		weekday_names_long[wd - 1] = go_date_weekday_name (wd, FALSE);
		weekday_names_short[wd - 1] = go_date_weekday_name (wd, TRUE);
	}

	/* xgettext: This is a C format string where %d will be replaced
	   by 1, 2, 3, or 4.  A year will then be appended and we'll get
	   something like 3Q2005.  If that makes no sense in your language,
	   translate to the empty string.  */
	qtemplate = _("%dQ");
	has_quarters = (qtemplate[0] != 0);
	if (has_quarters) {
		int q;
		for (q = 1; q <= 4; q++)
			quarters[q - 1] = g_strdup_printf (qtemplate, q);
	}
}

void
gnm_autofill_shutdown (void)
{
	GDateMonth m;
	GDateWeekday wd;
	int q;

	for (m = 1; m <= 12; m++) {
		g_free (month_names_long[m - 1]);
		g_free (month_names_short[m - 1]);
	}
	for (wd = 1; wd <= 7; wd++) {
		g_free (weekday_names_long[wd - 1]);
		g_free (weekday_names_short[wd - 1]);
	}
	for (q = 1; q <= 4; q++)
		g_free (quarters[q - 1]);
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
	char * (*hint) (AutoFiller *af, GnmCellPos *pos, int n);
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
	GODateConventions const *dateconv;
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
	    gnm_cell_has_expr (cell) ||
	    !VALUE_IS_NUMBER (value) ||
	    VALUE_IS_BOOLEAN (value)) {
		af->status = AFS_ERROR;
		return;
	}

	f = value_get_as_float (value);

	switch (n) {
	case 0:
		afa->dateconv = workbook_date_conv (cell->base.sheet->workbook);
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
		gnm_float step2 = (f - afa->base) / n;
		gnm_float step_sum = gnm_abs (afa->step) + gnm_abs (step2);
		gnm_float err = step_sum
			? gnm_abs (afa->step - step2) / step_sum
			: 0;
		/* Be fairly lenient: */
		if (err > (n + 64) * GNM_EPSILON) {
			af->status = AFS_ERROR;
			return;
		}
	}
	}
}

static GnmValue *
afa_compute (AutoFillerArithmetic *afa, int n)
{
	gnm_float f = afa->base + n * afa->step;
	GnmValue *v = value_new_float (f);
	if (afa->format)
		value_set_fmt (v, afa->format);
	return v;
}

static void
afa_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	GnmValue *v = afa_compute (afa, n);
	gnm_cell_set_value (cell, v);
}

static char *
afa_hint (AutoFiller *af, GnmCellPos *pos, int n)
{
	AutoFillerArithmetic *afa = (AutoFillerArithmetic *)af;
	GnmValue *v = afa_compute (afa, n);
	char *res = format_value (NULL, v, NULL, -1, afa->dateconv);
	value_release (v);
	return res;
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
	res->dateconv = NULL;
	res->singleton = singleton;

	return &res->filler;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	gnm_float base, step;
	GString *prefix, *suffix;
	gboolean fixed_length;
	int base_phase, phases;
	gsize numlen;
	gnm_float p10;
} ArithString;

static void
as_finalize (ArithString *as)
{
	if (as->prefix)
		g_string_free (as->prefix, TRUE);
	if (as->suffix)
		g_string_free (as->suffix, TRUE);
}

static
gboolean
as_check_prefix_suffix (ArithString *as, char const *s, gsize slen)
{
	if (as->prefix) {
		if (slen < as->prefix->len ||
		    memcmp (s, as->prefix->str, as->prefix->len) != 0)
			return TRUE;
		s += as->prefix->len;
		slen -= as->prefix->len;
	}

	if (as->suffix) {
		if (slen < as->suffix->len ||
		    memcmp (s + slen - as->suffix->len,
			    as->suffix->str,
			    as->suffix->len) != 0)
			return TRUE;
	}

	return FALSE;
}

static gnm_float
as_compute_val (ArithString *as, int n)
{
	int pn = (n * as->step + as->base_phase) / as->phases;
	gnm_float f = as->base + pn;
	if (as->fixed_length)
		f = gnm_fmod (f, as->p10);
	return f;
}

static char *
as_compute (ArithString *as, int n)
{
	gnm_float f = as_compute_val (as, n);
	char const *prefix = as->prefix ? as->prefix->str : "";
	char const *suffix = as->suffix ? as->suffix->str : "";

	if (as->fixed_length) {
		return g_strdup_printf ("%s%0*.0" GNM_FORMAT_f "%s",
					prefix,
					(int)as->numlen, f,
					suffix);
	} else {
		return g_strdup_printf ("%s%.0" GNM_FORMAT_f "%s",
					prefix,
					f,
					suffix);
	}
}

static gboolean
as_teach_first (ArithString *as, char const *s)
{
	gsize pl;
	char *end;

	for (pl = 0; s[pl]; pl++) {
		if (g_ascii_isdigit (s[pl]))
			break;
		if (!as->fixed_length &&
		    (s[pl] == '+' || s[pl] == '-') &&
		    g_ascii_isdigit (s[pl + 1]))
			break;
	}
	if (s[pl] == 0)
		return TRUE;

	if (pl > 0) {
		if (as->prefix)
			g_string_append_len (as->prefix, s, pl);
		else
			return TRUE;  /* No prefix allowed.  */
	}
	errno = 0;
	as->base = strtol (s + pl, &end, 10);
	as->step = 1;
	if (errno)
		return TRUE;
	if (*end) {
		if (as->suffix)
			g_string_append (as->suffix, end);
		else
			return TRUE;  /* No suffix allowed.  */
	}

	as->numlen = end - (s + pl);
	as->p10 = gnm_pow10 (as->numlen);

	return FALSE;
}

static gboolean
as_teach_rest (ArithString *as, char const *s, int n, int phase)
{
	gsize slen = strlen (s);
	char *end;
	gnm_float val;
	char const *s2 = s + (as->prefix ? as->prefix->len : 0);

	if (as_check_prefix_suffix (as, s, slen))
		return TRUE;

	if (g_ascii_isspace (*s2))
		return TRUE;

	errno = 0;
	if (as->fixed_length) {
		if (!g_ascii_isdigit (*s2))
			return TRUE;
		val = strtol (s2, &end, 10);
		if (as->numlen != (gsize)(end - s2))
			return TRUE;
	} else {
		/*
		 * Verify no leading zero so the fixed-length
		 * version gets a chance.
		 */
		char const *s3 = s2;
		if (!g_ascii_isdigit (*s3))
			s3++;
		if (s3[0] == '0' && g_ascii_isdigit (s3[1]))
			return TRUE;
		val = strtol (s2, &end, 10);
	}

	if (errno == ERANGE || end != s + slen - (as->suffix ? as->suffix->len : 0))
		return TRUE;

	if (n == 1) {
		as->step = (val - as->base) * as->phases + (phase - as->base_phase);
		if (as->fixed_length && as->step < 0)
			as->step += as->p10 * as->phases;
	} else {
		gnm_float f = as_compute_val (as, n);
		if (gnm_abs (f - val) > 0.5)
			return TRUE;
	}

	return FALSE;
}

/* ------------------------------------------------------------------------- */

/*
 * Arithmetic sequences in strings:
 *
 * "Foo 1", "Foo 2", "Foo 3", ...
 * "1 Bar", "3 Bar", "5 Bar", ...
 * "Fall '99", "Fall '00", "Fall '01", ...
 */

typedef struct {
	AutoFiller filler;

	gboolean singleton;  /* Missing step becomes 1.  */
	ArithString as;
} AutoFillerNumberString;

static void
afns_finalize (AutoFiller *af)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	as_finalize (&afns->as);
	af_finalize (af);
}

static void
afns_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	GnmValue *value = cell ? cell->value : NULL;
	char const *s;

	if (value == NULL ||
	    gnm_cell_has_expr (cell) ||
	    !VALUE_IS_STRING (value)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	s = value_peek_string (value);

	if (n == 0) {
		if (as_teach_first (&afns->as, s))
			goto bad;

		if (afns->singleton)
			af->status = AFS_READY;
	} else {
		if (as_teach_rest (&afns->as, s, n, 0))
			goto bad;

		af->status = AFS_READY;
	}
}

static char *
afns_compute (AutoFillerNumberString *afns, int n)
{
	return as_compute (&afns->as, n);
}

static void
afns_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	char *s = afns_compute (afns, n);
	gnm_cell_set_value (cell, value_new_string_nocopy (s));
}

static char *
afns_hint (AutoFiller *af, GnmCellPos *pos, int n)
{
	AutoFillerNumberString *afns = (AutoFillerNumberString *)af;
	return afns_compute (afns, n);
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
	res->as.fixed_length = fixed_length;
	res->as.prefix = g_string_new (NULL);
	res->as.suffix = g_string_new (NULL);
	res->as.base_phase = 0;
	res->as.phases = 1;

	return &res->filler;
}

/* ------------------------------------------------------------------------- */
/*
 * Month sequences:
 *
 * 1-Jan-2009, 1-Feb-2009, 1-Mar-2009, ...
 * 31-Jan-2009, 28-Feb-2009, 31-Mar-2009, ...
 * 1-Jan-2009, 1-Jan-2010, 1-Jan-2011, ...
 */

typedef struct {
	AutoFiller filler;

	GODateConventions const *dateconv;
	GDate base;
	GOFormat *format;
	int nmonths;
	gboolean end_of_month, same_of_month;
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

	if (value == NULL || gnm_cell_has_expr (cell)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	sf = gnm_cell_get_format (cell);
	if (gnm_format_is_date_for_value (sf, value) != 1)
		goto bad;

	afm->dateconv = workbook_date_conv (cell->base.sheet->workbook);
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

		if (day != g_date_get_day (&afm->base))
			afm->same_of_month = FALSE;

		if (!afm->same_of_month && !afm->end_of_month)
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

static GnmValue *
afm_compute (AutoFillerMonth *afm, int n)
{
	GDate d = afm->base;
	GnmValue *v;

	gnm_date_add_months (&d, n * afm->nmonths);

	if (!g_date_valid (&d) || g_date_get_year (&d) > 9999)
		return NULL;

	if (afm->end_of_month) {
		int year = g_date_get_year (&d);
		int month = g_date_get_month (&d);
		g_date_set_day (&d, g_date_get_days_in_month (month, year));
	}

	v = value_new_int (datetime_g_to_serial (&d, afm->dateconv));
	if (afm->format)
		value_set_fmt (v, afm->format);
	return v;
}

static void
afm_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerMonth *afm = (AutoFillerMonth *)af;
	GnmValue *v = afm_compute (afm, n);

	if (v)
		gnm_cell_set_value (cell, v);
	else {
		GnmEvalPos ep;
		eval_pos_init_cell (&ep, cell);
		gnm_cell_set_value (cell, value_new_error_VALUE (&ep));
	}
}

static char *
afm_hint (AutoFiller *af, GnmCellPos *pos, int n)
{
	AutoFillerMonth *afm = (AutoFillerMonth *)af;
	GnmValue *v = afm_compute (afm, n);
	char *res = NULL;

	if (v) {
		res = format_value (NULL, v, NULL, -1, afm->dateconv);
		value_release (v);
	}

	return res;
}

static AutoFiller *
auto_filler_month (void)
{
	AutoFillerMonth *res = g_new (AutoFillerMonth, 1);

	res->filler.status = AFS_INCOMPLETE;
	res->filler.priority = 200;
	res->filler.finalize = afm_finalize;
	res->filler.teach_cell = afm_teach_cell;
	res->filler.set_cell = afm_set_cell;
	res->filler.hint = afm_hint;
	res->format = NULL;
	res->dateconv = NULL;
	res->end_of_month = TRUE;
	res->same_of_month = TRUE;

	return &res->filler;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	AutoFiller filler;

	char **list;
	gboolean with_number;
	ArithString as;
} AutoFillerList;

static void
afl_finalize (AutoFiller *af)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	as_finalize (&afl->as);
	af_finalize (af);
}

static int
afl_compute_phase (AutoFillerList *afl, int n)
{
	return (int)(n * afl->as.step + afl->as.base_phase) %
		afl->as.phases;
}

static void
afl_teach_cell (AutoFiller *af, const GnmCell *cell, int n)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	GnmValue *value = cell ? cell->value : NULL;
	char const *s;
	gsize elen = 0;
	int ph;

	if (value == NULL ||
	    gnm_cell_has_expr (cell) ||
	    !VALUE_IS_STRING (value)) {
	bad:
		af->status = AFS_ERROR;
		return;
	}

	s = value_peek_string (value);
	for (ph = 0; ph < afl->as.phases; ph++) {
		char const *e = afl->list[ph];
		elen = strlen (e);
		/* This isn't UTF-8 pretty.  */
		/* This isn't case pretty.  */
		/* This won't work if one list item is a prefix of another.  */
		if (strncmp (s, e, elen) == 0)
			break;
	}
	if (ph == afl->as.phases)
		goto bad;

	if (n == 0) {
		afl->as.base_phase = ph;

		if (afl->with_number) {
			afl->as.prefix = g_string_new (NULL);
			afl->as.suffix = g_string_new (NULL);
			if (as_teach_first (&afl->as, s + elen))
				goto bad;
		} else {
			if (s[elen] != 0)
				goto bad;
		}
	} else {
		if (afl->with_number) {
			if (as_teach_rest (&afl->as, s + elen, n, ph))
				goto bad;
		} else {
			if (s[elen] != 0)
				goto bad;

			if (n == 1) {
				int step = ph - afl->as.base_phase;
				if (step == 0)
					goto bad;
				if (step < 0)
					step += afl->as.phases;
				afl->as.step = step;
			} else {
				if (ph != afl_compute_phase (afl, n))
					goto bad;
			}
		}

		af->status = AFS_READY;
	}
}

static char *
afl_compute (AutoFillerList *afl, int n)
{
	GString *res = g_string_new (afl->list[afl_compute_phase (afl, n)]);

	if (afl->with_number) {
		char *s = as_compute (&afl->as, n);
		g_string_append (res, s);
		g_free (s);
	}

	return g_string_free (res, FALSE);
}

static void
afl_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	char *str = afl_compute (afl, n);
	GnmValue *val = value_new_string_nocopy (str);
	gnm_cell_set_value (cell, val);
}

static char *
afl_hint (AutoFiller *af, GnmCellPos *pos, int n)
{
	AutoFillerList *afl = (AutoFillerList *)af;
	return afl_compute (afl, n);
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
	res->with_number = with_number;
	res->as.phases = g_strv_length (list);
	res->as.fixed_length = TRUE;
	res->as.prefix = NULL;
	res->as.suffix = NULL;

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

static char *
afc_set_cell_hint (AutoFiller *af, GnmCell *cell, GnmCellPos const *pos,
		   int n, gboolean doit)
{
	AutoFillerCopy *afe = (AutoFillerCopy *)af;
	GnmCell const *src = afe->cells[n % afe->size];
	char *res = NULL;
	if (src && gnm_cell_has_expr (src)) {
		GnmExprRelocateInfo rinfo;
		GnmExprTop const *texpr;
		GnmExprTop const *src_texpr = src->base.texpr;
		GnmExprArrayCorner const *array =
			gnm_expr_top_get_array_corner (src_texpr);
		Sheet *sheet = src->base.sheet;

		/* Arrays are always assigned fully at the corner.  */
		if (gnm_expr_top_is_array_elem (src_texpr, NULL, NULL))
			return NULL;

		rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
		rinfo.target_sheet = rinfo.origin_sheet = NULL;
		rinfo.col_offset   = rinfo.row_offset = 0;
		rinfo.origin.start = rinfo.origin.end = *pos;
		parse_pos_init (&rinfo.pos, sheet->workbook, sheet,
			pos->col, pos->row);

		texpr = gnm_expr_top_relocate (src_texpr, &rinfo, FALSE);

		/* Clip arrays that are only partially copied.  */
		if (array) {
                        GnmExpr const *aexpr;
			guint limit_x = afe->last.col - pos->col + 1;
			guint limit_y = afe->last.row - pos->row + 1;
                        unsigned cols = MIN (limit_x, array->cols);
                        unsigned rows = MIN (limit_y, array->rows);

                        if (texpr) {
                                aexpr = gnm_expr_copy (texpr->expr->array_corner.expr);
                                gnm_expr_top_unref (texpr);
                        } else
                                aexpr = gnm_expr_copy (array->expr);

			if (doit)
				gnm_cell_set_array_formula
					(cell->base.sheet,
					 pos->col, cell->pos.row,
					 pos->col + (cols - 1),
					 pos->row + (rows - 1),
					 gnm_expr_top_new (aexpr));
			else {
				res = gnm_expr_as_string (aexpr,
					&rinfo.pos, gnm_conventions_default);
				gnm_expr_free (aexpr);
			}
		} else if (texpr) {
			if (doit)
				gnm_cell_set_expr (cell, texpr);
			else
				res = gnm_expr_top_as_string (texpr,
					&rinfo.pos, gnm_conventions_default);
			gnm_expr_top_unref (texpr);
		} else {
			if (doit)
				gnm_cell_set_expr (cell, src_texpr);
			else
				res = gnm_expr_top_as_string (src_texpr,
					&rinfo.pos, gnm_conventions_default);
		}
	} else if (src) {
		if (doit)
			gnm_cell_set_value (cell, value_dup (src->value));
		else {
			Sheet const *sheet = src->base.sheet;
			GODateConventions const *dateconv =
				workbook_date_conv (sheet->workbook);
			GOFormat const *format = gnm_cell_get_format (src);
			return format_value (format, src->value, NULL, -1,
					     dateconv);
		}
	} else {
		if (doit)
			sheet_cell_remove (cell->base.sheet, cell, TRUE, TRUE);
		else
			res = g_strdup (_("(empty)"));
	}

	return res;
}

static void
afc_set_cell (AutoFiller *af, GnmCell *cell, int n)
{
	afc_set_cell_hint (af, cell, &cell->pos, n, TRUE);
}

static char *
afc_hint (AutoFiller *af, GnmCellPos *pos, int n)
{
	return afc_set_cell_hint (af, NULL, pos, n, FALSE);
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
	res->cells = g_new0 (GnmCell const *, size);

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

static char *
sheet_autofill_dir (Sheet *sheet, gboolean singleton,
		    int base_col, int base_row,
		    int region_size,
		    int count_max,
		    int col_inc, int row_inc,
		    int last_col, int last_row,
		    gboolean doit)
{
	GList *fillers = NULL;
	GList *f;
	int i;
	AutoFiller *af = NULL;
	GnmStyle const **styles;
	char *hint = NULL;

	if (count_max <= 0 || region_size <= 0)
		return NULL;

	fillers = g_list_prepend
		(fillers, auto_filler_arithmetic (singleton));
	fillers = g_list_prepend
		(fillers, auto_filler_number_string (singleton, TRUE));
	fillers = g_list_prepend
		(fillers, auto_filler_number_string (singleton, FALSE));
	fillers = g_list_prepend
		(fillers, auto_filler_month ());
	fillers = g_list_prepend
		(fillers, auto_filler_copy (region_size, last_col, last_row));
	fillers = g_list_prepend (fillers, auto_filler_list (quarters, 50, TRUE));

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

	styles = doit ? g_new (GnmStyle const *, region_size) : NULL;

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

		if (styles) {
			styles[i] = sheet_style_get (sheet, col, row);
			gnm_style_ref (styles[i]);
		}
	}

	/* Find the best filler that's ready.  */
	for (f = fillers; f; f = f->next) {
		AutoFiller *af1 = f->data;
		if (af1->status == AFS_READY &&
		    (af == NULL || af1->priority > af->priority)) {
			af = af1;
		}
	}

	if (!af) {
		/* Strange, but no fill.  */
	} else if (doit) {
		for (; i < count_max; i++) {
			int col = base_col + i * col_inc;
			int row = base_row + i * row_inc;
			int j = i % region_size;
			GnmCell *cell = sheet_cell_fetch (sheet, col, row);
			af->set_cell (af, cell, i);

			sheet_style_set_pos (sheet, col, row, gnm_style_dup (styles[j]));
		}
	} else {
		int i = count_max - 1;
		GnmCellPos pos;
		pos.col = base_col + i * col_inc;
		pos.row = base_row + i * row_inc;
		hint = af->hint (af, &pos, i);
	}

	while (fillers) {
		AutoFiller *af = fillers->data;
		fillers = g_list_delete_link (fillers, fillers);
		af->finalize (af);
	}

	if (styles) {
		int i;
		for (i = 0; i < region_size; i++)
			gnm_style_unref (styles[i]);
		g_free (styles);
	}

	return hint;
}

static void
add_item (GString *dst, char *item, char const *sep)
{
	if (!dst) return;
	if (dst->len)
		g_string_append (dst, sep);
	if (item) {
		g_string_append (dst, item);
		g_free (item);
	} else
		g_string_append (dst, "?");
}

static char *
sheet_autofill_internal (Sheet *sheet, gboolean singleton,
			 int base_col, int base_row,
			 int w, int h,
			 int end_col, int end_row,
			 gboolean doit)
{
	int series;
	int right_col = MAX (base_col, end_col);
	int bottom_row = MAX (base_row, end_row);
	GString *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	if (!doit)
		res = g_string_new (NULL);

	if (base_col > end_col || base_row > end_row) {
		if (base_col != end_col + w - 1) {
			/* LEFT */
			for (series = 0; series < h; series++)
				add_item (res,
					  sheet_autofill_dir (sheet, singleton,
							      base_col, base_row - series,
							      w, ABS (base_col - (end_col - 1)),
							      -1, 0,
							      right_col, bottom_row,
							      doit),
					  "\n");
		} else {
			/* UP */
			for (series = 0; series < w; series++)
				add_item (res,
					  sheet_autofill_dir (sheet, singleton,
							      base_col - series, base_row,
							      h, ABS (base_row - (end_row - 1)),
							      0, -1,
							      right_col, bottom_row,
							      doit),
					  " | ");
		}
	} else {
		if (end_col != base_col + w - 1) {
			/* RIGHT */
			for (series = 0; series < h; series++)
				add_item (res,
					  sheet_autofill_dir (sheet, singleton,
							      base_col, base_row + series,
							      w, ABS (base_col - (end_col + 1)),
							      1, 0,
							      right_col, bottom_row,
							      doit),
					  "\n");
		} else {
			/* DOWN */
			for (series = 0; series < w; series++)
				add_item (res,
					  sheet_autofill_dir (sheet, singleton,
							      base_col + series, base_row,
							      h, ABS (base_row - (end_row + 1)),
							      0, 1,
							      right_col, bottom_row,
							      doit),
					  " | ");
		}
	}

	return doit ? NULL : g_string_free (res, FALSE);
}



/**
 * gnm_autofill_fill :
 *
 * An internal routine to autofill a region.  It does NOT
 * queue a recalc, flag a status update, or regen spans.
 */
void
gnm_autofill_fill (Sheet *sheet, gboolean singleton,
		   int base_col, int base_row,
		   int w, int h,
		   int end_col, int end_row)
{
	sheet_autofill_internal (sheet, singleton,
				 base_col, base_row,
				 w, h,
				 end_col, end_row,
				 TRUE);
}

char *
gnm_autofill_hint (Sheet *sheet, gboolean default_increment,
		   int base_col, int base_row,
		   int w,        int h,
		   int end_col,  int end_row)
{
	return sheet_autofill_internal (sheet, default_increment,
					base_col, base_row,
					w, h,
					end_col, end_row,
					FALSE);
}
