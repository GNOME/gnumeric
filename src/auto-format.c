/*
 * auto-format.c: Suggest formats for expressions.
 *
 * NOTE: If you were looking for the code to automatically put style on a
 * region, you are in the wrong place.  See the files file-autoft.c and
 * dialogs/dialog-autoformat.c instead.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "auto-format.h"

#include "gutils.h"
#include "formats.h"
#include "expr.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "format.h"

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

static GHashTable *auto_format_function_hash;

void
auto_format_init (void)
{
	auto_format_function_hash =
		g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);
}

static void
cb_free_name (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
}

void
auto_format_shutdown (void)
{
	g_hash_table_foreach (auto_format_function_hash,
			      cb_free_name,
			      NULL);
	g_hash_table_destroy (auto_format_function_hash);
	auto_format_function_hash = NULL;
}

/* ------------------------------------------------------------------------- */

void
auto_format_function_result (FunctionDefinition *fd, AutoFormatTypes res)
{
	const char *name;

	g_return_if_fail (fd != NULL);
	g_return_if_fail (res != AF_UNKNOWN);
	g_return_if_fail (res != AF_EXPLICIT);

	name = function_def_get_name (fd);
	g_hash_table_insert (auto_format_function_hash,
			     g_strdup (name), GINT_TO_POINTER (res));
}

/* ------------------------------------------------------------------------- */

static AutoFormatTypes do_af_suggest_list (ExprList *list,
					   EvalPos const *epos,
					   StyleFormat **explicit);

struct cb_af_suggest { AutoFormatTypes typ; StyleFormat **explicit; };

static Value *
cb_af_suggest (Sheet *sheet, int col, int row, Cell *cell, void *_data)
{
	struct cb_af_suggest *data = _data;

	*(data->explicit) = cell_get_format (cell);
	if (*(data->explicit)) {
		data->typ = AF_EXPLICIT;
		return value_terminate ();
	}
	return NULL;
}

static AutoFormatTypes
do_af_suggest (ExprTree const *expr, const EvalPos *epos, StyleFormat **explicit)
{
	switch (expr->any.oper) {
	case OPER_EQUAL:
	case OPER_GT:
	case OPER_LT:
	case OPER_GTE:
	case OPER_LTE:
	case OPER_NOT_EQUAL:
		return AF_UNITLESS;  /* Close enough.  */

	case OPER_MULT:
		/* Fall through.  This isn't quite right, but good enough.  */
	case OPER_ADD: {
		/* Return the first interesting type we see.  */
		AutoFormatTypes typ;

		typ = do_af_suggest (expr->binary.value_a, epos, explicit);
		if (typ != AF_UNKNOWN && typ != AF_UNITLESS)
			return typ;

		return do_af_suggest (expr->binary.value_b, epos, explicit);
	}

	case OPER_SUB: {
		AutoFormatTypes typ1, typ2;
		StyleFormat *explicit1 = NULL, *explicit2 = NULL;

		typ1 = do_af_suggest (expr->binary.value_a, epos, &explicit1);
		typ2 = do_af_suggest (expr->binary.value_b, epos, &explicit2);

		if (typ1 == AF_DATE && typ2 == AF_DATE)
			return AF_UNITLESS;
		else if (typ1 != AF_UNKNOWN && typ1 != AF_UNITLESS) {
			*explicit = explicit1;
			return typ1;
		} else {
			*explicit = explicit2;
			return typ2;
		}
	}

	case OPER_DIV:
		/* Check the left-hand side only.  */
		return do_af_suggest (expr->binary.value_a, epos, explicit);

	case OPER_FUNCALL: {
		AutoFormatTypes typ;
		const char *name;

		name = function_def_get_name (expr->func.func);
		typ = (AutoFormatTypes)
			GPOINTER_TO_INT
			(g_hash_table_lookup (auto_format_function_hash, name));

		switch (typ) {
		case AF_FIRST_ARG_FORMAT:
			return do_af_suggest_list (expr->func.arg_list,
						   epos, explicit);

		case AF_FIRST_ARG_FORMAT2: {
			ExprList *l;
			l = expr->func.arg_list;
			if (l) l = l->next;
			return do_af_suggest_list (l, epos, explicit);
		}

		default:
			return typ;
		}
	}

	case OPER_CONSTANT: {
		const Value *v = expr->constant.value;

		switch (v->type) {
		case VALUE_STRING:
		case VALUE_ERROR:
		case VALUE_ARRAY:
			return AF_UNKNOWN;

		case VALUE_CELLRANGE: {
			struct cb_af_suggest closure;

			/* If we don't have a sheet, we cannot look up vars. */
			if (epos->sheet == NULL)
				return AF_UNKNOWN;

			closure.typ = AF_UNKNOWN;
			closure.explicit = explicit;
			workbook_foreach_cell_in_range (epos, v, TRUE,
							&cb_af_suggest,
							&closure);
			return closure.typ;
		}

		default:
			return AF_UNITLESS;
		}
	}

	case OPER_VAR: {
		Sheet const *sheet;
		CellRef const *ref;
		Cell const *cell;
		CellPos pos;

		ref = &expr->var.ref;
		sheet = eval_sheet (ref->sheet, epos->sheet);
		/* If we don't have a sheet, we cannot look up vars.  */
		if (sheet == NULL)
			return AF_UNKNOWN;

		cellref_get_abs_pos (ref, &epos->eval, &pos);
		cell = sheet_cell_get (sheet, pos.col, pos.row);
		if (cell == NULL)
			return AF_UNKNOWN;

		*explicit = cell_get_format (cell);
		return *explicit ? AF_EXPLICIT : AF_UNKNOWN;
	}

	case OPER_UNARY_NEG:
	case OPER_UNARY_PLUS:
		return do_af_suggest (expr->unary.value, epos, explicit);

	case OPER_PERCENT:
		return AF_PERCENT;

	case OPER_EXP:
	case OPER_CONCAT:
	case OPER_NAME:
	case OPER_ARRAY:
	default:
		return AF_UNKNOWN;
	}
}

static AutoFormatTypes
do_af_suggest_list (ExprList *list, const EvalPos *epos, StyleFormat **explicit)
{
	AutoFormatTypes typ = AF_UNKNOWN;
	while (list && (typ == AF_UNKNOWN || typ == AF_UNITLESS)) {
		typ = do_af_suggest (list->data, epos, explicit);
		list = list->next;
	}
	return typ;
}

/* ------------------------------------------------------------------------- */

static char *
auto_format_suggest (ExprTree const *expr, EvalPos const *epos)
{
	StyleFormat *explicit = NULL;

	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (epos != NULL, NULL);

	switch (do_af_suggest (expr, epos, &explicit)) {
	case AF_EXPLICIT:
		break;

	case AF_DATE: /* FIXME: any better idea?  */
		explicit = style_format_default_date ();
		break;

	case AF_TIME: /* FIXME: any better idea?  */
		explicit = style_format_default_time ();
		break;

	case AF_PERCENT: /* FIXME: any better idea?  */
		explicit = style_format_default_percentage ();
		break;

	case AF_MONETARY: /* FIXME: any better idea?  */
		explicit = style_format_default_money ();
		break;

	case AF_FIRST_ARG_FORMAT:
		g_assert_not_reached ();

	default:
		explicit = NULL;
	}

	return explicit;
}

/* ------------------------------------------------------------------------- */
/* This is just a StyleFormat version of the above.  Eventually, this needs  */
/* to be the primitive and the above should go away.                         */

StyleFormat *
auto_style_format_suggest (const ExprTree *expr, const EvalPos *epos)
{
	char *format;
	StyleFormat *result = NULL;
	format = auto_format_suggest (expr, epos);
	if (format) {
		result = style_format_new_XL (format, FALSE);
		g_free (format);
	}
	return result;
}

/* ------------------------------------------------------------------------- */
