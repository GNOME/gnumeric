/* vim: set sw=8: */

/*
 * graph-series.c: Support routines for graph series.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include "config.h"
#include "graph-series.h"
#include "idl/gnumeric-graphs.h"
#include <bonobo.h>
#include <gtk/gtkobject.h>
#include "dependent.h"
#include "expr.h"
#include "value.h"
#include "cell.h"
#include "gnumeric-type-util.h"

typedef enum { SERIES_SCALAR, SERIES_DATE, SERIES_STRING } GraphSeriesType;
struct _GraphSeries {
	GtkObject 	obj;
	Dependent 	dep;

	GraphSeriesType  type;
	gboolean	 is_column;
	Range		 range;	/* TODO : add support for discontinuous */
	char		*name;

	CORBA_Object    vector_ref;
	union {
		POA_GNOME_Gnumeric_VectorScalar		scalar;
		POA_GNOME_Gnumeric_VectorDate		date;
		POA_GNOME_Gnumeric_VectorString		string;
	} servant;

	/* The remote server monitoring this series */
	union {
		GNOME_Gnumeric_VectorScalarNotify	scalar;
		GNOME_Gnumeric_VectorDateNotify		date;
		GNOME_Gnumeric_VectorStringNotify	string;
	} subscriber;
};

typedef struct {
	GtkObjectClass parent_class;
} GraphSeriesClass;

#define GRAPH_SERIES_TYPE        (graph_series_get_type ())
#define GRAPH_SERIES(o)          (GTK_CHECK_CAST ((o), GRAPH_SERIES_TYPE, GraphSeries))
#define GRAPH_SERIES_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRAPH_SERIES_TYPE, GraphSeriesClass))
#define IS_GRAPH_SERIES(o)       (GTK_CHECK_TYPE ((o), GRAPH_SERIES_TYPE))
#define IS_GRAPH_SERIES_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRAPH_SERIES_TYPE))
#define DEP_TO_GRAPH_SERIES(ptr) (GraphSeries *)(((void *)ptr) - GTK_STRUCT_OFFSET(GraphSeries, dep))
#define SERVANT_TO_GRAPH_SERIES(ptr) (GraphSeries *)(((void *)ptr) - GTK_STRUCT_OFFSET(GraphSeries, servant))

static GtkType graph_series_get_type (void);

/***************************************************************************/

static GNOME_Gnumeric_SeqScalar *
graph_series_seq_scalar (GraphSeries *series)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqScalar *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqScalar__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_double_allocbuf (len);

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = series->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		values->_buffer [i] = value_get_as_float (elem);
	}

	value_release (v);

	return values;
}
static GNOME_Gnumeric_SeqDate *
graph_series_seq_date (GraphSeries *series)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqDate *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqDate__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_long_allocbuf (len);

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = series->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		values->_buffer [i] = value_get_as_int (elem);
	}

	return values;
}
static GNOME_Gnumeric_SeqString *
graph_series_seq_string (GraphSeries *series)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqString *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqString__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_string_allocbuf (len);

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = series->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		char * tmp = value_get_as_string (elem);
		values->_buffer [i] = CORBA_string_dup (tmp);
		g_free (tmp);
	}

	return values;
}

static void
graph_series_eval (Dependent *dep)
{
	CORBA_Environment ev;
	GraphSeries *series;

	series = DEP_TO_GRAPH_SERIES (dep);
	series = GRAPH_SERIES (series);

	g_return_if_fail (series != NULL);

	CORBA_exception_init (&ev);
	switch (series->type) {
	case SERIES_SCALAR :
		GNOME_Gnumeric_VectorScalarNotify_valueChanged (
			series->subscriber.scalar,
			0, graph_series_seq_scalar (series), &ev);
		break;

	case SERIES_DATE :
		GNOME_Gnumeric_VectorDateNotify_valueChanged (
			series->subscriber.date,
			0, graph_series_seq_date (series), &ev);
		break;

	case SERIES_STRING :
		GNOME_Gnumeric_VectorStringNotify_valueChanged (
			series->subscriber.string,
			0, graph_series_seq_string (series), &ev);
		break;

	default :
		g_assert_not_reached ();
	}
	if (ev._major != CORBA_NO_EXCEPTION)
		g_warning ("Problems notifying graph of change %p", series);
	CORBA_exception_free (&ev);
}

/******************************************************************************/

static void
impl_vector_scalar_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqScalar **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_SCALAR);

	*name = CORBA_string_dup (series->name);
	*values = graph_series_seq_scalar (series);
}

static void
impl_vector_date_value (PortableServer_Servant servant,
			GNOME_Gnumeric_SeqDate **values,
			CORBA_char **name,
			CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_DATE);

	*name = CORBA_string_dup (series->name);
	*values = graph_series_seq_date (series);
}

static void
impl_vector_string_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqString **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_STRING);

	*name = CORBA_string_dup (series->name);
	*values = graph_series_seq_string (series);
}

/******************************************************************************/

static void
impl_vector_scalar_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqScalar *vals,
			    CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_STRING);

	g_warning ("Gnumeric : scalar series changed remotely (%p)", series);
}

static void
impl_vector_date_changed (PortableServer_Servant servant,
			  const CORBA_short start,
			  const GNOME_Gnumeric_SeqDate *vals,
			  CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_DATE);

	g_warning ("Gnumeric : date series changed remotely (%p)", series);
}

static void
impl_vector_string_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqString *vals,
			    CORBA_Environment *ev)
{
	GraphSeries *series = SERVANT_TO_GRAPH_SERIES (servant);

	g_return_if_fail (GRAPH_SERIES (series) != NULL);
	g_return_if_fail (series->type == SERIES_STRING);

	g_warning ("Gnumeric : string series changed remotely (%p)", series);
}

/******************************************************************************/

static POA_GNOME_Gnumeric_VectorScalar__vepv vector_scalar_vepv;
static POA_GNOME_Gnumeric_VectorDate__vepv vector_date_vepv;
static POA_GNOME_Gnumeric_VectorString__vepv vector_string_vepv;

static void
corba_implementation_classes_init (void)
{
	static POA_GNOME_Gnumeric_VectorScalarNotify__epv
		vector_scalar_notify_epv;
	static POA_GNOME_Gnumeric_VectorScalar__epv
		vector_scalar_epv;
	static POA_GNOME_Gnumeric_VectorDateNotify__epv
		vector_date_notify_epv;
	static POA_GNOME_Gnumeric_VectorDate__epv
		vector_date_epv;
	static POA_GNOME_Gnumeric_VectorStringNotify__epv
		vector_string_notify_epv;
	static POA_GNOME_Gnumeric_VectorString__epv
		vector_string_epv;

	vector_scalar_notify_epv.valueChanged = &impl_vector_scalar_changed;
	vector_scalar_epv.value = & impl_vector_scalar_value;
	vector_scalar_vepv.GNOME_Gnumeric_VectorScalarNotify_epv =
		&vector_scalar_notify_epv;
	vector_scalar_vepv.GNOME_Gnumeric_VectorScalar_epv =
		&vector_scalar_epv;

	vector_date_notify_epv.valueChanged = & impl_vector_date_changed;
	vector_date_epv.value = & impl_vector_date_value;
	vector_date_vepv.GNOME_Gnumeric_VectorDateNotify_epv =
		&vector_date_notify_epv;
	vector_date_vepv.GNOME_Gnumeric_VectorDate_epv =
		&vector_date_epv;

	vector_string_notify_epv.valueChanged = & impl_vector_string_changed;
	vector_string_epv.value = & impl_vector_string_value;
	vector_string_vepv.GNOME_Gnumeric_VectorStringNotify_epv =
		&vector_string_notify_epv;
	vector_string_vepv.GNOME_Gnumeric_VectorString_epv =
		&vector_string_epv;
}

static void
graph_series_destroy (GtkObject *object)
{
	GraphSeries *series = GRAPH_SERIES(object);

	printf ("Destroying series %p\n", object);
	dependent_unlink (&series->dep, NULL);
	if (series->dep.expression != NULL) {
		expr_tree_unref (series->dep.expression);
		series->dep.expression = NULL;
	}
	if (series->name != NULL) {
		g_free (series->name);
		series->name = NULL;
	}
}

static void
graph_series_class_init (GtkObjectClass *object_class)
{
	static GtkObjectClass *graph_series_parent_class = NULL;

	graph_series_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = & graph_series_destroy;
	corba_implementation_classes_init ();
}

static void
graph_series_init (GtkObject *object)
{
	GraphSeries *series = GRAPH_SERIES (object);

	//series->type;
	series->vector_ref = CORBA_OBJECT_NIL;
	series->subscriber.scalar = CORBA_OBJECT_NIL;
	series->subscriber.date = CORBA_OBJECT_NIL;
	series->subscriber.string = CORBA_OBJECT_NIL;
}

static GNUMERIC_MAKE_TYPE (graph_series,"GraphSeries",GraphSeries,
			   &graph_series_class_init, &graph_series_init,
			   gtk_object_get_type ())


static void
graph_series_set_expr (Dependent *dep, ExprTree *expr)
{
	ParsePos pos;
	char * new_str;

	pos.sheet = dep->sheet;
	pos.eval.col = pos.eval.row = 0;
	new_str = expr_tree_as_string (expr, &pos);
	printf("new = %s\n", new_str);
	g_free (new_str);
	new_str = expr_tree_as_string (dep->expression, &pos);
	printf("old = %s\n", new_str);
	g_free (new_str);

	expr_tree_ref (expr);
	dependent_unlink (dep, NULL);
	expr_tree_unref (dep->expression);
	dep->expression = expr;
	dependent_changed (dep, NULL, TRUE);
}

static void
graph_series_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GraphSeries%p", dep);
}

static guint
graph_series_get_dep_type ()
{
	static guint32 type = 0;
	if (type == 0) {
		static DependentClass klass;
		klass.eval = &graph_series_eval;
		klass.set_expr = &graph_series_set_expr;
		klass.debug_name = &graph_series_debug_name;
		type = dependent_type_register (&klass);
	}
	return type;
}


/* TODO:
 * Do we need to evaluate this as an expression ?
 */
static Value *
cb_check_range_for_pure_string (Sheet *sheet, int col, int row,
				Cell *cell, void *user_data)
{
	if (cell == NULL || cell->value->type != VALUE_STRING)
		return value_terminate ();
	return NULL;
}

GraphSeries *
graph_series_new (Sheet *sheet, Range const *r, char *name)
{
	CORBA_Environment ev;
	PortableServer_Servant serv = CORBA_OBJECT_NIL;
	GraphSeries *series;
	GraphSeriesType type;

	series = gtk_type_new (graph_series_get_type ());

	if (sheet_cell_foreach_range (sheet, FALSE,
				      r->start.col, r->start.row,
				      r->end.col, r->end.row,
				      &cb_check_range_for_pure_string,
				      NULL))
		type = SERIES_SCALAR;
	else
		type = SERIES_STRING;

	printf ("series::new (%d) = 0x%p\n", type, series);
	series->type = type;
	series->is_column = (r->start.col == r->end.col);
	series->range = *r;
	series->name = name;

	series->dep.sheet = sheet;
	series->dep.flags = graph_series_get_dep_type ();
	series->dep.expression = expr_tree_new_constant (
		value_new_cellrange_r (sheet, r));
	dependent_changed (&series->dep, NULL, FALSE);

	CORBA_exception_init (&ev);
	switch (type) {
	case SERIES_SCALAR :
		serv = &series->servant.scalar;
		series->servant.scalar.vepv = &vector_scalar_vepv;
		POA_GNOME_Gnumeric_VectorScalar__init (serv, &ev);
		break;

	case SERIES_DATE :
		serv = &series->servant.date;
		series->servant.date.vepv = &vector_date_vepv;
		POA_GNOME_Gnumeric_VectorDate__init (serv, &ev);
		break;

	case SERIES_STRING :
		serv = &series->servant.string;
		series->servant.string.vepv = &vector_string_vepv;
		POA_GNOME_Gnumeric_VectorString__init (serv, &ev);
		break;

	default :
		g_assert_not_reached ();
	};

	if (ev._major == CORBA_NO_EXCEPTION) {
		PortableServer_POA poa = bonobo_poa ();
		PortableServer_ObjectId *oid;

		oid = PortableServer_POA_activate_object (poa, serv, &ev);
		CORBA_free (oid);
		series->vector_ref = PortableServer_POA_servant_to_reference (poa, serv, &ev);
	}
	CORBA_exception_free (&ev);

	return series;
}

void
graph_series_set_subscriber (GraphSeries *series, CORBA_Object graph_manager)
{
	CORBA_Environment ev;
	GNOME_Gnumeric_Graph_Manager manager = graph_manager;

	g_return_if_fail (series->subscriber.scalar == CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	switch (series->type) {
	case SERIES_SCALAR :
		series->subscriber.scalar =
			GNOME_Gnumeric_Graph_Manager_addVectorScalar (manager,
				series->vector_ref, &ev);
		break;

	case SERIES_DATE :
		series->subscriber.scalar =
			GNOME_Gnumeric_Graph_Manager_addVectorDate (manager,
				series->vector_ref, &ev);
		break;

	case SERIES_STRING :
		series->subscriber.scalar =
			GNOME_Gnumeric_Graph_Manager_addVectorString (manager,
				series->vector_ref, &ev);
		break;
	default :
		g_assert_not_reached();
	}
	if (ev._major != CORBA_NO_EXCEPTION)
		g_warning ("Problems registering series %p", series);
	CORBA_exception_free (&ev);
}
