/*
 * auto-format.c: Suggest formats for expressions.
 *
 * NOTE: If you were looking for the code to automatically put style on a
 * region, you are in the wrong place.  See the files file-autoft.c and
 * dialogs/dialog-autoformat.c instead.
 *
 * Authors:
 *   Morten Welinder <terra@gnome.org>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <auto-format.h>

#include <compilation.h>
#include <func.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <sheet.h>
#include <workbook.h>
#include <goffice/goffice.h>

/* ------------------------------------------------------------------------- */
/*
 * An important note about correctness.
 *
 * For some functions it is easy to tell what correct behaviour is;
 * if the evaluation of the percent operator yields anything but x/100
 * that is bad.
 *
 * This function is not that simple.
 *
 * If we fail to suggest a format when one might have been deduced, that
 * is really not a big deal.  So the fact that "=date(2000,1,1)^1" is not
 * recognised as a date bothers no-one.
 *
 * If we occasionally suggest a format where none is reasonable, that is
 * not a problem either.  "=pv(1,2,3,4,5)*today()" has no reasonable
 * format, but we assign one.  Tough.
 *
 * On the other hand, if we suggest a bad format for a function that does
 * have a good format, this is bad.  (Since the user will just select
 * another format, it isn't critical, just bad.)
 *
 * Please resist the temptation of making this ridiculously smart.  For
 * example, avoid too much algebra here and don't look at actual numbers
 * encountered.  Let the evaluator do that.  One reason for this is that
 * if you are entering a range of similar data, you really want the same
 * format.  You don't want a different number of decimals for "22%" and
 * "22.5%".
 *
 * (The problem here is actually more a physics problem -- what are the
 * units -- than a math problem.)
 */
/* ------------------------------------------------------------------------- */

#define AF_EXPLICIT ((GnmFuncFlags)(GNM_FUNC_AUTO_MASK + 1))

static GnmFuncFlags do_af_suggest_list (int argc,
					GnmExprConstPtr const *argv,
					GnmEvalPos const *epos,
					GOFormat const **explicit);

struct cb_af_suggest { GnmFuncFlags typ; GOFormat const **explicit; };

static GnmValue *
cb_af_suggest (GnmCellIter const *iter, gpointer user)
{
	struct cb_af_suggest *data = user;

	*(data->explicit) = gnm_cell_get_format (iter->cell);
	if (*(data->explicit)) {
		data->typ = AF_EXPLICIT;
		return VALUE_TERMINATE;
	}
	return NULL;
}

static gboolean
is_date (GnmFuncFlags typ, GOFormat const *explicit)
{
	return (typ == GNM_FUNC_AUTO_DATE ||
		(typ == AF_EXPLICIT && go_format_is_date (explicit)));
}

static GnmFuncFlags
do_af_suggest (GnmExpr const *expr, GnmEvalPos const *epos, GOFormat const **explicit)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_EQUAL:
	case GNM_EXPR_OP_GT:
	case GNM_EXPR_OP_LT:
	case GNM_EXPR_OP_GTE:
	case GNM_EXPR_OP_LTE:
	case GNM_EXPR_OP_NOT_EQUAL:
		return GNM_FUNC_AUTO_UNITLESS;  /* Close enough.  */

	case GNM_EXPR_OP_MULT:
		/* Fall through.  This isn't quite right, but good enough.  */
	case GNM_EXPR_OP_ADD: {
		/* Return the first interesting type we see.  */
		GnmFuncFlags typ;

		typ = do_af_suggest (expr->binary.value_a, epos, explicit);
		if (typ != GNM_FUNC_AUTO_UNKNOWN && typ != GNM_FUNC_AUTO_UNITLESS)
			return typ;

		return do_af_suggest (expr->binary.value_b, epos, explicit);
	}

	case GNM_EXPR_OP_SUB: {
		GnmFuncFlags typ1, typ2;
		GOFormat const *explicit1 = NULL, *explicit2 = NULL;

		typ1 = do_af_suggest (expr->binary.value_a, epos, &explicit1);
		typ2 = do_af_suggest (expr->binary.value_b, epos, &explicit2);

		if (is_date (typ1, explicit1) && is_date (typ2, explicit2))
			return GNM_FUNC_AUTO_UNITLESS;
		else if (typ1 != GNM_FUNC_AUTO_UNKNOWN && typ1 != GNM_FUNC_AUTO_UNITLESS) {
			*explicit = explicit1;
			return typ1;
		} else {
			*explicit = explicit2;
			return typ2;
		}
	}

	case GNM_EXPR_OP_DIV:
		/* Check the left-hand side only.  */
		return do_af_suggest (expr->binary.value_a, epos, explicit);

	case GNM_EXPR_OP_FUNCALL: {
		GnmFuncFlags typ = (gnm_func_get_flags (expr->func.func) &
				    GNM_FUNC_AUTO_MASK);

		switch (typ) {
		case GNM_FUNC_AUTO_FIRST:
			return do_af_suggest_list (expr->func.argc,
						   expr->func.argv,
						   epos, explicit);

		case GNM_FUNC_AUTO_SECOND:
			return do_af_suggest_list (expr->func.argc - 1,
						   expr->func.argv + 1,
						   epos, explicit);

		default:
			return typ;
		}
	}

	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *v = expr->constant.value;

		switch (v->v_any.type) {
		case VALUE_STRING:
		case VALUE_ERROR:
			return GNM_FUNC_AUTO_UNKNOWN;

		case VALUE_CELLRANGE: {
			struct cb_af_suggest closure;

			/* If we don't have a sheet, we cannot look up vars. */
			if (epos->sheet == NULL)
				return GNM_FUNC_AUTO_UNKNOWN;

			closure.typ = GNM_FUNC_AUTO_UNKNOWN;
			closure.explicit = explicit;
			workbook_foreach_cell_in_range (epos, v,
				CELL_ITER_IGNORE_BLANK,
				&cb_af_suggest, &closure);
			return closure.typ;
		}

		default:
			return GNM_FUNC_AUTO_UNITLESS;
		}
	}

	case GNM_EXPR_OP_CELLREF: {
		Sheet const *sheet;
		GnmCellRef const *ref;
		GnmCell const *cell;
		GnmCellPos pos;

		ref = &expr->cellref.ref;
		sheet = eval_sheet (ref->sheet, epos->sheet);
		/* If we don't have a sheet, we cannot look up vars.  */
		if (sheet == NULL)
			return GNM_FUNC_AUTO_UNKNOWN;

		gnm_cellpos_init_cellref (&pos, ref, &epos->eval, sheet);
		cell = sheet_cell_get (sheet, pos.col, pos.row);
		if (cell == NULL)
			return GNM_FUNC_AUTO_UNKNOWN;

		*explicit = gnm_cell_get_format (cell);
		return *explicit ? AF_EXPLICIT : GNM_FUNC_AUTO_UNKNOWN;
	}

	case GNM_EXPR_OP_PAREN:
	case GNM_EXPR_OP_UNARY_NEG:
	case GNM_EXPR_OP_UNARY_PLUS:
		return do_af_suggest (expr->unary.value, epos, explicit);

	case GNM_EXPR_OP_PERCENTAGE:
		return GNM_FUNC_AUTO_PERCENT;

	case GNM_EXPR_OP_EXP:
	case GNM_EXPR_OP_CAT:
	case GNM_EXPR_OP_NAME:
	case GNM_EXPR_OP_ARRAY_CORNER:
	case GNM_EXPR_OP_ARRAY_ELEM:
	default:
		return GNM_FUNC_AUTO_UNKNOWN;
	}
}

static GnmFuncFlags
do_af_suggest_list (int argc, GnmExprConstPtr const *argv,
		    GnmEvalPos const *epos, GOFormat const **explicit)
{
	int i;

	for (i = 0; i < argc; i++) {
		GnmFuncFlags typ = do_af_suggest (argv[i], epos, explicit);
		if (typ != GNM_FUNC_AUTO_UNKNOWN &&
		    typ != GNM_FUNC_AUTO_UNITLESS)
			return typ;
	}

	return GNM_FUNC_AUTO_UNKNOWN;
}

/* ------------------------------------------------------------------------- */

GNM_BEGIN_KILL_SWITCH_WARNING

/**
 * gnm_auto_style_format_suggest:
 * @texpr: A #GnmExprTop
 * @epos: A #GnmEvalPos
 *
 * Suggest a format for @texpr.
 *
 * Returns: (transfer full) (nullable): Suggested format.
 */
GOFormat const *
gnm_auto_style_format_suggest (GnmExprTop const *texpr, GnmEvalPos const *epos)
{
	GOFormat const *explicit = NULL;

	g_return_val_if_fail (texpr != NULL, NULL);
	g_return_val_if_fail (epos != NULL, NULL);

	switch (do_af_suggest (texpr->expr, epos, &explicit)) {
	case AF_EXPLICIT:
		break;

	case GNM_FUNC_AUTO_DATE: /* FIXME: any better idea?  */
		explicit = go_format_default_date ();
		break;

	case GNM_FUNC_AUTO_TIME: /* FIXME: any better idea?  */
		explicit = go_format_default_time ();
		break;

	case GNM_FUNC_AUTO_PERCENT: /* FIXME: any better idea?  */
		explicit = go_format_default_percentage ();
		break;

	case GNM_FUNC_AUTO_MONETARY: /* FIXME: any better idea?  */
		explicit = go_format_default_money ();
		break;

	case GNM_FUNC_AUTO_FIRST:
	case GNM_FUNC_AUTO_SECOND:
		g_assert_not_reached ();

	default:
		explicit = NULL;
	}

	if (explicit)
		go_format_ref (explicit);

	return explicit;
}

GNM_END_KILL_SWITCH_WARNING
