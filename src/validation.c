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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <validation.h>
#include <validation-combo.h>

#include <numbers.h>
#include <expr.h>
#include <mstyle.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <workbook.h>
#include <workbook-control.h>
#include <parse-util.h>

#include <sheet-view.h>
#include <sheet-object.h>
#include <sheet-style.h>
#include <widgets/gnm-validation-combo-view.h>
#include <widgets/gnm-cell-combo-view.h>
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
	/* Note: no entry for GNM_VALIDATION_OP_NONE */
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

static GObjectClass *gvc_parent_klass;

static void
gnm_validation_combo_finalize (GObject *object)
{
	GnmValidationCombo *vcombo = GNM_VALIDATION_COMBO (object);

	if (NULL != vcombo->validation) {
		gnm_validation_unref (vcombo->validation);
		vcombo->validation = NULL;
	}

	gvc_parent_klass->finalize (object);
}

static void
gnm_validation_combo_init (G_GNUC_UNUSED SheetObject *so)
{
}

static SheetObjectView *
gnm_validation_combo_view_new (SheetObject *so, SheetObjectViewContainer *container)
{
	return gnm_cell_combo_view_new (so,
		gnm_validation_combo_view_get_type (), container);
}

static void
gnm_validation_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);
	gobject_class->finalize	= gnm_validation_combo_finalize;
	so_class->new_view = gnm_validation_combo_view_new;

	gvc_parent_klass = g_type_class_peek_parent (gobject_class);
}

typedef SheetObjectClass GnmValidationComboClass;
GSF_CLASS (GnmValidationCombo, gnm_validation_combo,
	   gnm_validation_combo_class_init, gnm_validation_combo_init,
	   gnm_cell_combo_get_type ())

SheetObject *
gnm_validation_combo_new (GnmValidation const *val, SheetView *sv)
{
	GnmValidationCombo *vcombo;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sv  != NULL, NULL);

	vcombo = g_object_new (GNM_VALIDATION_COMBO_TYPE, "sheet-view", sv, NULL);
	gnm_validation_ref (vcombo->validation = val);
	return GNM_SO (vcombo);
}

/***************************************************************************/

GType
gnm_validation_style_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_VALIDATION_STYLE_NONE,
			  "GNM_VALIDATION_STYLE_NONE", "none"},
			{ GNM_VALIDATION_STYLE_STOP,
			  "GNM_VALIDATION_STYLE_STOP", "stop"},
			{ GNM_VALIDATION_STYLE_WARNING,
			  "GNM_VALIDATION_STYLE_WARNING", "warning"},
			{ GNM_VALIDATION_STYLE_INFO,
			  "GNM_VALIDATION_STYLE_INFO", "info"},
			{ GNM_VALIDATION_STYLE_PARSE_ERROR,
			  "GNM_VALIDATION_STYLE_PARSE_ERROR", "parse-error"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmValidationStyle",
						values);
	}
	return etype;
}

GType
gnm_validation_type_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_VALIDATION_TYPE_ANY,
			  "GNM_VALIDATION_TYPE_ANY", "any"},
			{ GNM_VALIDATION_TYPE_AS_INT,
			  "GNM_VALIDATION_TYPE_AS_INT", "int"},
			{ GNM_VALIDATION_TYPE_AS_NUMBER,
			  "GNM_VALIDATION_TYPE_AS_NUMBER", "number"},
			{ GNM_VALIDATION_TYPE_IN_LIST,
			  "GNM_VALIDATION_TYPE_IN_LIST", "list"},
			{ GNM_VALIDATION_TYPE_AS_DATE,
			  "GNM_VALIDATION_TYPE_AS_DATE", "date"},
			{ GNM_VALIDATION_TYPE_AS_TIME,
			  "GNM_VALIDATION_TYPE_AS_TIME", "time"},
			{ GNM_VALIDATION_TYPE_TEXT_LENGTH,
			  "GNM_VALIDATION_TYPE_TEXT_LENGTH", "length"},
			{ GNM_VALIDATION_TYPE_CUSTOM,
			  "GNM_VALIDATION_TYPE_CUSTOM", "custom"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmValidationType",
						values);
	}
	return etype;
}

GType
gnm_validation_op_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_VALIDATION_OP_NONE,
			  "GNM_VALIDATION_OP_NONE", "none"},
			{ GNM_VALIDATION_OP_BETWEEN,
			  "GNM_VALIDATION_OP_BETWEEN", "between"},
			{ GNM_VALIDATION_OP_NOT_BETWEEN,
			  "GNM_VALIDATION_OP_NOT_BETWEEN", "not-between"},
			{ GNM_VALIDATION_OP_EQUAL,
			  "GNM_VALIDATION_OP_EQUAL", "equal"},
			{ GNM_VALIDATION_OP_NOT_EQUAL,
			  "GNM_VALIDATION_OP_NOT_EQUAL", "not-equal"},
			{ GNM_VALIDATION_OP_GT,
			  "GNM_VALIDATION_OP_GT", "gt"},
			{ GNM_VALIDATION_OP_LT,
			  "GNM_VALIDATION_OP_LT", "lt"},
			{ GNM_VALIDATION_OP_GTE,
			  "GNM_VALIDATION_OP_GTE", "gte"},
			{ GNM_VALIDATION_OP_LTE,
			  "GNM_VALIDATION_OP_LTE", "lte"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmValidationOp",
						values);
	}
	return etype;
}


/***************************************************************************/

/**
 * gnm_validation_new:
 * @style: #ValidationStyle
 * @op: #ValidationOp
 * @sheet: #Sheet
 * @title: will be copied.
 * @msg: will be copied.
 * @texpr0: (transfer full) (nullable): first expression
 * @texpr1: (transfer full) (nullable): second expression
 * @allow_blank:
 * @use_dropdown:
 *
 * Does _NOT_ require all necessary information to be set here.
 * gnm_validation_set_expr can be used to change the expressions after creation,
 * and gnm_validation_is_ok can be used to ensure that things are properly
 * setup.
 *
 * Returns: (transfer full): a new @GnmValidation object
 **/
GnmValidation *
gnm_validation_new (ValidationStyle style,
		    ValidationType type,
		    ValidationOp op,
		    Sheet *sheet,
		    char const *title, char const *msg,
		    GnmExprTop const *texpr0, GnmExprTop const *texpr1,
		    gboolean allow_blank, gboolean use_dropdown)
{
	GnmValidation *v;
	int nops;

	g_return_val_if_fail ((size_t)type < G_N_ELEMENTS (typeinfo), NULL);
	g_return_val_if_fail (op >= GNM_VALIDATION_OP_NONE, NULL);
	g_return_val_if_fail (op < (int)G_N_ELEMENTS (opinfo), NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	switch (type) {
	case GNM_VALIDATION_TYPE_CUSTOM:
	case GNM_VALIDATION_TYPE_IN_LIST:
		nops = 1;
		if (op != GNM_VALIDATION_OP_NONE) {
			/*
			 * This can happen if an .xls file was saved
			 * as a .gnumeric.
			 */
			op = GNM_VALIDATION_OP_NONE;
		}
		break;
	case GNM_VALIDATION_TYPE_ANY:
		nops = 0;
		break;
	default:
		nops = (op == GNM_VALIDATION_OP_NONE) ? 0 : opinfo[op].nops;
	}

	v = g_new0 (GnmValidation, 1);
	v->ref_count = 1;
	v->title = title && title[0] ? go_string_new (title) : NULL;
	v->msg = msg && msg[0] ? go_string_new (msg) : NULL;

	dependent_managed_init (&v->deps[0], sheet);
	if (texpr0) {
		if (nops > 0)
			dependent_managed_set_expr (&v->deps[0], texpr0);
		gnm_expr_top_unref (texpr0);
	}

	dependent_managed_init (&v->deps[1], sheet);
	if (texpr1) {
		if (nops > 1)
			dependent_managed_set_expr (&v->deps[1], texpr1);
		gnm_expr_top_unref (texpr1);
	}

	v->style = style;
	v->type = type;
	v->op = op;
	v->allow_blank = (allow_blank != FALSE);
	v->use_dropdown = (use_dropdown != FALSE);

	return v;
}

GnmValidation *
gnm_validation_dup_to (GnmValidation *v, Sheet *sheet)
{
	GnmValidation *dst;
	int i;

	g_return_val_if_fail (v != NULL, NULL);

	dst = gnm_validation_new (v->style, v->type, v->op,
				  sheet,
				  v->title ? v->title->str : NULL,
				  v->msg ? v->msg->str : NULL,
				  NULL, NULL,
				  v->allow_blank, v->use_dropdown);
	for (i = 0; i < 2; i++)
		gnm_validation_set_expr (dst, v->deps[i].base.texpr, i);
	return dst;
}

gboolean
gnm_validation_equal (GnmValidation const *a, GnmValidation const *b,
		      gboolean relax_sheet)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;

	if (!relax_sheet &&
	    gnm_validation_get_sheet (a) != gnm_validation_get_sheet (b))
		return FALSE;

	if (!(g_strcmp0 (a->title ? a->title->str : NULL,
			 b->title ? b->title->str : NULL) == 0 &&
	      g_strcmp0 (a->msg ? a->msg->str : NULL,
			 b->msg ? b->msg->str : NULL) == 0 &&
	      a->style == b->style &&
	      a->type == b->type &&
	      a->op == b->op &&
	      a->allow_blank == b->allow_blank &&
	      a->use_dropdown == b->use_dropdown))
		return FALSE;

	for (i = 0; i < 2; i++)
		if (!gnm_expr_top_equal (a->deps[i].base.texpr, b->deps[i].base.texpr))
			return FALSE;

	return TRUE;
}


GnmValidation *
gnm_validation_ref (GnmValidation const *v)
{
	g_return_val_if_fail (v != NULL, NULL);
	((GnmValidation *)v)->ref_count++;
	return ((GnmValidation *)v);
}

void
gnm_validation_unref (GnmValidation const *val)
{
	GnmValidation *v = (GnmValidation *)val;

	g_return_if_fail (v != NULL);

	v->ref_count--;

	if (v->ref_count < 1) {
		int i;

		go_string_unref (v->title);
		v->title = NULL;

		go_string_unref (v->msg);
		v->msg = NULL;

		for (i = 0 ; i < 2 ; i++)
			dependent_managed_set_expr (&v->deps[i], NULL);
		g_free (v);
	}
}

GType
gnm_validation_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmValidation",
			 (GBoxedCopyFunc)gnm_validation_ref,
			 (GBoxedFreeFunc)gnm_validation_unref);
	}
	return t;
}

/**
 * gnm_validation_get_sheet:
 * @v: #GnmValidation
 *
 * Returns: (transfer none): the sheet.
 **/
Sheet *
gnm_validation_get_sheet (GnmValidation const *v)
{
	g_return_val_if_fail (v != NULL, NULL);
	return v->deps[0].base.sheet;
}

/**
 * gnm_validation_set_expr:
 * @v: #GnmValidation
 * @texpr: #GnmExprTop
 * @indx: 0 or 1
 *
 * Assign an expression to a validation.  gnm_validation_is_ok can be used to
 * verify that @v has all of the required information.
 **/
void
gnm_validation_set_expr (GnmValidation *v,
			 GnmExprTop const *texpr, unsigned indx)
{
	g_return_if_fail (indx <= 1);

	dependent_managed_set_expr (&v->deps[indx], texpr);
}

GError *
gnm_validation_is_ok (GnmValidation const *v)
{
	unsigned nops, i;

	switch (v->type) {
	case GNM_VALIDATION_TYPE_CUSTOM:
	case GNM_VALIDATION_TYPE_IN_LIST:
		nops = 1;
		break;
	case GNM_VALIDATION_TYPE_ANY:
		nops = 0;
		break;
	default: nops = (v->op == GNM_VALIDATION_OP_NONE) ? 0 : opinfo[v->op].nops;
	}

	for (i = 0 ; i < 2 ; i++)
		if (v->deps[i].base.texpr == NULL) {
			if (i < nops)
				return g_error_new (1, 0, N_("Missing formula for validation"));
		} else {
			if (i >= nops)
				return g_error_new (1, 0, N_("Extra formula for validation"));
		}

	return NULL;
}

static ValidationStatus
validation_barf (WorkbookControl *wbc, GnmValidation const *gv,
		 char *def_msg, gboolean *showed_dialog)
{
	char const *msg = gv->msg ? gv->msg->str : def_msg;
	char const *title = gv->title ? gv->title->str : _("Gnumeric: Validation");
	ValidationStatus result;

	if (gv->style == GNM_VALIDATION_STYLE_NONE) {
		/* Invalid, but we're asked to ignore.  */
		result = GNM_VALIDATION_STATUS_VALID;
	} else {
		if (showed_dialog) *showed_dialog = TRUE;
		result = wb_control_validation_msg (wbc, gv->style, title, msg);
	}
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
	return validation_barf (wbc, v, msg, showed_dialog);		\
  } while (0)

/**
 * gnm_validation_eval:
 * @wbc:
 * @mstyle:
 * @sheet:
 * @pos:
 * @showed_dialog: (out) (optional):
 *
 * Checks the validation in @mstyle, if any.  Set @showed_dialog to %TRUE
 * if a dialog was showed as a result.
 **/
ValidationStatus
gnm_validation_eval (WorkbookControl *wbc, GnmStyle const *mstyle,
		     Sheet *sheet, GnmCellPos const *pos,
		     gboolean *showed_dialog)
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
		return GNM_VALIDATION_STATUS_VALID;

	if (v->type == GNM_VALIDATION_TYPE_ANY)
		return GNM_VALIDATION_STATUS_VALID;

	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell != NULL)
		gnm_cell_eval (cell);

	if (gnm_cell_is_empty (cell)) {
		if (v->allow_blank)
			return GNM_VALIDATION_STATUS_VALID;
		BARF (g_strdup_printf (_("Cell %s is not permitted to be blank"),
				       cell_name (cell)));
	}

	val = cell->value;
	switch (val->v_any.type) {
	case VALUE_ERROR:
		if (typeinfo[v->type].errors_not_allowed)
			BARF (g_strdup_printf (_("Cell %s is not permitted to contain error values"),
					       cell_name (cell)));
		break;

	case VALUE_BOOLEAN:
		if (typeinfo[v->type].bool_always_ok)
			return GNM_VALIDATION_STATUS_VALID;
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
	case GNM_VALIDATION_TYPE_AS_INT:
		x = value_get_as_float (val);
		if (gnm_fake_floor (x) == gnm_fake_ceil (x))
			break;
		else
			BARF (g_strdup_printf (_("'%s' is not an integer"),
					       value_peek_string (val)));

	case GNM_VALIDATION_TYPE_AS_NUMBER:
		x = value_get_as_float (val);
		break;

	case GNM_VALIDATION_TYPE_AS_DATE: /* What the hell does this do?  */
		x = value_get_as_float (val);
		if (x < 0)
			BARF (g_strdup_printf (_("'%s' is not a valid date"),
					       value_peek_string (val)));
		break;


	case GNM_VALIDATION_TYPE_AS_TIME: /* What the hell does this do?  */
		x = value_get_as_float (val);
		break;

	case GNM_VALIDATION_TYPE_IN_LIST: {
		GnmExprTop const *texpr = v->deps[0].base.texpr;
		if (texpr) {
			GnmValue *list = gnm_expr_top_eval
				(texpr, &ep,
				 GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
			GnmValue *res = value_area_foreach (list, &ep, CELL_ITER_IGNORE_BLANK,
				 (GnmValueIterFunc) cb_validate_custom, val);
			value_release (list);
			if (res == NULL) {
				GnmParsePos pp;
				char *expr_str = gnm_expr_top_as_string
					(texpr,
					 parse_pos_init_evalpos (&pp, &ep),
					 ep.sheet->convs);
				char *msg = g_strdup_printf (_("%s does not contain the new value."), expr_str);
				g_free (expr_str);
				BARF (msg);
			}
		}
		return GNM_VALIDATION_STATUS_VALID;
	}

	case GNM_VALIDATION_TYPE_TEXT_LENGTH:
		/* XL appears to use a very basic value->string mapping that
		 * ignores formatting.
		 * eg len (12/13/01) == len (37238) = 5
		 * This seems wrong for
		 */
		x = g_utf8_strlen (value_peek_string (val), -1);
		break;

	case GNM_VALIDATION_TYPE_CUSTOM: {
		gboolean valid;
		GnmExprTop const *texpr = v->deps[0].base.texpr;

		if (!texpr)
			return GNM_VALIDATION_STATUS_VALID;

		val = gnm_expr_top_eval (texpr, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		valid = value_get_as_bool (val, NULL);
		value_release (val);

		if (valid)
			return GNM_VALIDATION_STATUS_VALID;
		else {
			GnmParsePos pp;
			char *expr_str = gnm_expr_top_as_string
				(texpr,
				 parse_pos_init_evalpos (&pp, &ep),
				 ep.sheet->convs);
			char *msg = g_strdup_printf (_("%s is not true."), expr_str);
			g_free (expr_str);
			BARF (msg);
		}
	}

	default:
		g_assert_not_reached ();
		return GNM_VALIDATION_STATUS_VALID;
	}

	if (v->op == GNM_VALIDATION_OP_NONE)
		return GNM_VALIDATION_STATUS_VALID;

	nok = 0;
	for (i = 0; i < opinfo[v->op].nops; i++) {
		GnmExprTop const *texpr_i = v->deps[i].base.texpr;
		GnmExprTop const *texpr;
		GnmValue *cres;

		if (!texpr_i) {
			nok++;
			continue;
		}

		texpr = gnm_expr_top_new
			(gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_float (x)),
			  opinfo[v->op].ops[i],
			  gnm_expr_copy (texpr_i->expr)));
		cres = gnm_expr_top_eval
			(texpr, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		if (value_get_as_bool (cres, NULL))
			nok++;
		value_release (cres);
		gnm_expr_top_unref (texpr);
	}

	if (nok < opinfo[v->op].ntrue)
		BARF (g_strdup_printf (_("%s is out of permitted range"),
				       value_peek_string (val)));

	return GNM_VALIDATION_STATUS_VALID;
}

#undef BARF

typedef struct {
	WorkbookControl *wbc;
	Sheet *sheet;
	GnmCellPos const *pos;
	gboolean *showed_dialog;
	ValidationStatus status;
} validation_eval_t;

static GnmValue *
validation_eval_range_cb (GnmCellIter const *iter, validation_eval_t *closure)
{
	ValidationStatus status;
	gboolean showed_dialog;
	GnmStyle const *mstyle = sheet_style_get
		(closure->sheet, iter->pp.eval.col, iter->pp.eval.row);

	if (mstyle != NULL) {
		status = gnm_validation_eval (closure->wbc, mstyle,
					  closure->sheet, &iter->pp.eval,
					  &showed_dialog);
		if (closure->showed_dialog)
			*closure->showed_dialog = *closure->showed_dialog || showed_dialog;

		if (status != GNM_VALIDATION_STATUS_VALID) {
			closure->status = status;
			return VALUE_TERMINATE;
		}
	}

	return NULL;
}

ValidationStatus
gnm_validation_eval_range (WorkbookControl *wbc,
			   Sheet *sheet, GnmCellPos const *pos,
			   GnmRange const *r,
			   gboolean *showed_dialog)
{
	GnmValue *result;
	validation_eval_t closure;
	GnmEvalPos ep;
	GnmValue *cell_range = value_new_cellrange_r (sheet, r);

	closure.wbc = wbc;
	closure.sheet = sheet;
	closure.pos = pos;
	closure.showed_dialog = showed_dialog;
	closure.status = GNM_VALIDATION_STATUS_VALID;

	eval_pos_init_pos (&ep, sheet, pos);

	result = workbook_foreach_cell_in_range (&ep, cell_range, CELL_ITER_ALL,
						 (CellIterFunc) validation_eval_range_cb,
						 &closure);

	value_release (cell_range);

	if (result == NULL)
		return GNM_VALIDATION_STATUS_VALID;
	return closure.status;
}
