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
#include <bonobo/bonobo-main.h>
#include "idl/Graph.h"
#include "graph-vector.h"

static POA_GNOME_Gnumeric_VectorNotify__vepv vector_notify_vepv;
static POA_GNOME_Gnumeric_VectorNotify__epv  vector_notify_epv;

static void
impl_changed (PortableServer_Servant servant,
	      const CORBA_short low, const CORBA_short high,
	      CORBA_Environment *ev)
{
	NotifierServer *ns = (NotifierServer *) servant;
	GraphVector *gv = ns->graph_vector;
	gboolean only_numbers;
	GNOME_Gnumeric_Vector vector = gv->vector_object;
	CORBA_Environment myev;

	CORBA_exception_init (&myev);
	
	/*
	 * Find out if the vector still contains only numbers
	 */
	only_numbers = GNOME_Gnumeric_Vector_only_numbers (vector, &myev);
	if (ev->_major != CORBA_NO_EXCEPTION){
		CORBA_exception_free (&myev);
		return;
	}

	/*
	 * Process according to the current vector state
	 */
	if (gv->contains_numbers){
		if (only_numbers){
			GNOME_Gnumeric_DoubleVec *vals;
			int j, i;
			
			/*
			 * Reload the changed range
			 */
			vals = GNOME_Gnumeric_Vector_get_numbers (vector, low, high, &myev);
			if (ev->_major != CORBA_NO_EXCEPTION){
				CORBA_exception_free (&myev);
				return;
			}
			
			for (i = low, j = 0; i < high; i++, j++)
				gv->u.double_vec->_buffer [i] = vals->_buffer [j];

			CORBA_free (vals);
		} else {
			/*
			 * The vector no longer contains only numbers, flush it.
			 *
			 * FIXME: We could be smarter and convert our array to the
			 * VecValue manually, and just reload the [low,high] set of values.
			 */
			GNOME_Gnumeric_VecValueVec *vals;
			
			vals = GNOME_Gnumeric_Vector_get_vec_values (vector, 0, -1, &myev);
			if (ev->_major != CORBA_NO_EXCEPTION){
				CORBA_exception_free (&myev);
				return;
			}
			
			CORBA_free (gv->u.double_vec);
			gv->u.values_vec = vals;
			gv->contains_numbers = FALSE;
		}
	} else {
		
		if (only_numbers){
			/*
			 * The vector now only contains numbers, flush the values,
			 * and load the numbers.
			 *
			 * FIXME: We could be smarter and convert our array to the
			 * DoubleVec manually, and just reload the [low,high] set of values.
			 */
			GNOME_Gnumeric_DoubleVec *vals;
			
			vals = GNOME_Gnumeric_Vector_get_numbers (vector, 0, -1, &myev);
			if (ev->_major != CORBA_NO_EXCEPTION){
				CORBA_exception_free (&myev);
				return;
			}
			
			CORBA_free (gv->u.values_vec);
			gv->contains_numbers = TRUE;
			gv->u.double_vec = vals;
		} else {
			GNOME_Gnumeric_VecValueVec *vals;
			int j, i;
			
			/*
			 * Reload the changed range
			 */
			vals = GNOME_Gnumeric_Vector_get_vec_values (vector, low, high, &myev);
			if (ev->_major != CORBA_NO_EXCEPTION){
				CORBA_exception_free (&myev);
				return;
			}
			
			for (i = low, j = 0; i < high; i++, j++){
				if (gv->u.values_vec->_buffer [i]._d == GNOME_Gnumeric_VALUE_STRING){
					CORBA_free (gv->u.values_vec->_buffer [i]._u.str);
				}
				
				gv->u.values_vec->_buffer [i]._d = vals->_buffer [j]._d;
				
				if (vals->_buffer [j]._d == GNOME_Gnumeric_VALUE_STRING){
					gv->u.values_vec->_buffer [i]._u.str =
						CORBA_string_dup (vals->_buffer [j]._u.str);
				} else {
					gv->u.values_vec->_buffer [i]._u.v_float =
						vals->_buffer [j]._u.v_float;
				}
			}
			CORBA_free (vals);
		}
	}
	(*gv->change)(gv, low, high, gv->change_data);

	CORBA_exception_free (&myev);
}

/*
* This creates a CORBA server for the VectorNotify interface, and activates
* it.  This one is invoked when the data changes on the spreadsheet
*/
static gboolean
setup_notifier (GraphVector *v)
{
	CORBA_Environment ev;
	
	vector_notify_epv.changed = impl_changed;
	vector_notify_vepv.GNOME_Gnumeric_VectorNotify_epv = &vector_notify_epv;

	v->notifier_server = g_new0 (NotifierServer, 1);
	v->notifier_server->corba_server.vepv = &vector_notify_vepv;
	v->notifier_server->graph_vector = v;

	CORBA_exception_init (&ev);
        POA_GNOME_Gnumeric_VectorNotify__init ((PortableServer_Servant) v->notifier_server, &ev);
        if (ev._major != CORBA_NO_EXCEPTION){
                g_free (v->notifier_server);
                CORBA_exception_free (&ev);
                return FALSE;
        }
	CORBA_free (PortableServer_POA_activate_object (
                bonobo_poa (), v->notifier_server, &ev));
	v->corba_object_reference = PortableServer_POA_servant_to_reference (
                bonobo_poa(), v->notifier_server, &ev);

        CORBA_exception_free (&ev);

	return TRUE;
}

static void
destroy_notifier (GraphVector *vector)
{
	CORBA_Environment ev;
	PortableServer_ObjectId *oid;

	if (!vector->notifier_server)
		return;

	CORBA_exception_init (&ev);
	CORBA_Object_release (vector->vector_object, &ev);
	oid = PortableServer_POA_servant_to_id (bonobo_poa(), vector->notifier_server, &ev);
	PortableServer_POA_deactivate_object (bonobo_poa (), oid, &ev);
	CORBA_Object_release (vector->corba_object_reference, &ev);
	POA_GNOME_Gnumeric_VectorNotify__fini (vector->notifier_server, &ev);
	CORBA_free(oid);
	CORBA_exception_free (&ev);
}

GraphVector *
graph_vector_new (GNOME_Gnumeric_Vector vector, GraphVectorChangeNotifyFn change, void *data, gboolean guess)
{
	GraphVector *gv = g_new (GraphVector, 1);
	CORBA_Environment ev;

	g_assert (change != NULL);
	g_assert (vector != NULL);
	
	CORBA_exception_init (&ev);
	
	gv->vector_object = CORBA_Object_duplicate (vector, &ev);
	gv->contains_numbers = TRUE;
	gv->change = change;
	gv->change_data = data;
	
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
	CORBA_exception_free (&ev);

	if (!setup_notifier (gv)){
		graph_vector_destroy (gv);
		return NULL;
	}

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

	if (vector->contains_numbers){
		return vector->u.double_vec->_buffer [pos];
	} else {
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
