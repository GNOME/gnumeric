/* vim: set sw=8: */

/*
 * graph-vector.c: Support routines for graph vector.
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jgoldberg@home.com)
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

#include <config.h>
#include "gnumeric-graph.h"
#include "eval.h"
#include "expr.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook-private.h"

char const *const gnm_graph_vector_type_name [] =
{
    "Unknown", "scalars", "dates (unimplemented)", "strings",
};

#include "sheet-object-container.h"
#include "idl/gnumeric-graphs.h"
#include <bonobo.h>
#include <liboaf/liboaf.h>
#include <gal/util/e-util.h>

#define	MANAGER		  GNOME_Gnumeric_Graph_Manager_v2
#define	MANAGER1(suffix)  GNOME_Gnumeric_Graph_Manager_v2_ ## suffix
#define	MANAGER_OAF	 "IDL:GNOME/Gnumeric/Graph/Manager_v2:1.0"

struct _GnmGraph {
	SheetObjectContainer	parent;

	GPtrArray		*vectors;
	BonoboObjectClient	*manager_client;
	MANAGER	 		 manager;
};

typedef struct {
	SheetObjectContainerClass parent;
} GnmGraphClass;

struct _GnmGraphVector {
	GtkObject 	obj;
	Dependent 	dep;

	GnmGraphVectorType  type;
	gboolean	 is_column;
	Value		*value;
	GnmGraph	*graph;
	gboolean	 initialized : 1;
	gboolean	 activated : 1;
	int		 id;

	CORBA_Object    corba_obj;	/* local CORBA object */
	union {
		POA_GNOME_Gnumeric_Scalar_Vector	scalar;
		POA_GNOME_Gnumeric_Date_Vector		date;
		POA_GNOME_Gnumeric_String_Vector	string;
		PortableServer_POA			any;
	} servant;

	/* The remote server monitoring this vector */
	union {
		GNOME_Gnumeric_Scalar_Vector		scalar;
		GNOME_Gnumeric_Date_Vector		date;
		GNOME_Gnumeric_String_Vector		string;
		CORBA_Object				any;
	} subscriber;
};

typedef struct {
	GtkObjectClass parent_class;
} GnmGraphVectorClass;

#define DEP_TO_GRAPH_VECTOR(ptr)	\
	(GnmGraphVector *)(((char *)ptr) - GTK_STRUCT_OFFSET(GnmGraphVector, dep))
#define SERVANT_TO_GRAPH_VECTOR(ptr)	\
	(GnmGraphVector *)(((char *)ptr) - GTK_STRUCT_OFFSET(GnmGraphVector, servant))

/***************************************************************************/

static void
gnm_graph_clear_vectors_internal (GnmGraph *graph, gboolean unsubscribe)
{
	int i;

	/* Release the vectors */
	g_return_if_fail (graph->vectors != NULL);

	for (i = graph->vectors->len; i-- > 0 ; ) {
		GnmGraphVector *vector = g_ptr_array_index (graph->vectors, i);

		if (vector == NULL)
			continue;

		vector->graph = NULL;
		gtk_object_unref (GTK_OBJECT (vector));
	}
	if (unsubscribe) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		MANAGER1 (clearVectors) (graph->manager, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : while clearing the vectors in graph %p",
				   bonobo_exception_get_text (&ev), graph);
		}
		CORBA_exception_free (&ev);
	}

}

static void
gnm_graph_add_vector (GnmGraph *graph, GnmGraphVector *vector)
{
	CORBA_Environment ev;
	int id;

	g_return_if_fail (vector->subscriber.any == CORBA_OBJECT_NIL);
	g_return_if_fail (vector->graph == NULL);
	g_return_if_fail (IS_GNUMERIC_GRAPH (graph));

	CORBA_exception_init (&ev);

	id = graph->vectors->len;
	switch (vector->type) {
	case GNM_VECTOR_SCALAR :
		vector->subscriber.scalar = MANAGER1 (addScalarVector) (
			graph->manager, vector->corba_obj, id, &ev);
		break;

	case GNM_VECTOR_DATE :
		vector->subscriber.date = MANAGER1 (addDateVector) (
			graph->manager, vector->corba_obj, id, &ev);
		break;

	case GNM_VECTOR_STRING :
		vector->subscriber.string = MANAGER1 (addStringVector) (
			graph->manager, vector->corba_obj, id, &ev);
		break;

	default :
		g_assert_not_reached();
	}

	if (ev._major == CORBA_NO_EXCEPTION) {
		g_ptr_array_add (graph->vectors, vector);
		vector->graph = graph;
		vector->id = id;
	} else {
		g_warning ("'%s' : while subscribing vector %p",
			   bonobo_exception_get_text (&ev), vector);
	}

	CORBA_exception_free (&ev);
}

static char *
oaf_exception_id (CORBA_Environment *ev)
{
        if (ev->_major == CORBA_USER_EXCEPTION) {
                if (!strcmp (ev->_repo_id, "IDL:OAF/GeneralError:1.0")) {
                        OAF_GeneralError *err = ev->_params;
                        
                        if (!err || !err->description) {
                                return "No general exception error message";
                        } else {
                                return err->description;
                        }
                } else {
                        return ev->_repo_id;
                }
        } else {
                return CORBA_exception_id (ev);
        }
}

/* FIXME : Should we take a CommandContext to report errors to ? */
GnmGraph *
gnm_graph_new (Workbook *wb)
{
	CORBA_Environment  ev;
	Bonobo_Unknown	   o;
	GnmGraph	  *graph = NULL;

	CORBA_exception_init (&ev);

	o = (Bonobo_Unknown)oaf_activate ("repo_ids.has('" MANAGER_OAF "')",
					  NULL, 0, NULL, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while attempting to activate a graphing component",
			   oaf_exception_id (&ev));
	} else if (o == CORBA_OBJECT_NIL) {
		g_warning ("No graphing component is installed.  Oaf has nothing registered that implements the required interface.\noaf-query 'repo_ids.has('" MANAGER_OAF "')\nshould return a value.");
	} else {
		graph = gtk_type_new (GNUMERIC_GRAPH_TYPE);

		printf ("gnumeric : graph new %p\n", graph);

		graph->vectors = g_ptr_array_new ();
		graph->manager = Bonobo_Unknown_queryInterface (o, MANAGER_OAF, &ev);

		g_return_val_if_fail (graph->manager != NULL, NULL);

		graph->manager_client = bonobo_object_client_from_corba (graph->manager);
		bonobo_object_release_unref (o, &ev);

		if (sheet_object_bonobo_construct (SHEET_OBJECT_BONOBO (graph),
				wb->priv->bonobo_container, NULL) == NULL ||
		    !sheet_object_bonobo_set_server (SHEET_OBJECT_BONOBO (graph),
				graph->manager_client)) {
			gtk_object_destroy (GTK_OBJECT (graph));
			graph = NULL;
		}
	}

	CORBA_exception_free (&ev);

	return graph;
}

GtkWidget *
gnm_graph_type_selector (GnmGraph *graph)
{
	CORBA_Environment  ev;
	GtkWidget	  *res = NULL;
	Bonobo_Control	   control;

	CORBA_exception_init (&ev);
	control = MANAGER1 (getTypeSelectControl) (graph->manager, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while attempting to activate a graphing component",
			   bonobo_exception_get_text (&ev));
	} else if (control == CORBA_OBJECT_NIL) {
		g_warning ("A graphing component activated but return NUL ??");
	} else
		res = bonobo_widget_new_control_from_objref (control, CORBA_OBJECT_NIL);
	CORBA_exception_free (&ev);

	return res;
}

void
gnm_graph_clear_vectors (GnmGraph *graph)
{
	gnm_graph_clear_vectors_internal (graph, TRUE);
}

void
gnm_graph_arrange_vectors (GnmGraph *graph)
{
	CORBA_Environment  ev;
	CORBA_exception_init (&ev);
	MANAGER1 (arrangeVectors) (graph->manager, NULL, NULL, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while auto arranging the vectors in graph %p",
			   bonobo_exception_get_text (&ev), graph);
	}
	CORBA_exception_free (&ev);
}

static void
gnm_graph_init (GtkObject *obj)
{
	GnmGraph *graph = GNUMERIC_GRAPH (obj);

	graph->vectors = NULL;
	graph->manager = CORBA_OBJECT_NIL;
	graph->manager_client = NULL;
}

static GtkObjectClass *gnm_graph_parent_class = NULL;

static void
gnm_graph_destroy (GtkObject *obj)
{
	GnmGraph *graph = GNUMERIC_GRAPH (obj);

	printf ("gnumeric : graph destroy %p\n", obj);
	if (graph->manager_client != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (graph->manager_client));
		graph->manager_client = NULL;
		graph->manager = CORBA_OBJECT_NIL;
	}
	if (graph->vectors != NULL) {
		/* no need to unsubscribe, the whole graph is going away */
		gnm_graph_clear_vectors_internal (graph, FALSE);
		g_ptr_array_free (graph->vectors, TRUE);
		graph->vectors = NULL;
	}

	if (gnm_graph_parent_class->destroy)
		gnm_graph_parent_class->destroy (obj);
}

static void
gnm_graph_class_init (GtkObjectClass *object_class)
{
	gnm_graph_parent_class = gtk_type_class (SHEET_OBJECT_CONTAINER_TYPE);

	object_class->destroy = &gnm_graph_destroy;
}

E_MAKE_TYPE (gnm_graph, "GnmGraph", GnmGraph,
	     gnm_graph_class_init, gnm_graph_init, SHEET_OBJECT_CONTAINER_TYPE)

/***************************************************************************/

static GNOME_Gnumeric_Scalar_Seq *
gnm_graph_vector_seq_scalar (GnmGraphVector *vector)
{
	int i, len;
	EvalPos pos;
	GNOME_Gnumeric_Scalar_Seq *values;
	Value *v = vector->value;

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_Scalar_Seq__alloc ();
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

	return values;
}
static GNOME_Gnumeric_Date_Seq *
gnm_graph_vector_seq_date (GnmGraphVector *vector)
{
	int i, len;
	EvalPos pos;
	GNOME_Gnumeric_Date_Seq *values;
	Value *v = vector->value;

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_Date_Seq__alloc ();
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
static GNOME_Gnumeric_String_Seq *
gnm_graph_vector_seq_string (GnmGraphVector *vector)
{
	int i, len;
	EvalPos pos;
	GNOME_Gnumeric_String_Seq *values;
	Value *v = vector->value;

	len = value_area_get_height  (&pos, v);
	values = GNOME_Gnumeric_String_Seq__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_string_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		char const *tmp = value_peek_string (elem);
		values->_buffer[i] = CORBA_string_dup (tmp);
	}

	return values;
}

static void
gnm_graph_vector_eval (Dependent *dep)
{
	CORBA_Environment ev;
	GnmGraphVector *vector;
	EvalPos ep;

	vector = DEP_TO_GRAPH_VECTOR (dep);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));

	if (vector->value != NULL)
		value_release (vector->value);
	vector->value = eval_expr (eval_pos_init_dep (&ep, &vector->dep),
				   vector->dep.expression, EVAL_PERMIT_NON_SCALAR);

	CORBA_exception_init (&ev);
	switch (vector->type) {
	case GNM_VECTOR_SCALAR :
		GNOME_Gnumeric_Scalar_Vector_changed (
			vector->subscriber.scalar,
			0, gnm_graph_vector_seq_scalar (vector), &ev);
		break;

	case GNM_VECTOR_DATE :
		GNOME_Gnumeric_Date_Vector_changed (
			vector->subscriber.date,
			0, gnm_graph_vector_seq_date (vector), &ev);
		break;

	case GNM_VECTOR_STRING :
		GNOME_Gnumeric_String_Vector_changed (
			vector->subscriber.string,
			0, gnm_graph_vector_seq_string (vector), &ev);
		break;

	default :
		g_assert_not_reached ();
	}
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while notifying graph of a change in %p",
			   bonobo_exception_get_text (&ev), vector);
	}
	CORBA_exception_free (&ev);
}

/******************************************************************************/

static void
impl_scalar_vector_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_Scalar_Seq **values,
			  CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_SCALAR);

	*values = gnm_graph_vector_seq_scalar (vector);
}

static void
impl_date_vector_value (PortableServer_Servant servant,
			GNOME_Gnumeric_Date_Seq **values,
			CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_DATE);

	*values = gnm_graph_vector_seq_date (vector);
}

static void
impl_string_vector_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_String_Seq **values,
			  CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_STRING);

	*values = gnm_graph_vector_seq_string (vector);
}

/******************************************************************************/

static void
impl_vector_selection_selected (PortableServer_Servant servant,
				const GNOME_Gnumeric_SeqPair * ranges,
				CORBA_Environment * ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_warning ("Gnumeric : VectorSelection::selected (%p) placeholder\n", vector);
}

static void
impl_scalar_vector_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_Scalar_Seq *vals,
			    CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_STRING);

	g_warning ("Gnumeric : scalar vector changed remotely (%p)", vector);
}

static void
impl_date_vector_changed (PortableServer_Servant servant,
			  const CORBA_short start,
			  const GNOME_Gnumeric_Date_Seq *vals,
			  CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_DATE);

	g_warning ("Gnumeric : date vector changed remotely (%p)", vector);
}

static void
impl_string_vector_changed (PortableServer_Servant servant,
			    const CORBA_short start,
			    const GNOME_Gnumeric_String_Seq *vals,
			    CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_STRING);

	g_warning ("Gnumeric : string vector changed remotely (%p)", vector);
}

/******************************************************************************/

static GtkObjectClass *gnm_graph_vector_parent_class = NULL;
static POA_GNOME_Gnumeric_VectorSelection__vepv	vector_selection_vepv;
static POA_GNOME_Gnumeric_Scalar_Vector__vepv	scalar_vector_vepv;
static POA_GNOME_Gnumeric_Date_Vector__vepv	date_vector_vepv;
static POA_GNOME_Gnumeric_String_Vector__vepv	string_vector_vepv;

static void
gnm_graph_vector_set_expr (Dependent *dep, ExprTree *expr)
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
gnm_graph_vector_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GnmGraphVector%p", dep);
}

static guint
gnm_graph_vector_get_dep_type (void)
{
	static guint32 type = 0;
	if (type == 0) {
		static DependentClass klass;
		klass.eval = &gnm_graph_vector_eval;
		klass.set_expr = &gnm_graph_vector_set_expr;
		klass.debug_name = &gnm_graph_vector_debug_name;
		type = dependent_type_register (&klass);
	}
	return type;
}

static Value *
cb_check_range_for_pure_string (EvalPos const *ep, Value const *v, void *user)
{
	if (v == NULL || v->type != VALUE_STRING)
		return value_terminate ();
	return NULL;
}

static GnmGraphVector *
gnm_graph_vector_corba_init (GnmGraphVector *vector)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	switch (vector->type) {
	case GNM_VECTOR_SCALAR :
		vector->servant.scalar.vepv = &scalar_vector_vepv;
		POA_GNOME_Gnumeric_Scalar_Vector__init (
			&vector->servant.scalar, &ev);
		break;

	case GNM_VECTOR_DATE :
		vector->servant.date.vepv = &date_vector_vepv;
		POA_GNOME_Gnumeric_Date_Vector__init (
			&vector->servant.date, &ev);
		break;

	case GNM_VECTOR_STRING :
		vector->servant.string.vepv = &string_vector_vepv;
		POA_GNOME_Gnumeric_String_Vector__init (
			&vector->servant.string, &ev);
		break;

	default :
		g_assert_not_reached ();
	};

	if (ev._major == CORBA_NO_EXCEPTION) {
		PortableServer_ObjectId *oid;
		PortableServer_POA poa = bonobo_poa ();
		
		vector->initialized = TRUE;

		oid = PortableServer_POA_activate_object (poa,
			&vector->servant.any, &ev);
		vector->activated = (ev._major == CORBA_NO_EXCEPTION);

		vector->corba_obj = PortableServer_POA_servant_to_reference (poa,
			&vector->servant.any, &ev);
		CORBA_free (oid);
	} else {
		g_warning ("'%s' : while creating a vector",
			   bonobo_exception_get_text (&ev));
		gtk_object_unref (GTK_OBJECT (vector));
		vector = NULL;
	}
	CORBA_exception_free (&ev);

	return vector;
}

/**
 * gnm_graph_vector_new :
 *
 * @graph : the container.
 * @expr  : the expression to evaluate for this vector.
 * @type  : optional, pass GNM_VECTOR_AUTO, and we will make a guess.
 * @sheet : this a dependentContainer when I create it.
 */
GnmGraphVector *
gnm_graph_vector_new (GnmGraph *graph, ExprTree *expr,
		      GnmGraphVectorType type, Sheet *sheet)
{
	GnmGraphVector *vector;
	EvalPos ep;

	vector = gtk_type_new (gnm_graph_vector_get_type ());
	vector->dep.sheet = sheet;
	vector->dep.flags = gnm_graph_vector_get_dep_type ();
	vector->dep.expression = expr;
	dependent_link (&vector->dep, NULL);

	vector->value = eval_expr (eval_pos_init_dep (&ep, &vector->dep),
				   vector->dep.expression, EVAL_PERMIT_NON_SCALAR);
	switch (type) {
	case GNM_VECTOR_AUTO :
		type = (value_area_foreach (&ep, vector->value,
				&cb_check_range_for_pure_string, NULL) != NULL)
			? GNM_VECTOR_SCALAR : GNM_VECTOR_STRING;
		break;
	case GNM_VECTOR_SCALAR :
	case GNM_VECTOR_STRING :
		break;
	case GNM_VECTOR_DATE :
		g_warning ("Date vectors aren't supported yet");
		type = GNM_VECTOR_SCALAR;
		break;
	default :
		g_warning ("Unknown vector type");
		type = GNM_VECTOR_SCALAR;
	};

	vector->is_column = value_area_get_width (&ep, vector->value) == 1;
	vector->type = type;
	vector = gnm_graph_vector_corba_init (vector);
	if (vector != NULL)
		gnm_graph_add_vector (graph, vector);
	printf ("vector::new (%d) = 0x%p\n", type, vector);

	return vector;
}

static void
gnm_graph_vector_corba_destroy (GnmGraphVector *vector)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	if (vector->activated) {
		PortableServer_ObjectId *oid;
		PortableServer_POA poa = bonobo_poa ();

		oid = PortableServer_POA_servant_to_id (poa,
			&vector->servant.any, &ev);
		PortableServer_POA_deactivate_object (poa, oid, &ev);
		vector->activated = FALSE;
		CORBA_free (oid);

		g_return_if_fail (ev._major == CORBA_NO_EXCEPTION);
	}
	if (vector->initialized) {
		switch (vector->type) {
		case GNM_VECTOR_SCALAR :
			POA_GNOME_Gnumeric_Scalar_Vector__fini (
				&vector->servant.scalar, &ev);
			break;
		case GNM_VECTOR_DATE :
			POA_GNOME_Gnumeric_Date_Vector__fini (
				&vector->servant.date, &ev);
			break;
		case GNM_VECTOR_STRING :
			POA_GNOME_Gnumeric_String_Vector__fini (
				&vector->servant.string, &ev);
			break;
		default :
			g_warning ("Should not be reached");
		};

		g_return_if_fail (ev._major == CORBA_NO_EXCEPTION);
	}
	CORBA_exception_free (&ev);

}

static void
gnm_graph_vector_destroy (GtkObject *obj)
{
	GnmGraphVector *vector = GNUMERIC_GRAPH_VECTOR (obj);

	printf ("graph-vector::destroy %p\n", obj);
	dependent_unlink (&vector->dep, NULL);
	if (vector->dep.expression != NULL) {
		expr_tree_unref (vector->dep.expression);
		vector->dep.expression = NULL;
	}

	gnm_graph_vector_corba_destroy (vector);

	/* if we are still linked in remove us from the graph */
	if (vector->graph != NULL) {
		g_ptr_array_index (vector->graph->vectors, vector->id) = NULL;
		vector->graph = NULL;
		/* vector->id = -1; leave the ID intact for debugging */
	}

	if (gnm_graph_vector_parent_class->destroy)
		gnm_graph_vector_parent_class->destroy (obj);
}

static void
gnm_graph_vector_corba_class_init (void)
{
	static POA_GNOME_Gnumeric_VectorSelection__epv	selection_epv;
	static POA_GNOME_Gnumeric_Scalar_Vector__epv	scalar_epv;
	static POA_GNOME_Gnumeric_Date_Vector__epv	date_epv;
	static POA_GNOME_Gnumeric_String_Vector__epv	string_epv;

	selection_epv.selected = &impl_vector_selection_selected;
	vector_selection_vepv.GNOME_Gnumeric_VectorSelection_epv =
		&selection_epv;

	scalar_epv.changed = &impl_scalar_vector_changed;
	scalar_epv.value = &impl_scalar_vector_value;
	scalar_vector_vepv.GNOME_Gnumeric_Scalar_Vector_epv =
		&scalar_epv;
	scalar_vector_vepv.GNOME_Gnumeric_VectorSelection_epv =
		&selection_epv;

	date_epv.changed = & impl_date_vector_changed;
	date_epv.value = &impl_date_vector_value;
	date_vector_vepv.GNOME_Gnumeric_Date_Vector_epv =
		&date_epv;
	date_vector_vepv.GNOME_Gnumeric_VectorSelection_epv =
		&selection_epv;

	string_epv.changed = & impl_string_vector_changed;
	string_epv.value = &impl_string_vector_value;
	string_vector_vepv.GNOME_Gnumeric_String_Vector_epv =
		&string_epv;
	string_vector_vepv.GNOME_Gnumeric_VectorSelection_epv =
		&selection_epv;
}
static void
gnm_graph_vector_class_init (GtkObjectClass *object_class)
{
	gnm_graph_vector_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = & gnm_graph_vector_destroy;

	gnm_graph_vector_corba_class_init ();
}

static void
gnm_graph_vector_init (GtkObject *obj)
{
	GnmGraphVector *vector = GNUMERIC_GRAPH_VECTOR (obj);

	vector->subscriber.any	= CORBA_OBJECT_NIL;
	vector->corba_obj	= CORBA_OBJECT_NIL;
	vector->graph		= NULL; /* don't attach until we subscribe */
	vector->activated	= FALSE;
	vector->initialized	= FALSE;
}

E_MAKE_TYPE (gnm_graph_vector,"GnmGraphVector",GnmGraphVector,
	     gnm_graph_vector_class_init, gnm_graph_vector_init, GTK_TYPE_OBJECT)
