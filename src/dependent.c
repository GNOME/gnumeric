/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dependent.c:  Manage calculation dependencies between objects
 *
 * Copyright (C) 2000-2002
 *  Jody Goldberg   (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include "dependent.h"

#include "workbook-priv.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "workbook-view.h"
#include "rendered-value.h" /* FIXME : should not be needed with JIT-R */
#include "ranges.h"
#include "gutils.h"

#include <string.h>

#define BUCKET_SIZE	128

static void dynamic_dep_eval 	   (Dependent *dep);
static void dynamic_dep_debug_name (Dependent const *dep, FILE *out);

static CellPos const dummy = { 0, 0 };
static GPtrArray *dep_classes = NULL;
static DependentClass dynamic_dep_class = {
	dynamic_dep_eval,
	NULL,
	dynamic_dep_debug_name,
};
typedef struct {
	Dependent  base;
	Dependent *container;
	GSList    *ranges;
} DynamicDep;

void
dependent_types_init (void)
{
	g_return_if_fail (dep_classes == NULL);

	/* Init with a trio of NULL classes so we can access directly */
	dep_classes = g_ptr_array_new ();
	g_ptr_array_add	(dep_classes, NULL); /* bogus filler */
	g_ptr_array_add	(dep_classes, NULL); /* Cell */
	g_ptr_array_add	(dep_classes, &dynamic_dep_class);
}

void
dependent_types_shutdown (void)
{
	g_return_if_fail (dep_classes != NULL);
	g_ptr_array_free (dep_classes, TRUE);
}

/**
 * dependent_register_type :
 * @klass : A vtable
 *
 * Store the vtable and allocate an ID for a new class
 * of dependents.
 */
guint32
dependent_type_register (DependentClass const *klass)
{
	guint32 res;

	g_return_val_if_fail (dep_classes != NULL, 0);

	g_ptr_array_add	(dep_classes, (gpointer)klass);
	res = dep_classes->len-1;

	g_return_val_if_fail (res <= DEPENDENT_TYPE_MASK, res);

	return res;
}

/*
 * dependent_flag_recalc:
 * @dep: the dependent that contains the expression needing recomputation.
 *
 * Marks @dep as needing recalculation
 * NOTE : it does NOT recursively dirty dependencies.
 */
#define dependent_flag_recalc(dep) \
  do { (dep)->flags |= DEPENDENT_NEEDS_RECALC; } while (0)

/**
 * dependent_changed:
 * @cell : the dependent that changed
 *
 * Links the dependent and queues a recalc.
 */
static void
dependent_changed (Dependent *dep)
{
	if (dep->sheet->workbook->recursive_dirty_enabled)
		dependent_queue_recalc (dep);
	else
		dependent_flag_recalc (dep);
}

/**
 * dependent_set_expr :
 * @dep : The dependent we are interested in.
 * @new_expr : new expression.
 *
 * When the expression associated with a dependent needs to change this routine
 * dispatches to the virtual handler unlinking if necessary.  Adds a ref to
 * @new_expr.
 * NOTE : it does NOT relink dependents in case they are going to move later.
 */
void
dependent_set_expr (Dependent *dep, GnmExpr const *new_expr)
{
	int const t = dependent_type (dep);

#if 0
{
	ParsePos pos;
	char *str;

	parse_pos_init_dep (&pos, dep);
	dependent_debug_name (dep, stdout);

	str = gnm_expr_as_string (new_expr, &pos, gnm_expr_conventions_default);
	printf(" new = %s\n", str);
	g_free (str);

	str = gnm_expr_as_string (dep->expression, &pos, gnm_expr_conventions_default);
	printf("\told = %s\n", str);
	g_free (str);
}
#endif

	if (dependent_is_linked (dep))
		dependent_unlink (dep, NULL);

	if (t == DEPENDENT_CELL) {
		/*
		 * Explicitly do not check for array subdivision, we may be
		 * replacing the corner of an array.
		 */
		cell_set_expr_unsafe (DEP_TO_CELL (dep), new_expr);
	} else {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		if (new_expr != NULL)
			gnm_expr_ref (new_expr);
		if (klass->set_expr != NULL)
			(*klass->set_expr) (dep, new_expr);

		if (dep->expression != NULL)
			gnm_expr_unref (dep->expression);
		dep->expression = new_expr;
		if (new_expr != NULL)
			dependent_changed (dep);
	}
}

/**
 * dependent_set_sheet
 * @dep :
 * @sheet :
 */
void
dependent_set_sheet (Dependent *dep, Sheet *sheet)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet == NULL);
	g_return_if_fail (!dependent_is_linked (dep));

	dep->sheet = sheet;
	if (dep->expression != NULL) {
		CellPos const *pos = (dependent_is_cell (dep))
			? &DEP_TO_CELL(dep)->pos : &dummy;
		dependent_link (dep, pos);
		dependent_changed (dep);
	}
}

static void
cb_cell_list_deps (Dependent *dep, gpointer user)
{
	GSList **list = (GSList **)user;
	*list = g_slist_prepend (*list, dep);
}

static GSList *
cell_list_deps (const Cell *cell)
{
	GSList *deps = NULL;
	cell_foreach_dep (cell, cb_cell_list_deps, &deps);
	return deps;
}


/**
 * dependent_queue_recalc_list :
 * @list :
 *
 * Queues any elements of @list for recalc that are not already queued,
 * and marks all elements as needing a recalc.
 */
static void
dependent_queue_recalc_list (GSList *list)
{
	GSList *work = NULL;

	for (; list != NULL ; list = list->next) {
		Dependent *dep = list->data;
		if (!dependent_needs_recalc (dep)) {
			dependent_flag_recalc (dep);
			work = g_slist_prepend (work, dep);
		}
	}

	/*
	 * Work is now a list of marked cells whose dependencies need
	 * to be marked.  Marking early guarentees that we will not
	 * get duplicates.  (And it thus limits the length of the list.)
	 * We treat work as a stack.
	 */

	while (work) {
		Dependent *dep = work->data;
		int const t = dependent_type (dep);

		/* Pop the top element.  */
		list = work;
		work = work->next;
		g_slist_free_1 (list);

		if (t == DEPENDENT_CELL) {
			GSList *deps = cell_list_deps (DEP_TO_CELL (dep));
			GSList *waste = NULL;
			GSList *next;
			for (list = deps; list != NULL ; list = next) {
				Dependent *dep = list->data;
				next = list->next;
				if (dependent_needs_recalc (dep)) {
					list->next = waste;
					waste = list;
				} else {
					dependent_flag_recalc (dep);
					list->next = work;
					work = list;
				}
			}
			g_slist_free (waste);
		} else if (t == DEPENDENT_DYNAMIC_DEP) {
			DynamicDep const *dyn = (DynamicDep *)dep;
			dependent_flag_recalc (dyn->container);
			work = g_slist_prepend (work, dyn->container);
		}
	}
}

void
dependent_queue_recalc (Dependent *dep)
{
	g_return_if_fail (dep != NULL);

	if (!dependent_needs_recalc (dep)) {
		GSList listrec;
		listrec.next = NULL;
		listrec.data = dep;
		dependent_queue_recalc_list (&listrec);
	}
}

/**************************************************************************/
#define ENABLE_MICRO_HASH
#ifdef ENABLE_MICRO_HASH
typedef struct {
	gint     num_buckets;
	gint     num_elements;
	union {
		GSList **buckets;
		GSList *singleton;
	} u;
} MicroHash;

#define MICRO_HASH_MIN_SIZE 11
#define MICRO_HASH_MAX_SIZE 13845163
#define MICRO_HASH_RESIZE(hash_table)						\
G_STMT_START {									\
	if ((hash_table->num_buckets > MICRO_HASH_MIN_SIZE &&			\
	     hash_table->num_buckets >= 3 * hash_table->num_elements) ||	\
	    (hash_table->num_buckets < MICRO_HASH_MAX_SIZE &&			\
	     3 * hash_table->num_buckets <= hash_table->num_elements))		\
		micro_hash_resize (hash_table);					\
} G_STMT_END

#define MICRO_HASH_hash(key) ((guint)(key))

static void
micro_hash_resize (MicroHash *hash_table)
{
	GSList **new_buckets, *node, *next;
	guint bucket;
	gint old_num_buckets = hash_table->num_buckets;
	gint new_num_buckets = g_spaced_primes_closest (hash_table->num_elements);

	if (new_num_buckets < MICRO_HASH_MIN_SIZE)
		new_num_buckets = MICRO_HASH_MIN_SIZE;
	else if (new_num_buckets > MICRO_HASH_MAX_SIZE)
		new_num_buckets = MICRO_HASH_MAX_SIZE;

	if (old_num_buckets <= 1) {
		if (new_num_buckets == 1)
			return;
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (node = hash_table->u.singleton; node; node = next) {
			next = node->next;
			bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
			node->next = new_buckets [bucket];
			new_buckets [bucket] = node;
		}
		hash_table->u.buckets = new_buckets;
	} else if (new_num_buckets > 1) {
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (old_num_buckets = hash_table->num_buckets; old_num_buckets-- > 0 ; )
			for (node = hash_table->u.buckets [old_num_buckets]; node; node = next) {
				next = node->next;
				bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
				node->next = new_buckets [bucket];
				new_buckets [bucket] = node;
			}
		g_free (hash_table->u.buckets);
		hash_table->u.buckets = new_buckets;
	} else {
		GSList *singleton = NULL;
		while (old_num_buckets-- > 0)
			singleton = g_slist_concat (hash_table->u.buckets [old_num_buckets], singleton);
		g_free (hash_table->u.buckets);
		hash_table->u.singleton = singleton;
	}

	hash_table->num_buckets = new_num_buckets;
}

static void
micro_hash_insert (MicroHash *hash_table, gpointer key)
{
	GSList **head;
	int const hash_size = hash_table->num_buckets;

	if (hash_size > 1) {
		guint const bucket = MICRO_HASH_hash (key) % hash_size;
		head = hash_table->u.buckets + bucket;
	} else
		head = & (hash_table->u.singleton);

	if (g_slist_find (*head, key) == NULL) {
		*head = g_slist_prepend (*head, key);
		hash_table->num_elements++;
		MICRO_HASH_RESIZE (hash_table);
	}
}

static void
micro_hash_remove (MicroHash *hash_table, gpointer key)
{
	GSList **head, *old;
	int const hash_size = hash_table->num_buckets;

	if (hash_size > 1) {
		guint const bucket = MICRO_HASH_hash (key) % hash_size;
		head = hash_table->u.buckets + bucket;
	} else
		head = & (hash_table->u.singleton);

	for (; *head != NULL ; head = &((*head)->next))
		if ((*head)->data == key) {
			old = *head;
			*head = old->next;
			g_slist_free_1 (old);
			hash_table->num_elements--;
			MICRO_HASH_RESIZE (hash_table);
			return;
		}
}

static void
micro_hash_release (MicroHash *hash_table)
{
	guint i = hash_table->num_buckets;

	if (i > 1) {
		while (i-- > 0)
			g_slist_free (hash_table->u.buckets[i]);
		g_free (hash_table->u.buckets);
		hash_table->u.buckets = NULL;
	} else {
		g_slist_free (hash_table->u.singleton);
		hash_table->u.singleton = NULL;
	}
	hash_table->num_elements = 0;
	hash_table->num_buckets = 1;
}

static void
micro_hash_init (MicroHash *hash_table, gpointer key)
{
	hash_table->num_elements = 1;
	hash_table->num_buckets = 1;
	hash_table->u.singleton = g_slist_prepend (NULL, key);
}

/*************************************************************************/

typedef MicroHash	DepCollection;
#define dep_collection_init(dc, dep)	\
	micro_hash_init (&(dc), dep)
#define dep_collection_insert(dc, dep)	\
	micro_hash_insert (&(dc), dep)
#define dep_collection_remove(dc, dep)	\
	micro_hash_remove (&(dc), dep)
#define dep_collection_release(dc)      \
	micro_hash_release (&(dc))
#define dep_collection_is_empty(dc)				\
	(dc.num_elements == 0)
#define dep_collection_foreach_dep(dc, dep, code) do {		\
	GSList *l;						\
	int i = dc.num_buckets;					\
	if (i <= 1) { 						\
		for (l = dc.u.singleton; l ; l = l->next) {	\
			Dependent *dep = l->data;		\
			code					\
		}						\
	} else while (i-- > 0) {				\
		for (l = dc.u.buckets [i]; l ; l = l->next) {	\
			Dependent *dep = l->data;		\
			code					\
		}						\
	}							\
} while (0)
#define dep_collection_foreach_list(dc, list, code) do {	\
	GSList *list;						\
	int i = dc.num_buckets;					\
	if (i <= 1) { 						\
		list = dc.u.singleton;				\
		code						\
	} else while (i-- > 0) {				\
		list = dc.u.buckets[i];				\
		code						\
	}							\
} while (0)
#else
typedef GSList *	DepCollection;
#define dep_collection_init(dc, dep)	\
	dc = g_slist_prepend (NULL, dep)
#define dep_collection_insert(dc, dep) \
	if (!g_slist_find (dc, dep)) dc = g_slist_prepend (dc, dep)
#define dep_collection_remove(dc, dep)	\
	dc = g_slist_remove (dc, dep);
#define dep_collection_release(dc)      \
	g_slist_free (dc)
#define dep_collection_is_empty(dc)	\
	(dc == NULL)
#define dep_collection_foreach_dep(dc, dep, code) do {		\
	GSList *l;						\
	for (l = dc; l != NULL ; l = l->next) {			\
		Dependent *dep = l->data;			\
		code						\
	}							\
} while (0)
#define dep_collection_foreach_list(dc, list, code) do {	\
	GSList *list = dc;					\
	code							\
} while (0)
#endif

/**************************************************************************
 * Data structures for managing dependencies between objects.
 *
 * The DependencyRange hash needs to be improved.  It is a huge
 * performance hit when there are large numbers of range depends.
 */

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another objects in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	DepCollection	deps;	/* Must be first */
	Range  range;
} DependencyRange;

/*
 *  A DependencySingle stores a list of dependents that rely
 * on the cell at @pos.
 *
 * A change in this cell will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	DepCollection	deps;	/* Must be first */
	CellPos pos;
} DependencySingle;

/* A utility type */
typedef struct {
	DepCollection	deps;	/* Must be first */
} DependencyAny;

static guint
deprange_hash (DependencyRange const *r)
{
	return ((((r->range.start.row << 8) + r->range.end.row) << 8) +
		(r->range.start.col << 8) + (r->range.end.col));
}
static gint
deprange_equal (DependencyRange const *r1, DependencyRange const *r2)
{
	return range_equal (&(r1->range), &(r2->range));
}

static guint
depsingle_hash (DependencySingle const *depsingle)
{
	return (depsingle->pos.row << 8) ^ depsingle->pos.col;
}
static gint
depsingle_equal (DependencySingle const *a, DependencySingle const *b)
{
	return (a->pos.row == b->pos.row && a->pos.col == b->pos.col);
}

static DependentFlags
link_single_dep (Dependent *dep, CellPos const *pos, CellRef const *ref)
{
	DependencySingle lookup;
	DependencySingle *single;
	GnmDepContainer *deps;
	DependentFlags flag = DEPENDENT_NO_FLAG;
 
	if (ref->sheet != NULL) {
		if (ref->sheet != dep->sheet)
			flag = (ref->sheet->workbook != dep->sheet->workbook)
				? DEPENDENT_GOES_INTERBOOK : DEPENDENT_GOES_INTERSHEET;
		deps = ref->sheet->deps;
	} else
		deps = dep->sheet->deps;

	/* Inserts if it is not already there */
	cellref_get_abs_pos (ref, pos, &lookup.pos);
	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single == NULL) {
		single = gnm_mem_chunk_alloc (deps->single_pool);
		*single = lookup;
		dep_collection_init (single->deps, dep);
		g_hash_table_insert (deps->single_hash, single, single);
	} else
		dep_collection_insert (single->deps, dep);

	return flag;
}

static void
unlink_single_dep (Dependent *dep, CellPos const *pos, CellRef const *a)
{
	DependencySingle lookup;
	DependencySingle *single;
	GnmDepContainer *deps = eval_sheet (a->sheet, dep->sheet)->deps;

	if (!deps)
		return;

	cellref_get_abs_pos (a, pos, &lookup.pos);
	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL) {
		dep_collection_remove (single->deps, dep);
		if (dep_collection_is_empty (single->deps)) {
			g_hash_table_remove (deps->single_hash, single);
			dep_collection_release (single->deps);
			gnm_mem_chunk_free (deps->single_pool, single);
		}
	}
}

static void
link_range_dep (GnmDepContainer *deps, Dependent *dep,
		DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;

	for ( ; i <= end; i++) {
		/* Look it up */
		DependencyRange *result;

		if (deps->range_hash [i] == NULL) {
			deps->range_hash [i] = g_hash_table_new (
				(GHashFunc)  deprange_hash,
				(GEqualFunc) deprange_equal);
			result = NULL;
		} else {
			result = g_hash_table_lookup (deps->range_hash[i], r);
			if (result) {
				/* Inserts if it is not already there */
				dep_collection_insert (result->deps, dep);
				continue;
			}
		}

		/* Create a new DependencyRange structure */
		result = gnm_mem_chunk_alloc (deps->range_pool);
		*result = *r;
		dep_collection_init (result->deps, dep);
		g_hash_table_insert (deps->range_hash[i], result, result);
	}
}

static void
unlink_range_dep (GnmDepContainer *deps, Dependent *dep,
		  DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;
	DependencyRange *result;

	if (!deps)
		return;

	for ( ; i <= end; i++) {
		result = g_hash_table_lookup (deps->range_hash[i], r);
		if (result) {
			dep_collection_remove (result->deps, dep);
			if (dep_collection_is_empty (result->deps)) {
				g_hash_table_remove (deps->range_hash[i], result);
				gnm_mem_chunk_free (deps->range_pool, result);
			}
		}
	}
}

static DependentFlags
link_cellrange_dep (Dependent *dep, CellPos const *pos,
		    CellRef const *a, CellRef const *b)
{
	DependencyRange range;
	DependentFlags flag = DEPENDENT_NO_FLAG;
 
	cellref_get_abs_pos (a, pos, &range.range.start);
	cellref_get_abs_pos (b, pos, &range.range.end);
	range_normalize (&range.range);

	if (a->sheet != NULL) {
		if (a->sheet != dep->sheet)
			flag = (a->sheet->workbook != dep->sheet->workbook)
				? DEPENDENT_GOES_INTERBOOK : DEPENDENT_GOES_INTERSHEET;

		if (b->sheet != NULL && a->sheet != b->sheet) {
			Workbook const *wb = a->sheet->workbook;
			int i = a->sheet->index_in_wb;
			int stop = b->sheet->index_in_wb;
			if (i < stop) { int tmp = i; i = stop ; stop = tmp; }

			g_return_val_if_fail (b->sheet->workbook == wb, flag);

			while (i <= stop) {
				Sheet *sheet = g_ptr_array_index (wb->sheets, i);
				link_range_dep (sheet->deps, dep, &range);
			}
			flag |= DEPENDENT_HAS_3D;
		} else
			link_range_dep (a->sheet->deps, dep, &range);
	} else
		link_range_dep (dep->sheet->deps, dep, &range);

	return flag;
}
static void
unlink_cellrange_dep (Dependent *dep, CellPos const *pos,
		      CellRef const *a, CellRef const *b)
{
	DependencyRange range;

	cellref_get_abs_pos (a, pos, &range.range.start);
	cellref_get_abs_pos (b, pos, &range.range.end);
	range_normalize (&range.range);

	if (a->sheet != NULL) {
		if (b->sheet != NULL && a->sheet != b->sheet) {
			Workbook const *wb = a->sheet->workbook;
			int i = a->sheet->index_in_wb;
			int stop = b->sheet->index_in_wb;
			if (i < stop) { int tmp = i; i = stop ; stop = tmp; }

			g_return_if_fail (b->sheet->workbook == wb);

			while (i <= stop) {
				Sheet *sheet = g_ptr_array_index (wb->sheets, i);
				unlink_range_dep (sheet->deps, dep, &range);
			}
		} else
			unlink_range_dep (a->sheet->deps, dep, &range);
	} else
		unlink_range_dep (dep->sheet->deps, dep, &range);
}

static DependentFlags
link_expr_dep (Dependent *dep, CellPos const *pos, GnmExpr const *tree)
{
	switch (tree->any.oper) {
	case GNM_EXPR_OP_ANY_BINARY:
		return  link_expr_dep (dep, pos, tree->binary.value_a) |
			link_expr_dep (dep, pos, tree->binary.value_b);
	case GNM_EXPR_OP_ANY_UNARY : return link_expr_dep (dep, pos, tree->unary.value);
	case GNM_EXPR_OP_CELLREF   : return link_single_dep (dep, pos, &tree->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		/* TODO: pass in eval flags so that we can use implicit
		 * intersection
		 */
		if (VALUE_CELLRANGE == tree->constant.value->type)
			return link_cellrange_dep (dep, pos,
				&tree->constant.value->v_range.cell.a,
				&tree->constant.value->v_range.cell.b);
		return DEPENDENT_NO_FLAG;

	/* TODO : Can we use argument types to be smarter here ? */
	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l;
		DependentFlags flag = DEPENDENT_NO_FLAG;
		if (tree->func.func->fn_type == GNM_FUNC_TYPE_STUB)
			gnm_func_load_stub (tree->func.func);
		if (tree->func.func->linker) {
			EvalPos		 ep;
			FunctionEvalInfo fei;
			fei.pos = eval_pos_init_dep (&ep, dep);
			fei.func_call = (GnmExprFunction const *)tree;
			flag = tree->func.func->linker (&fei);
		}
		for (l = tree->func.arg_list; l; l = l->next)
			flag |= link_expr_dep (dep, pos, l->data);
		return flag;
	}

	case GNM_EXPR_OP_NAME:
		expr_name_add_dep (tree->name.name, dep);
		if (tree->name.name->active)
			return link_expr_dep (dep, pos, tree->name.name->expr_tree) | DEPENDENT_USES_NAME;
		return DEPENDENT_USES_NAME;

	case GNM_EXPR_OP_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			CellRef a;

			/* We cannot support array expressions unless
			 * we have a position.
			 */
			g_return_val_if_fail (pos != NULL, DEPENDENT_NO_FLAG);

			a.col_relative = a.row_relative = FALSE;
			a.sheet = dep->sheet;
			a.col   = pos->col - tree->array.x;
			a.row   = pos->row - tree->array.y;

			return link_single_dep (dep, pos, &a);
		} else
			/* Corner cell depends on the contents of the expr */
			return link_expr_dep (dep, pos, tree->array.corner.expr);

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;
		DependentFlags res = DEPENDENT_NO_FLAG;

		for (l = tree->set.set; l; l = l->next)
			res |= link_expr_dep (dep, pos, l->data);
		return res;
	}
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		return DEPENDENT_NO_FLAG; /* handled at run time */

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
	}
	return 0;
}

static void
unlink_expr_dep (Dependent *dep, CellPos const *pos, GnmExpr const *tree)
{
	switch (tree->any.oper) {
	case GNM_EXPR_OP_ANY_BINARY:
		unlink_expr_dep (dep, pos, tree->binary.value_a);
		unlink_expr_dep (dep, pos, tree->binary.value_b);
		return;

	case GNM_EXPR_OP_ANY_UNARY : unlink_expr_dep (dep, pos, tree->unary.value);
		return;
	case GNM_EXPR_OP_CELLREF	    : unlink_single_dep (dep, pos, &tree->cellref.ref);
		return;

	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_CELLRANGE == tree->constant.value->type)
			unlink_cellrange_dep (dep, pos,
				&tree->constant.value->v_range.cell.a,
				&tree->constant.value->v_range.cell.b);
		return;

	/*
	 * FIXME: needs to be taught implicit intersection +
	 * more cunning handling of argument type matching.
	 */
	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l;
		if (tree->func.func->unlinker) {
			EvalPos		 ep;
			FunctionEvalInfo fei;
			fei.pos = eval_pos_init_dep (&ep, dep);
			fei.func_call = (GnmExprFunction const *)tree;
			tree->func.func->unlinker (&fei);
		}
		for (l = tree->func.arg_list; l; l = l->next)
			unlink_expr_dep (dep, pos, l->data);
		return;
	}

	case GNM_EXPR_OP_NAME:
		expr_name_remove_dep (tree->name.name, dep);
		if (tree->name.name->active)
			unlink_expr_dep (dep, pos, tree->name.name->expr_tree);
		return;

	case GNM_EXPR_OP_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			CellRef a;

			/* We cannot support array expressions unless
			 * we have a position.
			 */
			g_return_if_fail (pos != NULL);

			a.col_relative = a.row_relative = FALSE;
			a.sheet = dep->sheet;
			a.col   = pos->col - tree->array.x;
			a.row   = pos->row - tree->array.y;

			unlink_single_dep (dep, pos, &a);
		} else
			/* Corner cell depends on the contents of the expr */
			unlink_expr_dep (dep, pos, tree->array.corner.expr);
		return;

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;

		for (l = tree->set.set; l; l = l->next)
			unlink_expr_dep (dep, pos, l->data);
		return;
	}

	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		return;

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
	}
}

static void
workbook_link_3d_dep (Dependent *dep)
{
	Workbook *wb = dep->sheet->workbook;

	if (wb->being_reordered)
		return;
	if (wb->sheet_order_dependents == NULL)
		wb->sheet_order_dependents =
			g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_insert (wb->sheet_order_dependents, dep, dep);
}

static void
workbook_unlink_3d_dep (Dependent *dep)
{
	Workbook *wb = dep->sheet->workbook;

	/* during destruction */
	if (wb->sheet_order_dependents == NULL)
		return;

	if (wb->being_reordered)
		return;
	g_hash_table_remove (dep->sheet->workbook->sheet_order_dependents, dep);
}

/*****************************************************************************/

static void dynamic_dep_eval (__attribute__((unused)) Dependent *dep) { }

static void
dynamic_dep_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "DynamicDep%p", dep);
}
void
dependent_add_dynamic_dep (Dependent *dep, ValueRange const *v)
{
	DependentFlags   flags;
	DynamicDep	*dyn;
	CellPos const	*pos;
	
	g_return_if_fail (dep != NULL);

	pos = (dependent_is_cell (dep))
		? &DEP_TO_CELL(dep)->pos : &dummy;

	if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
		dyn = g_hash_table_lookup (dep->sheet->deps->dynamic_deps, dep);
	else {
		dep->flags |= DEPENDENT_HAS_DYNAMIC_DEPS;
		dyn = g_new (DynamicDep, 1);
		dyn->base.flags		= DEPENDENT_DYNAMIC_DEP;
		dyn->base.sheet		= dep->sheet;
		dyn->base.expression	= NULL;
		dyn->container		= dep;
		dyn->ranges		= NULL;
		g_hash_table_insert (dep->sheet->deps->dynamic_deps, dep, dyn);
	}

	flags = link_cellrange_dep (&dyn->base, pos, &v->cell.a, &v->cell.b);
	dyn->ranges = g_slist_prepend (dyn->ranges, value_duplicate ((Value *)v));
	if (flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

static void
dynamic_dep_free (DynamicDep *dyn)
{
	Dependent *dep = dyn->container;
	CellPos const *pos = (dependent_is_cell (dep))
		? &DEP_TO_CELL(dep)->pos : &dummy;
	ValueRange *v;
	GSList *ptr = dyn->ranges;

	for (ptr = dyn->ranges ; ptr != NULL ; ptr = ptr->next) {
		v = ptr->data;
		unlink_cellrange_dep (&dyn->base, pos, &v->cell.a, &v->cell.b);
		value_release ((Value *)v);
	}
	g_slist_free (dyn->ranges);
	dyn->ranges = NULL;

	if (dyn->base.flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (&dyn->base);
}

static void
dependent_clear_dynamic_deps (Dependent *dep)
{
	DynamicDep *dyn = g_hash_table_lookup (dep->sheet->deps->dynamic_deps, dep);
	if (dyn != NULL)
		dynamic_dep_free (dyn);
}

/*****************************************************************************/

/**
 * dependent_link:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Adds the dependent to the workbook wide list of dependents.
 */
void
dependent_link (Dependent *dep, CellPos const *pos)
{
	Sheet *sheet;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (!(dep->flags & DEPENDENT_IS_LINKED));
	g_return_if_fail (IS_SHEET (dep->sheet));
	g_return_if_fail (dep->sheet->deps != NULL);

	sheet = dep->sheet;

	/* Make this the new head of the dependent list.  */
	dep->prev_dep = NULL;
	dep->next_dep = sheet->deps->dependent_list;
	if (dep->next_dep)
		dep->next_dep->prev_dep = dep;
	sheet->deps->dependent_list = dep;
	dep->flags |=
		DEPENDENT_IS_LINKED |
		link_expr_dep (dep, pos, dep->expression);

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

/**
 * dependent_unlink:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Removes the dependent from its containers set of dependents and always
 * removes the linkages to what it depends on.
 */
void
dependent_unlink (Dependent *dep, CellPos const *pos)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dependent_is_linked (dep));
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (IS_SHEET (dep->sheet));

	if (pos == NULL)
		pos = (dependent_is_cell (dep)) ? &DEP_TO_CELL(dep)->pos : &dummy;

	unlink_expr_dep (dep, pos, dep->expression);
	if (dep->sheet->deps != NULL) {
		if (dep->sheet->deps->dependent_list == dep)
			dep->sheet->deps->dependent_list = dep->next_dep;
		if (dep->next_dep)
			dep->next_dep->prev_dep = dep->prev_dep;
		if (dep->prev_dep)
			dep->prev_dep->next_dep = dep->next_dep;
	}

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (dep);
	if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
		dependent_clear_dynamic_deps (dep);
	dep->flags &= ~DEPENDENT_LINK_FLAGS;
}

/**
 * cell_eval_content:
 * @cell: the cell to evaluate.
 *
 * This function evaluates the contents of the cell,
 * it should not be used by anyone. It is an internal
 * function.
 **/
gboolean
cell_eval_content (Cell *cell)
{
	static Cell *iterating = NULL;
	Value   *v;
	EvalPos	 pos;
	int	 max_iteration;

	if (!cell_has_expr (cell))
		return TRUE;

	/* do this here rather than dependent_eval
	 * because this routine is sometimes called
	 * directly
	 */
	if (cell->base.flags & DEPENDENT_HAS_DYNAMIC_DEPS) {
		dependent_clear_dynamic_deps (CELL_TO_DEP (cell));
		cell->base.flags &= ~DEPENDENT_HAS_DYNAMIC_DEPS;
	}

#ifdef DEBUG_EVALUATION
	{
		ParsePos pp;
		char *str = gnm_expr_as_string (cell->base.expression,
			parse_pos_init_cell (&pp, cell), gnm_expr_conventions_default);
		printf ("{\nEvaluating %s: %s;\n", cell_name (cell), str);
		g_free (str);
	}
#endif

	/* This is the bottom of a cycle */
	if (cell->base.flags & DEPENDENT_BEING_CALCULATED) {
		if (!cell->base.sheet->workbook->iteration.enabled)
			return TRUE;

		/* but not the first bottom */
		if (cell->base.flags & DEPENDENT_BEING_ITERATED) {
#ifdef DEBUG_EVALUATION
			printf ("}; /* already-iterate (%d) */\n", iterating == NULL);
#endif
			return iterating == NULL;
		}

		/* if we are still marked as iterating then make this the last
		 * time through.
		 */
		if (iterating == cell) {
#ifdef DEBUG_EVALUATION
			puts ("}; /* NO-iterate (1) */");
#endif
			return TRUE;
		} else if (iterating == NULL) {
			cell->base.flags |= DEPENDENT_BEING_ITERATED;
			iterating = cell;
#ifdef DEBUG_EVALUATION
			puts ("}; /* START iterate = TRUE (0) */");
#endif
			return FALSE;
		} else {
#ifdef DEBUG_EVALUATION
			puts ("}; /* other-iterate (0) */");
#endif
			return FALSE;
		}
	}

	/* Prepare to calculate */
	eval_pos_init_cell (&pos, cell);
	cell->base.flags |= DEPENDENT_BEING_CALCULATED;
	max_iteration = cell->base.sheet->workbook->iteration.max_number;

iterate :
	v = gnm_expr_eval (cell->base.expression, &pos,
		GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (v == NULL)
		v = value_new_error (&pos, "Internal error");

#ifdef DEBUG_EVALUATION
	{
		char *valtxt = v
			? value_get_as_string (v)
			: g_strdup ("NULL");
		printf ("Evaluation(%d) %s := %s\n", max_iteration, cell_name (cell), valtxt);
		g_free (valtxt);
	}
#endif

	/* The top of a cycle */
	if (cell->base.flags & DEPENDENT_BEING_ITERATED) {
		cell->base.flags &= ~DEPENDENT_BEING_ITERATED;

		/* We just completed the last iteration, don't change things */
		if (iterating && max_iteration-- > 0) {
			/* If we are within bounds make this the last round */
			if (value_diff (cell->value, v) < cell->base.sheet->workbook->iteration.tolerance)
				max_iteration = 0;
			else {
#ifdef DEBUG_EVALUATION
				puts ("/* iterate == NULL */");
#endif
				iterating = NULL;
			}
			value_release (cell->value);
			cell->value = v;
#ifdef DEBUG_EVALUATION
			puts ("/* LOOP */");
#endif
			goto iterate;
		}
		g_return_val_if_fail (iterating, TRUE);
		iterating = NULL;
	} else {
		/* do not use cell_assign_value unless you pass in the format */
		if (cell->value != NULL)
			value_release (cell->value);
		cell->value = v;

		/* Optimization : Since we don't span calculated cells
		 * it is ok, to wipe rendered values.  The drawing routine
		 * will handle it.
		 */
		if (cell->rendered_value != NULL) {
			rendered_value_destroy (cell->rendered_value);
			cell->rendered_value = NULL;
		}
	}

	if (iterating == cell)
		iterating = NULL;

#ifdef DEBUG_EVALUATION
	printf ("} (%d)\n", iterating == NULL);
#endif
	cell->base.flags &= ~DEPENDENT_BEING_CALCULATED;
	cell->row_info->needs_respan = TRUE;
	return iterating == NULL;
}

/**
 * dependent_eval :
 * @dep :
 */
gboolean
dependent_eval (Dependent *dep)
{
	if (dependent_needs_recalc (dep)) {
		int const t = dependent_type (dep);

		if (t != DEPENDENT_CELL) {
			DependentClass *klass = g_ptr_array_index (dep_classes, t);

			g_return_val_if_fail (klass, FALSE);

			if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS) {
				dependent_clear_dynamic_deps (dep);
				dep->flags &= ~DEPENDENT_HAS_DYNAMIC_DEPS;
			}

			(*klass->eval) (dep);
		} else {
			/* This will clear the dynamic deps too, see comment there
			 * to explain asymetry.
			 */
			gboolean finished = cell_eval_content (DEP_TO_CELL (dep));

			/* This should always be the top of the stack */
			g_return_val_if_fail (finished, FALSE);
		}

		/* Don't clear flag until after in case we iterate */
		dep->flags &= ~DEPENDENT_NEEDS_RECALC;
		return TRUE;
	}
	return FALSE;
}


/**
 * cell_queue_recalc :
 * @cell :
 *
 * Queue the cell and everything that depends on it for recalculation.
 * If a dependency is already queued ignore it.
 */
void
cell_queue_recalc (Cell const *cell)
{
	g_return_if_fail (cell != NULL);

	if (!cell_needs_recalc (cell)) {
		GSList *deps;

		if (cell_has_expr (cell))
			dependent_flag_recalc (CELL_TO_DEP (cell));

		deps = cell_list_deps (cell);
		dependent_queue_recalc_list (deps);
		g_slist_free (deps);
	}
}

typedef struct {
	int      col, row;
	DepFunc	 func;
	gpointer user;
} search_rangedeps_closure_t;

static void
cb_search_rangedeps (gpointer key, __attribute__((unused)) gpointer value,
		     gpointer closure)
{
	search_rangedeps_closure_t const *c = closure;
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

#if 0
	/* When things get slow this is a good test to enable */
	static int counter = 0;
	if ((++counter % 100000) == 0)
	    printf ("%d\n", counter / 100000);
#endif

	if (range_contains (range, c->col, c->row)) {
		DepFunc	 func = c->func;
		dep_collection_foreach_dep (deprange->deps, dep,
			(func) (dep, c->user););
	}
}

static void
cell_foreach_range_dep (Cell const *cell, DepFunc func, gpointer user)
{
	search_rangedeps_closure_t closure;
	GHashTable *bucket =
		cell->base.sheet->deps->range_hash [cell->pos.row /BUCKET_SIZE];

	if (bucket != NULL) {
		closure.col   = cell->pos.col;
		closure.row   = cell->pos.row;
		closure.func  = func;
		closure.user  = user;
		g_hash_table_foreach (bucket,
			&cb_search_rangedeps, &closure);
	}
}

static void
cell_foreach_single_dep (Sheet const *sheet, int col, int row,
			 DepFunc func, gpointer user)
{
	DependencySingle lookup, *single;
	GnmDepContainer *deps = sheet->deps;

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL)
		dep_collection_foreach_dep (single->deps, dep,
			(*func) (dep, user););
}

void
cell_foreach_dep (Cell const *cell, DepFunc func, gpointer user)
{
	g_return_if_fail (cell != NULL);

	/* accelerate exit */
	if (!cell->base.sheet->deps)
		return;

	cell_foreach_range_dep (cell, func, user);
	cell_foreach_single_dep (cell->base.sheet, cell->pos.col, cell->pos.row,
				 func, user);
}

static void
cb_recalc_all_depends (gpointer key, __attribute__((unused)) gpointer value,
		       __attribute__((unused)) gpointer ignore)
{
	DependencyAny const *depany = key;
	dep_collection_foreach_list (depany->deps, list,
		dependent_queue_recalc_list (list););
}

static void
cb_range_contained_depend (gpointer key, __attribute__((unused)) gpointer value,
			   gpointer user)
{
	DependencyRange const *deprange  = key;
	Range const *range = &deprange->range;
	Range const *target = user;

	if (range_overlap (target, range))
		dep_collection_foreach_list (deprange->deps, list,
			dependent_queue_recalc_list (list););
}

static void
cb_single_contained_depend (gpointer key,
			    __attribute__((unused)) gpointer value,
			    gpointer user)
{
	DependencySingle const *depsingle  = key;
	Range const *target = user;

	if (range_contains (target, depsingle->pos.col, depsingle->pos.row))
		dep_collection_foreach_list (depsingle->deps, list,
			dependent_queue_recalc_list (list););
}

/**
 * sheet_region_queue_recalc :
 *
 * @sheet : The sheet.
 * @range : Optionally NULL range.
 *
 * Queues things that depend on @sheet!@range for recalc.
 *
 * If @range is NULL the entire sheet is used.
 */
void
sheet_region_queue_recalc (Sheet const *sheet, Range const *r)
{
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	if (r == NULL) {
		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep,
			dependent_flag_recalc (dep););

		/* look for things that depend on the sheet */
		for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					&cb_recalc_all_depends, NULL);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_recalc_all_depends, NULL);
	} else {
		int const first = r->start.row / BUCKET_SIZE;

		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			Cell *cell = DEP_TO_CELL (dep);
			if (dependent_is_cell (dep) &&
			    range_contains (r, cell->pos.col, cell->pos.row))
				dependent_flag_recalc (dep);
		});

		/* look for things that depend on target region */
		for (i = (r->end.row)/BUCKET_SIZE; i >= first ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					&cb_range_contained_depend, (gpointer)r);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_single_contained_depend, (gpointer)r);
	}
}

typedef struct
{
    	int dep_type;
	union {
		EvalPos    pos;
		Dependent *dep;
	} u;
	GnmExpr const *oldtree;
} ExprRelocateStorage;

/**
 * dependents_unrelocate_free :
 * @info :
 *
 * Free the undo info associated with a dependent relocation.
 */
void
dependents_unrelocate_free (GSList *info)
{
	GSList *ptr = info;
	for (; ptr != NULL ; ptr = ptr->next) {
		ExprRelocateStorage *tmp = (ExprRelocateStorage *)(ptr->data);
		gnm_expr_unref (tmp->oldtree);
		g_free (tmp);
	}
	g_slist_free (info);
}

/**
 * dependents_unrelocate :
 * @info :
 *
 * Apply _and_ free the undo info associated with a dependent relocation.
 */
void
dependents_unrelocate (GSList *info)
{
	GSList *ptr = info;
	for (; ptr != NULL ; ptr = ptr->next) {
		ExprRelocateStorage *tmp = (ExprRelocateStorage *)(ptr->data);

		if (tmp->dep_type == DEPENDENT_CELL) {
			Cell *cell = sheet_cell_get (tmp->u.pos.sheet,
						     tmp->u.pos.eval.col,
						     tmp->u.pos.eval.row);

			/* It is possible to have a NULL if the relocation info
			 * is not really relevant.  eg when undoing a pasted
			 * cut that was relocated but also saved to a buffer.
			 */
			if (cell != NULL)
				sheet_cell_set_expr (cell, tmp->oldtree);
		} else {
			dependent_set_expr (tmp->u.dep, tmp->oldtree);
			dependent_flag_recalc (tmp->u.dep);
			dependent_link (tmp->u.dep, NULL);
		}
		gnm_expr_unref (tmp->oldtree);
		g_free (tmp);
	}
	g_slist_free (info);
}

/**
 * dependents_link :
 * @deps : An slist of dependents that GETS FREED
 * @rwinfo : optionally NULL
 *
 * link a list of dependents, BUT if the optional @rwinfo is specified and we
 * are invalidating a sheet or workbook don't bother to link things in the same
 * sheet or workbook.
 */
void
dependents_link (GSList *deps, GnmExprRewriteInfo const *rwinfo)
{
	GSList *ptr = deps;

	/* put them back */
	for (; ptr != NULL ; ptr = ptr->next) {
		Dependent *dep = ptr->data;
		if (rwinfo != NULL) {
			if (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK) {
				if (rwinfo->u.workbook == dep->sheet->workbook)
					continue;
			} else if (rwinfo->type == GNM_EXPR_REWRITE_SHEET)
				if (rwinfo->u.sheet == dep->sheet)
					continue;
		}
		if (dep->sheet->deps != NULL && !dependent_is_linked (dep)) {
			dependent_link (dep, dependent_is_cell (dep)
				? &DEP_TO_CELL (dep)->pos : &dummy);
			dependent_queue_recalc (dep);
		}
	}

	g_slist_free (deps);
}

typedef struct {
	Range const *target;
	GSList *list;
} CollectClosure;

static void
cb_range_contained_collect (DependencyRange const *deprange,
			    __attribute__((unused)) gpointer ignored,
			    CollectClosure *user)
{
	Range const *range = &deprange->range;

	if (range_overlap (user->target, range))
		dep_collection_foreach_dep (deprange->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE))) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

static void
cb_single_contained_collect (DependencySingle const *depsingle,
			     __attribute__((unused)) gpointer ignored,
			     CollectClosure *user)
{
	if (range_contains (user->target, depsingle->pos.col, depsingle->pos.row))
		dep_collection_foreach_dep (depsingle->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE))) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

/**
 * dependents_relocate:
 * Fixes references to or from a region that is going to be moved.
 *
 * @info : the descriptor record for what is being moved where.
 *
 * Returns a list of the locations and expressions that were changed outside of
 * the region.
 * NOTE : Does not queue the changed elemenents or their recursive dependents
 * for recalc
 */
GSList *
dependents_relocate (GnmExprRelocateInfo const *info)
{
	GnmExprRewriteInfo rwinfo;
	Dependent *dep;
	GSList    *l, *dependents = NULL, *undo_info = NULL;
	Sheet	  *sheet;
	Range const   *r;
	GnmExpr const *newtree;
	int i;
	CollectClosure collect;

	g_return_val_if_fail (info != NULL, NULL);

	/* short circuit if nothing would move */
	if (info->col_offset == 0 && info->row_offset == 0 &&
	    info->origin_sheet == info->target_sheet)
		return NULL;

	sheet = info->origin_sheet;
	r     = &info->origin;

	/* collect contained cells with expressions */
	SHEET_FOREACH_DEPENDENT (info->origin_sheet, dep, {
		Cell *cell = DEP_TO_CELL (dep);
		if (dependent_is_cell (dep) &&
		    range_contains (r, cell->pos.col, cell->pos.row)) {
			dependents = g_slist_prepend (dependents, dep);
			dep->flags |= DEPENDENT_FLAGGED;
		}
	});

	/* collect the things that depend on source region */
	collect.target = r;
	collect.list = dependents;
	g_hash_table_foreach (sheet->deps->single_hash,
		(GHFunc) &cb_single_contained_collect,
		(gpointer)&collect);
	{
		int const first = r->start.row / BUCKET_SIZE;
		GHashTable *hash;
		for (i = (r->end.row)/BUCKET_SIZE; i >= first ; i--) {
			hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					(GHFunc) &cb_range_contained_collect,
					(gpointer)&collect);
		}
	}
	dependents = collect.list;

	rwinfo.type = GNM_EXPR_REWRITE_RELOCATE;
	memcpy (&rwinfo.u.relocate, info, sizeof (GnmExprRelocateInfo));

	for (l = dependents; l; l = l->next) {
		dep = l->data;
		dep->flags &= ~DEPENDENT_FLAGGED;
		sheet_flag_status_update_range (dep->sheet, NULL);

		eval_pos_init_dep (&rwinfo.u.relocate.pos, dep);

		/* its possible nothing changed for contained deps
		 * using absolute references
		 */
		newtree = gnm_expr_rewrite (dep->expression, &rwinfo);
		if (newtree != NULL) {
			int const t = dependent_type (dep);
			ExprRelocateStorage *tmp =
				g_new (ExprRelocateStorage, 1);

			tmp->dep_type = t;
			if (t != DEPENDENT_CELL)
				tmp->u.dep = dep;
			else
				tmp->u.pos = rwinfo.u.relocate.pos;
			tmp->oldtree = dep->expression;
			gnm_expr_ref (tmp->oldtree);
			undo_info = g_slist_prepend (undo_info, tmp);

			dependent_set_expr (dep, newtree); /* unlinks */
			gnm_expr_unref (newtree);

			/* queue the things that depend on the changed dep
			 * even if it is going to move.
			 */
			dependent_queue_recalc (dep);

			/* relink if it is not going to move, if it is moving
			 * then the caller is responsible for relinking.
			 * This avoids a link/unlink/link tuple
			 */
			if (t == DEPENDENT_CELL) {
				CellPos const *pos = &DEP_TO_CELL (dep)->pos;
				if (dep->sheet != sheet ||
				    !range_contains (r, pos->col, pos->row))
					dependent_link (dep, pos);
			}
		}

		/* Not the most efficient, but probably not too bad.  It is
		 * definitely cheaper tha finding the set of effected sheets.
		 */
		sheet_flag_status_update_range (dep->sheet, NULL);
	}

	g_slist_free (dependents);

	return undo_info;
}

/*******************************************************************/

inline static void
invalidate_refs (Dependent *dep, GnmExprRewriteInfo const *rwinfo)
{
	GnmExpr const *newtree = gnm_expr_rewrite (dep->expression, rwinfo);

	/* We are told this dependent depends on this region, hence if newtree
	 * is null then either
	 * 1) we did not depend on it ( ie. serious breakage )
	 * 2j we had a duplicate reference and we have already removed it.
	 * 3) We depended on things via a name which will be invalidated elsewhere
	 */
	if (newtree != NULL) {
		dependent_set_expr (dep, newtree);
		gnm_expr_unref (newtree);
	}
}

/*
 * WARNING : Hash is pointing to freed memory once this is complete
 * This is tightly coupled with do_deps_destroy.
 */
static void
cb_dep_hash_invalidate (__attribute__((unused)) gpointer key, gpointer value,
			gpointer closure)
{
	GnmExprRewriteInfo const *rwinfo = closure;
	DependencyAny *depany = value;
#ifndef ENABLE_MICRO_HASH
	GSList *deps = depany->deps;
	GSList *ptr = deps;
	Dependent *dependent;

	depany->deps = NULL;	/* poison it */
	if (rwinfo->type == GNM_EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else if (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet->workbook != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else {
		g_assert_not_reached ();
	}

	g_slist_free (deps);
	g_free (depany);
#else
	if (rwinfo->type == GNM_EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		dep_collection_foreach_dep (depany->deps, dep,
			if (dep->sheet != target)
				invalidate_refs (dep, rwinfo););
	} else if (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		dep_collection_foreach_dep (depany->deps, dep,
			if (dep->sheet->workbook != target)
				invalidate_refs (dep, rwinfo););
	} else {
		g_assert_not_reached ();
	}

	micro_hash_release (&depany->deps);
	/*
	 * Don't free -- we junk the pools later (and we don't know which one
	 * to use right here).
	 */
#endif
}

static void
cb_name_invalidate (GnmNamedExpr *nexpr, __attribute__((unused)) gpointer value,
		    GnmExprRewriteInfo const *rwinfo)
{
	GnmExpr const *new_expr = NULL;

	if (((rwinfo->type == GNM_EXPR_REWRITE_SHEET &&
	     rwinfo->u.sheet != nexpr->pos.sheet) ||
	    (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK &&
	     rwinfo->u.workbook != nexpr->pos.wb))) {
		new_expr = gnm_expr_rewrite (nexpr->expr_tree, rwinfo);
		g_return_if_fail (new_expr != NULL);
	}

	g_return_if_fail (nexpr->dependents == NULL ||
			  g_hash_table_size (nexpr->dependents) == 0);
	expr_name_set_expr (nexpr, new_expr);
}

static void
cb_collect_deps_of_name (Dependent *dep, __attribute__((unused)) gpointer value,
			 GSList **accum)
{
	/* grab unflagged linked depends */
	if ((dep->flags & (DEPENDENT_FLAGGED|DEPENDENT_IS_LINKED)) == DEPENDENT_IS_LINKED) {
		dep->flags |= DEPENDENT_FLAGGED;
		*accum = g_slist_prepend (*accum, dep);
	}
}

static void
cb_collect_deps_of_names (GnmNamedExpr *nexpr,
			  __attribute__((unused)) gpointer value,
			  GSList **accum)
{
	if (nexpr->dependents)
		g_hash_table_foreach (nexpr->dependents,
			(GHFunc)cb_collect_deps_of_name, (gpointer)accum);
}

/*
 * do_deps_destroy :
 * Invalidate references of all kinds to the target region described by
 * @rwinfo.
 */
static void
do_deps_destroy (Sheet *sheet, GnmExprRewriteInfo const *rwinfo)
{
	DependentFlags filter = DEPENDENT_LINK_FLAGS; /* unlink everything */
	GnmDepContainer *deps;

	g_return_if_fail (IS_SHEET (sheet));

	/* The GnmDepContainer contains the names that reference this, not the
	 * names it contains.  Remove them here. NOTE : they may continue to exist
	 * inactively for a bit.  Be careful to remove them _before_ destroying
	 * the deps.  This is a bit wasteful in that we unlink and relink a few
	 * things that are going to be deleted.  However, it is necessary to
	 * catch all the different life cycles
	 */
	gnm_named_expr_collection_free (&sheet->names);

	deps = sheet->deps;
	if (deps == NULL)
		return;

	/* Destroy the records of what depends on this sheet.  There is no need
	 * to delicately remove individual items from the lists.  The only
	 * purpose that serves is to validate the state of our data structures.
	 * If required this optimization can be disabled for debugging.
	 */
	sheet->deps = NULL;

	if (deps->range_hash) {
		int i;
		for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
			GHashTable *hash = deps->range_hash[i];
			if (hash != NULL) {
				g_hash_table_foreach (hash,
					&cb_dep_hash_invalidate, (gpointer)rwinfo);
				g_hash_table_destroy (hash);
			}
		}
		g_free (deps->range_hash);
		deps->range_hash = NULL;
	}
	if (deps->range_pool) {
		gnm_mem_chunk_destroy (deps->range_pool, TRUE);
		deps->range_pool = NULL;
	}

	if (deps->single_hash) {
		g_hash_table_foreach (deps->single_hash,
			&cb_dep_hash_invalidate, (gpointer)rwinfo);
		g_hash_table_destroy (deps->single_hash);
		deps->single_hash = NULL;
	}
	if (deps->single_pool) {
		gnm_mem_chunk_destroy (deps->single_pool, TRUE);
		deps->single_pool = NULL;
	}

	if (deps->referencing_names) {
		GSList *ptr, *accum = NULL;
		Dependent *dep;
		GHashTable *names = deps->referencing_names;

		deps->referencing_names = NULL;

		/* collect the deps of the names */
		g_hash_table_foreach (names,
			(GHFunc)cb_collect_deps_of_names,
			(gpointer)&accum);

		for (ptr = accum ; ptr != NULL ; ptr = ptr->next) {
			dep = ptr->data;
			dep->flags &= ~DEPENDENT_FLAGGED;
			dependent_unlink (dep, NULL);
		}

		/* now that all of the dependents of these names are unlinked.
		 * change the references in the names to avoid this sheet */
		g_hash_table_foreach (names,
			(GHFunc)cb_name_invalidate, (gpointer)rwinfo);

		/* the relink things en-mass in case one of the deps outside
		 * this sheet used multiple names that referenced us */
		dependents_link (accum, rwinfo);

		g_hash_table_destroy (names);
	}

	if (deps->dynamic_deps) {
		g_hash_table_destroy (deps->dynamic_deps);
		deps->dynamic_deps = NULL;
	}

	/* TODO : when we support inter-app depends we'll need a new flag */
	/* TODO : Add an 'application quit flag' to ignore interbook too */
	if (sheet->deps == NULL) {
		filter = DEPENDENT_GOES_INTERBOOK | DEPENDENT_USES_NAME;
		if (rwinfo->type == GNM_EXPR_REWRITE_SHEET)
			filter |= DEPENDENT_GOES_INTERSHEET;
	}

	/* Now we remove any links from dependents in this sheet to
	 * to other containers.  If the entire workbook is going away
	 * just look for inter-book links. (see comment above)
	 */
	DEPENDENT_CONTAINER_FOREACH_DEPENDENT (deps, dep, {
		if (dep->flags & filter)
			unlink_expr_dep (dep, dependent_is_cell (dep)
				? &DEP_TO_CELL (dep)->pos : &dummy,
				dep->expression);
		dep->flags &= ~DEPENDENT_LINK_FLAGS;
	});

	g_free (deps);
}

void
sheet_deps_destroy (Sheet *sheet)
{
	GnmExprRewriteInfo rwinfo;

	g_return_if_fail (IS_SHEET (sheet));

	rwinfo.type = GNM_EXPR_REWRITE_SHEET;
	rwinfo.u.sheet = sheet;

	do_deps_destroy (sheet, &rwinfo);
}

void
workbook_deps_destroy (Workbook *wb)
{
	GnmExprRewriteInfo rwinfo;

	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (wb->sheets != NULL);

	rwinfo.type = GNM_EXPR_REWRITE_WORKBOOK;
	rwinfo.u.workbook = wb;

	if (wb->sheet_order_dependents != NULL) {
		g_hash_table_destroy (wb->sheet_order_dependents);
		wb->sheet_order_dependents = NULL;
	}

	gnm_named_expr_collection_free (&wb->names);
	WORKBOOK_FOREACH_SHEET (wb, sheet, do_deps_destroy (sheet, &rwinfo););
}

void
workbook_queue_all_recalc (Workbook *wb)
{
	/* FIXME : warning what about dependents in other workbooks */
	WORKBOOK_FOREACH_DEPENDENT (wb, dep, dependent_flag_recalc (dep););
}

/**
 * workbook_recalc :
 * @wb :
 *
 * Computes all dependents in @wb that have been flags as requiring
 * recomputation.
 *
 * NOTE! This does not recalc dependents in other workbooks.
 */
void
workbook_recalc (Workbook *wb)
{
	gboolean redraw = FALSE;

	g_return_if_fail (IS_WORKBOOK (wb));

	WORKBOOK_FOREACH_DEPENDENT (wb, dep, redraw |= dependent_eval (dep););
	if (redraw) {
		WORKBOOK_FOREACH_SHEET (wb, sheet, sheet_redraw_all (sheet, FALSE););
	}
}

/**
 * workbook_recalc_all :
 * @wb :
 *
 * Queues all dependents for recalc and marks them all as dirty.
 */
void
workbook_recalc_all (Workbook *wb)
{
	workbook_queue_all_recalc (wb);
	workbook_recalc (wb);
	WORKBOOK_FOREACH_VIEW (wb, view,
		sheet_update (wb_view_cur_sheet (view)););
}

GnmDepContainer *
gnm_dep_container_new (void)
{
	GnmDepContainer *deps = g_new (GnmDepContainer, 1);

	deps->dependent_list = NULL;

	deps->range_hash  = g_new0 (GHashTable *,
				    (SHEET_MAX_ROWS-1)/BUCKET_SIZE + 1);
	deps->range_pool  = gnm_mem_chunk_new ("range pool",
					       sizeof (DependencyRange),
					       16 * 1024 - 100);
	deps->single_hash = g_hash_table_new ((GHashFunc) depsingle_hash,
					      (GEqualFunc) depsingle_equal);
	deps->single_pool = gnm_mem_chunk_new ("single pool",
					       sizeof (DependencySingle),
					       16 * 1024 - 100);
	deps->referencing_names = g_hash_table_new (g_direct_hash,
						    g_direct_equal);

	deps->dynamic_deps = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) dynamic_dep_free);

	return deps;
}

/****************************************************************************
 * Debug utils
 */
static void
dump_dependent_list (GSList *l)
{
	printf ("(");
	while (l != NULL) {
		Dependent *dep = l->data;
		dependent_debug_name (dep, stdout);
		l = l->next;
		if (l != NULL)
			printf (", ");
	}
	printf (")\n");
}

static void
dump_range_dep (gpointer key, __attribute__((unused)) gpointer value,
		__attribute__((unused)) gpointer closure)
{
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

	/* 2 calls to col_name and row_name.  It uses a static buffer */
	printf ("\t%s:", cellpos_as_string (&range->start));
	printf ("%s <- ", cellpos_as_string (&range->end));

	dep_collection_foreach_list (deprange->deps, list,
		dump_dependent_list (list););
}

static void
dump_single_dep (gpointer key, __attribute__((unused)) gpointer value,
		 __attribute__((unused)) gpointer closure)
{
	DependencySingle *depsingle = key;

	printf ("\t%s <- ", cellpos_as_string (&depsingle->pos));

	dep_collection_foreach_list (depsingle->deps, list,
		dump_dependent_list (list););
}

/**
 * gnm_dep_container_dump :
 * @deps :
 *
 * A useful utility for checking the state of the dependency data structures.
 */
void
gnm_dep_container_dump (GnmDepContainer const *deps)
{
	int i;

	g_return_if_fail (deps != NULL);

	for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL && g_hash_table_size (hash) > 0) {
			printf ("Bucket %d (%d-%d): Range hash size %d: range over which cells in list depend\n",
				i, i * BUCKET_SIZE, (i + 1) * BUCKET_SIZE - 1,
				g_hash_table_size (hash));
			g_hash_table_foreach (hash,
					      dump_range_dep, NULL);
		}
	}

	if (g_hash_table_size (deps->single_hash) > 0) {
		printf ("Single hash size %d: cell on which list of cells depend\n",
			g_hash_table_size (deps->single_hash));
		g_hash_table_foreach (deps->single_hash,
				      dump_single_dep, NULL);
	}
}

/**
 * dependent_debug_name :
 * @dep : The dependent we are interested in.
 * @file : FILE * to print to.
 *
 * A useful little debugging utility.
 */
void
dependent_debug_name (Dependent const *dep, FILE *out)
{
	int t;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (out != NULL);
	g_return_if_fail (dep_classes);

	if (dep->sheet != NULL)
		fprintf (out, "%s!", dep->sheet->name_quoted);
	else
		g_warning ("Invalid dep, missing sheet");

	t = dependent_type (dep);
	if (t != DEPENDENT_CELL) {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		(*klass->debug_name) (dep, out);
	} else
		fprintf (out, "%s", cell_name (DEP_TO_CELL (dep)));
}

