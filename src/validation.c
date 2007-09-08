/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * validation.c: Implementation of validation.
 *
 * Copyright (C) Jody Goldberg <jody@gnome.org>
 *
 * based on work by
 *	 Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "validation.h"
#include "validation-combo.h"

#include "numbers.h"
#include "expr.h"
#include "mstyle.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "workbook-control.h"
#include "parse-util.h"

#include "sheet-view.h"
#include "sheet-object.h"
#include "gnm-validation-combo-foo-view.h"
#include "gnm-cell-combo-foo-view.h"
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>

static const struct {
	gboolean errors_not_allowed;
	gboolean strings_not_allowed;
	gboolean bool_always_ok;
} typeinfo[] = {
	{ FALSE, FALSE, TRUE },		/* ANY */
	{ TRUE,  TRUE,  TRUE },		/* AS_INT */
	{ TRUE,  TRUE,  TRUE },		/* AS_NUMBER */
	{ TRUE,  FALSE, FALSE },	/* IN_LIST */
	{ TRUE,  TRUE,  TRUE },		/* AS_DATE */
	{ TRUE,  TRUE,  TRUE },		/* AS_TIME */
	{ TRUE,  FALSE, FALSE },	/* TEXT_LENGTH */
	{ FALSE, FALSE, FALSE }		/* CUSTOM */
};

#define NONE (GnmExprOp)-1

static struct {
	int nops;
	GnmExprOp ops[2];
	int ntrue;
	char const *name;
} const opinfo[] = {
	/* Note: no entry for VALIDATION_OP_NONE */
	{ 2, { GNM_EXPR_OP_GTE,       GNM_EXPR_OP_LTE }, 2, N_("Between") },
	{ 2, { GNM_EXPR_OP_LT,        GNM_EXPR_OP_GT  }, 1, N_("Not_Between") },
	{ 1, { GNM_EXPR_OP_EQUAL,     NONE            }, 1, N_("Equal") },
	{ 1, { GNM_EXPR_OP_NOT_EQUAL, NONE            }, 1, N_("Not Equal") },
	{ 1, { GNM_EXPR_OP_GT,        NONE            }, 1, N_("Greater Than") },
	{ 1, { GNM_EXPR_OP_LT,        NONE            }, 1, N_("Less Than") },
	{ 1, { GNM_EXPR_OP_GTE,       NONE            }, 1, N_("Greater than or Equal") },
	{ 1, { GNM_EXPR_OP_LTE,       NONE            }, 1, N_("Less than or Equal") },
};

#undef NONE

/***************************************************************************/

static void
gnm_validation_combo_finalize (GObject *object)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (object);
	GObjectClass *parent;
	if (NULL != vcombo->validation) {
		validation_unref (vcombo->validation);
		vcombo->validation = NULL;
	}
	if (NULL != vcombo->sv) {
		sv_weak_unref (&vcombo->sv);
		vcombo->sv = NULL;
	}
	parent = g_type_class_peek (SHEET_OBJECT_TYPE);
	parent->finalize (object);
}

static void
gnm_validation_combo_init (SheetObject *so)
{
	/* keep the arrows from wandering with their cells */
	so->flags &= ~SHEET_OBJECT_MOVE_WITH_CELLS;
}
static SheetObjectView *
gnm_validation_combo_foo_view_new (SheetObject *so, SheetObjectViewContainer *container)
{
	return gnm_cell_combo_foo_view_new (so,
		gnm_validation_combo_foo_view_get_type (), container);
}
static void
gnm_validation_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (gobject_class);
	gobject_class->finalize	= gnm_validation_combo_finalize;
	so_class->new_view	  = gnm_validation_combo_foo_view_new;
	so_class->read_xml_dom	  = NULL;
	so_class->write_xml_sax	  = NULL;
	so_class->prep_sax_parser = NULL;
	so_class->copy            = NULL;
}

typedef SheetObjectClass GnmValidationComboClass;
GSF_CLASS (GnmValidationCombo, gnm_validation_combo,
	   gnm_validation_combo_class_init, gnm_validation_combo_init,
	   SHEET_OBJECT_TYPE);

SheetObject *
gnm_validation_combo_new (GnmValidation const *val, SheetView *sv)
{
	GnmValidationCombo *vcombo;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sv  != NULL, NULL);

	vcombo = g_object_new (GNM_VALIDATION_COMBO_TYPE, NULL);
	validation_ref (vcombo->validation = val);
	sv_weak_ref (vcombo->sv = sv, &vcombo->sv);
	return SHEET_OBJECT (vcombo);
}

/***************************************************************************/

/**
 * validation_new :
 * @title : will be copied.
 * @msg   : will be copied.
 * @texpr0 : absorb the reference to the expression (optionally %NULL).
 * @texpr1 : absorb the reference to the expression (optionally %NULL).
 *
 * Does _NOT_ require all necessary information to be set here.
 * validation_set_expr can be used to change the expressions after creation,
 * and validation_is_ok can be used to ensure that things are properly setup.
 *
 * Returns a new @GnmValidation object that needs to be unrefed.
 **/
GnmValidation *
validation_new (ValidationStyle style,
		ValidationType  type,
		ValidationOp    op,
		char const *title, char const *msg,
		GnmExprTop const *texpr0, GnmExprTop const *texpr1,
		gboolean allow_blank, gboolean use_dropdown)
{
	GnmValidation *v;
	int nops, i;

	g_return_val_if_fail (type >= 0, NULL);
	g_return_val_if_fail (type < G_N_ELEMENTS (typeinfo), NULL);
	g_return_val_if_fail (op >= VALIDATION_OP_NONE, NULL);
	g_return_val_if_fail (op < (int)G_N_ELEMENTS (opinfo), NULL);

	switch (type) {
	case VALIDATION_TYPE_CUSTOM:
	case VALIDATION_TYPE_IN_LIST:
		nops = 1;
		if (op != VALIDATION_OP_NONE) {
			/*
			 * This can happen if an .xls file was saved
			 * as a .gnumeric.
			 */
			op = VALIDATION_OP_NONE;
		}
		break;
	case VALIDATION_TYPE_ANY:
		nops = 0;
		break;
	default:
		nops = (op == VALIDATION_OP_NONE) ? 0 : opinfo[op].nops;
	}

	v = g_new0 (GnmValidation, 1);
	v->ref_count = 1;
	v->title = title && title[0] ? gnm_string_get (title) : NULL;
	v->msg = msg && msg[0] ? gnm_string_get (msg) : NULL;
	v->texpr[0] = texpr0;
	v->texpr[1] = texpr1;
	v->style = style;
	v->type = type;
	v->op = op;
	v->allow_blank = (allow_blank != FALSE);
	v->use_dropdown = (use_dropdown != FALSE);

	/* Clear excess expressions.  */
	for (i = nops; i < 2; i++)
		if (v->texpr[i]) {
			gnm_expr_top_unref (v->texpr[i]);
			v->texpr[i] = NULL;
		}

	return v;
}

void
validation_ref (GnmValidation const *v)
{
	g_return_if_fail (v != NULL);
	((GnmValidation *)v)->ref_count++;
}

void
validation_unref (GnmValidation const *val)
{
	GnmValidation *v = (GnmValidation *)val;

	g_return_if_fail (v != NULL);

	v->ref_count--;

	if (v->ref_count < 1) {
		int i;

		if (v->title != NULL) {
			gnm_string_unref (v->title);
			v->title = NULL;
		}
		if (v->msg != NULL) {
			gnm_string_unref (v->msg);
			v->msg = NULL;
		}
		for (i = 0 ; i < 2 ; i++)
			if (v->texpr[i] != NULL) {
				gnm_expr_top_unref (v->texpr[i]);
				v->texpr[i] = NULL;
			}
		g_free (v);
	}
}

/**
 * validation_set_expr :
 * @v : #GnmValidation
 * @texpr : #GnmExprTop
 * @indx : 0 or 1
 *
 * Assign an expression to a validation.  validation_is_ok can be used to
 * verify that @v has all of the requisit information.
 **/
void
validation_set_expr (GnmValidation *v,
		     GnmExprTop const *texpr, unsigned indx)
{
	g_return_if_fail (indx <= 1);

	if (NULL != texpr)
		gnm_expr_top_ref (texpr);
	if (NULL != v->texpr[indx])
		gnm_expr_top_unref (v->texpr[indx]);
	v->texpr[indx] = texpr;
}

GError *
validation_is_ok (GnmValidation const *v)
{
	unsigned nops, i;

	switch (v->type) {
	case VALIDATION_TYPE_CUSTOM:
	case VALIDATION_TYPE_IN_LIST:	nops = 1; break;
	case VALIDATION_TYPE_ANY:	nops = 0; break;
	default: nops = (v->op == VALIDATION_OP_NONE) ? 0 : opinfo[v->op].nops;
	}

	for (i = 0 ; i < 2 ; i++)
		if (v->texpr[i] == NULL) {
			if (i < nops)
				return g_error_new (1, 0, N_("Missing formula for validation"));
		} else {
			if (i >= nops)
				return g_error_new (1, 0, N_("Extra formula for validation"));
		}

	return NULL;
}

static ValidationStatus
validation_barf (WorkbookControl *wbc, GnmValidation const *gv, char *def_msg)
{
	char const *msg = gv->msg ? gv->msg->str : def_msg;
	char const *title = gv->title ? gv->title->str : _("Gnumeric: Validation");
	ValidationStatus result = wb_control_validation_msg (wbc,
		gv->style, title, msg);
	g_free (def_msg);
	return result;
}

static GnmValue *
cb_validate_custom (GnmValueIter const *v_iter, GnmValue const *target)
{
	if (value_compare (v_iter->v, target, FALSE) == IS_EQUAL)
		return VALUE_TERMINATE;
	else
		return NULL;
}

#define BARF(msg)					\
  do {							\
	if (showed_dialog) *showed_dialog = TRUE;	\
	return validation_barf (wbc, v, msg);		\
  } while (0)

/**
 * validation_eval:
 * @wbc :
 * @mstyle :
 * @sheet :
 *
 * validation set in the GnmStyle if applicable.
 **/
ValidationStatus
validation_eval (WorkbookControl *wbc, GnmStyle const *mstyle,
		 Sheet *sheet, GnmCellPos const *pos, gboolean *showed_dialog)
{
	GnmValidation const *v;
	GnmCell *cell;
	GnmValue *val;
	gnm_float x;
	int nok, i;
	GnmEvalPos ep;

	if (showed_dialog) *showed_dialog = FALSE;

	v = gnm_style_get_validation (mstyle);
	if (v == NULL)
		return VALIDATION_STATUS_VALID;

	if (v->type == VALIDATION_TYPE_ANY)
		return VALIDATION_STATUS_VALID;

	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell != NULL)
		gnm_cell_eval (cell);

	if (gnm_cell_is_empty (cell)) {
		if (v->allow_blank)
			return VALIDATION_STATUS_VALID;
		BARF (g_strdup_printf (_("Cell %s is not permitted to be blank"),
				       cell_name (cell)));
	}

	val = cell->value;
	switch (val->type) {
	case VALUE_ERROR:
		if (typeinfo[v->type].errors_not_allowed)
			BARF (g_strdup_printf (_("Cell %s is not permitted to contain error values"),
					       cell_name (cell)));
		break;

	case VALUE_BOOLEAN:
		if (typeinfo[v->type].bool_always_ok)
			return VALIDATION_STATUS_VALID;
		break;

	case VALUE_STRING:
		if (typeinfo[v->type].strings_not_allowed)
			BARF (g_strdup_printf (_("Cell %s is not permitted to contain strings"),
					       cell_name (cell)));
		break;

	default:
		break;
	}

	eval_pos_init_cell (&ep, cell);

	switch (v->type) {
	case VALIDATION_TYPE_AS_INT:
		x = value_get_as_float (val);
		if (gnm_fake_floor (x) == gnm_fake_ceil (x))
			break;
		else
			BARF (g_strdup_printf (_("'%s' is not an integer"),
					       value_peek_string (val)));

	case VALIDATION_TYPE_AS_NUMBER:
		x = value_get_as_float (val);
		break;

	case VALIDATION_TYPE_AS_DATE: /* What the hell does this do?  */
		x = value_get_as_float (val);
		if (x < 0)
			BARF (g_strdup_printf (_("'%s' is not a valid date"),
					       value_peek_string (val)));
		break;


	case VALIDATION_TYPE_AS_TIME: /* What the hell does this do?  */
		x = value_get_as_float (val);
		break;

	case VALIDATION_TYPE_IN_LIST:
		if (NULL != v->texpr[0]) {
			GnmValue *list = gnm_expr_top_eval (v->texpr[0], &ep,
				 GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
			GnmValue *res = value_area_foreach (list, &ep, CELL_ITER_IGNORE_BLANK,
				 (GnmValueIterFunc) cb_validate_custom, val);
			value_release (list);
			if (res == NULL) {
				GnmParsePos pp;
				char *expr_str = gnm_expr_top_as_string
					(v->texpr[0],
					 parse_pos_init_evalpos (&pp, &ep),
					 gnm_conventions_default);
				char *msg = g_strdup_printf (_("%s does not contain the new value."), expr_str);
				g_free (expr_str);
				BARF (msg);
			}
		}
		return VALIDATION_STATUS_VALID;

	case VALIDATION_TYPE_TEXT_LENGTH:
		/* XL appears to use a very basic value->string mapping that
		 * ignores formatting.
		 * eg len (12/13/01) == len (37238) = 5
		 * This seems wrong for
		 */
		x = g_utf8_strlen (value_peek_string (val), -1);
		break;

	case VALIDATION_TYPE_CUSTOM: {
		gboolean valid;

		if (v->texpr[0] == NULL)
			return VALIDATION_STATUS_VALID;

		val = gnm_expr_top_eval (v->texpr[0], &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		valid = value_get_as_bool (val, NULL);
		value_release (val);

		if (valid)
			return VALIDATION_STATUS_VALID;
		else {
			GnmParsePos pp;
			char *expr_str = gnm_expr_top_as_string
				(v->texpr[0],
				 parse_pos_init_evalpos (&pp, &ep),
				 gnm_conventions_default);
			char *msg = g_strdup_printf (_("%s is not true."), expr_str);
			g_free (expr_str);
			BARF (msg);
		}
	}

	default:
		g_assert_not_reached ();
		return VALIDATION_STATUS_VALID;
	}

	if (v->op == VALIDATION_OP_NONE)
		return VALIDATION_STATUS_VALID;

	nok = 0;
	for (i = 0; i < opinfo[v->op].nops; i++) {
		GnmExprTop const *texpr = v->texpr[i];
		GnmExpr const *expr;
		GnmValue *cres;

		if (!texpr) {
			nok++;
			continue;
		}

		expr = gnm_expr_new_binary
			(gnm_expr_new_constant (value_new_float (x)),
			 opinfo[v->op].ops[i],
			 gnm_expr_copy (texpr->expr));
		cres = gnm_expr_eval (expr, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		if (value_get_as_bool (cres, NULL))
			nok++;
		value_release (cres);
		gnm_expr_free (expr);
	}

	if (nok < opinfo[v->op].ntrue)
		BARF (g_strdup_printf (_("%s is out of permitted range"),
				       value_peek_string (val)));

	return VALIDATION_STATUS_VALID;
}

#undef BARF
