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
#include "position.h"
#include "format.h"
#include "auto-format.h"
#include "ranges.h"
#include "parse-util.h"
#include <goffice/graph/go-data-impl.h>

#include <gsf/gsf-impl-utils.h>
#include <string.h>

static GnmDependent *gnm_go_data_get_dep (GOData const *obj);

static  GOData *
gnm_go_data_dup (GOData const *src)
{
	GOData *dst = g_object_new (G_OBJECT_TYPE (src), NULL);
	GnmDependent const *src_dep = gnm_go_data_get_dep (src);
	GnmDependent *dst_dep = gnm_go_data_get_dep (dst);

	dst_dep->expression = src_dep->expression;
	dst_dep->sheet      = src_dep->sheet;
	if (dst_dep->expression == NULL) {
		char const *str = g_object_get_data (G_OBJECT (src), "from-str");
		g_object_set_data_full (G_OBJECT (dst),
			"from-str", g_strdup (str), g_free);
	} else
		gnm_expr_ref (dst_dep->expression);

	return GO_DATA (dst);
}

static gboolean
gnm_go_data_eq (GOData const *data_a, GOData const *data_b)
{
	GnmDependent const *a = gnm_go_data_get_dep (data_a);
	GnmDependent const *b = gnm_go_data_get_dep (data_b);
	if (a->expression == NULL && b->expression == NULL) {
		char const *str_a = g_object_get_data (G_OBJECT (data_a), "from-str");
		char const *str_b = g_object_get_data (G_OBJECT (data_b), "from-str");

		if (str_a != NULL && str_b != NULL)
			return 0 == strcmp (str_a, str_b);
		return FALSE;
	}

	return gnm_expr_equal (a->expression, b->expression);
}

static GOFormat *
gnm_go_data_preferred_fmt (GOData const *dat)
{
	GnmEvalPos ep;
	GnmDependent const *dep = gnm_go_data_get_dep (dat);
	return auto_style_format_suggest (dep->expression,
		eval_pos_init_dep (&ep, dep));
}

static char *
gnm_go_data_as_str (GOData const *dat)
{
	GnmParsePos pp;
	GnmDependent const *dep = gnm_go_data_get_dep (dat);
	if (dep->sheet == NULL)
		return g_strdup ("No sheet for GnmGOData");
	return gnm_expr_as_string (dep->expression,
		parse_pos_init_dep (&pp, dep),
		gnm_expr_conventions_default);
}

static  gboolean
gnm_go_data_from_str (GOData *dat, char const *str)
{
	GnmExpr const *expr;
	GnmParsePos   pp;
	GnmDependent *dep = gnm_go_data_get_dep (dat);

	/* Its too early in the life cycle to know where we
	 * are.  Wait until later when we parse the sheet */
	if (dep->sheet == NULL) {
		g_object_set_data_full (G_OBJECT (dat),
			"from-str", g_strdup (str), g_free);
		return TRUE;
	}

	expr = gnm_expr_parse_str_simple (str, parse_pos_init_dep (&pp, dep));
	if (expr != NULL) {
		dependent_set_expr (dep, expr);
		gnm_expr_unref (expr);
		return TRUE;
	}
	return FALSE;
}

void
gnm_go_data_set_sheet (GOData *dat, Sheet *sheet)
{
	GnmDependent *dep = gnm_go_data_get_dep (dat);

	if (dep == NULL)
		return;

	if (sheet != NULL) {
		/* no expression ?
		 * Do we need to parse one now that we have more context ? */
		if (dep->expression == NULL) {
			char const *str = g_object_get_data (G_OBJECT (dat), "from-str");
			if (str != NULL) { /* bingo */
				dep->sheet = sheet; /* cheat a bit */
				if (gnm_go_data_from_str (dat, str)) {
					g_object_set_data (G_OBJECT (dat),
							   "from-str", NULL); /* free it */
					go_data_emit_changed (GO_DATA (dat));
				}
			}
		}

		dep->sheet = NULL;
		dependent_set_sheet (dep, sheet);
	} else if (dependent_is_linked (dep)) {
		dependent_unlink (dep, NULL);
		dep->sheet = NULL;
	}
}

/**************************************************************************/

struct _GnmGODataScalar {
	GODataScalar	 base;
	GnmDependent	 dep;
	GnmValue	*val;
	char		*val_str;
};
typedef GODataScalarClass GnmGODataScalarClass;

#define DEP_TO_SCALAR(d_ptr) (GnmGODataScalar *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataScalar, dep))

static GObjectClass *scalar_parent_klass;

static GnmValue *
scalar_get_val (GnmGODataScalar *scalar)
{
	if (scalar->val != NULL) {
		value_release (scalar->val);
		scalar->val = NULL;
		g_free (scalar->val_str);
		scalar->val_str = NULL;
	}
	if (scalar->val == NULL) {
		if (scalar->dep.expression != NULL) {
			GnmEvalPos pos;
			scalar->val = gnm_expr_eval (scalar->dep.expression,
				eval_pos_init_dep (&pos, &scalar->dep),
				GNM_EXPR_EVAL_PERMIT_EMPTY);
		} else
			scalar->val = value_new_empty ();
	}
	return scalar->val;
}

static void
gnm_go_data_scalar_eval (GnmDependent *dep)
{
	GnmGODataScalar *scalar = DEP_TO_SCALAR (dep);

	if (scalar->val != NULL) {
		value_release (scalar->val);
		scalar->val = NULL;
	}
	g_free (scalar->val_str);
	scalar->val_str = NULL;
	go_data_emit_changed (GO_DATA (scalar));
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
	godata_klass->dup		= gnm_go_data_dup;
	godata_klass->eq		= gnm_go_data_eq;
	godata_klass->preferred_fmt	= gnm_go_data_preferred_fmt;
	godata_klass->as_str		= gnm_go_data_as_str;
	godata_klass->from_str		= gnm_go_data_from_str;
	scalar_klass->get_value		= gnm_go_data_scalar_get_value;
	scalar_klass->get_str		= gnm_go_data_scalar_get_str;
}

static void
gnm_go_data_scalar_debug_name (GnmDependent const *dep, FILE *out)
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

/**************************************************************************/

struct _GnmGODataVector {
	GODataVector	base;
	GnmDependent	 dep;
	GnmValue	*val;
	gboolean	 as_col;
};
typedef GODataVectorClass GnmGODataVectorClass;

#define DEP_TO_VECTOR(d_ptr) (GnmGODataVector *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataVector, dep))

static GObjectClass *vector_parent_klass;

static void
gnm_go_data_vector_eval (GnmDependent *dep)
{
	GnmGODataVector *vec = DEP_TO_VECTOR (dep);

	if (vec->val != NULL) {
		value_release (vec->val);
		vec->val = NULL;
	}
	go_data_emit_changed (GO_DATA (vec));
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
	if (vec->base.values != NULL) {
		g_free (vec->base.values);
		vec->base.values = NULL;
	}

	if (vector_parent_klass->finalize)
		(*vector_parent_klass->finalize) (obj);
}

static void
gnm_go_data_vector_load_len (GODataVector *dat)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmEvalPos ep;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	unsigned h, w;
	int old_len = dat->len;

	eval_pos_init_dep (&ep, &vec->dep);
	if (vec->val == NULL)
		vec->val = gnm_expr_eval (vec->dep.expression, &ep,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);

#if 0
	{
		char *str = go_data_as_str (dat);
		g_warning ("load_len '%s'", str);
		g_free (str);
	}
#endif

	if (vec->val != NULL) {
		switch (vec->val->type) {
		case VALUE_CELLRANGE:
			rangeref_normalize (&vec->val->v_range.cell, &ep,
				&start_sheet, &end_sheet, &r);

			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;

			if (r.end.col >= r.start.col && r.end.row >= r.start.row) {
				w = range_width (&r);
				h = range_height (&r);
				if (w > 0 && h > 0)
					dat->len = ((vec->as_col = (h > w))) ? h : w;
				else
					dat->len = 0;
			} else
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
	if (dat->values != NULL && old_len != dat->len) {
		g_free (dat->values);
		dat->values = NULL;
	}
	dat->base.flags |= GO_DATA_VECTOR_LEN_CACHED;
}

struct assign_closure {
	double minimum, maximum;
	double *vals;
	unsigned last;
	unsigned i;
};

static GnmValue *
cb_assign_val (Sheet *sheet, int col, int row,
	       GnmCell *cell, struct assign_closure *dat)
{
	GnmValue *v;
	double res;

	if (cell != NULL) {
		cell_eval (cell);
		v = cell->value;
	} else
		v = NULL;

	if (VALUE_IS_EMPTY_OR_ERROR (v)) {
		dat->vals[dat->i++] = gnm_nan;
		return NULL;
	}

	dat->last = dat->i;
	if (v->type == VALUE_STRING) {
		v = format_match_number (v->v_str.val->str, NULL,
				workbook_date_conv (sheet->workbook));
		if (v == NULL) {
			dat->vals[dat->i++] = gnm_pinf;
			return NULL;
		}
		res = value_get_as_float (v);
		value_release (v);
	} else
		res = value_get_as_float (v);

	dat->vals[dat->i++] = res;
	if (dat->minimum > res)
		dat->minimum = res;
	if (dat->maximum < res)
		dat->maximum = res;
	return NULL;
}

static void
gnm_go_data_vector_load_values (GODataVector *dat)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmEvalPos ep;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	int len = go_data_vector_get_len (dat); /* force calculation */
	double *vals, minimum, maximum;
	GnmValue *v;
	struct assign_closure closure;

	if (dat->len <= 0) {
		dat->values = NULL;
		dat->minimum = gnm_nan;
		dat->maximum = gnm_nan;
		dat->base.flags |= GO_DATA_CACHE_IS_VALID;
		return;
	}

	if (dat->values == NULL)
		dat->values = g_new (double, dat->len);
	vals = dat->values;
	switch (vec->val->type) {
	case VALUE_CELLRANGE:
		rangeref_normalize (&vec->val->v_range.cell,
			eval_pos_init_dep (&ep, &vec->dep),
			&start_sheet, &end_sheet, &r);

		/* clip here rather than relying on sheet_foreach
		 * because that only clips if we ignore blanks */
		if (vec->as_col) {
			r.end.col = r.start.col;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;
		} else {
			r.end.row = r.start.row;
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;
		}

		/* In case the sheet is empty */
		if (r.start.col <= r.end.col && r.start.row <= r.end.row) {
			closure.maximum = - G_MAXDOUBLE;
			closure.minimum = G_MAXDOUBLE;
			closure.vals = dat->values;
			closure.last = -1;
			closure.i = 0;
			sheet_foreach_cell_in_range (start_sheet, CELL_ITER_ALL,
				r.start.col, r.start.row, r.end.col, r.end.row,
				(CellIterFunc)cb_assign_val, &closure);
			dat->len = closure.last + 1; /* clip */
			minimum = closure.minimum;
			maximum = closure.maximum;
		} else
			minimum = maximum = vals[0] = gnm_nan;
		break;

	case VALUE_ARRAY :
		maximum = - G_MAXDOUBLE;
		minimum = G_MAXDOUBLE;
		while (len-- > 0) {
			v = vec->as_col
				? vec->val->v_array.vals [0][len]
				: vec->val->v_array.vals [len][0];

			if (VALUE_IS_EMPTY_OR_ERROR (v)) {
				vals[len] = gnm_nan;
				continue;
			} else if (v->type == VALUE_STRING) {
				GnmValue *tmp = format_match_number (v->v_str.val->str, NULL,
						workbook_date_conv (vec->dep.sheet->workbook));
				if (tmp != NULL) {
					vals[len] = value_get_as_float (tmp);
					value_release (tmp);
					continue;
				}
			}
			vals[len] = value_get_as_float (v);
			if (minimum > vals[len])
				minimum = vals[len];
			if (maximum < vals[len])
				maximum = vals[len];
		}
		break;

	case VALUE_STRING :
		v = format_match_number (vec->val->v_str.val->str, NULL,
			workbook_date_conv (vec->dep.sheet->workbook));
		if (v != NULL) {
			minimum = maximum = vals[0] = value_get_as_float (v);
			value_release (v);
			break;
		}
		/* fall through to errors */

	case VALUE_EMPTY :
	case VALUE_ERROR :
		minimum = maximum = vals[0] = gnm_nan;
		break;
	default :
		minimum = maximum = vals[0] = value_get_as_float (vec->val);
		break;
	}

	dat->values = vals;
	dat->minimum = minimum;
	dat->maximum = maximum;
	dat->base.flags |= GO_DATA_CACHE_IS_VALID;
}

static double
gnm_go_data_vector_get_value (GODataVector *dat, unsigned i)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmValue *v;
	GnmEvalPos ep;
	gboolean valid;

	if (vec->val == NULL)
		gnm_go_data_vector_load_len (dat);

	eval_pos_init_dep (&ep, &vec->dep);
	v = value_dup (vec->as_col
		? value_area_get_x_y (vec->val, 0, i, &ep)
		: value_area_get_x_y (vec->val, i, 0, &ep));
	v = value_coerce_to_number (v, &valid, &ep);

	if (valid) {
		gnm_float res = value_get_as_float (v);
		value_release (v);
		return res;
	}
	return gnm_nan;
}

static char *
gnm_go_data_vector_get_str (GODataVector *dat, unsigned i)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmValue const *v;
	GnmEvalPos ep;
	GnmFormat const *format = NULL;
	GnmDateConventions const *date_conv = NULL;

	if (vec->val == NULL)
		gnm_go_data_vector_load_len (dat);

	g_return_val_if_fail (vec->val != NULL, NULL);

	v = vec->val;
	eval_pos_init_dep (&ep, &vec->dep);
	if (v->type == VALUE_CELLRANGE) {
		Sheet *start_sheet, *end_sheet;
		GnmCell  *cell;
		GnmRange  r;

		rangeref_normalize (&v->v_range.cell, &ep,
				    &start_sheet, &end_sheet, &r);
		if (vec->as_col)
			r.start.row += i;
		else
			r.start.col += i;
		cell = sheet_cell_get (start_sheet, r.start.col, r.start.row);
		if (cell == NULL)
			return NULL;
		cell_eval (cell);
		v = cell->value;
		format = cell_get_format (cell);
		date_conv = workbook_date_conv (start_sheet->workbook);
	} else if (v->type == VALUE_ARRAY)
		v = vec->as_col
			? value_area_get_x_y (v, 0, i, &ep)
			: value_area_get_x_y (v, i, 0, &ep);

	switch (v->type){
	case VALUE_ARRAY:
	case VALUE_CELLRANGE:
		g_warning ("nested non-scalar types ?");
		return NULL;

	default :
		return format_value (format, v, NULL, 8, date_conv);
	}
}

static void
gnm_go_data_vector_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataVectorClass *vector_klass = (GODataVectorClass *) gobject_klass;

	vector_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= gnm_go_data_vector_finalize;
	godata_klass->dup		= gnm_go_data_dup;
	godata_klass->eq		= gnm_go_data_eq;
	godata_klass->preferred_fmt	= gnm_go_data_preferred_fmt;
	godata_klass->as_str		= gnm_go_data_as_str;
	godata_klass->from_str		= gnm_go_data_from_str;
	vector_klass->load_len		= gnm_go_data_vector_load_len;
	vector_klass->load_values	= gnm_go_data_vector_load_values;
	vector_klass->get_value		= gnm_go_data_vector_get_value;
	vector_klass->get_str		= gnm_go_data_vector_get_str;
}

static void
gnm_go_data_vector_debug_name (GnmDependent const *dep, FILE *out)
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

/*******************************************************************************/

static GnmDependent *
gnm_go_data_get_dep (GOData const *dat)
{
	if (IS_GNM_GO_DATA_SCALAR (dat))
		return &((GnmGODataScalar *)dat)->dep;
	if (IS_GNM_GO_DATA_VECTOR (dat))
		return &((GnmGODataVector *)dat)->dep;
	return NULL;
}
