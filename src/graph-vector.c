/* vim: set sw=8: */

/*
 * graph-vector.c: Support routines for graph vector.
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
#include "graph-vector.h"
#include "idl/gnumeric-graphs.h"
#include <bonobo.h>
#include <gtk/gtkobject.h>
#include "eval.h"
#include "expr.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "gnumeric-type-util.h"

typedef enum { VECTOR_SCALAR, VECTOR_DATE, VECTOR_STRING } GraphVectorType;
struct _GraphVector {
	GtkObject 	obj;
	Dependent 	dep;

	GraphVectorType  type;
	gboolean	 is_column;
	Range		 range;	/* TODO : add support for discontinuous */
	char		*name;

	CORBA_Object    vector_ref;	/* local CORBA object */
	union {
		POA_GNOME_Gnumeric_VectorScalar		scalar;
		POA_GNOME_Gnumeric_VectorDate		date;
		POA_GNOME_Gnumeric_VectorString		string;
	} servant;

	/* The remote server monitoring this vector */
	union {
		GNOME_Gnumeric_VectorScalarNotify	scalar;
		GNOME_Gnumeric_VectorDateNotify		date;
		GNOME_Gnumeric_VectorStringNotify	string;
	} subscriber;
};

typedef struct {
	GtkObjectClass parent_class;
} GraphVectorClass;

#define GRAPH_VECTOR_TYPE        (graph_vector_get_type ())
#define GRAPH_VECTOR(o)          (GTK_CHECK_CAST ((o), GRAPH_VECTOR_TYPE, GraphVector))
#define GRAPH_VECTOR_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRAPH_VECTOR_TYPE, GraphVectorClass))
#define IS_GRAPH_VECTOR(o)       (GTK_CHECK_TYPE ((o), GRAPH_VECTOR_TYPE))
#define IS_GRAPH_VECTOR_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRAPH_VECTOR_TYPE))
#define DEP_TO_GRAPH_VECTOR(ptr) (GraphVector *)(((char *)ptr) - GTK_STRUCT_OFFSET(GraphVector, dep))
#define SERVANT_TO_GRAPH_VECTOR(ptr) (GraphVector *)(((char *)ptr) - GTK_STRUCT_OFFSET(GraphVector, servant))

static GtkType graph_vector_get_type (void);

/***************************************************************************/

static GNOME_Gnumeric_SeqScalar *
graph_vector_seq_scalar (GraphVector *vector)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqScalar *values;

	pos.sheet = vector->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, vector->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqScalar__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_double_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		values->_buffer [i] = value_get_as_float (elem);
	}

	value_release (v);

	return values;
}
static GNOME_Gnumeric_SeqDate *
graph_vector_seq_date (GraphVector *vector)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqDate *values;

	pos.sheet = vector->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, vector->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqDate__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_long_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		values->_buffer [i] = value_get_as_int (elem);
	}

	return values;
}
static GNOME_Gnumeric_SeqString *
graph_vector_seq_string (GraphVector *vector)
{
	int i, len;
	Value *v;
	EvalPos pos;
	GNOME_Gnumeric_SeqString *values;

	pos.sheet = vector->dep.sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, vector->dep.expression, EVAL_PERMIT_NON_SCALAR);

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_SeqString__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_string_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		const char *tmp = value_peek_string (elem);
		values->_buffer[i] = CORBA_string_dup (tmp);
	}

	return values;
}

static void
graph_vector_eval (Dependent *dep)
{
	CORBA_Environment ev;
	GraphVector *vector;

	vector = DEP_TO_GRAPH_VECTOR (dep);
	vector = GRAPH_VECTOR (vector);

	g_return_if_fail (vector != NULL);

	CORBA_exception_init (&ev);
	switch (vector->type) {
	case VECTOR_SCALAR :
		GNOME_Gnumeric_VectorScalarNotify_valueChanged (
			vector->subscriber.scalar,
			0, graph_vector_seq_scalar (vector), &ev);
		break;

	case VECTOR_DATE :
		GNOME_Gnumeric_VectorDateNotify_valueChanged (
			vector->subscriber.date,
			0, graph_vector_seq_date (vector), &ev);
		break;

	case VECTOR_STRING :
		GNOME_Gnumeric_VectorStringNotify_valueChanged (
			vector->subscriber.string,
			0, graph_vector_seq_string (vector), &ev);
		break;

	default :
		g_assert_not_reached ();
	}
	if (ev._major != CORBA_NO_EXCEPTION)
		g_warning ("Problems notifying graph of change %p", vector);
	CORBA_exception_free (&ev);
}

/******************************************************************************/

static void
impl_vector_scalar_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqScalar **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_SCALAR);

	*name = CORBA_string_dup (vector->name);
	*values = graph_vector_seq_scalar (vector);
}

static void
impl_vector_date_value (PortableServer_Servant servant,
			GNOME_Gnumeric_SeqDate **values,
			CORBA_char **name,
			CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_DATE);

	*name = CORBA_string_dup (vector->name);
	*values = graph_vector_seq_date (vector);
}

static void
impl_vector_string_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_SeqString **values,
			  CORBA_char **name,
			  CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_STRING);

	*name = CORBA_string_dup (vector->name);
	*values = graph_vector_seq_string (vector);
}

/******************************************************************************/

static void
impl_vector_scalar_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqScalar *vals,
			    CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_STRING);

	g_warning ("Gnumeric : scalar vector changed remotely (%p)", vector);
}

static void
impl_vector_date_changed (PortableServer_Servant servant,
			  const CORBA_short start,
			  const GNOME_Gnumeric_SeqDate *vals,
			  CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_DATE);

	g_warning ("Gnumeric : date vector changed remotely (%p)", vector);
}

static void
impl_vector_string_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_SeqString *vals,
			    CORBA_Environment *ev)
{
	GraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (GRAPH_VECTOR (vector) != NULL);
	g_return_if_fail (vector->type == VECTOR_STRING);

	g_warning ("Gnumeric : string vector changed remotely (%p)", vector);
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
graph_vector_destroy (GtkObject *object)
{
	GraphVector *vector = GRAPH_VECTOR(object);

	printf ("Destroying vector %p\n", object);
	dependent_unlink (&vector->dep, NULL);
	if (vector->dep.expression != NULL) {
		expr_tree_unref (vector->dep.expression);
		vector->dep.expression = NULL;
	}
	if (vector->name != NULL) {
		g_free (vector->name);
		vector->name = NULL;
	}
}

static void
graph_vector_class_init (GtkObjectClass *object_class)
{
	static GtkObjectClass *graph_vector_parent_class = NULL;

	graph_vector_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = & graph_vector_destroy;
	corba_implementation_classes_init ();
}

static void
graph_vector_init (GtkObject *object)
{
	GraphVector *vector = GRAPH_VECTOR (object);

	/* vector->type; */
	vector->vector_ref = CORBA_OBJECT_NIL;
	vector->subscriber.scalar = CORBA_OBJECT_NIL;
	vector->subscriber.date = CORBA_OBJECT_NIL;
	vector->subscriber.string = CORBA_OBJECT_NIL;
}

static GNUMERIC_MAKE_TYPE (graph_vector,"GraphVector",GraphVector,
			   &graph_vector_class_init, &graph_vector_init,
			   gtk_object_get_type ())


static void
graph_vector_set_expr (Dependent *dep, ExprTree *expr)
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
graph_vector_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GraphVector%p", dep);
}

static guint
graph_vector_get_dep_type (void)
{
	static guint32 type = 0;
	if (type == 0) {
		static DependentClass klass;
		klass.eval = &graph_vector_eval;
		klass.set_expr = &graph_vector_set_expr;
		klass.debug_name = &graph_vector_debug_name;
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

GraphVector *
graph_vector_new (Sheet *sheet, Range const *r, char *name)
{
	CORBA_Environment ev;
	PortableServer_Servant serv = CORBA_OBJECT_NIL;
	GraphVector *vector;
	GraphVectorType type;

	vector = gtk_type_new (graph_vector_get_type ());

	if (sheet_foreach_cell_in_range (sheet, FALSE,
				      r->start.col, r->start.row,
				      r->end.col, r->end.row,
				      &cb_check_range_for_pure_string,
				      NULL))
		type = VECTOR_SCALAR;
	else
		type = VECTOR_STRING;

	printf ("vector::new (%d) = 0x%p\n", type, vector);
	vector->type = type;
	vector->is_column = (r->start.col == r->end.col);
	vector->range = *r;
	vector->name = name;

	vector->dep.sheet = sheet;
	vector->dep.flags = graph_vector_get_dep_type ();
	vector->dep.expression = expr_tree_new_constant (
		value_new_cellrange_r (sheet, r));
	dependent_link (&vector->dep, NULL);

	CORBA_exception_init (&ev);
	switch (type) {
	case VECTOR_SCALAR :
		serv = &vector->servant.scalar;
		vector->servant.scalar.vepv = &vector_scalar_vepv;
		POA_GNOME_Gnumeric_VectorScalar__init (serv, &ev);
		break;

	case VECTOR_DATE :
		serv = &vector->servant.date;
		vector->servant.date.vepv = &vector_date_vepv;
		POA_GNOME_Gnumeric_VectorDate__init (serv, &ev);
		break;

	case VECTOR_STRING :
		serv = &vector->servant.string;
		vector->servant.string.vepv = &vector_string_vepv;
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
		vector->vector_ref = PortableServer_POA_servant_to_reference (poa, serv, &ev);
	}
	CORBA_exception_free (&ev);

	return vector;
}

void
graph_vector_set_subscriber (GraphVector *vector, CORBA_Object graph_manager)
{
	CORBA_Environment ev;
	GNOME_Gnumeric_Graph_Manager manager = graph_manager;

	g_return_if_fail (vector->subscriber.scalar == CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	switch (vector->type) {
	case VECTOR_SCALAR :
		vector->subscriber.scalar =
			GNOME_Gnumeric_Graph_Manager_addVectorScalar (manager,
				vector->vector_ref, &ev);
		break;

	case VECTOR_DATE :
		vector->subscriber.date =
			GNOME_Gnumeric_Graph_Manager_addVectorDate (manager,
				vector->vector_ref, &ev);
		break;

	case VECTOR_STRING :
		vector->subscriber.string =
			GNOME_Gnumeric_Graph_Manager_addVectorString (manager,
				vector->vector_ref, &ev);
		break;
	default :
		g_assert_not_reached();
	}

	if (ev._major != CORBA_NO_EXCEPTION)
		g_warning ("Problems registering vector %p", vector);

	CORBA_exception_free (&ev);
}

void
graph_vector_unsubscribe (GraphVector *vector)
{
	CORBA_Environment ev;

	g_return_if_fail (vector->subscriber.scalar != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	switch (vector->type) {
	case VECTOR_SCALAR :
		GNOME_Gnumeric_VectorScalar_remove (vector->subscriber.scalar, &ev);
		break;

	case VECTOR_DATE :
		GNOME_Gnumeric_VectorDate_remove (vector->subscriber.date, &ev);
		break;

	case VECTOR_STRING :
		GNOME_Gnumeric_VectorString_remove (vector->subscriber.string, &ev);
		break;
	default :
		g_assert_not_reached();
	}

	if (ev._major != CORBA_NO_EXCEPTION)
		g_warning ("Problems unregistering vector %p", vector);

	CORBA_exception_free (&ev);
}
