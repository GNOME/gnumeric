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
#include "value.h"
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

	if (scalar->val != NULL) {
		value_release (scalar->val);
		scalar->val = NULL;
		g_free (scalar->val_str);
		scalar->val_str = NULL;
	}
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
};
typedef GODataVectorClass GnmGODataVectorClass;

#define DEP_TO_VECTOR(d_ptr) (GnmGODataVector *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataVector, dep))

static GObjectClass *vector_parent_klass;

static Value *
vector_get_val (GnmGODataVector *vector)
{
	if (vector->val == NULL) {
		EvalPos pos;
		vector->val = gnm_expr_eval (vector->dep.expression,
			eval_pos_init_dep (&pos, &vector->dep),
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
	}
	return vector->val;
}

static void
gnm_go_data_vector_eval (Dependent *dep)
{
	GnmGODataVector *vector = DEP_TO_VECTOR (dep);

	if (vector->val != NULL) {
		value_release (vector->val);
		vector->val = NULL;
	}
	go_data_emit_changed (GO_DATA (vector));
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
	GnmGODataVector const *vector = (GnmGODataVector const *)dat;
	return gnm_expr_as_string (vector->dep.expression,
		parse_pos_init_dep (&pp, &vector->dep),
		gnm_expr_conventions_default);
}

static int
gnm_go_data_vector_get_len (GODataVector *vec)
{
	return 0;
}

static double *
gnm_go_data_vector_get_values (GODataVector *vec)
{
#if 0
	int i, len;
	EvalPos ep;
	GNOME_Gnumeric_Scalar_Seq *values;
	Value *v = vector->value;

	eval_pos_init_dep (&ep, &vector->dep);
	len = (v == NULL) ? 1 : (vector->is_column
		? value_area_get_height (v, &ep)
		: value_area_get_width (v, &ep));

	values = GNOME_Gnumeric_Scalar_Seq__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_double_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* TODO : handle blanks */
	if (v == NULL) {
		values->_buffer[0] = 0.;
		return values;
	}

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (v, 0, i, &ep)
			: value_area_get_x_y (v, i, 0, &ep);

		if (elem == NULL) {
			values->_buffer [i] = 0.;	/* TODO : handle blanks */
			continue;
		} else if (elem->type == VALUE_STRING) {
			Value *tmp = format_match_number (elem->v_str.val->str, NULL);
			if (tmp != NULL) {
				values->_buffer [i] = value_get_as_float (tmp);
				value_release (tmp);
				continue;
			}
		}
		values->_buffer [i] = value_get_as_float (elem);
	}

	return values;
#endif
	return NULL;
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
	vector_klass->get_len		= gnm_go_data_vector_get_len;
	vector_klass->get_values	= gnm_go_data_vector_get_values;
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
