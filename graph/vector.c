/*
 * Vector CORBA server
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support
 */
#include <config.h>
#include "Graph.h"
#include <bonobo/gnome-object.h>
#include "vector.h"

static GnomeObjectClass *vector_parent_class;

/* The entry point vectors for the server we provide */
POA_GNOME_Gnumeric_Vector__epv  vector_epv;
POA_GNOME_Gnumeric_Vector__vepv vector_vepv;

#define vector_from_servant(x) VECTOR (gnome_object_from_servant (x))

static void
vector_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (vector_parent_class)->destroy (object);
}

static CORBA_boolean
impl_vector_only_numbers (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);

	return vec->type (vec->user_data);
}

static GNOME_Gnumeric_DoubleVec *
impl_vector_get_numbers (PortableServer_Servant servant,
			 CORBA_short low, CORBA_short high,
			 CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);

	return vec->get_numbers (low, high, vec->user_data);
}

static GNOME_Gnumeric_VecValueVec *
impl_vector_get_vec_values (PortableServer_Servant servant,
			    CORBA_short low, CORBA_short high,
			    CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);

	return vec->get_values (low, high, vec->user_data);
}

static void
impl_vector_set (PortableServer_Servant servant, CORBA_short pos,
		 CORBA_double val, CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);
	
	if (vec->set (pos, val, ev, vec->user_data))
		return;
	else
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Vector_Failed, NULL);
}

static void
impl_vector_set_notify (PortableServer_Servant servant,
			GNOME_Gnumeric_VectorNotify vector_notify,
			CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);

	vec->notify = vector_notify;
}

static CORBA_short
impl_vector_count (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Vector *vec = vector_from_servant (servant);

	return vec->len (vec->user_data);
}

static void
init_vector_corba_class (void)
{
	vector_epv.only_numbers   = impl_vector_only_numbers;
	vector_epv.get_numbers    = impl_vector_get_numbers;
	vector_epv.get_vec_values = impl_vector_get_vec_values;
	vector_epv.set            = impl_vector_set;
	vector_epv.count          = impl_vector_count;
	vector_epv.set_notify     = impl_vector_set_notify;

	vector_vepv.GNOME_Unknown_epv = &gnome_object_epv;
	vector_vepv.GNOME_Gnumeric_Vector_epv = &vector_epv;
}

static void
vector_class_init (GtkObjectClass *object_class)
{
	vector_parent_class = gtk_type_class (gnome_object_get_type ());
	
	object_class->destroy = vector_destroy;
	
	init_vector_corba_class ();
}

static void
vector_init (GtkObject *object)
{
	Vector *vector = VECTOR (object);

	vector->notify = CORBA_OBJECT_NIL;
}

GtkType
vector_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GnumericVector",
			sizeof (Vector),
			sizeof (VectorClass),
			(GtkClassInitFunc) vector_class_init,
			(GtkObjectInitFunc) vector_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_object_get_type (), &info);
	}

	return type;
}

GNOME_Gnumeric_Vector
vector_corba_object_create (GnomeObject *object)
{
	POA_GNOME_Gnumeric_Vector *servant;
	CORBA_Environment ev;
	
	servant = (POA_GNOME_Gnumeric_Vector *) g_new0 (GnomeObjectServant, 1);
	servant->vepv = &vector_vepv;

	CORBA_exception_init (&ev);
	POA_GNOME_Gnumeric_Vector__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	CORBA_exception_free (&ev);
	return (GNOME_View) gnome_object_activate_servant (object, servant);
}

Vector *
vector_new (VectorGetNumFn get_numbers, VectorGetValFn get_values,
	    VectorSetFn set, VectorLenFn len,
	    GNOME_Gnumeric_VectorNotify notify,
	    void *data)
{
	Vector *vector;
	GNOME_Gnumeric_Vector corba_vector;
	
	g_return_val_if_fail (get_numbers != NULL, NULL);
	g_return_val_if_fail (get_values != NULL, NULL);
	g_return_val_if_fail (set != NULL, NULL);
	g_return_val_if_fail (notify != NULL, NULL);
	g_return_val_if_fail (len != NULL, NULL);

	vector = gtk_type_new (vector_get_type ());

	corba_vector = vector_corba_object_create (GNOME_OBJECT (vector));
	if (corba_vector == NULL){
		gtk_object_destroy (GTK_OBJECT (vector));
		return NULL;
	}
	
	gnome_object_construct (GNOME_OBJECT (vector), corba_vector);

	vector->get_numbers = get_numbers;
	vector->get_values = get_values;
	vector->set = set;
	vector->notify = notify;
	vector->len = len;
	
	return vector;
}
	
void
vector_changed (Vector *vector, int low, int high)
{
	CORBA_Environment ev;
	
	g_return_if_fail (vector != NULL);
	g_return_if_fail (IS_VECTOR (vector));

	if (vector->notify == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);
	GNOME_Gnumeric_VectorNotify_changed (vector->notify, low, high, &ev);
	CORBA_exception_free (&ev);
}
