/*
 * Graph component-side vector representation
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *
 * (C) 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 */
#include <config.h>
#include <math.h>
#include <stdlib.h>
#include "idl/Graph.h"
#include "graph-vector.h"

static POA_GNOME_Gnumeric_VectorNotify__epv *vector_notify_epv;

static void
setup_notifier (GraphVector *v)
{
	g_warning ("Miguel should test before committing");
}

static void
destroy_notifier (GraphVector *vector)
{
}

GraphVector *
graph_vector_new (GNOME_Gnumeric_Vector vector, GraphVectorChangeNotifyFn change, void *data, gboolean guess)
{
	GraphVector *gv = g_new (GraphVector, 1);
	CORBA_Environment ev;
	
	gv->vector_object = vector;
	gv->contains_numbers = TRUE;
	gv->change = change;
	gv->change_data = data;
	
	CORBA_exception_init (&ev);
	if (guess)
		gv->contains_numbers = GNOME_Gnumeric_Vector_only_numbers (vector, &ev);

	/*
	 * FIXME: we should only transfer a few numbers, and load more on demand.
	 */
	if (gv->contains_numbers)
		gv->u.double_vec = GNOME_Gnumeric_Vector_get_numbers (vector, 0, -1, &ev);
	else
		gv->u.values_vec = GNOME_Gnumeric_Vector_get_vec_values (vector, 0, -1, &ev);

	if (ev._major != CORBA_NO_EXCEPTION){
		gv->u.double_vec = NULL;
		gv->u.values_vec = NULL;
	}

	setup_notifier (vector);

	CORBA_exception_free (&ev);
	return gv;
}

void
graph_vector_destroy (GraphVector *vector)
{
	CORBA_Environment ev;

	destroy_notifier (vector);
	
	CORBA_exception_init (&ev);
	GNOME_Gnumeric_Vector_unref (vector->vector_object, &ev);
	CORBA_exception_free (&ev);
	
	CORBA_free (vector->vector_object);
	
	if (vector->contains_numbers)
		CORBA_free (vector->u.double_vec);
	else
		CORBA_free (vector->u.values_vec);
	
	g_free (vector);
}

int
graph_vector_count (GraphVector *vector)
{
	/*
	 * FIXME: Note that we should cache the size of the actual
	 * vector, as this just works for the current code base (in
	 * which the entire vector is tranfered
	 */
	if (vector->contains_numbers)
		return vector->u.double_vec->_length;
	else
		return vector->u.values_vec->_length;
}

double
graph_vector_get_double (GraphVector *vector, int pos)
{
	g_return_val_if_fail (vector != NULL, 0.0);
	g_return_val_if_fail (pos >= 0, 0.0);

	if (pos >= graph_vector_count (vector))
		return 0.0;

	if (vector->contains_numbers)
		return vector->u.double_vec->_buffer [pos];
	else {
		GNOME_Gnumeric_VecValue *vv = &vector->u.values_vec->_buffer [pos];
		
		if (vv->_d == GNOME_Gnumeric_VALUE_FLOAT)
			return vv->_u.v_float;
		else
			return atof (vv->_u.str);
	}
}

char *
graph_vector_get_string (GraphVector *vector, int pos)
{
	g_return_val_if_fail (vector != NULL, g_strdup (""));
	g_return_val_if_fail (pos >= 0, g_strdup (""));

	if (pos >= graph_vector_count (vector))
		return g_strdup ("");

	if (vector->contains_numbers)
		return g_strdup_printf ("%g", vector->u.double_vec->_buffer [pos]);
	else {
		GNOME_Gnumeric_VecValue *vv = &vector->u.values_vec->_buffer [pos];
		
		if (vv->_d == GNOME_Gnumeric_VALUE_FLOAT)
			return g_strdup_printf ("%g", vv->_u.v_float);
		else
			return g_strdup (vv->_u.str);
	}
}

void
graph_vector_low_high (GraphVector *vector, double *low, double *high)
{
	const int n = graph_vector_count (vector);
	int i;

	*low = *high = 0;
	
	for (i = 0; i < n; i++){
		double v;
		
		v = graph_vector_get_double (vector, i);
		if (v < *low)
			*low = v;
		else if (v > *high)
			*high = v;
	}
}

int
graph_vector_buffer_size (GraphVector *vector)
{
	g_return_val_if_fail (vector != NULL, 0);
	
	/* For now, the current implementation keeps an entire copy of the data */

	return graph_vector_count (vector);
}
