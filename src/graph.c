/*
 * graph.c: The gnumeric specific data wrappers for GOffice
 *
 * Copyright (C) 2003-2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <graph.h>
#include <dependent.h>
#include <expr.h>
#include <cell.h>
#include <value.h>
#include <number-match.h>
#include <mathfunc.h>
#include <sheet.h>
#include <workbook.h>
#include <position.h>
#include <gnm-format.h>
#include <auto-format.h>
#include <ranges.h>
#include <parse-util.h>
#include <expr-impl.h>
#include <goffice/goffice.h>

#include <gsf/gsf-impl-utils.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

static char *
get_pending_str (const GOData *data)
{
	return g_object_get_data (G_OBJECT (data), "unserialize");
}

static GnmConventions *
get_pending_convs (const GOData *data)
{
	return g_object_get_data (G_OBJECT (data), "unserialize-convs");
}

static void
set_pending_str (const GOData *data, const char *str)
{
	g_object_set_data_full (G_OBJECT (data),
				"unserialize", g_strdup (str),
				g_free);
}

static void
set_pending_convs (GOData *data, const GnmConventions *convs)
{
	g_object_set_data_full (G_OBJECT (data),
				"unserialize-convs",
				gnm_conventions_ref ((gpointer)convs),
				(GDestroyNotify)gnm_conventions_unref);
}

/* ------------------------------------------------------------------------- */

static char *
render_val (GnmValue const *v, int i, int j,
	    GOFormat const *fmt, GnmEvalPos const *ep)
{
	GODateConventions const *date_conv;

	if (!v)
		return NULL;

	date_conv = ep->sheet ? sheet_date_conv (ep->sheet) : NULL;

#if 0
	g_printerr ("Rendering %s with fmt=%s\n",
		    value_peek_string (v),
		    fmt ? go_format_as_XL (fmt) : "-");
#endif

	if (VALUE_IS_CELLRANGE (v)) {
		Sheet *start_sheet, *end_sheet;
		GnmCell *cell;
		GnmRange r;

		gnm_rangeref_normalize (&v->v_range.cell, ep,
					&start_sheet, &end_sheet, &r);
		r.start.row += i;
		r.start.col += j;
		cell = sheet_cell_get (start_sheet, r.start.col, r.start.row);
		if (cell == NULL)
			return NULL;
		gnm_cell_eval (cell);
		v = cell->value;

		if (fmt == NULL)
			fmt = gnm_cell_get_format (cell);
	} else if (VALUE_IS_ARRAY (v))
		v = value_area_get_x_y (v, i, j, ep);

	return format_value (fmt, v, -1, date_conv);
}

/* ------------------------------------------------------------------------- */

static GnmDependent *gnm_go_data_get_dep (GOData const *obj);

static GOData *
gnm_go_data_dup (GOData const *src)
{
	GOData *dst = g_object_new (G_OBJECT_TYPE (src), NULL);
	GnmDependent const *src_dep = gnm_go_data_get_dep (src);
	GnmDependent *dst_dep = gnm_go_data_get_dep (dst);

	dst_dep->texpr = src_dep->texpr;
	if (dst_dep->texpr)
		gnm_expr_top_ref (dst_dep->texpr);

	if (src_dep->sheet)
		dependent_set_sheet (dst_dep, src_dep->sheet);

	if (dst_dep->texpr == NULL) {
		set_pending_str (dst, get_pending_str (src));
		set_pending_convs (dst, get_pending_convs (src));
	}

	return GO_DATA (dst);
}

static gboolean
gnm_go_data_eq (GOData const *data_a, GOData const *data_b)
{
	GnmDependent const *a = gnm_go_data_get_dep (data_a);
	GnmDependent const *b = gnm_go_data_get_dep (data_b);

	if (a->texpr == NULL && b->texpr == NULL) {
		if (go_str_compare (get_pending_str (data_a),
				    get_pending_str (data_b)))
			return FALSE;
		if (get_pending_convs (data_a) != get_pending_convs (data_b))
			return FALSE;
		return TRUE;
	}

	return a->texpr && b->texpr && gnm_expr_top_equal (a->texpr, b->texpr);
}

static GOFormat *
gnm_go_data_preferred_fmt (GOData const *dat)
{
	GnmEvalPos ep;
	GnmDependent const *dep = gnm_go_data_get_dep (dat);

	g_return_val_if_fail (dep != NULL, NULL);

	eval_pos_init_dep (&ep, dep);
	return dep->texpr
		? (GOFormat *)gnm_auto_style_format_suggest (dep->texpr, &ep)
		: NULL;
}

static GODateConventions const *
gnm_go_data_date_conv (GOData const *dat)
{
	GnmDependent const *dep = gnm_go_data_get_dep (dat);

	g_return_val_if_fail (dep != NULL, NULL);

	if (!dep->sheet)
		return NULL;

	return sheet_date_conv (dep->sheet);
}

static char *
gnm_go_data_serialize (GOData const *dat, gpointer user)
{
	GnmParsePos pp;
	GnmConventions const *convs = user;
	GnmDependent const *dep = gnm_go_data_get_dep (dat);
	char *res;
	if (dep->sheet == NULL)
		return g_strdup ("No sheet for GnmGOData");
	if (!convs) {
		g_warning ("NULL convs in gnm_go_data_serialize");
		convs = gnm_conventions_default;
	}

	parse_pos_init_dep (&pp, dep);

	res = GO_IS_DATA_VECTOR (dat)
		? gnm_expr_top_multiple_as_string (dep->texpr, &pp, convs)
		: gnm_expr_top_as_string (dep->texpr, &pp, convs);

#if 0
	g_printerr ("Serializing %s\n", res);
#endif
	return res;
}

static gboolean
gnm_go_data_unserialize (GOData *dat, char const *str, gpointer user)
{
	GnmConventions const *convs = user;
	GnmExprTop const *texpr;
	GnmParsePos   pp;
	GnmDependent *dep = gnm_go_data_get_dep (dat);
	size_t len;

	if (!convs) {
		g_warning ("NULL convs in gnm_go_data_serialize");
		convs = gnm_conventions_default;
	}

	/* It is too early in the life cycle to know where we
	 * are.  Wait until later when we parse the sheet */
	if (dep->sheet == NULL) {
		set_pending_str (dat, str);
		/* Ugh.  We assume that convs will stay valid.  */
		set_pending_convs (dat, convs);
		return TRUE;
	}

	parse_pos_init_dep (&pp, dep);
	texpr = gnm_expr_parse_str (str, &pp, (GO_IS_DATA_VECTOR (dat))?
	                            GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS:
		                    GNM_EXPR_PARSE_DEFAULT,
				    convs, NULL);
	// Bummer.  We have managed to create files containing (1,2)
	// which should be read as a vector.  See #492
	if (!texpr && GO_IS_DATA_VECTOR (dat) && ((len = strlen (str)) > 2) &&
	    str[0] == '(' && str[len - 1] == ')') {
		char *str2 = g_strndup (str + 1, len - 2);
		texpr = gnm_expr_parse_str (str2, &pp,
					    GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS,
					    convs, NULL);
		g_free (str2);
	}
	if (texpr != NULL) {
		dependent_set_expr (dep, texpr);
		gnm_expr_top_unref (texpr);
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

	if (dependent_is_linked (dep)) {
		dependent_unlink (dep);
		dep->sheet = NULL;
	}

	if (sheet == NULL)
		return;

	/* no expression?
	 * Do we need to parse one now that we have more context ? */
	if (dep->texpr == NULL) {
		char const *str = get_pending_str (dat);
		GnmConventions *convs = get_pending_convs (dat);
		if (str != NULL) { /* bingo */
			dep->sheet = sheet; /* cheat a bit */
			if (gnm_go_data_unserialize (dat, str, convs)) {
				set_pending_str (dat, NULL);
				set_pending_convs (dat, NULL);
				go_data_emit_changed (GO_DATA (dat));
			}
		}
	}

	dep->sheet = NULL;
	dependent_set_sheet (dep, sheet);
}

/**
 * gnm_go_data_get_sheet:
 * @dat: #GOData
 *
 * Returns: (transfer none): the sheet.
 **/
Sheet *
gnm_go_data_get_sheet (GOData const *dat)
{
	GnmDependent *dep = gnm_go_data_get_dep (dat);
	g_return_val_if_fail (dep != NULL, NULL);
	return dep->sheet;
}

GnmExprTop const *
gnm_go_data_get_expr (GOData const *dat)
{
	GnmDependent *dep = gnm_go_data_get_dep (dat);
	if (!dep)
		return NULL;
	return dep->texpr;
}

/**
 * gnm_go_data_foreach_dep:
 * @dat: #GOData
 * @so: #SheetObject
 * @func: (scope call):
 * @user: user data.
 *
 **/
void
gnm_go_data_foreach_dep (GOData *dat, SheetObject *so,
			 SheetObjectForeachDepFunc func, gpointer user)
{
	GnmDependent *dep = gnm_go_data_get_dep (dat);
	if (dep)
		func (dep, so, user);
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
		if (scalar->dep.texpr != NULL) {
			GnmEvalPos pos;

			eval_pos_init_dep (&pos, &scalar->dep);

			scalar->val = gnm_expr_top_eval
				(scalar->dep.texpr, &pos,
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

	value_release (scalar->val);
	scalar->val = NULL;
	g_free (scalar->val_str);
	scalar->val_str = NULL;
	go_data_emit_changed (GO_DATA (scalar));
}

static void
gnm_go_data_scalar_finalize (GObject *obj)
{
	GnmGODataScalar *scalar = (GnmGODataScalar *)obj;

	dependent_set_expr (&scalar->dep, NULL);
	value_release (scalar->val);
	scalar->val = NULL;
	g_free (scalar->val_str);
	scalar->val_str = NULL;

	scalar_parent_klass->finalize (obj);
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
	GOFormat const *fmt = NULL;

	if (scalar->val_str == NULL) {
		GnmEvalPos ep;

		eval_pos_init_dep (&ep, &scalar->dep);
		if (scalar->dep.texpr)
			fmt = gnm_auto_style_format_suggest (scalar->dep.texpr, &ep);
		scalar->val_str =
			render_val (scalar_get_val (scalar), 0, 0, fmt, &ep);
	}
	go_format_unref (fmt);
	return scalar->val_str;
}

static PangoAttrList const *
gnm_go_data_scalar_get_markup (GODataScalar *dat)
{
	PangoAttrList const *res = NULL;
	GOFormat const *fmt = gnm_go_data_preferred_fmt (GO_DATA (dat));
	if (fmt && go_format_is_markup (fmt))
		res = go_format_get_markup (fmt);
	go_format_unref (fmt);
	return res;
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
	godata_klass->date_conv		= gnm_go_data_date_conv;
	godata_klass->serialize		= gnm_go_data_serialize;
	godata_klass->unserialize	= gnm_go_data_unserialize;
	scalar_klass->get_value		= gnm_go_data_scalar_get_value;
	scalar_klass->get_str		= gnm_go_data_scalar_get_str;
	scalar_klass->get_markup	= gnm_go_data_scalar_get_markup;
}

static void
gnm_go_data_scalar_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "GraphScalar%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (gnm_go_data_scalar,
			    .eval = gnm_go_data_scalar_eval,
			    .debug_name = gnm_go_data_scalar_debug_name,
			    .q_array_context = FALSE)

static void
gnm_go_data_scalar_init (GObject *obj)
{
	GnmGODataScalar *scalar = (GnmGODataScalar *)obj;
	scalar->dep.flags = gnm_go_data_scalar_get_dep_type ();
}

GSF_CLASS (GnmGODataScalar, gnm_go_data_scalar,
	   gnm_go_data_scalar_class_init, gnm_go_data_scalar_init,
	   GO_TYPE_DATA_SCALAR)

GOData *
gnm_go_data_scalar_new_expr (Sheet *sheet, GnmExprTop const *texpr)
{
	GnmGODataScalar *res = g_object_new (gnm_go_data_scalar_get_type (), NULL);
	res->dep.texpr = texpr;
	res->dep.sheet = sheet;
	return GO_DATA (res);
}

/**************************************************************************/

struct _GnmGODataVector {
	GODataVector	base;
	GnmDependent	 dep;
	GnmValue	*val;
	gboolean	 as_col;
	GPtrArray       *markup;
	GPtrArray       *strs;
};
typedef GODataVectorClass GnmGODataVectorClass;

#define DEP_TO_VECTOR(d_ptr) (GnmGODataVector *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataVector, dep))

static GObjectClass *vector_parent_klass;

static void
gnm_go_data_vector_eval (GnmDependent *dep)
{
	GnmGODataVector *vec = DEP_TO_VECTOR (dep);

	value_release (vec->val);
	vec->val = NULL;
	if (vec->markup) {
		g_ptr_array_free (vec->markup, TRUE);
		vec->markup = NULL;
	}
	if (vec->strs) {
		g_ptr_array_free (vec->strs, TRUE);
		vec->strs = NULL;
	}
	go_data_emit_changed (GO_DATA (vec));
}

static void
gnm_go_data_vector_finalize (GObject *obj)
{
	GnmGODataVector *vec = (GnmGODataVector *)obj;

	dependent_set_expr (&vec->dep, NULL);
	value_release (vec->val);
	vec->val = NULL;

	g_free (vec->base.values);
	vec->base.values = NULL;
	if (vec->markup) {
		g_ptr_array_free (vec->markup, TRUE);
		vec->markup = NULL;
	}
	if (vec->strs) {
		g_ptr_array_free (vec->strs, TRUE);
		vec->strs = NULL;
	}

	vector_parent_klass->finalize (obj);
}

static void
gnm_go_data_vector_load_len (GODataVector *dat)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmEvalPos ep;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	int old_len = dat->len;
	guint64 new_len = 0;

	eval_pos_init_dep (&ep, &vec->dep);
	if (vec->val == NULL && vec->dep.texpr != NULL) {
		GSList *l = NULL;
		if (GNM_EXPR_GET_OPER (vec->dep.texpr->expr) == GNM_EXPR_OP_SET &&
		    gnm_expr_is_rangeref (vec->dep.texpr->expr) &&
		    ((l = gnm_expr_top_get_ranges (vec->dep.texpr))) &&
		    l->next != NULL) {
			unsigned len = g_slist_length (l);
			GSList *cur = l;
			unsigned i;
			vec->val = value_new_array_empty (len, 1);
			for (i = 0; i < len; i++) {
				vec->val->v_array.vals[i][0] = cur->data;
				cur = cur->next;
			}
		} else {
			if (l) {
				GSList *cur;
				for (cur = l; cur != NULL; cur = cur->next)
					value_release (cur->data);
			}
			vec->val = gnm_expr_top_eval_fake_array
				(vec->dep.texpr, &ep,
				 GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
				 GNM_EXPR_EVAL_PERMIT_EMPTY);
		}
		g_slist_free (l);
	}

	if (vec->val != NULL) {
		switch (vec->val->v_any.type) {
		case VALUE_CELLRANGE:
			gnm_rangeref_normalize (&vec->val->v_range.cell, &ep,
				&start_sheet, &end_sheet, &r);

			/* add +1 to max_used so that we can have matrices with
			 * empty cells at the end, see #684072 */
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used + 1;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used + 1;

			if (r.end.col >= r.start.col && r.end.row >= r.start.row) {
				guint w = range_width (&r);
				guint h = range_height (&r);
				vec->as_col = h > w;
				new_len = (guint64)h * w * (end_sheet->index_in_wb - start_sheet->index_in_wb + 1);
			}
			break;

		case VALUE_ARRAY : {
			GnmValue *v;
			int i, j;
			new_len = 0;
			for (j = 0; j < vec->val->v_array.y; j++)
				for (i = 0; i < vec->val->v_array.x; i++) {
					v = vec->val->v_array.vals[i][j];
					if (VALUE_IS_CELLRANGE (v)) {
						gnm_rangeref_normalize (&v->v_range.cell, &ep,
							&start_sheet, &end_sheet, &r);
						new_len += (guint64)range_width (&r) * range_height (&r)
							* (end_sheet->index_in_wb - start_sheet->index_in_wb + 1);
					} else
						new_len++;
				}
			vec->as_col = (vec->val->v_array.y > vec->val->v_array.x);
			break;
		}
		case VALUE_ERROR:
			new_len = 0;
			break;

		default:
			new_len = 1;
			vec->as_col = TRUE;
		}
	} else
		new_len = 0;

	/* Protect against overflow in ->len as well as when allocating ->values. */
	new_len = MIN (new_len, (gint64)(G_MAXINT / sizeof (dat->values[0])));
	dat->len = new_len;

	if (dat->values != NULL && old_len != dat->len) {
		g_free (dat->values);
		dat->values = NULL;
	}
	dat->base.flags |= GO_DATA_VECTOR_LEN_CACHED;
}

struct assign_closure {
	const GODateConventions *date_conv;
	double minimum, maximum;
	double *vals;
	gssize vals_len;
	guint64 last;
	guint64 i;
};

static GnmValue *
cb_assign_val (GnmCellIter const *iter, struct assign_closure *dat)
{
	GnmValue *v;
	double res;

	if ((gssize)dat->i >= dat->vals_len)
		return NULL;

	if (iter->cell != NULL) {
		gnm_cell_eval (iter->cell);
		v = iter->cell->value;
	} else
		v = NULL;

	if (VALUE_IS_EMPTY_OR_ERROR (v)) {
		dat->vals[dat->i++] = go_nan;
		return NULL;
	}

	dat->last = dat->i;
	if (VALUE_IS_STRING (v)) {
		v = format_match_number (value_peek_string (v), NULL,
					 dat->date_conv);
		if (v == NULL) {
			dat->vals[dat->i++] = go_nan;
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
	double *vals, minimum, maximum;
	GnmValue *v;
	struct assign_closure closure;

	(void)go_data_vector_get_len (dat); /* force calculation */

	if (dat->len <= 0 || !vec->dep.sheet) {
		dat->values = NULL;
		dat->minimum = go_nan;
		dat->maximum = go_nan;
		dat->base.flags |= GO_DATA_CACHE_IS_VALID;
		return;
	}

	closure.date_conv = sheet_date_conv (vec->dep.sheet);

	if (dat->values == NULL)
		dat->values = g_new (double, dat->len);
	vals = dat->values;
	switch (vec->val->v_any.type) {
	case VALUE_CELLRANGE:
		gnm_rangeref_normalize (&vec->val->v_range.cell,
			eval_pos_init_dep (&ep, &vec->dep),
			&start_sheet, &end_sheet, &r);

		/* clip here rather than relying on sheet_foreach
		 * because that only clips if we ignore blanks
		 * but add +1 to max_used so that we can have matrices with
		 * empty cells at the end, see #684072 */
		if (r.end.row > start_sheet->rows.max_used)
			r.end.row = start_sheet->rows.max_used + 1;
		if (r.end.col > start_sheet->cols.max_used)
			r.end.col = start_sheet->cols.max_used + 1;

		/* In case the sheet is empty */
		if (r.start.col <= r.end.col && r.start.row <= r.end.row) {
			closure.maximum = - G_MAXDOUBLE;
			closure.minimum = G_MAXDOUBLE;
			closure.vals = dat->values;
			closure.vals_len = dat->len;
			closure.last = -1;
			closure.i = 0;
			if (start_sheet != end_sheet)
				workbook_foreach_cell_in_range (&ep, vec->val,
				                                CELL_ITER_IGNORE_FILTERED,
				                                (CellIterFunc)cb_assign_val,
				                                &closure);
			else
				sheet_foreach_cell_in_range
					(start_sheet, CELL_ITER_IGNORE_FILTERED,
					 &r,
					 (CellIterFunc)cb_assign_val, &closure);
			dat->len = closure.last + 1; /* clip */
			minimum = closure.minimum;
			maximum = closure.maximum;
		} else
			minimum = maximum = vals[0] = go_nan;
		break;

	case VALUE_ARRAY : {
		guint64 last = 0, max = dat->len;
		int len = vec->val->v_array.y * vec->val->v_array.x;
		int x = 0, y = vec->val->v_array.y;
		GnmValue *v;
		maximum = - G_MAXDOUBLE;
		minimum = G_MAXDOUBLE;
		while (len-- > 0) {
			if (x == 0) {
				x = vec->val->v_array.x;
				y--;
			}
			x--;
			v = vec->val->v_array.vals [x][y];

			if (VALUE_IS_CELLRANGE (v)) {
				gnm_rangeref_normalize (&v->v_range.cell,
					eval_pos_init_dep (&ep, &vec->dep),
					&start_sheet, &end_sheet, &r);

				/* clip here rather than relying on sheet_foreach
				 * because that only clips if we ignore blanks */
				if (r.end.row > start_sheet->rows.max_used)
					r.end.row = start_sheet->rows.max_used;
				if (r.end.col > start_sheet->cols.max_used)
					r.end.col = start_sheet->cols.max_used;

				if (r.start.col <= r.end.col && r.start.row <= r.end.row) {
					closure.maximum = - G_MAXDOUBLE;
					closure.minimum = G_MAXDOUBLE;
					closure.vals = dat->values;
					closure.vals_len = max;
					closure.last = last - 1;
					closure.i = last;
					if (start_sheet != end_sheet)
						workbook_foreach_cell_in_range (&ep, vec->val,
								                CELL_ITER_IGNORE_FILTERED,
								                (CellIterFunc)cb_assign_val,
								                &closure);
					else
						sheet_foreach_cell_in_range (start_sheet,
									     CELL_ITER_IGNORE_FILTERED,
									     &r,
									     (CellIterFunc)cb_assign_val, &closure);
					last = dat->len = closure.last + 1; /* clip */
					if (minimum > closure.minimum)
					    minimum = closure.minimum;
					if (maximum < closure.maximum)
					maximum = closure.maximum;
				}
				continue;
			} else if (VALUE_IS_EMPTY_OR_ERROR (v)) {
				vals[len] = go_nan;
				continue;
			} else if (VALUE_IS_STRING (v)) {
				GnmValue *tmp = format_match_number
					(value_peek_string (v), NULL,
					 closure.date_conv);
				if (tmp == NULL) {
					vals[len] = go_nan;
					continue;
				}
				vals[len] = value_get_as_float (tmp);
				value_release (tmp);
			} else
				vals[len] = value_get_as_float (v);
			if (minimum > vals[len])
				minimum = vals[len];
			if (maximum < vals[len])
				maximum = vals[len];
		}
		break;
	}

	case VALUE_STRING:
		v = format_match_number (value_peek_string (vec->val),
					 NULL,
					 closure.date_conv);
		if (v != NULL) {
			minimum = maximum = vals[0] = value_get_as_float (v);
			value_release (v);
			break;
		}
		/* fall through to errors */

	case VALUE_EMPTY:
	case VALUE_ERROR:
		minimum = maximum = vals[0] = go_nan;
		break;
	default:
		minimum = maximum = vals[0] = value_get_as_float (vec->val);
		break;
	}

	dat->values = vals;
	dat->minimum = minimum;
	dat->maximum = maximum;
	dat->base.flags |= GO_DATA_CACHE_IS_VALID;
	if (go_finite (minimum) && go_finite (maximum) && minimum <= maximum)
		dat->base.flags |= GO_DATA_HAS_VALUE;

}

static double
gnm_go_data_vector_get_value (GODataVector *dat, unsigned i)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;

	if (vec->val == NULL)
		gnm_go_data_vector_load_len (dat);

	if (dat->len <= 0)
		return go_nan;

	if ((dat->base.flags & GO_DATA_CACHE_IS_VALID) == 0)
		gnm_go_data_vector_load_values (dat);
	return (i < (unsigned) dat->len)? dat->values[i]: go_nan;
}

struct string_closure {
	GPtrArray *strs;
	GODateConventions const *date_conv;
};

static gpointer
cb_assign_string (GnmCellIter const *iter, struct string_closure *closure)
{
	GnmValue *v = NULL;
	char *str = NULL;

	if (iter->cell != NULL) {
		gnm_cell_eval (iter->cell);
		v = iter->cell->value;
	}
	if (v != NULL)
		str = format_value (gnm_cell_get_format (iter->cell), v, -1, closure->date_conv);
	g_ptr_array_add (closure->strs, str);

	return NULL;
}

static char *
gnm_go_data_vector_get_str (GODataVector *dat, unsigned i)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;
	GnmEvalPos ep;
	int j;
	GOFormat const *fmt = NULL;
	char *ret = NULL;
	GnmValue *v = NULL;

	if (vec->val == NULL)
		gnm_go_data_vector_load_len (dat);
	g_return_val_if_fail (vec->val != NULL, NULL);

	eval_pos_init_dep (&ep, &vec->dep);
	if (VALUE_IS_ARRAY (vec->val)) {
		/* we need to cache the strings if needed */
		if (vec->strs == NULL) {
			int len = vec->val->v_array.y * vec->val->v_array.x;
			int x = 0, y = vec->val->v_array.y;
			struct string_closure closure;
			closure.strs = vec->strs = g_ptr_array_new_with_free_func (g_free);
			closure.date_conv = ep.sheet ? sheet_date_conv (ep.sheet) : NULL;
			while (len-- > 0) {
				if (x == 0) {
					x = vec->val->v_array.x;
					y--;
				}
				x--;
				v = vec->val->v_array.vals [x][y];

				if (VALUE_IS_CELLRANGE (v)) {
					/* actually we only need to cache in that case */
					Sheet *start_sheet, *end_sheet;
					GnmRange r;
					gnm_rangeref_normalize (&v->v_range.cell,
						eval_pos_init_dep (&ep, &vec->dep),
						&start_sheet, &end_sheet, &r);

					/* clip here rather than relying on sheet_foreach
					 * because that only clips if we ignore blanks */
					if (r.end.row > start_sheet->rows.max_used)
						r.end.row = start_sheet->rows.max_used;
					if (r.end.col > start_sheet->cols.max_used)
						r.end.col = start_sheet->cols.max_used;

					if (r.start.col <= r.end.col && r.start.row <= r.end.row)
						sheet_foreach_cell_in_range (start_sheet,
									     CELL_ITER_IGNORE_FILTERED,
									     &r,
									     (CellIterFunc)cb_assign_string, &closure);
				} else if (VALUE_IS_EMPTY_OR_ERROR (v)) {
					/* Not sure about what to do */
					/* The use of g_ptr_array_insert there and g-ptr_array_add
					 * for cell ranges looks like a bugs nest */
					g_ptr_array_insert (vec->strs, 0, g_strdup (""));
				} else {
					g_ptr_array_insert (vec->strs, 0, value_get_as_string (v));
				}
			}
		}
		if (vec->strs && vec->strs->len > i)
			ret = g_ptr_array_index (vec->strs, i);
		if (ret != NULL)
			return g_strdup (ret);
	} else if (VALUE_IS_CELLRANGE (vec->val)) {
		Sheet *start_sheet, *end_sheet;
		GnmRange r;
		if (vec->strs == NULL) {
			struct string_closure closure;
			closure.strs = vec->strs = g_ptr_array_new_with_free_func (g_free);
			closure.date_conv = ep.sheet ? sheet_date_conv (ep.sheet) : NULL;
			gnm_rangeref_normalize (&vec->val->v_range.cell,
				eval_pos_init_dep (&ep, &vec->dep),
				&start_sheet, &end_sheet, &r);

			/* clip here rather than relying on sheet_foreach
			 * because that only clips if we ignore blanks */
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;

			if (r.start.col <= r.end.col && r.start.row <= r.end.row)
				sheet_foreach_cell_in_range (start_sheet,
							     CELL_ITER_IGNORE_FILTERED,
							     &r,
							     (CellIterFunc)cb_assign_string, &closure);
		}
		if (vec->strs && vec->strs->len > i)
			ret = g_ptr_array_index (vec->strs, i);
		if (ret != NULL)
			return g_strdup (ret);
	}
	if (vec->as_col)
		j = 0;
	else
		j = i, i = 0;
	ret = render_val (((v != NULL)? v: vec->val), i, j, fmt, &ep);
	return ret;
}

static void
cond_pango_attr_list_unref (PangoAttrList *al)
{
	if (al)
		pango_attr_list_unref (al);
}

static gpointer
cb_assign_markup (GnmCellIter const *iter, GPtrArray *markup)
{
	PangoAttrList const *l = NULL;

	if (iter->cell != NULL) {
		GOFormat const *fmt = gnm_cell_get_format (iter->cell);
		if (go_format_is_markup (fmt))
			l = go_format_get_markup (fmt);
	}
	g_ptr_array_add (markup,
			 l ? pango_attr_list_ref ((PangoAttrList *)l) : NULL);

	return NULL;
}

static PangoAttrList *
gnm_go_data_vector_get_markup (GODataVector *dat, unsigned i)
{
	GnmGODataVector *vec = (GnmGODataVector *)dat;

	if (vec->markup == NULL) {
		/* load markups */
		GnmEvalPos ep;
		GnmRange r;
		Sheet *start_sheet, *end_sheet;
		GnmValue *v;

		go_data_vector_get_len (dat); /* force calculation */
		if (dat->len <= 0 || !vec->dep.sheet)
			return NULL;
		vec->markup = g_ptr_array_new_with_free_func
			((GDestroyNotify)cond_pango_attr_list_unref);
		switch (vec->val->v_any.type) {
		case VALUE_CELLRANGE:
			gnm_rangeref_normalize (&vec->val->v_range.cell,
				eval_pos_init_dep (&ep, &vec->dep),
				&start_sheet, &end_sheet, &r);

			/* clip here rather than relying on sheet_foreach
			 * because that only clips if we ignore blanks */
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;

			/* In case the sheet is empty */
			if (r.start.col <= r.end.col && r.start.row <= r.end.row) {
				sheet_foreach_cell_in_range (start_sheet,
							     CELL_ITER_ALL,
							     &r,
							     (CellIterFunc)cb_assign_markup, vec->markup);
			}
			break;

		case VALUE_ARRAY: {
			int len = vec->as_col? vec->val->v_array.y: vec->val->v_array.x;
			while (len-- > 0) {
				v = vec->as_col
					? vec->val->v_array.vals [0][len]
					: vec->val->v_array.vals [len][0];

				if (VALUE_IS_CELLRANGE (v)) {
					gnm_rangeref_normalize (&v->v_range.cell,
						eval_pos_init_dep (&ep, &vec->dep),
						&start_sheet, &end_sheet, &r);

					/* clip here rather than relying on sheet_foreach
					 * because that only clips if we ignore blanks */
					if (r.end.row > start_sheet->rows.max_used)
						r.end.row = start_sheet->rows.max_used;
					if (r.end.col > start_sheet->cols.max_used)
						r.end.col = start_sheet->cols.max_used;

					if (r.start.col <= r.end.col && r.start.row <= r.end.row)
						sheet_foreach_cell_in_range (start_sheet,
									     CELL_ITER_ALL,
									     &r,
									     (CellIterFunc)cb_assign_markup, vec->markup);
				}
			}
			break;
		}

		default:
			break;
		}
	}

	return pango_attr_list_copy ((vec->markup->len > i)?
	                             g_ptr_array_index (vec->markup, i): NULL);
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
	godata_klass->date_conv		= gnm_go_data_date_conv;
	godata_klass->serialize		= gnm_go_data_serialize;
	godata_klass->unserialize	= gnm_go_data_unserialize;
	vector_klass->load_len		= gnm_go_data_vector_load_len;
	vector_klass->load_values	= gnm_go_data_vector_load_values;
	vector_klass->get_value		= gnm_go_data_vector_get_value;
	vector_klass->get_str		= gnm_go_data_vector_get_str;
	vector_klass->get_markup	= gnm_go_data_vector_get_markup;
}

static void
gnm_go_data_vector_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "GraphVector%p", (void *)dep);
}
static DEPENDENT_MAKE_TYPE (gnm_go_data_vector,
			    .eval = gnm_go_data_vector_eval,
			    .debug_name = gnm_go_data_vector_debug_name,
			    .q_array_context = TRUE)

static void
gnm_go_data_vector_init (GObject *obj)
{
	GnmGODataVector *vec = (GnmGODataVector *)obj;
	vec->dep.flags = gnm_go_data_vector_get_dep_type ();
}

GSF_CLASS (GnmGODataVector, gnm_go_data_vector,
	   gnm_go_data_vector_class_init, gnm_go_data_vector_init,
	   GO_TYPE_DATA_VECTOR)

GOData *
gnm_go_data_vector_new_expr (Sheet *sheet, GnmExprTop const *texpr)
{
	GnmGODataVector *res = g_object_new (gnm_go_data_vector_get_type (), NULL);
	res->dep.texpr = texpr;
	res->dep.sheet = sheet;
	return GO_DATA (res);
}

/**************************************************************************/

struct _GnmGODataMatrix {
	GODataMatrix	base;
	GnmDependent	 dep;
	GnmValue	*val;
};
typedef GODataMatrixClass GnmGODataMatrixClass;

#define DEP_TO_MATRIX(d_ptr) (GnmGODataMatrix *)(((char *)d_ptr) - G_STRUCT_OFFSET (GnmGODataMatrix, dep))

static GObjectClass *matrix_parent_klass;

static void
gnm_go_data_matrix_eval (GnmDependent *dep)
{
	GnmGODataMatrix *mat = DEP_TO_MATRIX (dep);

	value_release (mat->val);
	mat->val = NULL;
	go_data_emit_changed (GO_DATA (mat));
}

static void
gnm_go_data_matrix_finalize (GObject *obj)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)obj;

	dependent_set_expr (&mat->dep, NULL);
	value_release (mat->val);
	mat->val = NULL;

	g_free (mat->base.values);
	mat->base.values = NULL;

	matrix_parent_klass->finalize (obj);
}

static void
gnm_go_data_matrix_load_size (GODataMatrix *dat)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)dat;
	GnmEvalPos ep;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	unsigned h, w;
	int old_rows = dat->size.rows, old_columns = dat->size.columns;

	eval_pos_init_dep (&ep, &mat->dep);
	if (mat->val == NULL && mat->dep.texpr) {
		mat->val = gnm_expr_top_eval (mat->dep.texpr, &ep,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
	}

	if (mat->val != NULL) {
		switch (mat->val->v_any.type) {
		case VALUE_CELLRANGE:
			gnm_rangeref_normalize (&mat->val->v_range.cell, &ep,
				&start_sheet, &end_sheet, &r);
			if (r.end.col > start_sheet->cols.max_used)
				r.end.col = start_sheet->cols.max_used;
			if (r.end.row > start_sheet->rows.max_used)
				r.end.row = start_sheet->rows.max_used;

			if (r.end.col >= r.start.col && r.end.row >= r.start.row) {
				w = range_width (&r);
				h = range_height (&r);
				if (w > 0 && h > 0) {
					dat->size.rows = h;
					dat->size.columns = w;
				}
				else {
					dat->size.rows = 0;
					dat->size.columns = 0;
				}
			} else {
				dat->size.rows = 0;
				dat->size.columns = 0;
			}
			break;

		case VALUE_ARRAY:
			dat->size.rows = mat->val->v_array.y;
			dat->size.columns = mat->val->v_array.x;
			break;

		default:
			dat->size.rows = 1;
			dat->size.columns = 1;
		}
	} else {
		dat->size.rows = 0;
		dat->size.columns = 0;
	}
	if (dat->values != NULL &&
			(old_rows != dat->size.rows || old_columns != dat->size.columns)) {
		g_free (dat->values);
		dat->values = NULL;
	}
	dat->base.flags |= GO_DATA_MATRIX_SIZE_CACHED;
}

struct assign_matrix_closure {
	const GODateConventions *date_conv;
	double minimum, maximum;
	double *vals;
	int first_row, first_col;
	int last_row, last_col;
	int row, col, columns, k;
};

static GnmValue *
cb_assign_matrix_val (GnmCellIter const *iter,
		      struct assign_matrix_closure *dat)
{
	GnmValue *v;
	double res;

	if (dat->first_col == -1)
		dat->first_col = iter->pp.eval.col;
	dat->col = iter->pp.eval.col - dat->first_col;
	if (dat->first_row == -1)
		dat->first_row = iter->pp.eval.row;
	dat->row = iter->pp.eval.row - dat->first_row;

	if (iter->cell != NULL) {
		gnm_cell_eval (iter->cell);
		v = iter->cell->value;
	} else
		v = NULL;


	if (VALUE_IS_EMPTY_OR_ERROR (v)) {
		dat->vals[dat->row * dat->columns + dat->col] = go_nan;
		return NULL;
	}

	if (dat->last_row < dat->row)
		dat->last_row = dat->row;
	if (dat->last_col < dat->col)
		dat->last_col = dat->col;

	if (VALUE_IS_STRING (v)) {
		v = format_match_number (value_peek_string (v), NULL,
					 dat->date_conv);
		if (v == NULL) {
			dat->vals[dat->row * dat->columns + dat->col] = go_nan;
			/* may be go_pinf should be more appropriate? */
			return NULL;
		}
		res = value_get_as_float (v);
		value_release (v);
	} else
		res = value_get_as_float (v);

	dat->vals[dat->row * dat->columns + dat->col] = res;
	if (dat->minimum > res)
		dat->minimum = res;
	if (dat->maximum < res)
		dat->maximum = res;
	return NULL;
}

static void
gnm_go_data_matrix_load_values (GODataMatrix *dat)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)dat;
	GnmEvalPos ep;
	GnmRange r;
	Sheet *start_sheet, *end_sheet;
	GODataMatrixSize size = go_data_matrix_get_size (dat); /* force calculation */
	double *vals, minimum, maximum;
	GnmValue *v;
	int row, col, cur;
	struct assign_matrix_closure closure;

	if (size.rows <= 0 || size.columns <= 0) {
		dat->values = NULL;
		dat->minimum = go_nan;
		dat->maximum = go_nan;
		dat->base.flags |= GO_DATA_CACHE_IS_VALID;
		return;
	}

	closure.date_conv = sheet_date_conv (mat->dep.sheet);

	if (dat->values == NULL)
		dat->values = g_new (double, size.rows * size.columns);
	vals = dat->values;
	switch (mat->val->v_any.type) {
	case VALUE_CELLRANGE:
		gnm_rangeref_normalize (&mat->val->v_range.cell,
			eval_pos_init_dep (&ep, &mat->dep),
			&start_sheet, &end_sheet, &r);

		/* In case the sheet is empty */
		if (r.start.col <= r.end.col && r.start.row <= r.end.row) {
			closure.maximum = - G_MAXDOUBLE;
			closure.minimum = G_MAXDOUBLE;
			closure.vals = dat->values;
			closure.first_row = closure.last_row = -1;
			closure.first_col = closure.last_col = -1;
			closure.row = closure.col = 0;
			closure.columns = dat->size.columns;
			sheet_foreach_cell_in_region (start_sheet, CELL_ITER_ALL,
				r.start.col, r.start.row,
				r.start.col + dat->size.columns - 1,
				r.start.row + dat->size.rows - 1,
				(CellIterFunc)cb_assign_matrix_val, &closure);
#warning "Should we clip the matrix?"
			minimum = closure.minimum;
			maximum = closure.maximum;
			if (minimum > maximum)
				minimum = maximum = go_nan;
		} else
			minimum = maximum = vals[0] = go_nan;
		break;

	case VALUE_ARRAY:
		maximum = - G_MAXDOUBLE;
		minimum = G_MAXDOUBLE;
		for (col = 0; col < size.columns; col ++)
			for (row = 0; row < size.rows; row++) {
				v = mat->val->v_array.vals[col][row];
				cur = col * size.rows + row;
				if (VALUE_IS_EMPTY_OR_ERROR (v)) {
					vals[row * size.columns + col] = go_nan;
					continue;
				} else if (VALUE_IS_STRING (v)) {
					GnmValue *tmp = format_match_number
						(value_peek_string (v), NULL,
						 closure.date_conv);
					if (tmp == NULL) {
						vals[cur] = go_nan;
						continue;
					}
					vals[cur] = value_get_as_float (tmp);
					value_release (tmp);
				} else
					vals[cur] = value_get_as_float (v);
				if (minimum > vals[cur])
					minimum = vals[cur];
				if (maximum < vals[cur])
					maximum = vals[cur];
			}
		if (minimum > maximum)
			minimum = maximum = go_nan;
		break;

	case VALUE_STRING:
		v = format_match_number (value_peek_string (mat->val),
					 NULL,
					 closure.date_conv);
		if (v != NULL) {
			vals[0] = value_get_as_float (v);
			minimum = maximum = go_nan;
			value_release (v);
			break;
		}
		/* fall through to errors */

	case VALUE_EMPTY:
	case VALUE_ERROR:
		minimum = maximum = vals[0] = go_nan;
		break;
	default:
		vals[0] = value_get_as_float (mat->val);
		minimum = maximum = go_nan;
		break;
	}

	dat->values = vals;
	dat->minimum = minimum;
	dat->maximum = maximum;
	dat->base.flags |= GO_DATA_CACHE_IS_VALID;
}

static double
gnm_go_data_matrix_get_value (GODataMatrix *dat, unsigned i, unsigned j)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)dat;
	GnmValue *v;
	GnmEvalPos ep;
	gboolean valid;

	if (mat->val == NULL)
		gnm_go_data_matrix_load_size (dat);

	eval_pos_init_dep (&ep, &mat->dep);
	/* i is row and j is column */
	v = value_dup (value_area_get_x_y (mat->val, j, i, &ep));
	if (NULL == v)
		return go_nan;

	v = value_coerce_to_number (v, &valid, &ep);
	if (valid) {
		gnm_float res = value_get_as_float (v);
		value_release (v);
		return res;
	}
	value_release (v);
	return go_nan;
}

static char *
gnm_go_data_matrix_get_str (GODataMatrix *dat, unsigned i, unsigned j)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)dat;
	GnmEvalPos ep;
	GOFormat const *fmt = NULL;

	if (mat->val == NULL)
		gnm_go_data_matrix_load_size (dat);
	g_return_val_if_fail (mat->val != NULL, NULL);

	eval_pos_init_dep (&ep, &mat->dep);
	return render_val (mat->val, i, j, fmt, &ep);
}

static void
gnm_go_data_matrix_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataMatrixClass *matrix_klass = (GODataMatrixClass *) gobject_klass;

	matrix_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize		= gnm_go_data_matrix_finalize;
	godata_klass->dup		= gnm_go_data_dup;
	godata_klass->eq		= gnm_go_data_eq;
	godata_klass->preferred_fmt	= gnm_go_data_preferred_fmt;
	godata_klass->date_conv		= gnm_go_data_date_conv;
	godata_klass->serialize		= gnm_go_data_serialize;
	godata_klass->unserialize	= gnm_go_data_unserialize;
	matrix_klass->load_size		= gnm_go_data_matrix_load_size;
	matrix_klass->load_values	= gnm_go_data_matrix_load_values;
	matrix_klass->get_value		= gnm_go_data_matrix_get_value;
	matrix_klass->get_str		= gnm_go_data_matrix_get_str;
}

static void
gnm_go_data_matrix_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "GraphMatrix%p", (void *)dep);
}
static DEPENDENT_MAKE_TYPE (gnm_go_data_matrix,
			    .eval = gnm_go_data_matrix_eval,
			    .debug_name = gnm_go_data_matrix_debug_name,
			    .q_array_context = TRUE)

static void
gnm_go_data_matrix_init (GObject *obj)
{
	GnmGODataMatrix *mat = (GnmGODataMatrix *)obj;
	mat->dep.flags = gnm_go_data_matrix_get_dep_type ();
}

GSF_CLASS (GnmGODataMatrix, gnm_go_data_matrix,
	   gnm_go_data_matrix_class_init, gnm_go_data_matrix_init,
	   GO_TYPE_DATA_MATRIX)

GOData *
gnm_go_data_matrix_new_expr (Sheet *sheet, GnmExprTop const *texpr)
{
	GnmGODataMatrix *res = g_object_new (gnm_go_data_matrix_get_type (), NULL);
	res->dep.texpr = texpr;
	res->dep.sheet = sheet;
	return GO_DATA (res);
}

/*******************************************************************************/

static GnmDependent *
gnm_go_data_get_dep (GOData const *dat)
{
	if (GNM_IS_GO_DATA_SCALAR (dat))
		return &((GnmGODataScalar *)dat)->dep;
	if (GNM_IS_GO_DATA_VECTOR (dat))
		return &((GnmGODataVector *)dat)->dep;
	if (GNM_IS_GO_DATA_MATRIX (dat))
		return &((GnmGODataMatrix *)dat)->dep;
	return NULL;
}
