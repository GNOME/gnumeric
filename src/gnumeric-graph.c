/* vim: set sw=8: */

/*
 * graph-vector.c: Support routines for graph vector.
 *
 * Copyright (C) 2000-2001 Jody Goldberg (jody@gnome.org)
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

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gnumeric-graph.h"

#include "eval.h"
#include "expr.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook-private.h"
#include "value.h"
#include "ranges.h"
#include "formats.h"
#include "format.h"
#include "mstyle.h"
#include "sheet-style.h"
#include "sheet-object-container.h"

#include "dialogs.h"
#include "sheet-control-gui.h"

#include <idl/GNOME_Gnumeric_Graph.h>
#include <bonobo.h>
#include <liboaf/liboaf.h>
#include <gal/util/e-util.h>
#include <gal/util/e-xml-utils.h>
#include <libxml/parser.h>

#define DISABLE_DEBUG
#ifndef DISABLE_DEBUG
#define d(code)	do { code; } while (0)
#else
#define d(code)	
#endif

#define	MANAGER		  GNOME_Gnumeric_Graph_v2_Manager
#define	MANAGER1(suffix)  GNOME_Gnumeric_Graph_v2_Manager_ ## suffix
#define	CMANAGER1(suffix) CORBA_sequence_GNOME_v2_Gnumeric_Graph_Manager_ ## suffix
#define	MANAGER_OAF	 "IDL:GNOME/Gnumeric/Graph_v2/Manager:1.0"

struct _GnmGraph {
	SheetObjectContainer	parent;

	BonoboObjectClient	*manager_client;
	MANAGER	 		 manager;

	GPtrArray		*vectors;
	xmlDoc			*xml_doc;
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
	int		 id;
	gboolean	 initialized : 1;
	gboolean	 activated : 1;
	gboolean	 is_header : 1;
	GnmGraphVector  *header;

	CORBA_Object    corba_obj;	/* local CORBA object */
	union {
		POA_GNOME_Gnumeric_Scalar_Vector	scalar;
		POA_GNOME_Gnumeric_String_Vector	string;
		PortableServer_POA			any;
	} servant;

	/* The remote server monitoring this vector */
	union {
		GNOME_Gnumeric_Scalar_Vector		scalar;
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

char const *const gnm_graph_vector_type_name [] =
{
    "Unknown", "scalars", "dates (unimplemented)", "strings",
};

/***************************************************************************/

static void
impl_vector_selection_selected (PortableServer_Servant servant,
				const GNOME_Gnumeric_SeqPair *ranges,
				CORBA_Environment * ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_warning ("Gnumeric : VectorSelection::selected (%p) placeholder\n", vector);
}

static GNOME_Gnumeric_Scalar_Seq *
gnm_graph_vector_seq_scalar (GnmGraphVector *vector)
{
	int i, len;
	EvalPos pos;
	GNOME_Gnumeric_Scalar_Seq *values;
	Value *v = vector->value;

	eval_pos_init_dep (&pos, &vector->dep);
	len = (v == NULL) ? 1 :
		(vector->is_column
			? value_area_get_height (&pos, v)
			: value_area_get_width (&pos, v));

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
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);

		/* TODO : handle blanks */
		values->_buffer [i] = elem ? value_get_as_float (elem) : 0.;
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

	eval_pos_init_dep (&pos, &vector->dep);
	len = (v == NULL) ? 1 :
		(vector->is_column
		   ? value_area_get_height (&pos, v)
		   : value_area_get_width (&pos, v));
	values = GNOME_Gnumeric_String_Seq__alloc ();
	values->_length = values->_maximum = len;
	values->_buffer = CORBA_sequence_CORBA_string_allocbuf (len);
	values->_release = CORBA_TRUE;

	/* TODO : handle blanks */
	if (v == NULL) {
		values->_buffer[0] = CORBA_string_dup ("");
		return values;
	}

	/* FIXME : This is dog slow */
	for (i = 0; i < len ; ++i) {
		Value const *elem = vector->is_column
			? value_area_get_x_y (&pos, v, 0, i)
			: value_area_get_x_y (&pos, v, i, 0);
		/* TODO : handle blanks */
		char const *tmp = elem ? value_peek_string (elem) : "";
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
	ExprEvalFlags flags = EVAL_PERMIT_NON_SCALAR;

	vector = DEP_TO_GRAPH_VECTOR (dep);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));

	if (vector->value != NULL)
		value_release (vector->value);
	if (vector->type == GNM_VECTOR_STRING)
		flags |= EVAL_PERMIT_EMPTY;
	vector->value = expr_eval (vector->dep.expression,
		eval_pos_init_dep (&ep, &vector->dep), flags);

	CORBA_exception_init (&ev);
	switch (vector->type) {
	case GNM_VECTOR_SCALAR :
	case GNM_VECTOR_DATE : {
		GNOME_Gnumeric_Scalar_Seq *seq =
			gnm_graph_vector_seq_scalar (vector);
		GNOME_Gnumeric_Scalar_Vector_changed (
			vector->subscriber.scalar, 0, seq, &ev);
		CORBA_free (seq);
		break;
	}

	case GNM_VECTOR_STRING : {
		GNOME_Gnumeric_String_Seq *seq =
			gnm_graph_vector_seq_string (vector);
		GNOME_Gnumeric_String_Vector_changed (
			vector->subscriber.string, 0, seq, &ev);
		CORBA_free (seq);
		break;
	}

	default :
		g_assert_not_reached ();
	}
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while notifying graph of a change in %p",
			   bonobo_exception_get_text (&ev), vector);
	}
	CORBA_exception_free (&ev);
}

static void
impl_scalar_vector_value (PortableServer_Servant servant,
			  GNOME_Gnumeric_Scalar_Seq **values,
			  CORBA_Environment *ev)
{
	GnmGraphVector *vector = SERVANT_TO_GRAPH_VECTOR (servant);

	g_return_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vector));
	g_return_if_fail (vector->type == GNM_VECTOR_SCALAR ||
			  vector->type == GNM_VECTOR_DATE);

	*values = gnm_graph_vector_seq_scalar (vector);
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

Dependent const *
gnm_graph_vector_get_dependent (GnmGraphVector const *vec)
{
	g_return_val_if_fail (IS_GNUMERIC_GRAPH_VECTOR (vec), NULL);

	return &vec->dep;
}

/******************************************************************************/

static GtkObjectClass *gnm_graph_vector_parent_class = NULL;
static POA_GNOME_Gnumeric_VectorSelection__vepv	vector_selection_vepv;
static POA_GNOME_Gnumeric_Scalar_Vector__vepv	scalar_vector_vepv;
static POA_GNOME_Gnumeric_String_Vector__vepv	string_vector_vepv;

Bonobo_Control
gnm_graph_get_config_control (GnmGraph *graph, char const *which_control)
{
	Bonobo_Control control = CORBA_OBJECT_NIL;

	g_return_val_if_fail (IS_GNUMERIC_GRAPH (graph), NULL);

	/* TODO : restart things if it dies */
	if (graph->manager != CORBA_OBJECT_NIL) {
		CORBA_Environment  ev;
		CORBA_exception_init (&ev);
		control = MANAGER1 (configure) (graph->manager, which_control, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : while gettting graph %s control",
				   bonobo_exception_get_text (&ev), which_control);
			control = CORBA_OBJECT_NIL;
		} else if (control == CORBA_OBJECT_NIL) {
			g_warning ("Was this an unknown config control ??");
		} else
		CORBA_exception_free (&ev);
	}

	return control;
}

static void
gnm_graph_vector_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "GnmGraphVector%p", dep);
}

static DEPENDENT_MAKE_TYPE (gnm_graph_vector, NULL)

static Value *
cb_check_range_for_pure_string (EvalPos const *ep, Value const *v, void *user)
{
	if (v == NULL || v->type != VALUE_STRING)
		return value_terminate ();
	return NULL;
}

static gboolean
gnm_graph_vector_corba_init (GnmGraphVector *vector)
{
	CORBA_Environment ev;
	gboolean ok;

	CORBA_exception_init (&ev);

	switch (vector->type) {
	case GNM_VECTOR_SCALAR :
	case GNM_VECTOR_DATE :
		vector->servant.scalar.vepv = &scalar_vector_vepv;
		POA_GNOME_Gnumeric_Scalar_Vector__init (
			&vector->servant.scalar, &ev);
		break;

	case GNM_VECTOR_STRING :
		vector->servant.string.vepv = &string_vector_vepv;
		POA_GNOME_Gnumeric_String_Vector__init (
			&vector->servant.string, &ev);
		break;

	default :
		g_assert_not_reached ();
	};

	if ((ok = ev._major == CORBA_NO_EXCEPTION)) {
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
	}
	CORBA_exception_free (&ev);

	return ok;
}

static void
gnm_graph_vector_corba_destroy (GnmGraphVector *vector)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	if (vector->subscriber.any != CORBA_OBJECT_NIL) {
		CORBA_Object_release(vector->subscriber.any, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : while releasing a vector",
				   bonobo_exception_get_text (&ev));
		}
		vector->subscriber.any	= CORBA_OBJECT_NIL;
	}
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
		case GNM_VECTOR_DATE :
			POA_GNOME_Gnumeric_Scalar_Vector__fini (
				&vector->servant.scalar, &ev);
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

	d(printf ("graph-vector::destroy %p\n", obj));

	dependent_unlink (&vector->dep, NULL);
	if (vector->dep.expression != NULL) {
		expr_tree_unref (vector->dep.expression);
		vector->dep.expression = NULL;
	}

	gnm_graph_vector_corba_destroy (vector);

	if (vector->value != NULL) {
		value_release (vector->value);
		vector->value = NULL;
	}

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


/***************************************************************************/

static GtkObjectClass *gnm_graph_parent_class = NULL;

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
	g_ptr_array_set_size (graph->vectors, 0);
	if (unsubscribe) {
		CORBA_Environment ev;

		if (graph->manager == CORBA_OBJECT_NIL)
			return;

		CORBA_exception_init (&ev);
		MANAGER1 (clearVectors) (graph->manager, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("'%s' : while clearing the vectors in graph %p",
				   bonobo_exception_get_text (&ev), graph);
		}
		CORBA_exception_free (&ev);
	}
}

static gboolean
gnm_graph_subscribe_vector (GnmGraph *graph, GnmGraphVector *vector)
{
	CORBA_Environment ev;
	int id;
	gboolean ok;

	CORBA_exception_init (&ev);

	if (graph->manager == CORBA_OBJECT_NIL)
		return FALSE;

	/* Pass a place holder for the format */
	id = graph->vectors->len;
	vector->subscriber.any = MANAGER1 (addVector) (
		graph->manager, vector->corba_obj, vector->type, id,
		CORBA_string_dup (""), &ev);

	if ((ok = ev._major == CORBA_NO_EXCEPTION)) {
		g_ptr_array_add (graph->vectors, vector);
		vector->graph = graph;
		vector->id = id;
	} else {
		vector->subscriber.any = CORBA_OBJECT_NIL;
		g_warning ("'%s' : while subscribing vector %p",
			   bonobo_exception_get_text (&ev), vector);
	}

	CORBA_exception_free (&ev);

	return ok;
}

/**
 * gnm_graph_add_vector :
 *
 * @graph : the container.
 * @expr  : the expression to evaluate for this vector.
 * @type  : optional, pass GNM_VECTOR_AUTO, and we will make a guess.
 * @sheet : this a dependentContainer when I create it.
 *
 * Returns the ID of the vector
 */
int
gnm_graph_add_vector (GnmGraph *graph, ExprTree *expr,
		      GnmGraphVectorType type, Sheet *sheet)
{
	static CellPos const dummy = {0,0};
	GnmGraphVector *vector;
	EvalPos ep;
	int i;
	ExprEvalFlags flags = EVAL_PERMIT_NON_SCALAR;

	g_return_val_if_fail (IS_GNUMERIC_GRAPH (graph), -1);

	/* If this graph already has this vector don't duplicate it.
	 * This is useful when importing a set of series with a common dimension.
	 * eg a set of bars with common categories.
	 */
	for (i = graph->vectors->len ; i-- > 0 ; ) {
		vector = g_ptr_array_index (graph->vectors, i);
		if ((type == GNM_VECTOR_AUTO || type == vector->type) &&
		    expr_tree_equal (expr, vector->dep.expression)) {
			d({
				ParsePos pos;
				char *expr_str;
				parse_pos_init (&pos, NULL, sheet, 0, 0);
				expr_str = expr_tree_as_string (expr, &pos);
				printf ("vector::ref (%d) @ %p = %s\n",
					vector->type, vector, expr_str);
				g_free (expr_str);
			});
			return vector->id;
		}
	}

	vector = gtk_type_new (gnm_graph_vector_get_type ());
	vector->dep.sheet = sheet;
	vector->dep.flags = gnm_graph_vector_get_dep_type ();
	vector->dep.expression = expr;
	vector->is_header = FALSE;
	vector->header = NULL;
	dependent_link (&vector->dep, &dummy);

	if (type == GNM_VECTOR_STRING || type == GNM_VECTOR_AUTO)
		flags |= EVAL_PERMIT_EMPTY;
	vector->value = expr_eval (vector->dep.expression,
		eval_pos_init_dep (&ep, &vector->dep), flags);

	if (type == GNM_VECTOR_AUTO) {
		type = GNM_VECTOR_SCALAR;
		if (vector->value != NULL) {
			if (value_area_foreach (&ep, vector->value, &cb_check_range_for_pure_string, NULL) != NULL &&
			    vector->value->type == VALUE_CELLRANGE) {
				Range  r;
				Sheet *start_sheet, *end_sheet;
				char *fmt;
				FormatCharacteristics info;
				FormatFamily family;
					
				value_cellrange_normalize (&ep, vector->value, &start_sheet, &end_sheet, &r);
				fmt = cell_get_format (sheet_cell_get (start_sheet, r.start.col, r.start.row));
				family = cell_format_classify (fmt, &info);
				g_free (fmt);
				if (family == FMT_DATE)
					type = GNM_VECTOR_DATE;
			} else
				type = GNM_VECTOR_STRING;
		}
	}

	vector->is_column = (vector->value != NULL &&
			     value_area_get_width (&ep, vector->value) == 1);
	vector->type = type;
	if (!gnm_graph_vector_corba_init (vector) ||
	    !gnm_graph_subscribe_vector (graph, vector)) {
		gtk_object_unref (GTK_OBJECT (vector));
		vector = NULL;
	} else {
		d({
			ParsePos pos;
			char *expr_str;
			parse_pos_init (&pos, NULL, sheet, 0, 0);
			expr_str = expr_tree_as_string (expr, &pos);
			printf ("vector::new (%d) @ %p = %s\n", type, vector, expr_str);
			g_free (expr_str);
		});
	}

	return vector ? vector->id : -1;
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

static gboolean
gnm_graph_setup (GnmGraph *graph, Workbook *wb)
{
	CORBA_Environment  ev;
	Bonobo_Unknown	   o;

	CORBA_exception_init (&ev);

	o = (Bonobo_Unknown)oaf_activate ("repo_ids.has('" MANAGER_OAF "')",
					  NULL, 0, NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION || o == CORBA_OBJECT_NIL) {
		g_warning ("'%s' : while attempting to activate a graphing component.\n"
			   "oaf-run-query \"repo_ids.has('" MANAGER_OAF "')\"\nshould return a value.",
			   oaf_exception_id (&ev));
		graph = NULL;
	} else {
		graph->manager = Bonobo_Unknown_queryInterface (o, MANAGER_OAF, &ev);

		g_return_val_if_fail (graph->manager != CORBA_OBJECT_NIL, TRUE);

		graph->manager_client = bonobo_object_client_from_corba (graph->manager);
		bonobo_object_release_unref (o, &ev);

		if (sheet_object_bonobo_construct (SHEET_OBJECT_BONOBO (graph),
						   wb->priv->bonobo_container, NULL) == NULL ||
		    !sheet_object_bonobo_set_server (SHEET_OBJECT_BONOBO (graph),
						     graph->manager_client)) {
			graph = NULL;
		}
	}

	CORBA_exception_free (&ev);

	return graph == NULL;
}

/* FIXME : Should we take a CommandContext to report errors to ? */
GnmGraph *
gnm_graph_new (Workbook *wb)
{
	GnmGraph *graph = gtk_type_new (GNUMERIC_GRAPH_TYPE);

	d(printf ("gnumeric : graph new %p\n", graph));

	if (gnm_graph_setup (graph, wb)) {
		gtk_object_destroy (GTK_OBJECT (graph));
		return NULL;
	}
	return graph;
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
	GNOME_Gnumeric_VectorIDs *data, *headers;
	unsigned i, len = 0;

	g_return_if_fail (IS_GNUMERIC_GRAPH (graph));

	if (graph->manager == CORBA_OBJECT_NIL)
		return;

	for (i = 0; i < graph->vectors->len ; i++) {
		GnmGraphVector *vector = g_ptr_array_index (graph->vectors, i);
		if (!vector->is_header)
			len++;
	}

	data = GNOME_Gnumeric_VectorIDs__alloc ();
	data->_length = data->_maximum = len;
	data->_buffer = CORBA_sequence_GNOME_Gnumeric_VectorID_allocbuf (len);
	data->_release = CORBA_TRUE;
	headers = GNOME_Gnumeric_VectorIDs__alloc ();
	headers->_length = data->_maximum = len;
	headers->_buffer = CORBA_sequence_GNOME_Gnumeric_VectorID_allocbuf (len);
	headers->_release = CORBA_TRUE;

	len = 0;
	for (i = 0; i < graph->vectors->len ; i++) {
		GnmGraphVector *vector = g_ptr_array_index (graph->vectors, i);
		if (!vector->is_header) {
			data->_buffer[len] = vector->id;
			headers->_buffer[len] = (vector->header != NULL)
				? vector->header->id : -1;
			len++;
		}
	}

	CORBA_exception_init (&ev);
	MANAGER1 (arrangeVectors) (graph->manager, data, headers, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("'%s' : while auto arranging the vectors in graph %p",
			   bonobo_exception_get_text (&ev), graph);
	}
	CORBA_exception_free (&ev);
	CORBA_free (headers);
	CORBA_free (data);
}

void
gnm_graph_range_to_vectors (GnmGraph *graph,
			    Sheet *sheet,
			    Range const *src,
			    gboolean default_to_cols)
{
	int i, count;
	gboolean has_header, as_cols;
	Range vector = *src;
	CellRef header;

	if (range_trim (sheet, &vector, TRUE) ||
	    range_trim (sheet, &vector, FALSE))
		return;

	/* Special case the handling of a vector rather than a range.
	 * it should stay in its orientation,  only ranges get split
	 */
	as_cols = (src->start.col == src->end.col || default_to_cols);
	has_header = range_has_header (sheet, src, as_cols, TRUE);
	header.sheet = sheet;
	header.col_relative = header.row_relative = FALSE;
	header.col = vector.start.col;
	header.row = vector.start.row;

	if (as_cols) {
		if (has_header)
			vector.start.row++;
		count = vector.end.col - vector.start.col;
		vector.end.col = vector.start.col;
	} else {
		if (has_header)
			vector.start.col++;
		count = vector.end.row - vector.start.row;
		vector.end.row = vector.start.row;
	}

	for (i = 0 ; i <= count ; i++) {
		int data_id = gnm_graph_add_vector (graph,
			expr_tree_new_constant (
				value_new_cellrange_r (sheet, &vector)),
			GNM_VECTOR_AUTO, sheet);

		if (has_header) {
			GnmGraphVector *h_vec, *d_vec;
			int header_id = gnm_graph_add_vector (graph,
				expr_tree_new_var (&header),
				GNM_VECTOR_STRING, sheet);
			h_vec = g_ptr_array_index (graph->vectors, header_id);
			h_vec->is_header = TRUE;
			d_vec = g_ptr_array_index (graph->vectors, data_id);
			d_vec->header = h_vec;
		}

		if (as_cols)
			vector.end.col = vector.start.col = ++header.col;
		else
			vector.end.row = vector.start.row = ++header.row;
	}
}

static void
gnm_graph_clear_xml (GnmGraph *graph)
{
	if (graph->xml_doc != NULL) {
		xmlFreeDoc (graph->xml_doc);
		graph->xml_doc = NULL;
	}
}

xmlDoc *
gnm_graph_get_spec (GnmGraph *graph, gboolean force_update)
{
	CORBA_Environment  ev;
	GNOME_Gnumeric_Buffer *spec;

	g_return_val_if_fail (IS_GNUMERIC_GRAPH (graph), NULL);

	if (graph->manager == CORBA_OBJECT_NIL)
		return NULL;

	if (!force_update && graph->xml_doc != NULL)
		return graph->xml_doc;

	CORBA_exception_init (&ev);
	spec = MANAGER1 (_get_spec) (graph->manager, &ev);
	if (ev._major == CORBA_NO_EXCEPTION) {
		xmlParserCtxtPtr pctxt;

		/* A limit in libxml */
		g_return_val_if_fail (spec->_length >= 4, NULL);

		pctxt = xmlCreatePushParserCtxt (NULL, NULL,
			(char const *)spec->_buffer, spec->_length, NULL);
		xmlParseChunk (pctxt, "", 0, TRUE);

		gnm_graph_clear_xml (graph);
		graph->xml_doc = pctxt->myDoc;

#if DEBUG_INFO > 0
		xmlDocDump (stdout, graph->xml_doc);
#endif

		xmlFreeParserCtxt (pctxt);
		CORBA_free (spec);
	} else {
		g_warning ("'%s' : retrieving the specification for graph %p",
			   bonobo_exception_get_text (&ev), graph);
	}
	CORBA_exception_free (&ev);

	return graph->xml_doc;
}

/**
 * gnm_graph_import_specification :
 *
 * @graph : the graph we are specifing
 * @spec  : an xml document in a simple format
 *
 * Takes a simplied xml description of the graph an sends it over to the grph
 * manager to flesh out and generate.
 */
void
gnm_graph_import_specification (GnmGraph *graph, xmlDocPtr spec)
{
	CORBA_Environment  ev;
	GNOME_Gnumeric_Buffer *partial;
	xmlChar *mem;
	int size;

	g_return_if_fail (IS_GNUMERIC_GRAPH (graph));

	if (graph->manager == CORBA_OBJECT_NIL)
		return;

	xmlDocDumpMemory (spec, &mem, &size);

	partial = GNOME_Gnumeric_Buffer__alloc ();
	partial->_length = partial->_maximum = size;
	partial->_buffer = mem;
	partial->_release = CORBA_FALSE;

	CORBA_exception_init (&ev);
	MANAGER1 (_set_spec) (graph->manager, partial, &ev);
	if (ev._major == CORBA_NO_EXCEPTION)
		gnm_graph_get_spec (graph, TRUE);
	else {
		g_warning ("'%s' : importing the specification for graph %p",
			   bonobo_exception_get_text (&ev), graph);
	}
	CORBA_free (partial);
	xmlFree (mem);
	CORBA_exception_free (&ev);
}

GnmGraphVector *
gnm_graph_get_vector (GnmGraph *graph, int id)
{
	g_return_val_if_fail (IS_GNUMERIC_GRAPH (graph), NULL);
	g_return_val_if_fail (id >= 0, NULL);
	g_return_val_if_fail (id < (int)graph->vectors->len, NULL);

	return g_ptr_array_index (graph->vectors, id);
}

static void
gnm_graph_init (GtkObject *obj)
{
	GnmGraph *graph = GNUMERIC_GRAPH (obj);

	graph->vectors = NULL;
	graph->manager = CORBA_OBJECT_NIL;
	graph->xml_doc = NULL;
	graph->manager_client = NULL;
	graph->vectors = g_ptr_array_new ();
}

static void
gnm_graph_destroy (GtkObject *obj)
{
	GnmGraph *graph = GNUMERIC_GRAPH (obj);

	d(printf ("gnumeric : graph destroy %p\n", obj));

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
	gnm_graph_clear_xml (graph);

	if (gnm_graph_parent_class->destroy)
		gnm_graph_parent_class->destroy (obj);
}

static void
cb_graph_assign_data (GtkWidget *ignored, GtkObject *obj_view)
{
	SheetControlGUI *scg = sheet_object_view_control (obj_view);
	SheetObject     *so  = sheet_object_view_obj     (obj_view);
	dialog_graph_guru (scg_get_wbcg (scg), GNUMERIC_GRAPH (so), 1);
}

static void
gnm_graph_populate_menu (SheetObject *so,
			 GtkObject   *obj_view,
			 GtkMenu     *menu)
{
	GnmGraph *graph;
	GtkWidget *item;

	g_return_if_fail (IS_GNUMERIC_GRAPH (so));

	graph = GNUMERIC_GRAPH (so);
	item = gtk_menu_item_new_with_label (_("Data..."));
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (cb_graph_assign_data), obj_view);
	gtk_menu_append (menu, item);

	if (SHEET_OBJECT_CLASS (gnm_graph_parent_class)->populate_menu)
		SHEET_OBJECT_CLASS (gnm_graph_parent_class)->populate_menu (so, obj_view, menu);
}

static void
gnm_graph_user_config (SheetObject *so, SheetControlGUI	*scg)
{
	dialog_graph_guru (scg_get_wbcg (scg), GNUMERIC_GRAPH (so), 2);
}

static gboolean
gnm_graph_read_xml (SheetObject *so,
		    XmlParseContext const *ctxt, xmlNodePtr tree)
{
	GnmGraph *graph = GNUMERIC_GRAPH (so);
	xmlNode *tmp;
	xmlDoc *doc;

	if (gnm_graph_setup (graph, ctxt->wb))
		return TRUE;

	tmp = e_xml_get_child_by_name (tree, (xmlChar *)"Vectors");
	for (tmp = tmp->xmlChildrenNode; tmp; tmp = tmp->next) {
		int id, new_id, type;
		ParsePos pos;
		ExprTree *expr;
		xmlChar *content;

		if (strcmp (tmp->name, "Vector"))
			continue;

		content = xmlNodeGetContent (tmp);
		expr = expr_parse_str_simple ((gchar *)content,
			parse_pos_init (&pos, NULL, ctxt->sheet, 0, 0));
		xmlFree (content);

		g_return_val_if_fail (expr != NULL, TRUE);

		xml_node_get_int (tmp, "ID", &id);
		xml_node_get_int (tmp, "Type", &type);

		new_id = gnm_graph_add_vector (graph, expr, type, ctxt->sheet);

		g_return_val_if_fail (id == new_id, TRUE);
	}

	doc = xmlNewDoc ((xmlChar *)"1.0");
	doc->xmlRootNode = xmlCopyNode (
		e_xml_get_child_by_name (tree, (xmlChar *)"Graph"), TRUE);
	gnm_graph_import_specification (graph, doc);
	xmlFreeDoc (doc);

	return FALSE;
}

static gboolean
gnm_graph_write_xml (SheetObject const *so,
		     XmlParseContext const *ctxt, xmlNodePtr tree)
{
	GnmGraph *graph = GNUMERIC_GRAPH (so);
	xmlNode *vectors;
	ParsePos pp;
	unsigned i;

	vectors = xmlNewChild (tree, ctxt->ns, (xmlChar *)"Vectors", NULL);
	for (i = 0 ; i < graph->vectors->len; i++) {
		GnmGraphVector *vector = g_ptr_array_index (graph->vectors, i);
		xmlNode *node;
		xmlChar *encoded_expr_str;
		char *expr_str;

		if (vector == NULL)
			continue;
		expr_str = expr_tree_as_string (vector->dep.expression,
			parse_pos_init_dep (&pp, &vector->dep));
		encoded_expr_str = xmlEncodeEntitiesReentrant (ctxt->doc,
			(xmlChar *)expr_str);
		node = xmlNewChild (vectors, ctxt->ns, (xmlChar *)"Vector",
			(xmlChar *)encoded_expr_str);
		g_free (expr_str);
		xmlFree (encoded_expr_str);

		xml_node_set_int (node, "ID", i);
		xml_node_set_int (node, "Type", vector->type);
	}

	gnm_graph_get_spec (graph, TRUE);
	xmlAddChild (tree, xmlCopyNode (graph->xml_doc->xmlRootNode, TRUE));
	return FALSE;
}

static void
gnm_graph_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class;

	gnm_graph_parent_class = gtk_type_class (SHEET_OBJECT_CONTAINER_TYPE);

	object_class->destroy = &gnm_graph_destroy;

	sheet_object_class = SHEET_OBJECT_CLASS (object_class);
	sheet_object_class->populate_menu = gnm_graph_populate_menu;
	sheet_object_class->user_config   = gnm_graph_user_config;
	sheet_object_class->read_xml	  = gnm_graph_read_xml;
	sheet_object_class->write_xml	  = gnm_graph_write_xml;
}

E_MAKE_TYPE (gnm_graph, "GnmGraph", GnmGraph,
	     gnm_graph_class_init, gnm_graph_init, SHEET_OBJECT_CONTAINER_TYPE)

/*****************************************************************************/

/**
 * gnm_graph_series_get_dimension :
 * @series : the xml node holding series info.
 * @target : The name of the dimension we're looking for.
 *
 * A utility routine to find the child Dimension of @series named @target.
 */
xmlNode *
gnm_graph_series_get_dimension (xmlNode *series, xmlChar const *target)
{
	xmlNode *dim;
	xmlChar *dim_name;

	g_return_val_if_fail (series != NULL, NULL);

	/* attempt to find the matching dimension */
	for (dim = series->xmlChildrenNode; dim; dim = dim->next) {
		if (strcmp (dim->name, "Dimension"))
			continue;
		dim_name = xmlGetProp (dim, (xmlChar *)"dim_name");
		if (dim_name == NULL) {
			g_warning ("Missing dim_name in series dimension");
			continue;
		}
		if (strcmp (dim_name, target)) {
			xmlFree (dim_name);
			continue;
		}
		xmlFree (dim_name);
		return dim;
	}
	return NULL;
}

/**
 * gnm_graph_series_add_dimension :
 * Add a properly formated child for an additional dimension.
 * If we want to get fancy we could even check for duplicated here.
 */
xmlNode *
gnm_graph_series_add_dimension (xmlNode *series, char const *dim_name)
{
	xmlNode *res;

	g_return_val_if_fail (series != NULL, NULL);

	res = xmlNewChild (series, series->ns, (xmlChar *)"Dimension", NULL);
	xmlSetProp (res, (xmlChar *)"dim_name", dim_name);
	return res;
}

char *
gnm_graph_exception (CORBA_Environment *ev)
{
        if (ev->_major == CORBA_USER_EXCEPTION) {
		if (!strcmp (ev->_repo_id, "IDL:GNOME/Gnumeric/Error:1.0")) {
                        GNOME_Gnumeric_Error *err = ev->_params;
                        
                        if (!err || !err->mesg) {
                                return "No general exception error message";
                        } else {
                                return err->mesg;
                        }
                } else {
                        return ev->_repo_id;
                }
        } else
                return CORBA_exception_id (ev);
}
