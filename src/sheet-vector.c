/*
 * sheet-vector.c:  Implements sheet vectors.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999, 2000 Helix Code, Inc.  (http://www.helixcode.com)
 */
#include <config.h>
#include <bonobo/bonobo-object.h>
#include "idl/Gnumeric.h"
#include "gnumeric.h"
#include "sheet-vector.h"
#include "sheet.h"
#include "sheet-private.h"
#include "value.h"
#include "cell.h"
#include "ranges.h"
#include <stdio.h>

/* The entry point vectors for the server we provide */
static POA_GNOME_Gnumeric_Vector__epv  vector_epv;
static POA_GNOME_Gnumeric_Vector__vepv vector_vepv;

#define vector_from_servant(x) SHEET_VECTOR (bonobo_object_from_servant (x))

static BonoboObjectClass *vector_parent_class;

static CORBA_boolean
impl_vector_only_numbers (PortableServer_Servant servant,
			  CORBA_Environment *ev)
{
	printf ("FIXME: We are always reporting only numbers = TRUE\n");
	return CORBA_TRUE;
}

static int
find_block (SheetVector *vec, int index, int *ret_top, int *ret_idx)
{
	int total, i;

	for (i = total = 0; i < vec->n_blocks; i++){
		int old_total = total;

		total += vec->blocks [i].size;

		if (index <= total){
			*ret_top = total;
			*ret_idx = old_total - index;
			return i;
		}
	}

	g_warning ("Should not happen");

	return 0;
}

static GNOME_Gnumeric_DoubleVec *
impl_vector_get_numbers (PortableServer_Servant servant,
			 CORBA_short low, CORBA_short high,
			 CORBA_Environment *ev)
{
	SheetVector *vec = vector_from_servant (servant);
        GNOME_Gnumeric_DoubleVec *res;
	RangeBlock *block;
	int block_top, block_idx;
	int i, j, cols, rows, idx;

	if (high == -1)
		high = vec->len;

	printf ("Values requested are: %d %d\n", low, high);

	if ((low < 0) || (high > vec->len)){
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_OutOfRange, NULL);
		return NULL;
	}

	res = GNOME_Gnumeric_DoubleVec__alloc ();
        if (res == NULL)
                return NULL;

        res->_length = (high - low);
        res->_maximum = res->_length;
        res->_buffer = CORBA_sequence_CORBA_double_allocbuf (res->_length);

	/*
	 * Fill in the values
	 */
	block_idx = find_block (vec, low, &block_top, &idx);
	block = &vec->blocks [block_idx++];
	j = 0;
	cols = block->range.end.col - block->range.start.col + 1;
	rows = block->range.end.row - block->range.start.row + 1;

	for (i = low; i < high; i++, idx++){
		Cell *cell;
		int col, row;

		if (i == block_top){
			block = &vec->blocks [block_idx++];
			block_top += block->size;
			idx = 0;
			cols = block->range.end.col - block->range.start.col;
			rows = block->range.end.row - block->range.start.row;
		}

		col = idx / rows + block->range.start.col;
		row = idx % rows + block->range.start.row;

		cell = sheet_cell_get (vec->sheet, col, row);
		if (cell)
			res->_buffer [j++] = value_get_as_float (cell->value);
		else
			res->_buffer [j++] = 0.0;
	}

	for (j = 0, i = low; i < high; i++, j++){
		printf ("Valud %d: %g\n", i, res->_buffer [j]);
	}

	return res;
}

static GNOME_Gnumeric_VecValueVec *
impl_vector_get_vec_values (PortableServer_Servant servant,
			    CORBA_short low, CORBA_short high,
			    CORBA_Environment *ev)
{
	SheetVector *vec = vector_from_servant (servant);
        GNOME_Gnumeric_VecValueVec *res;
	RangeBlock *block;
	int block_top, block_idx;
	int i, j, cols, rows, idx;

	if (high == -1)
		high = vec->len;

	if ((low < 0) || (high > vec->len)){
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_OutOfRange, NULL);
		return NULL;
	}

	res = GNOME_Gnumeric_VecValueVec__alloc ();
        if (res == NULL)
                return NULL;

        res->_length = (high - low);
        res->_maximum = res->_length;
        res->_buffer = CORBA_sequence_GNOME_Gnumeric_VecValue_allocbuf (res->_length);

	/*
	 * Fill in the values
	 */
	block_idx = find_block (vec, low, &block_top, &idx);
	block = &vec->blocks [block_idx];
	block_idx++;
	j = 0;
	cols = block->range.end.col - block->range.start.col + 1;
	rows = block->range.end.row - block->range.start.row + 1;

	for (i = low; i < high; i++, idx++){
		GNOME_Gnumeric_VecValue vecvalue;
		Cell *cell;
		int col, row;

		if (i == block_top){
			block = &vec->blocks [block_idx++];
			block_top += block->size;
			idx = 0;
			cols = block->range.end.col - block->range.start.col;
			rows = block->range.end.row - block->range.start.row;
		}

		col = idx / rows + block->range.start.col;
		row = idx % rows + block->range.start.row;

		cell = sheet_cell_get (vec->sheet, col, row);

		if (cell){
			Value *value = cell->value;

			switch (value->type){
			case VALUE_EMPTY:
			case VALUE_ERROR:
			case VALUE_CELLRANGE:
			case VALUE_ARRAY:
				vecvalue._d= GNOME_Gnumeric_VALUE_FLOAT;
				vecvalue._u.v_float = 0.0;
				break;

			case VALUE_INTEGER:
				vecvalue._d= GNOME_Gnumeric_VALUE_FLOAT;
				vecvalue._u.v_float = value->v_int.val;
				break;

			case VALUE_FLOAT:
				vecvalue._d= GNOME_Gnumeric_VALUE_FLOAT;
				vecvalue._u.v_float = value->v_float.val;
				break;

			case VALUE_BOOLEAN:
				vecvalue._d= GNOME_Gnumeric_VALUE_FLOAT;
				vecvalue._u.v_float = value->v_bool.val;
				break;

			case VALUE_STRING:
				vecvalue._d= GNOME_Gnumeric_VALUE_STRING;
				vecvalue._u.str = CORBA_string_dup (value->v_str.val->str);
				break;

			}
		} else {
			vecvalue._d = GNOME_Gnumeric_VALUE_FLOAT;
			vecvalue._u.v_float = 0.0;
		}

		res->_buffer [j] = vecvalue;
		j++;
	}

	return res;

}

static CORBA_short
impl_vector_count (PortableServer_Servant servant, CORBA_Environment *ev)
{
	SheetVector *vec = vector_from_servant (servant);

	return vec->len;
}

static void
impl_vector_set (PortableServer_Servant servant, CORBA_short pos,
		 CORBA_double val, CORBA_Environment *ev)
{
/*	SheetVector *vec = vector_from_servant (servant);*/

	g_error ("Not implemented");
}

static void
impl_vector_set_notify (PortableServer_Servant servant,
			GNOME_Gnumeric_VectorNotify vector_notify,
			CORBA_Environment *ev)
{
	SheetVector *vec = vector_from_servant (servant);

	vec->notify = CORBA_Object_duplicate (vector_notify, ev);
}


static void
sheet_vector_destroy (GtkObject *object)
{
	SheetVector *vec = SHEET_VECTOR (object);
	CORBA_Environment ev;

	if (vec->sheet != NULL)
		g_error ("SheetVector has not been detached prior to destruction");

	CORBA_exception_init (&ev);
	CORBA_Object_release (vec->notify, &ev);
	CORBA_exception_free (&ev);

	if (vec->blocks)
		g_free (vec->blocks);

	GTK_OBJECT_CLASS (vector_parent_class)->destroy (object);
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

	vector_vepv.Bonobo_Unknown_epv = bonobo_object_get_epv ();
	vector_vepv.GNOME_Gnumeric_Vector_epv = &vector_epv;
}

static void
sheet_vector_class_init (GtkObjectClass *object_class)
{
	vector_parent_class = gtk_type_class (bonobo_object_get_type ());

	object_class->destroy = sheet_vector_destroy;

	init_vector_corba_class ();
}

static void
sheet_vector_init (GtkObject *object)
{
	SheetVector *vector = SHEET_VECTOR (object);

	vector->notify = CORBA_OBJECT_NIL;
}

GtkType
sheet_vector_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetVector",
			sizeof (SheetVector),
			sizeof (SheetVectorClass),
			(GtkClassInitFunc) sheet_vector_class_init,
			(GtkObjectInitFunc) sheet_vector_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (bonobo_object_get_type (), &info);
	}

	return type;
}

static GNOME_Gnumeric_Vector
sheet_vector_corba_object_create (BonoboObject *object)
{
	POA_GNOME_Gnumeric_Vector *servant;
	CORBA_Environment ev;

	servant = (POA_GNOME_Gnumeric_Vector *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &vector_vepv;

	CORBA_exception_init (&ev);
	POA_GNOME_Gnumeric_Vector__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	CORBA_exception_free (&ev);
	return (Bonobo_View) bonobo_object_activate_servant (object, servant);
}

SheetVector *
sheet_vector_new (Sheet *sheet)
{
	SheetVector *sheet_vector;
	GNOME_Gnumeric_Vector corba_vector;

	sheet_vector = gtk_type_new (sheet_vector_get_type ());

	sheet_vector->sheet = sheet;

	corba_vector = sheet_vector_corba_object_create (BONOBO_OBJECT (sheet_vector));
	if (corba_vector == NULL) {
		bonobo_object_unref (BONOBO_OBJECT (sheet_vector));
		return NULL;
	}

	bonobo_object_construct (BONOBO_OBJECT (sheet_vector), corba_vector);

	return sheet_vector;
}

void
sheet_vector_reset (SheetVector *sheet_vector)
{
	g_return_if_fail (sheet_vector != NULL);
	g_return_if_fail (IS_SHEET_VECTOR (sheet_vector));

	g_free (sheet_vector->blocks);
	sheet_vector->blocks = NULL;
	sheet_vector->n_blocks = 0;
	sheet_vector->len = 0;
}

void
sheet_vector_append_range (SheetVector *sheet_vector, Range *range)
{
	g_return_if_fail (sheet_vector != NULL);
	g_return_if_fail (IS_SHEET_VECTOR (sheet_vector));

	if (sheet_vector->blocks == NULL){
		sheet_vector->blocks = g_new0 (RangeBlock, 1);
		sheet_vector->n_blocks = 1;
		sheet_vector->blocks [0].size = (range->end.col - range->start.col + 1) *
			(range->end.row - range->start.row + 1);
		sheet_vector->blocks [0].range = *range;
		sheet_vector->len = sheet_vector->blocks [0].size;
	} else {
		g_error ("Not yet implemented");
	}
}

void
sheet_vector_attach (SheetVector *sheet_vector, Sheet *sheet)
{
	g_return_if_fail (sheet_vector != NULL);
	g_return_if_fail (IS_SHEET_VECTOR (sheet_vector));
	g_return_if_fail (IS_SHEET (sheet));

	sheet_vector->sheet = sheet;

	sheet->priv->sheet_vectors = g_slist_prepend (sheet->priv->sheet_vectors, sheet_vector);
}

void
sheet_vector_detach (SheetVector *sheet_vector)
{
	Sheet *sheet;

	g_return_if_fail (sheet_vector != NULL);
	g_return_if_fail (IS_SHEET_VECTOR (sheet_vector));

	sheet = sheet_vector->sheet;
	sheet_vector->sheet = NULL;

	g_return_if_fail (IS_SHEET (sheet));

	sheet->priv->sheet_vectors = g_slist_remove (sheet->priv->sheet_vectors, sheet_vector);
}

void
sheet_vectors_cell_changed (Cell *cell)
{
	GSList *l;
	const int col = cell->pos.col;
	const int row = cell->pos.row;
	int i;

	for (l = cell->sheet->priv->sheet_vectors; l; l = l->next){
		SheetVector *vec = l->data;

		if (vec->notify == CORBA_OBJECT_NIL)
			continue;

		for (i = 0; i < vec->n_blocks; i++)
			if (range_contains (&vec->blocks [i].range, col, row)){
				CORBA_Environment ev;

				/*
				 * FIXME: This is lame.  Find out the real index
				 * then, notify
				 */
				CORBA_exception_init (&ev);
				GNOME_Gnumeric_VectorNotify_changed (vec->notify, 0, vec->len, &ev);
				CORBA_exception_free (&ev);
			}
	}
}

void
sheet_vectors_shutdown (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	for (;sheet->priv->sheet_vectors;){
		SheetVector *sheet_vector = sheet->priv->sheet_vectors->data;

		sheet_vector_detach (sheet_vector);
		bonobo_object_unref (BONOBO_OBJECT (sheet->priv->sheet_vectors->data));
	}

	g_slist_free (sheet->priv->sheet_vectors);
	sheet->priv->sheet_vectors = NULL;
}

