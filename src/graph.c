/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * graph.c: The gnumeric specific data wrappers for GOffice
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifdef NEW_GRAPHS
#include <gnumeric-config.h>
#include "graph.h"
#include "dependent.h"
#include "expr.h"
#include "cell.h"
#include "value.h"
#include "number-match.h"
#include "mathfunc.h"
#include "sheet.h"
#include "workbook.h"
#include "str.h"
#include "parse-util.h"
#include <goffice/graph/go-data-impl.h>

#include <gsf/gsf-impl-utils.h>

struct _GnmGODataScalar {
	GODataScalar	 base;
	Dependent	 dep;
	Value		*val;
	char		*val_str;
};
typedef GODataScalarClass GnmGODataScalarClass;

#define DEP_TO_SCALAR(d_ptr) (GnmGODataScalar *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataScalar, dep))

static GObjectClass *scalar_parent_klass;

static Value *
scalar_get_val (GnmGODataScalar *scalar)
{
	if (scalar->val != NULL) {
		value_release (scalar->val);
		scalar->val = NULL;
		g_free (scalar->val_str);
		scalar->val_str = NULL;
	}
	if (scalar->val == NULL) {
		EvalPos pos;
		scalar->val = gnm_expr_eval (scalar->dep.expression,
			eval_pos_init_dep (&pos, &scalar->dep),
			GNM_EXPR_EVAL_PERMIT_EMPTY);
	}
	return scalar->val;
}

static void
gnm_go_data_scalar_eval (Dependent *dep)
{
	GnmGODataScalar *scalar = DEP_TO_SCALAR (dep);
	go_data_vector_emit_changed (GO_DATA_VECTOR (scalar));
}

static void
gnm_go_data_scalar_finalize (GObject *obj)
{
	GnmGODataScalar *scalar = (GnmGODataScalar *)obj;

	dependent_set_expr (&scalar->dep, NULL);
	if (scalar->val != NULL) {
		value_release (scalar->val);
		scalar->val = NULL;
	}
	g_free (scalar->val_str);
	scalar->val_str = NULL;

	if (scalar_parent_klass->finalize)
		(*scalar_parent_klass->finalize) (obj);
}

static gboolean
gnm_go_data_scalar_eq (GOData const *data_a, GOData const *data_b)
{
	GnmGODataScalar const *a = (GnmGODataScalar const *)data_a;
	GnmGODataScalar const *b = (GnmGODataScalar const *)data_b;
	return gnm_expr_equal (a->dep.expression, b->dep.expression);
}

static char *
gnm_go_data_scalar_as_str (GOData const *dat)
{
	ParsePos pp;
	GnmGODataScalar const *scalar = (GnmGODataScalar const *)dat;
	return gnm_expr_as_string (scalar->dep.expression,
		parse_pos_init_dep (&pp, &scalar->dep),
		gnm_expr_conventions_default);
}

static double
gnm_go_data_scalar_get_value (GODataScalar *dat)
{
	return value_get_as_float (scalar_get_val ((GnmGODataScalar *)dat));
}

static char const *
gnm_go_data_scalar_get_str (GODataScalar *dat)
{
	GnmGODataScalar *scalar = (GnmGODataScalar *)dat;
	if (scalar->val_str == NULL)
		scalar->val_str = value_get_as_string (scalar_get_val (scalar));
	return scalar->val_str;
}

static void
gnm_go_data_scalar_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataScalarClass *scalar_klass = (GODataScalarClass *) gobject_klass;

	scalar_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= gnm_go_data_scalar_finalize;
	godata_klass->eq		= gnm_go_data_scalar_eq;
	godata_klass->as_str		= gnm_go_data_scalar_as_str;
	scalar_klass->get_value		= gnm_go_data_scalar_get_value;
	scalar_klass->get_str		= gnm_go_data_scalar_get_str;
}

static void
gnm_go_data_scalar_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GraphScalar%p", dep);
}

static DEPENDENT_MAKE_TYPE (gnm_go_data_scalar, NULL)

static void
gnm_go_data_scalar_init (GObject *obj)
{
	GnmGODataScalar *scalar = (GnmGODataScalar *)obj;
	scalar->dep.flags = gnm_go_data_scalar_get_dep_type ();
}

GSF_CLASS (GnmGODataScalar, gnm_go_data_scalar,
	   gnm_go_data_scalar_class_init, gnm_go_data_scalar_init,
	   GO_DATA_SCALAR_TYPE)

GOData *
gnm_go_data_scalar_new_expr (Sheet *sheet, GnmExpr const *expr)
{
	GnmGODataScalar *res = g_object_new (gnm_go_data_scalar_get_type (), NULL);
	res->dep.expression = expr;
	res->dep.sheet = sheet;
	return GO_DATA (res);
}

void
gnm_go_data_scalar_set_sheet (GnmGODataScalar *scalar, Sheet *sheet)
{
	scalar->dep.sheet = NULL;
	dependent_set_sheet (&scalar->dep, sheet);
}

/**************************************************************************/

struct _GnmGODataVector {
	GODataVector	base;
	Dependent	 dep;
	Value		*val;
	gboolean	 as_col;
};
typedef GODataVectorClass GnmGODataVectorClass;

#define DEP_TO_VECTOR(d_ptr) (GnmGODataVector *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataVector, dep))

static GObjectClass *vector_parent_klass;

static void
gnm_go_data_vector_eval (Dependent *dep)
{
	GnmGODataVector *vec = DEP_TO_VECTOR (dep);

	if (vec->val != NULL) {
		value_release (vec->val);
		vec->val = NULL;
	}
	go_data_vector_emit_changed (GO_DATA_VECTOR (vec));
}

static void
gnm_go_data_vector_finalize (GObject *obj)
{
	GnmGODataVector *vec = (GnmGODataVector *)obj;

	dependent_set_expr (&vec->dep, NULL);
	if (vec->val != NULL) {
		value_release (vec->val);
		vec->val = NULL;
	}

	if (vector_parent_klass->finalize)
		(*vector_parent_klass->finalize) (obj);
}

static gboolean
gnm_go_data_vector_eq (GOData const *data_a, GOData const *data_b)
{
	GnmGODataVector const *a = (GnmGODataVector const *)data_a;
	GnmGODataVector const *b = (GnmGODataVector const *)data_b;
	return gnm_expr_equal (a->dep.expression, b->dep.expression);
}

static char *
gnm_go_data_vector_as_str (GOData const *dat)
{
	ParsePos pp;
	GnmGODataVector const *vec = (GnmGODataVector const *)dat;
	return gnm_expr_as_string (vec->dep.expression,
		parse_pos_init_dep (&pp, &vec->dep),
		gnm_expr_conventions_default);
}

static void
gnm_go_data_vector_load_len (GODataVector *dat)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	EvalPos ep;
	Range r;
	Sheet *start_sheet, *end_sheet;
	unsigned h, w;

	eval_pos_init_dep (&ep, &vec->dep);
	if (vec->val == NULL)
		vec->val = gnm_expr_eval (vec->dep.expression, &ep,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);

	if (vec->val != NULL) {
		switch (vec->val->type) {
		case VALUE_CELLRANGE:
			rangeref_normalize (&vec->val->v_range.cell, &ep,
				&start_sheet, &end_sheet, &r);

			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;

			w = r.end.col - r.start.col;
			h = r.end.row - r.start.row;
			if (w > 0 && h > 0)
				dat->len = ((vec->as_col = (h > w))) ? h : w;
			else
				dat->len = 0;
			break;

		case VALUE_ARRAY :
			vec->as_col = (vec->val->v_array.y > vec->val->v_array.x);
			dat->len =  vec->as_col ? vec->val->v_array.y : vec->val->v_array.x;
			break;

		default :
			dat->len = 1;
			vec->as_col = TRUE;
		}
	} else
		dat->len = 0;
	dat->base.flags |= GO_DATA_VECTOR_LEN_CACHED;
}

static Value *
cb_assign_val (Sheet *sheet, int col, int row,
	       Cell *cell, double **vals)
{
	Value *v;
	if (cell != NULL) {
		cell_eval (cell);
		v = cell->value;
	} else
		v = NULL;

	if (VALUE_IS_EMPTY_OR_ERROR (v)) {
		*((*vals)++) = gnm_nan;
		return NULL;
	}
	if (v->type == VALUE_STRING) {
		v = format_match_number (v->v_str.val->str, NULL,
				workbook_date_conv (sheet->workbook));
		if (v != NULL) {
			*((*vals)++) = value_get_as_float (v);
			value_release (v);
		} else
			*((*vals)++) = gnm_nan;
		return NULL;
	}
	*((*vals)++) = value_get_as_float (v);
	return NULL;
}

static void
gnm_go_data_vector_load_values (GODataVector *dat)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	EvalPos ep;
	Range r;
	Sheet *start_sheet, *end_sheet;
	int len = go_data_vector_get_len (dat); /* force calculation */
	double *vals;
	Value *v;

	if (dat->len <= 0)
		return;

	vals = dat->values = g_new (double, dat->len);
	switch (vec->val->type) {
	case VALUE_CELLRANGE:
		rangeref_normalize (&vec->val->v_range.cell,
			eval_pos_init_dep (&ep, &vec->dep),
			&start_sheet, &end_sheet, &r);

		if (vec->as_col) {
			r.end.col = r.start.col;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;
		} else {
			r.end.row = r.start.row;
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;
		}
		sheet_foreach_cell_in_range (start_sheet, CELL_ITER_ALL,
			r.start.col, r.start.row, r.end.col, r.end.row,
			(CellIterFunc)cb_assign_val, &vals);
		break;

	case VALUE_ARRAY :
		while (len-- > 0) {
			v = vec->as_col
				? vec->val->v_array.vals [0][len]
				: vec->val->v_array.vals [len][0];

			if (VALUE_IS_EMPTY_OR_ERROR (v)) {
				vals[len] = gnm_nan;
				continue;
			} else if (v->type == VALUE_STRING) {
				Value *tmp = format_match_number (v->v_str.val->str, NULL,
						workbook_date_conv (vec->dep.sheet->workbook));
				if (tmp != NULL) {
					vals[len] = value_get_as_float (tmp);
					value_release (tmp);
					continue;
				}
			}
			vals[len] = value_get_as_float (v);
		}
		break;

	case VALUE_STRING :
		v = format_match_number (vec->val->v_str.val->str, NULL,
			workbook_date_conv (vec->dep.sheet->workbook));
		if (v != NULL) {
			vals[0] = value_get_as_float (v);
			value_release (v);
			break;
		}
		/* fall through to errors */

	case VALUE_EMPTY :
	case VALUE_ERROR :
		vals[0] = gnm_nan;
		break;
	default :
		vals[0] = value_get_as_float (vec->val);
		break;
	}

	dat->values = vals;
	dat->base.flags |= GO_DATA_CACHE_IS_VALID;
}

static double
gnm_go_data_vector_get_value (GODataVector *vec, unsigned i)
{
	return 0.;
}

static char const *
gnm_go_data_vector_get_str (GODataVector *vec, unsigned i)
{
	return "";
}

static void
gnm_go_data_vector_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataVectorClass *vector_klass = (GODataVectorClass *) gobject_klass;

	vector_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= gnm_go_data_vector_finalize;
	godata_klass->eq		= gnm_go_data_vector_eq;
	godata_klass->as_str		= gnm_go_data_vector_as_str;
	vector_klass->load_len		= gnm_go_data_vector_load_len;
	vector_klass->load_values	= gnm_go_data_vector_load_values;
	vector_klass->get_value		= gnm_go_data_vector_get_value;
	vector_klass->get_str		= gnm_go_data_vector_get_str;
}

static void
gnm_go_data_vector_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GraphVector%p", dep);
}
static DEPENDENT_MAKE_TYPE (gnm_go_data_vector, NULL)

static void
gnm_go_data_vector_init (GObject *obj)
{
	GnmGODataVector *vec = (GnmGODataVector *)obj;
	vec->dep.flags = gnm_go_data_vector_get_dep_type ();
}

GSF_CLASS (GnmGODataVector, gnm_go_data_vector,
	   gnm_go_data_vector_class_init, gnm_go_data_vector_init,
	   GO_DATA_VECTOR_TYPE)

GOData *
gnm_go_data_vector_new_expr (Sheet *sheet, GnmExpr const *expr)
{
	GnmGODataVector *res = g_object_new (gnm_go_data_vector_get_type (), NULL);
	res->dep.expression = expr;
	res->dep.sheet = sheet;
	return GO_DATA (res);
}

void
gnm_go_data_vector_set_sheet (GnmGODataVector *vec, Sheet *sheet)
{
	vec->dep.sheet = NULL;
	dependent_set_sheet (&vec->dep, sheet);
}
#endif
