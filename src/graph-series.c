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
#include "gnumeric-type-util.h"

typedef struct {
	POA_GNOME_Gnumeric_VectorScalar	servant;
	GraphSeries *series;
} VectorScalar;
typedef struct {
	POA_GNOME_Gnumeric_VectorDate	servant;
	GraphSeries *series;
} VectorDate;
typedef struct {
	POA_GNOME_Gnumeric_VectorString	servant;
	GraphSeries *series;
} VectorString;

typedef enum { SERIES_SCALAR, SERIES_DATE, SERIES_STRING } GraphSeriesType;
struct _GraphSeries {
	GtkObject obj;

	Dependent dep;

	GraphSeriesType type;
	CORBA_Object    vector_ref;
	union {
		VectorScalar	scalar;
		VectorDate	date;
		VectorString	string;
	} vector;
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

static GtkType graph_series_get_type (void);

/***************************************************************************/

static GNOME_Gnumeric_SeqScalar *
graph_series_seq_scalar (GraphSeries *series)
{
	int i;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqScalar *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	i = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqScalar__alloc ();
	values->_length = values->_maximum = i;
	values->_buffer = CORBA_sequence_CORBA_double_allocbuf (i);
	while (i-- > 0)
		values->_buffer [i] = value_get_as_float (value_area_get_x_y (&pos, v, 0, i));

	return values;
}
static GNOME_Gnumeric_SeqDate *
graph_series_seq_date (GraphSeries *series)
{
	int i;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqDate *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	i = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqDate__alloc ();
	values->_length = values->_maximum = i;
	values->_buffer = CORBA_sequence_CORBA_long_allocbuf (i);
	while (i-- > 0)
		values->_buffer [i] = value_get_as_int (value_area_get_x_y (&pos, v, 0, i));

	return values;
}
static GNOME_Gnumeric_SeqString *
graph_series_seq_string (GraphSeries *series)
{
	int i;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqString *values;

	pos.sheet = series->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, series->dep.expression, EVAL_PERMIT_NON_SCALAR);

	i = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqString__alloc ();
	values->_length = values->_maximum = i;
	values->_buffer = CORBA_sequence_CORBA_string_allocbuf (i);
	while (i-- > 0) {
		char *tmp = value_get_as_string (value_area_get_x_y (&pos, v, 0, i));
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
	GNOME_Gnumeric_VectorScalarNotify_changed (
		series->subscriber.scalar,
		0, graph_series_seq_scalar (series), &ev);
	CORBA_exception_free (&ev);
}

/******************************************************************************/

static void
impl_vector_scalar_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqScalar **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
	g_return_if_fail (series->type == SERIES_SCALAR);

	*name = CORBA_string_dup ("BOBO");
	*values = graph_series_seq_scalar (series);
}

static void
impl_vector_date_value (PortableServer_Servant servant,
			GNOME_Gnumeric_SeqDate **values,
			CORBA_char **name,
			CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
	g_return_if_fail (series->type == SERIES_DATE);

	*name = CORBA_string_dup ("BOBO");
	*values = graph_series_seq_date (series);
}

static void
impl_vector_string_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqString **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
	g_return_if_fail (series->type == SERIES_STRING);

	*name = CORBA_string_dup ("BOBO");
	*values = graph_series_seq_string (series);
}

/******************************************************************************/

static void
impl_vector_scalar_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqScalar *vals,
			    CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
	g_return_if_fail (series->type == SERIES_STRING);

	g_warning ("Gnumeric : scalar series changed remotely (%p)", series);
}

static void
impl_vector_date_changed (PortableServer_Servant servant,
			  const CORBA_short start,
			  const GNOME_Gnumeric_SeqDate *vals,
			  CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
	g_return_if_fail (series->type == SERIES_DATE);

	g_warning ("Gnumeric : date series changed remotely (%p)", series);
}

static void
impl_vector_string_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqString *vals,
			    CORBA_Environment *ev)
{
	GraphSeries *series = GRAPH_SERIES (((VectorScalar *) servant)->series);

	g_return_if_fail (series != NULL);
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

	vector_scalar_notify_epv.changed = &impl_vector_scalar_changed;
	vector_scalar_epv.value = & impl_vector_scalar_value;
	vector_scalar_vepv.GNOME_Gnumeric_VectorScalarNotify_epv =
		&vector_scalar_notify_epv;
	vector_scalar_vepv.GNOME_Gnumeric_VectorScalar_epv =
		&vector_scalar_epv;

	vector_date_notify_epv.changed = & impl_vector_date_changed;
	vector_date_epv.value = & impl_vector_date_value;
	vector_date_vepv.GNOME_Gnumeric_VectorDateNotify_epv =
		&vector_date_notify_epv;
	vector_date_vepv.GNOME_Gnumeric_VectorDate_epv =
		&vector_date_epv;

	vector_string_notify_epv.changed = & impl_vector_string_changed;
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
	printf ("Destroying %p\n", object);
	dependent_unlink (&series->dep, NULL);
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
	expr_tree_ref (expr);
	expr_tree_unref (dep->expression);
	dep->expression = expr;
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

GraphSeries *
graph_series_new (Sheet *sheet, Range const *r)
{
	CORBA_Environment ev;
	PortableServer_Servant serv = CORBA_OBJECT_NIL;
	GraphSeries *series;
	GraphSeriesType type = SERIES_SCALAR;

	series = gtk_type_new (graph_series_get_type ());

	printf ("series::new () = 0x%p\n", series);
	series->dep.sheet = sheet;
	series->dep.flags = graph_series_get_dep_type ();
	series->dep.expression = expr_tree_new_constant (
		value_new_cellrange_r (sheet, r));
	dependent_changed (&series->dep, NULL, TRUE);

	CORBA_exception_init (&ev);

	switch (type) {
	case SERIES_SCALAR :
		serv = &series->vector.scalar;
		series->vector.scalar.series = series;
		series->vector.scalar.servant.vepv = &vector_scalar_vepv;
		POA_GNOME_Gnumeric_VectorScalar__init (serv, &ev);
		break;

	case SERIES_DATE :
		serv = &series->vector.date;
		series->vector.date.series = series;
		series->vector.date.servant.vepv = &vector_date_vepv;
		POA_GNOME_Gnumeric_VectorDate__init (serv, &ev);
		break;

	case SERIES_STRING :
		serv = &series->vector.string;
		series->vector.string.series = series;
		series->vector.string.servant.vepv = &vector_string_vepv;
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

CORBA_Object
graph_series_servant (GraphSeries const *series)
{
	g_return_val_if_fail (IS_GRAPH_SERIES (series), CORBA_OBJECT_NIL);

	return series->vector_ref;
}

void
graph_series_set_subscriber (GraphSeries *series, CORBA_Object obj)
{
	g_return_if_fail (series->subscriber.scalar == CORBA_OBJECT_NIL);
	series->subscriber.scalar = obj;
}
