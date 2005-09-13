/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dependent.c:  Manage calculation dependencies between objects
 *
 * Copyright (C) 2000-2005
 *  Jody Goldberg   (jody@gnome.org)
 *  Morten Welinder (terra@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "dependent.h"

#include "workbook-priv.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "str.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "workbook-view.h"
#include "rendered-value.h" /* FIXME : should not be needed with JIT-R */
#include "ranges.h"
#include "gutils.h"

#include <string.h>
#include <goffice/utils/go-glib-extras.h>

#define BUCKET_SIZE	128
#undef DEBUG_EVALUATION

static void dynamic_dep_eval 	   (GnmDependent *dep);
static void dynamic_dep_debug_name (GnmDependent const *dep, GString *target);
static void name_dep_eval 	   (GnmDependent *dep);
static void name_dep_debug_name	   (GnmDependent const *dep, GString *target);

static GnmCellPos const dummy = { 0, 0 };
static GPtrArray *dep_classes = NULL;
static DependentClass dynamic_dep_class = {
	dynamic_dep_eval,
	NULL,
	dynamic_dep_debug_name,
};
static DependentClass name_dep_class = {
	name_dep_eval,
	NULL,
	name_dep_debug_name,
};
typedef struct {
	GnmDependent  base;
	GnmDependent *container;
	GSList    *ranges;
	GSList    *singles;
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
	g_ptr_array_add	(dep_classes, &name_dep_class);
}

void
dependent_types_shutdown (void)
{
	g_return_if_fail (dep_classes != NULL);
	g_ptr_array_free (dep_classes, TRUE);
	dep_classes = NULL;
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
 * Queues a recalc.
 */
static void
dependent_changed (GnmDependent *dep)
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
 * When the expression associated with @dep needs to change this routine
 * dispatches to the virtual handler, unlinking @dep if necessary beforehand.
 * Adds a ref to @new_expr.
 *
 * NOTE : it does NOT relink dependents in case they are going to move later.
 **/
void
dependent_set_expr (GnmDependent *dep, GnmExpr const *new_expr)
{
	int const t = dependent_type (dep);

	if (dependent_is_linked (dep))
		dependent_unlink (dep);

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

static inline GnmCellPos const *
dependent_pos (GnmDependent const *dep)
{
	return dependent_is_cell (dep) ? &DEP_TO_CELL (dep)->pos : &dummy;
}


/**
 * dependent_set_sheet
 * @dep :
 * @sheet :
 */
void
dependent_set_sheet (GnmDependent *dep, Sheet *sheet)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet == NULL);
	g_return_if_fail (!dependent_is_linked (dep));

	dep->sheet = sheet;
	if (dep->expression != NULL) {
		dependent_link (dep);
		dependent_changed (dep);
	}
}

static void
cb_cell_list_deps (GnmDependent *dep, gpointer user)
{
	GSList **list = (GSList **)user;
	*list = g_slist_prepend (*list, dep);
}

static GSList *
cell_list_deps (GnmCell const *cell)
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
		GnmDependent *dep = list->data;
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
		GnmDependent *dep = work->data;
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
				GnmDependent *dep = list->data;
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
			if (!dependent_needs_recalc (dyn->container)) {
				dependent_flag_recalc (dyn->container);
				work = g_slist_prepend (work, dyn->container);
			}
		}
	}
}

void
dependent_queue_recalc (GnmDependent *dep)
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
	gint new_num_buckets;

	if (hash_table->num_elements <= 1)
		new_num_buckets = 1;
	else {
		new_num_buckets = g_spaced_primes_closest (hash_table->num_elements);
		if (new_num_buckets < MICRO_HASH_MIN_SIZE)
			new_num_buckets = MICRO_HASH_MIN_SIZE;
		else if (new_num_buckets > MICRO_HASH_MAX_SIZE)
			new_num_buckets = MICRO_HASH_MAX_SIZE;
	}

	if (old_num_buckets <= 1) {
		if (new_num_buckets == 1)
			return;
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (node = hash_table->u.singleton; node; node = next) {
			next = node->next;
			bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
			node->next = new_buckets[bucket];
			new_buckets[bucket] = node;
		}
		hash_table->u.buckets = new_buckets;
	} else if (new_num_buckets > 1) {
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (old_num_buckets = hash_table->num_buckets; old_num_buckets-- > 0 ; )
			for (node = hash_table->u.buckets[old_num_buckets]; node; node = next) {
				next = node->next;
				bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
				node->next = new_buckets[bucket];
				new_buckets[bucket] = node;
			}
		g_free (hash_table->u.buckets);
		hash_table->u.buckets = new_buckets;
	} else {
		GSList *singleton = NULL;
		while (old_num_buckets-- > 0)
			singleton = g_slist_concat (hash_table->u.buckets[old_num_buckets], singleton);
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

static inline gboolean
micro_hash_is_empty (MicroHash const *hash_table)
{
	return hash_table->num_elements == 0;
}

/*************************************************************************/

#define micro_hash_foreach_dep(dc, dep, code) do {		\
	GSList *l;						\
	int i = dc.num_buckets;					\
	if (i <= 1) { 						\
		for (l = dc.u.singleton; l ; l = l->next) {	\
			GnmDependent *dep = l->data;		\
			code					\
		}						\
	} else while (i-- > 0) {				\
		for (l = dc.u.buckets[i]; l ; l = l->next) {	\
			GnmDependent *dep = l->data;		\
			code					\
		}						\
	}							\
} while (0)
#define micro_hash_foreach_list(dc, list, code) do {		\
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
	MicroHash	deps;	/* Must be first */
	GnmRange  range;
} DependencyRange;

/*
 *  A DependencySingle stores a list of dependents that rely
 * on the cell at @pos.
 *
 * A change in this cell will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	MicroHash	deps;	/* Must be first */
	GnmCellPos pos;
} DependencySingle;

/* A utility type */
typedef struct {
	MicroHash	deps;	/* Must be first */
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
link_single_dep (GnmDependent *dep, GnmCellPos const *pos, GnmCellRef const *ref)
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
		single = go_mem_chunk_alloc (deps->single_pool);
		*single = lookup;
		micro_hash_init (&single->deps, dep);
		g_hash_table_insert (deps->single_hash, single, single);
	} else
		micro_hash_insert (&single->deps, dep);

	return flag;
}

static void
unlink_single_dep (GnmDependent *dep, GnmCellPos const *pos, GnmCellRef const *a)
{
	DependencySingle lookup;
	DependencySingle *single;
	GnmDepContainer *deps = eval_sheet (a->sheet, dep->sheet)->deps;

	if (!deps)
		return;

	cellref_get_abs_pos (a, pos, &lookup.pos);
	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL) {
		micro_hash_remove (&single->deps, dep);
		if (micro_hash_is_empty (&single->deps)) {
			g_hash_table_remove (deps->single_hash, single);
			micro_hash_release (&single->deps);
			go_mem_chunk_free (deps->single_pool, single);
		}
	}
}

static void
link_range_dep (GnmDepContainer *deps, GnmDependent *dep,
		DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;

	for ( ; i <= end; i++) {
		/* Look it up */
		DependencyRange *result;

		if (deps->range_hash[i] == NULL)
			deps->range_hash[i] = g_hash_table_new (
				(GHashFunc)  deprange_hash,
				(GEqualFunc) deprange_equal);
		else {
			result = g_hash_table_lookup (deps->range_hash[i], r);
			if (result) {
				/* Inserts if it is not already there */
				micro_hash_insert (&result->deps, dep);
				continue;
			}
		}

		/* Create a new DependencyRange structure */
		result = go_mem_chunk_alloc (deps->range_pool);
		*result = *r;
		micro_hash_init (&result->deps, dep);
		g_hash_table_insert (deps->range_hash[i], result, result);
	}
}

static void
unlink_range_dep (GnmDepContainer *deps, GnmDependent *dep,
		  DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;

	if (!deps)
		return;

	for ( ; i <= end; i++) {
		DependencyRange *result =
			g_hash_table_lookup (deps->range_hash[i], r);
		if (result) {
			micro_hash_remove (&result->deps, dep);
			if (micro_hash_is_empty (&result->deps)) {
				g_hash_table_remove (deps->range_hash[i], result);
				micro_hash_release (&result->deps);
				go_mem_chunk_free (deps->range_pool, result);
			}
		}
	}
}

static DependentFlags
link_cellrange_dep (GnmDependent *dep, GnmCellPos const *pos,
		    GnmCellRef const *a, GnmCellRef const *b)
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
			if (i > stop) { int tmp = i; i = stop ; stop = tmp; }

			g_return_val_if_fail (b->sheet->workbook == wb, flag);

			while (i <= stop) {
				Sheet *sheet = g_ptr_array_index (wb->sheets, i);
				i++;
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
unlink_cellrange_dep (GnmDependent *dep, GnmCellPos const *pos,
		      GnmCellRef const *a, GnmCellRef const *b)
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
			if (i > stop) { int tmp = i; i = stop ; stop = tmp; }

			g_return_if_fail (b->sheet->workbook == wb);

			while (i <= stop) {
				Sheet *sheet = g_ptr_array_index (wb->sheets, i);
				i++;
				unlink_range_dep (sheet->deps, dep, &range);
			}
		} else
			unlink_range_dep (a->sheet->deps, dep, &range);
	} else
		unlink_range_dep (dep->sheet->deps, dep, &range);
}

static DependentFlags
link_expr_dep (GnmDependent *dep, GnmExpr const *tree)
{
	g_return_val_if_fail (tree != NULL, DEPENDENT_NO_FLAG);

	switch (tree->any.oper) {
	case GNM_EXPR_OP_ANY_BINARY:
		return  link_expr_dep (dep, tree->binary.value_a) |
			link_expr_dep (dep, tree->binary.value_b);
	case GNM_EXPR_OP_ANY_UNARY:
		return link_expr_dep (dep, tree->unary.value);
	case GNM_EXPR_OP_CELLREF:
		return link_single_dep (dep, dependent_pos (dep), &tree->cellref.ref);

	case GNM_EXPR_OP_CONSTANT:
		/* TODO: pass in eval flags so that we can use implicit
		 * intersection
		 */
		if (VALUE_CELLRANGE == tree->constant.value->type)
			return link_cellrange_dep
				(dep,
				 dependent_pos (dep),
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
			GnmEvalPos		 ep;
			FunctionEvalInfo fei;
			fei.pos = eval_pos_init_dep (&ep, dep);
			fei.func_call = &tree->func;
			flag = tree->func.func->linker (&fei);
		}
		for (l = tree->func.arg_list; l; l = l->next)
			flag |= link_expr_dep (dep, l->data);
		return flag;
	}

	case GNM_EXPR_OP_NAME:
		expr_name_add_dep (tree->name.name, dep);
		if (tree->name.name->active)
			return link_expr_dep (dep, tree->name.name->expr) | DEPENDENT_USES_NAME;
		return DEPENDENT_USES_NAME;

	case GNM_EXPR_OP_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			GnmCellRef a;
			GnmCellPos const *pos = dependent_pos (dep);
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
			return link_expr_dep (dep, tree->array.corner.expr);

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;
		DependentFlags res = DEPENDENT_NO_FLAG;

		for (l = tree->set.set; l; l = l->next)
			res |= link_expr_dep (dep, l->data);
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
unlink_expr_dep (GnmDependent *dep, GnmExpr const *tree)
{
	switch (tree->any.oper) {
	case GNM_EXPR_OP_ANY_BINARY:
		unlink_expr_dep (dep, tree->binary.value_a);
		unlink_expr_dep (dep, tree->binary.value_b);
		return;

	case GNM_EXPR_OP_ANY_UNARY:
		unlink_expr_dep (dep, tree->unary.value);
		return;
	case GNM_EXPR_OP_CELLREF:
		unlink_single_dep (dep, dependent_pos (dep), &tree->cellref.ref);
		return;

	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_CELLRANGE == tree->constant.value->type)
			unlink_cellrange_dep (dep, dependent_pos (dep),
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
			GnmEvalPos ep;
			FunctionEvalInfo fei;
			fei.pos = eval_pos_init_dep (&ep, dep);
			fei.func_call = &tree->func;
			tree->func.func->unlinker (&fei);
		}
		for (l = tree->func.arg_list; l; l = l->next)
			unlink_expr_dep (dep, l->data);
		return;
	}

	case GNM_EXPR_OP_NAME:
		expr_name_remove_dep (tree->name.name, dep);
		if (tree->name.name->active)
			unlink_expr_dep (dep, tree->name.name->expr);
		return;

	case GNM_EXPR_OP_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			GnmCellRef a;
			GnmCellPos const *pos = dependent_pos (dep);

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
			unlink_expr_dep (dep, tree->array.corner.expr);
		return;

	case GNM_EXPR_OP_SET: {
		GnmExprList *l;

		for (l = tree->set.set; l; l = l->next)
			unlink_expr_dep (dep, l->data);
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
workbook_link_3d_dep (GnmDependent *dep)
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
workbook_unlink_3d_dep (GnmDependent *dep)
{
	Workbook *wb = dep->sheet->workbook;

	/* during destruction */
	if (wb->sheet_order_dependents == NULL)
		return;

	if (wb->being_reordered)
		return;

	g_hash_table_remove (wb->sheet_order_dependents, dep);
}

/*****************************************************************************/

static void
name_dep_eval (G_GNUC_UNUSED GnmDependent *dep)
{
}

static void
name_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Name%p", dep);
}

static void
dynamic_dep_eval (G_GNUC_UNUSED GnmDependent *dep)
{
}

static void
dynamic_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "DynamicDep%p", dep);
}

void
dependent_add_dynamic_dep (GnmDependent *dep, GnmValueRange const *v)
{
	DependentFlags   flags;
	DynamicDep	*dyn;
	GnmCellPos const	*pos;
	DependencyRange  range;

	g_return_if_fail (dep != NULL);

	pos = dependent_pos (dep);

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
		dyn->singles		= NULL;
		g_hash_table_insert (dep->sheet->deps->dynamic_deps, dep, dyn);
	}

	cellref_get_abs_pos (&v->cell.a, pos, &range.range.start);
	cellref_get_abs_pos (&v->cell.b, pos, &range.range.end);
	if (range_is_singleton (&range.range)) {
		flags = link_single_dep (&dyn->base, pos, &v->cell.a);
		dyn->singles = g_slist_prepend (dyn->singles, value_dup ((GnmValue *)v));
	} else {
		flags = link_cellrange_dep (&dyn->base, pos, &v->cell.a, &v->cell.b);
		dyn->ranges = g_slist_prepend (dyn->ranges, value_dup ((GnmValue *)v));
	}
	if (flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

static inline void
dependent_clear_dynamic_deps (GnmDependent *dep)
{
	g_hash_table_remove (dep->sheet->deps->dynamic_deps, dep);
}

/*****************************************************************************/

/**
 * dependent_link:
 * @dep : the dependent that changed
 *
 * Adds the dependent to the workbook wide list of dependents.
 */
void
dependent_link (GnmDependent *dep)
{
	Sheet *sheet;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (!(dep->flags & DEPENDENT_IS_LINKED));
	g_return_if_fail (IS_SHEET (dep->sheet));
	g_return_if_fail (dep->sheet->deps != NULL);

	sheet = dep->sheet;

	/* Make this the new tail of the dependent list.  */
	dep->prev_dep = sheet->deps->tail;
	dep->next_dep = NULL;
	if (dep->prev_dep)
		dep->prev_dep->next_dep = dep;
	else
		sheet->deps->head = dep; /* first element */
	sheet->deps->tail = dep;
	dep->flags |=
		DEPENDENT_IS_LINKED |
		link_expr_dep (dep, dep->expression);

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

/**
 * dependent_unlink:
 * @dep : the dependent that changed
 *
 * Removes the dependent from its container's set of dependents and always
 * removes the linkages to what it depends on.
 */
void
dependent_unlink (GnmDependent *dep)
{
	GnmDepContainer *contain;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dependent_is_linked (dep));
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (IS_SHEET (dep->sheet));

	unlink_expr_dep (dep, dep->expression);
	contain = dep->sheet->deps;
	if (contain != NULL) {
		if (contain->head == dep)
			contain->head = dep->next_dep;
		if (contain->tail == dep)
			contain->tail = dep->prev_dep;
		if (dep->next_dep)
			dep->next_dep->prev_dep = dep->prev_dep;
		if (dep->prev_dep)
			dep->prev_dep->next_dep = dep->next_dep;

		if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
			dependent_clear_dynamic_deps (dep);
	}

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (dep);
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
cell_eval_content (GnmCell *cell)
{
	static GnmCell *iterating = NULL;
	GnmValue   *v;
	GnmEvalPos	 pos;
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
		GnmParsePos pp;
		char *str = gnm_expr_as_string (cell->base.expression,
			parse_pos_init_cell (&pp, cell), gnm_expr_conventions_default);
		g_print ("{\nEvaluating %s: %s;\n", cell_name (cell), str);
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
			g_print ("}; /* already-iterate (%d) */\n", iterating == NULL);
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
		g_print ("Evaluation(%d) %s := %s\n", max_iteration, cell_name (cell), valtxt);
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
	g_print ("} (%d)\n", iterating == NULL);
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
dependent_eval (GnmDependent *dep)
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
			 * to explain asymmetry.
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
cell_queue_recalc (GnmCell *cell)
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
cb_search_rangedeps (gpointer key, G_GNUC_UNUSED gpointer value,
		     gpointer closure)
{
	search_rangedeps_closure_t const *c = closure;
	DependencyRange const *deprange = key;
	GnmRange const *range = &(deprange->range);

#if 0
	/* When things get slow this is a good test to enable */
	static int counter = 0;
	if ((++counter % 100000) == 0)
		g_print ("%d\n", counter / 100000);
#endif

	if (range_contains (range, c->col, c->row)) {
		DepFunc	 func = c->func;
		micro_hash_foreach_dep (deprange->deps, dep,
			(func) (dep, c->user););
	}
}

static void
cell_foreach_range_dep (GnmCell const *cell, DepFunc func, gpointer user)
{
	search_rangedeps_closure_t closure;
	GHashTable *bucket =
		cell->base.sheet->deps->range_hash[cell->pos.row / BUCKET_SIZE];

	if (bucket != NULL) {
		closure.col = cell->pos.col;
		closure.row = cell->pos.row;
		closure.func = func;
		closure.user = user;
		g_hash_table_foreach (bucket, &cb_search_rangedeps, &closure);
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
		micro_hash_foreach_dep (single->deps, dep,
			(*func) (dep, user););
}

void
cell_foreach_dep (GnmCell const *cell, DepFunc func, gpointer user)
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
cb_recalc_all_depends (gpointer key, G_GNUC_UNUSED gpointer value,
		       G_GNUC_UNUSED gpointer ignore)
{
	DependencyAny const *depany = key;
	micro_hash_foreach_list (depany->deps, list,
		dependent_queue_recalc_list (list););
}

static void
cb_range_contained_depend (gpointer key, G_GNUC_UNUSED gpointer value,
			   gpointer user)
{
	DependencyRange const *deprange  = key;
	GnmRange const *range = &deprange->range;
	GnmRange const *target = user;

	if (range_overlap (target, range))
		micro_hash_foreach_list (deprange->deps, list,
			dependent_queue_recalc_list (list););
}

static void
cb_single_contained_depend (gpointer key,
			    G_GNUC_UNUSED gpointer value,
			    gpointer user)
{
	DependencySingle const *depsingle  = key;
	GnmRange const *target = user;

	if (range_contains (target, depsingle->pos.col, depsingle->pos.row))
		micro_hash_foreach_list (depsingle->deps, list,
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
sheet_region_queue_recalc (Sheet const *sheet, GnmRange const *r)
{
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	if (r == NULL) {
		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep,
			dependent_flag_recalc (dep););

		/* look for things that depend on the sheet */
		for (i = (SHEET_MAX_ROWS - 1) / BUCKET_SIZE; i >= 0 ; i--) {
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
			GnmCell *cell = DEP_TO_CELL (dep);
			if (dependent_is_cell (dep) &&
			    range_contains (r, cell->pos.col, cell->pos.row))
				dependent_flag_recalc (dep);
		});

		/* look for things that depend on target region */
		for (i = r->end.row / BUCKET_SIZE; i >= first ; i--) {
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
		GnmEvalPos    pos;
		GnmDependent *dep;
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
		ExprRelocateStorage *tmp = ptr->data;
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
		ExprRelocateStorage *tmp = ptr->data;

		if (tmp->dep_type == DEPENDENT_CELL) {
			if (!IS_SHEET (tmp->u.pos.sheet)) {
				/* FIXME : happens when undoing a move from a deleted
				 * sheet.  Do we want to do something here */
			} else {
				GnmCell *cell = sheet_cell_get
					(tmp->u.pos.sheet,
					 tmp->u.pos.eval.col, tmp->u.pos.eval.row);

				/* It is possible to have a NULL if the relocation info
				 * is not really relevant.  eg when undoing a pasted
				 * cut that was relocated but also saved to a buffer */
				if (cell != NULL)
					sheet_cell_set_expr (cell, tmp->oldtree);
			}
		} else if (tmp->dep_type == DEPENDENT_NAME) {
		} else {
			dependent_set_expr (tmp->u.dep, tmp->oldtree);
			dependent_flag_recalc (tmp->u.dep);
			dependent_link (tmp->u.dep);
		}
		gnm_expr_unref (tmp->oldtree);
		g_free (tmp);
	}
	g_slist_free (info);
}

/**
 * dependents_link :
 * @deps : An slist of dependents.
 *
 * link a list of dependents.  (The list used to get freed, but is not
 * freed anymore.)
 */
void
dependents_link (GSList *deps)
{
	GSList *ptr = deps;

	/* put them back */
	for (; ptr != NULL ; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		if (dep->sheet->being_invalidated)
			continue;
		if (dep->sheet->deps != NULL && !dependent_is_linked (dep)) {
			dependent_link (dep);
			dependent_queue_recalc (dep);
		}
	}
}

typedef struct {
	GnmRange const *target;
	GSList *list;
} CollectClosure;

static void
cb_range_contained_collect (DependencyRange const *deprange,
			    G_GNUC_UNUSED gpointer ignored,
			    CollectClosure *user)
{
	GnmRange const *range = &deprange->range;

	if (range_overlap (user->target, range))
		micro_hash_foreach_dep (deprange->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE))) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

static void
cb_single_contained_collect (DependencySingle const *depsingle,
			     G_GNUC_UNUSED gpointer ignored,
			     CollectClosure *user)
{
	if (range_contains (user->target, depsingle->pos.col, depsingle->pos.row))
		micro_hash_foreach_dep (depsingle->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE))) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

/**
 * dependents_relocate:
 * @info : the descriptor record for what is being moved where.
 *
 * Fixes references to or from a region that is going to be moved.
 * Returns a list of the locations and expressions that were changed outside of
 * the region.
 *
 * NOTE : Does not queue the changed elemenents or their recursive dependents
 * 	for recalc
 **/
GSList *
dependents_relocate (GnmExprRelocateInfo const *info)
{
	GnmExprRewriteInfo rwinfo;
	GnmDependent *dep;
	GSList    *l, *dependents = NULL, *undo_info = NULL;
	Sheet	  *sheet;
	GnmRange const   *r;
	GnmExpr const *newtree;
	int i;
	CollectClosure collect;
	GHashTable *names;

	g_return_val_if_fail (info != NULL, NULL);

	/* short circuit if nothing would move */
	if (info->col_offset == 0 && info->row_offset == 0 &&
	    info->origin_sheet == info->target_sheet)
		return NULL;

	sheet = info->origin_sheet;
	r     = &info->origin;

	/* collect contained cells with expressions */
	SHEET_FOREACH_DEPENDENT (info->origin_sheet, dep, {
		GnmCell *cell = DEP_TO_CELL (dep);
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
		for (i = r->end.row / BUCKET_SIZE; i >= first ; i--) {
			hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					(GHFunc) &cb_range_contained_collect,
					(gpointer)&collect);
		}
	}
	dependents = collect.list;

	rwinfo.rw_type = GNM_EXPR_REWRITE_EXPR;
	memcpy (&rwinfo.u.relocate, info, sizeof (GnmExprRelocateInfo));

	for (l = dependents; l; l = l->next) {
		dep = l->data;
		dep->flags &= ~DEPENDENT_FLAGGED;
		sheet_flag_status_update_range (dep->sheet, NULL);

		eval_pos_init_dep (&rwinfo.u.relocate.pos, dep);

		/* it is possible nothing changed for contained deps
		 * using absolute references */
		newtree = gnm_expr_rewrite (dep->expression, &rwinfo);
		if (newtree != NULL) {
			int const t = dependent_type (dep);
			ExprRelocateStorage *tmp =
				g_new (ExprRelocateStorage, 1);

			tmp->dep_type = t;
			if (t == DEPENDENT_NAME) {
			} else {
				if (t == DEPENDENT_CELL)
					tmp->u.pos = rwinfo.u.relocate.pos;
				else
					tmp->u.dep = dep;
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
					GnmCellPos const *pos = &DEP_TO_CELL (dep)->pos;
					if (dep->sheet != sheet ||
					    !range_contains (r, pos->col, pos->row))
						dependent_link (dep);
				} else
					dependent_link (dep);
			}
		} else
			/* the expression may not be changing, but it depends
			 * on something that is */
			dependent_queue_recalc (dep);

		/* Not the most efficient, but probably not too bad.  It is
		 * definitely cheaper than finding the set of effected sheets. */
		sheet_flag_status_update_range (dep->sheet, NULL);
	}

	names = info->origin_sheet->deps->referencing_names;
	if (names != NULL) {
		rwinfo.rw_type = GNM_EXPR_REWRITE_NAME;
	}

	g_slist_free (dependents);

	return undo_info;
}

/*******************************************************************/

static gboolean
cb_collect_range (G_GNUC_UNUSED gpointer key,
		  DependencyAny *depany,
		  GSList **collector)
{
	*collector = g_slist_prepend (*collector, depany);
	return TRUE;
}

static void
dep_hash_destroy (GHashTable *hash, GSList **dyn_deps, Sheet *sheet, gboolean destroy)
{
	GSList *deps = NULL, *l;
	GnmExprRewriteInfo rwinfo;
	GSList *deplist = NULL;

	/* We collect first because we will be changing the hash.  */
	if (destroy) {
		g_hash_table_foreach_remove (hash,
					     (GHRFunc)cb_collect_range,
					     &deps);
		g_hash_table_destroy (hash);
	} else {
		g_hash_table_foreach (hash, (GHFunc)cb_collect_range, &deps);
	}

	for (l = deps; l; l = l->next) {
		DependencyAny *depany = l->data;

		micro_hash_foreach_dep (depany->deps, dep, {
			if (dependent_type (dep) == DEPENDENT_DYNAMIC_DEP) {
				GnmDependent *c = ((DynamicDep *)dep)->container;
				if (!c->sheet->being_invalidated)
					*dyn_deps =
						g_slist_prepend (*dyn_deps, c);
			} else if (!dep->sheet->being_invalidated) {
				/*
				 * We collect here instead of doing right away as
				 * the dependent_set_expr below can change the
				 * container we are looping over.
				 */
				deplist = g_slist_prepend (deplist, dep);
			}
		});

		if (destroy)
			micro_hash_release (&depany->deps);
	}
	g_slist_free (deps);

	/*
	 * We do this after the above loop as this loop will
	 * invalidate some of the DependencyAny pointers from
	 * above.  The testcase for that is 314207, deleting
	 * all but the first sheet in one go.
	 */
	rwinfo.rw_type = GNM_EXPR_REWRITE_INVALIDATE_SHEETS;
	for (l = deplist; l; l = l->next) {
		GnmDependent *dep = l->data;
		GnmExpr *e = (GnmExpr *)dep->expression;
		/* We are told this dependent depends on this region, hence if
		 * newtree is null then either
		 * 1) we did not depend on it (ie., serious breakage )
		 * 2) we had a duplicate reference and we have already removed it.
		 * 3) We depended on things via a name which will be
		 *    invalidated elsewhere */
		GnmExpr const *newtree = gnm_expr_rewrite (e, &rwinfo);
		if (newtree != NULL) {
			if (!destroy) {
				gnm_expr_ref (e);
				sheet->revive.dep_exprs =
					g_slist_prepend
					(g_slist_prepend (sheet->revive.dep_exprs, e),
					 dep);
			}
			dependent_set_expr (dep, newtree);
			gnm_expr_unref (newtree);
		}
	}
	g_slist_free (deplist);


}

static void
cb_collect_deps_of_name (GnmDependent *dep, G_GNUC_UNUSED gpointer value,
			 GSList **accum)
{
	/* grab unflagged linked depends */
	if ((dep->flags & (DEPENDENT_FLAGGED|DEPENDENT_IS_LINKED)) == DEPENDENT_IS_LINKED) {
		dep->flags |= DEPENDENT_FLAGGED;
		*accum = g_slist_prepend (*accum, dep);
	}
}

struct cb_collect_deps_of_names {
	GSList *names;
	GSList *deps;
};

static void
cb_collect_deps_of_names (GnmNamedExpr *nexpr,
			  G_GNUC_UNUSED gpointer value,
			  struct cb_collect_deps_of_names *accum)
{
	accum->names = g_slist_prepend (accum->names, nexpr);
	if (nexpr->dependents)
		g_hash_table_foreach (nexpr->dependents,
				      (GHFunc)cb_collect_deps_of_name,
				      &accum->deps);
}

static void
invalidate_name (GnmNamedExpr *nexpr, Sheet *sheet, gboolean destroy)
{
	GnmExpr *old_expr = (GnmExpr *)nexpr->expr;
	GnmExpr const *new_expr = NULL;
	gboolean scope_being_killed =
		nexpr->pos.sheet
		? nexpr->pos.sheet->being_invalidated
		: nexpr->pos.wb->during_destruction;

	if (!scope_being_killed) {
		GnmExprRewriteInfo rwinfo;
		rwinfo.rw_type = GNM_EXPR_REWRITE_INVALIDATE_SHEETS;
		new_expr = gnm_expr_rewrite (old_expr, &rwinfo);
		g_return_if_fail (new_expr != NULL);
	}

	if (nexpr->dependents && g_hash_table_size (nexpr->dependents))
		g_warning ("Left-over name dependencies:\n");

	if (!destroy) {
		gnm_expr_ref (old_expr);
		sheet->revive.name_exprs =
			g_slist_prepend (sheet->revive.name_exprs, old_expr);
		expr_name_ref (nexpr);
		sheet->revive.name_exprs =
			g_slist_prepend (sheet->revive.name_exprs, nexpr);
	}

	expr_name_set_expr (nexpr, new_expr);
}

static void
handle_dynamic_deps (GSList *dyn_deps)
{
	GSList *ptr;

	for (ptr = dyn_deps; ptr != NULL ; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS) {
			dependent_clear_dynamic_deps (dep);
			dep->flags &= ~DEPENDENT_HAS_DYNAMIC_DEPS;
		}
	}
	dependent_queue_recalc_list (dyn_deps);
	g_slist_free (dyn_deps);
}

static void
handle_referencing_names (GnmDepContainer *deps, Sheet *sheet, gboolean destroy)
{
	GSList *ptr;
	GHashTable *names = deps->referencing_names;
	struct cb_collect_deps_of_names accum;

	if (!names)
		return;

	if (destroy) {
		accum.deps = NULL;
		deps->referencing_names = NULL;
	} else
		accum.deps = sheet->revive.relink;

	accum.names = NULL;
	g_hash_table_foreach (names,
			      (GHFunc)cb_collect_deps_of_names,
			      (gpointer)&accum);

	for (ptr = accum.deps; ptr; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		dep->flags &= ~DEPENDENT_FLAGGED;
		dependent_unlink (dep);
	}

	/* now that all of the dependents of these names are unlinked.
	 * change the references in the names to avoid this sheet */
	for (ptr = accum.names; ptr; ptr = ptr->next) {
		GnmNamedExpr *nexpr = ptr->data;
		invalidate_name (nexpr, sheet, destroy);
	}
	g_slist_free (accum.names);

	/* then relink things en-mass in case one of the deps outside
	 * this sheet used multiple names that referenced us */
	dependents_link (accum.deps);

	if (destroy) {
		g_slist_free (accum.deps);
		g_hash_table_destroy (names);
	} else
		sheet->revive.relink = accum.deps;
}

static void
handle_outgoing_references (GnmDepContainer *deps, Sheet *sheet, gboolean destroy)
{
	DependentFlags what = DEPENDENT_USES_NAME;

	what |= sheet->workbook->during_destruction
		? DEPENDENT_GOES_INTERBOOK
		: DEPENDENT_GOES_INTERSHEET;
	DEPENDENT_CONTAINER_FOREACH_DEPENDENT (deps, dep, {
		if (dependent_is_linked (dep) && (dep->flags & what)) {
			dependent_unlink (dep);
			if (!destroy)
				sheet->revive.relink = g_slist_prepend (sheet->revive.relink, dep);
		}
	});
}

static void
clear_revive_info (Sheet *sheet)
{
	GSList *l;

	for (l = sheet->revive.name_exprs; l; l = l->next->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmExpr *expr = l->next->data;

		expr_name_unref (nexpr);
		gnm_expr_unref (expr);
	}
	g_slist_free (sheet->revive.name_exprs);
	sheet->revive.name_exprs = NULL;

	for (l = sheet->revive.dep_exprs; l; l = l->next->next) {
		GnmDependent *dep = l->data;
		GnmExpr *expr = l->next->data;
		(void)dep;
		gnm_expr_unref (expr);
	}
	g_slist_free (sheet->revive.dep_exprs);
	sheet->revive.dep_exprs = NULL;

	g_slist_free (sheet->revive.relink);
	sheet->revive.relink = NULL;
}

/*
 * do_deps_destroy:
 * Invalidate references of all kinds to the sheet.
 *
 * Also destroy the dependency container.
 */
static void
do_deps_destroy (Sheet *sheet)
{
	GnmDepContainer *deps;
	GSList *dyn_deps = NULL;
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->being_invalidated);

	/* The GnmDepContainer (i.e., sheet->deps) contains the names that
	 * reference this, not the names it contains.  Remove the locally
	 * defined names here.
	 *
	 * NOTE : they may continue to exist inactively for a bit.  Be
	 * careful to remove them _before_ destroying the deps.  This is
	 * a bit wasteful in that we unlink and relink a few things that
	 * are going to be deleted.  However, it is necessary to catch
	 * all the different life cycles.
	 */
	gnm_named_expr_collection_free (sheet->names);
	sheet->names = NULL;

	deps = sheet->deps;
	if (deps == NULL)
		return;

	/* Destroy the records of what depends on this sheet.  There is no need
	 * to delicately remove individual items from the lists.  The only
	 * purpose that serves is to validate the state of our data structures.
	 * If required this optimization can be disabled for debugging.
	 */
	sheet->deps = NULL;
	clear_revive_info (sheet);

	for (i = (SHEET_MAX_ROWS - 1) / BUCKET_SIZE; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL)
			dep_hash_destroy (hash, &dyn_deps, sheet, TRUE);
	}
	dep_hash_destroy (deps->single_hash, &dyn_deps, sheet, TRUE);

	g_free (deps->range_hash);
	deps->range_hash = NULL;
	go_mem_chunk_destroy (deps->range_pool, TRUE);
	deps->range_pool = NULL;

	deps->single_hash = NULL;
	go_mem_chunk_destroy (deps->single_pool, TRUE);
	deps->single_pool = NULL;

	/* Now that we have tossed all deps to this sheet we can queue the
	 * external dyn deps for recalc and free them */
	handle_dynamic_deps (dyn_deps);

	g_hash_table_destroy (deps->dynamic_deps);
	deps->dynamic_deps = NULL;

	handle_referencing_names (deps, sheet, TRUE);

	/* Now we remove any links from dependents in this sheet to
	 * to other containers.  If the entire workbook is going away
	 * just look for inter-book links.
	 */
	handle_outgoing_references (deps, sheet, TRUE);

	g_free (deps);
}

/*
 * do_deps_invalidate:
 * Invalidate references of all kinds to the sheet.
 */
static void
do_deps_invalidate (Sheet *sheet)
{
	GnmDepContainer *deps;
	GSList *dyn_deps = NULL;
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->being_invalidated);

	gnm_named_expr_collection_unlink (sheet->names);

	deps = sheet->deps;

	for (i = (SHEET_MAX_ROWS - 1) / BUCKET_SIZE; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL)
			dep_hash_destroy (hash, &dyn_deps, sheet, FALSE);
	}
	dep_hash_destroy (deps->single_hash, &dyn_deps, sheet, FALSE);

	/* Now that we have tossed all deps to this sheet we can queue the
	 * external dyn deps for recalc and free them */
	handle_dynamic_deps (dyn_deps);

	handle_referencing_names (deps, sheet, FALSE);

	/* Now we remove any links from dependents in this sheet to
	 * to other containers.  If the entire workbook is going away
	 * just look for inter-book links.
	 */
	handle_outgoing_references (deps, sheet, FALSE);
}

static void
cb_tweak_3d (GnmDependent *dep, G_GNUC_UNUSED gpointer value, GSList **deps)
{
	*deps = g_slist_prepend (*deps, dep);
}

static void
tweak_3d (Sheet *sheet, gboolean destroy)
{
	Workbook *wb = sheet->workbook;
	GSList *deps = NULL, *l;
	GnmExprRewriteInfo rwinfo;

	if (!wb->sheet_order_dependents)
		return;

	g_hash_table_foreach (wb->sheet_order_dependents,
			      (GHFunc)cb_tweak_3d,
			      &deps);

	rwinfo.rw_type = GNM_EXPR_REWRITE_INVALIDATE_SHEETS;
	for (l = deps; l; l = l->next) {
		GnmDependent *dep = l->data;
		GnmExpr *e = (GnmExpr *)dep->expression;
		GnmExpr const *newtree = gnm_expr_rewrite (e, &rwinfo);

		if (newtree != NULL) {
			if (!destroy) {
				gnm_expr_ref (e);
				sheet->revive.dep_exprs =
					g_slist_prepend
					(g_slist_prepend (sheet->revive.dep_exprs, e),
					 dep);
			}
			dependent_set_expr (dep, newtree);
			gnm_expr_unref (newtree);
			dependent_link (dep);
			dependent_changed (dep);
		}
	}
	g_slist_free (deps);
}


void
dependents_invalidate_sheet (Sheet *sheet, gboolean destroy)
{
	GSList l;

	g_return_if_fail (IS_SHEET (sheet));

	l.next = NULL;
	l.data = sheet;
	dependents_invalidate_sheets (&l, destroy);
}

void
dependents_invalidate_sheets (GSList *sheets, gboolean destroy)
{
	GSList *tmp;
	Workbook *last_wb;

	/* Mark all first.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		sheet->being_invalidated = TRUE;
	}

	/*
	 * Fixup 3d refs that start or end on one of these sheets.
	 * Ideally we do this one per workbook, but that is not critical
	 * so we are not going to outright sort the sheet list.
	 */
	last_wb = NULL;
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		Workbook *wb = sheet->workbook;

		if (wb != last_wb)
			tweak_3d (sheet, destroy);
		last_wb = wb;
	}

	/* Now invalidate.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		if (destroy)
			do_deps_destroy (sheet);
		else
			do_deps_invalidate (sheet);
	}

	/* Unmark.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		sheet->being_invalidated = FALSE;
	}
}

void
dependents_workbook_destroy (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (wb->during_destruction);
	g_return_if_fail (wb->sheets != NULL);

	/* Mark all first.  */
	WORKBOOK_FOREACH_SHEET (wb, sheet, sheet->being_invalidated = TRUE;);

	/* Kill 3d deps and workbook-level names, if needed.  */
	if (wb->sheet_order_dependents) {
		g_hash_table_destroy (wb->sheet_order_dependents);
		wb->sheet_order_dependents = NULL;
	}
	gnm_named_expr_collection_free (wb->names);
	wb->names = NULL;

	WORKBOOK_FOREACH_SHEET (wb, sheet, do_deps_destroy (sheet););

	/* Unmark.  */
	WORKBOOK_FOREACH_SHEET (wb, sheet, sheet->being_invalidated = FALSE;);
}

/*
 * dependents_revive_sheet:
 * Undo the effects of dependents_invalidate_sheet (sheet, FALSE).
 */
void
dependents_revive_sheet (Sheet *sheet)
{
	GSList *l;

	/* Restore the expressions of names that got changed.  */
	for (l = sheet->revive.name_exprs; l; l = l->next->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmExpr *expr = l->next->data;
		gnm_expr_ref (expr);
		expr_name_set_expr (nexpr, expr);
	}

	/* Restore the expressions of deps that got changed.  */
	for (l = sheet->revive.dep_exprs; l; l = l->next->next) {
		GnmDependent *dep = l->data;
		GnmExpr *expr = l->next->data;
		dependent_set_expr (dep, expr);
		dependent_link (dep);
		dependent_changed (dep);
	}

	dependents_link (sheet->revive.relink);

	/* Re-link local names.  */
	gnm_named_expr_collection_relink (sheet->names);

	clear_revive_info (sheet);

	gnm_dep_container_sanity_check (sheet->deps);
}

void
workbook_queue_all_recalc (Workbook *wb)
{
	/* FIXME: what about dependents in other workbooks */
	WORKBOOK_FOREACH_DEPENDENT (wb, dep, dependent_flag_recalc (dep););
}

/**
 * workbook_recalc :
 * @wb :
 *
 * Computes all dependents in @wb that have been flaged as requiring
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

static void
dynamic_dep_free (DynamicDep *dyn)
{
	GnmDependent *dep = dyn->container;
	GnmCellPos const *pos = dependent_pos (dep);
	GnmValueRange *v;
	GSList *ptr;

	for (ptr = dyn->singles ; ptr != NULL ; ptr = ptr->next) {
		v = ptr->data;
		unlink_single_dep (&dyn->base, pos, &v->cell.a);
		value_release ((GnmValue *)v);
	}
	g_slist_free (dyn->singles);
	dyn->singles = NULL;

	for (ptr = dyn->ranges ; ptr != NULL ; ptr = ptr->next) {
		v = ptr->data;
		unlink_cellrange_dep (&dyn->base, pos, &v->cell.a, &v->cell.b);
		value_release ((GnmValue *)v);
	}
	g_slist_free (dyn->ranges);
	dyn->ranges = NULL;

	if (dyn->base.flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (&dyn->base);
	g_free (dyn);
}

GnmDepContainer *
gnm_dep_container_new (void)
{
	GnmDepContainer *deps = g_new (GnmDepContainer, 1);

	deps->head = deps->tail = NULL;

	deps->range_hash  = g_new0 (GHashTable *,
				    (SHEET_MAX_ROWS - 1) / BUCKET_SIZE + 1);
	deps->range_pool  = go_mem_chunk_new ("range pool",
					       sizeof (DependencyRange),
					       16 * 1024 - 100);
	deps->single_hash = g_hash_table_new ((GHashFunc) depsingle_hash,
					      (GEqualFunc) depsingle_equal);
	deps->single_pool = go_mem_chunk_new ("single pool",
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
dump_dependent_list (GSList *l, GString *target)
{
	g_string_append_c (target, '(');
	while (l != NULL) {
		GnmDependent *dep = l->data;
		dependent_debug_name (dep, target);
		l = l->next;
		if (l != NULL)
			g_string_append (target, ", ");
	}
	g_string_append (target, ")");
}

static void
dump_range_dep (gpointer key, G_GNUC_UNUSED gpointer value,
		G_GNUC_UNUSED gpointer closure)
{
	DependencyRange const *deprange = key;
	GnmRange const *range = &(deprange->range);
	GString *target = g_string_new (NULL);

	g_string_append (target, "    ");
	g_string_append (target, range_name (range));
	g_string_append (target, " -> ");

	micro_hash_foreach_list (deprange->deps, list,
		dump_dependent_list (list, target););

	g_print ("%s\n", target->str);
	g_string_free (target, TRUE);
}

static void
dump_single_dep (gpointer key, G_GNUC_UNUSED gpointer value,
		 G_GNUC_UNUSED gpointer closure)
{
	DependencySingle *depsingle = key;
	GString *target = g_string_new (NULL);

	g_string_append (target, "    ");
	g_string_append (target, cellpos_as_string (&depsingle->pos));
	g_string_append (target, " -> ");

	micro_hash_foreach_list (depsingle->deps, list,
		dump_dependent_list (list, target););
	g_print ("%s\n", target->str);
	g_string_free (target, TRUE);
}

static void
dump_dynamic_dep (gpointer key, G_GNUC_UNUSED gpointer value,
		  G_GNUC_UNUSED gpointer closure)
{
	GnmDependent *dep = key;
	DynamicDep *dyn = value;
	GSList *l;
	GString *target = g_string_new (NULL);
	GnmParsePos pp;

	pp.eval = *dependent_pos (dyn->container);
	pp.sheet = dep->sheet;
	pp.wb = dep->sheet->workbook;

	g_string_append (target, "    ");
	dependent_debug_name (dep, target);
	g_string_append (target, " -> ");
	dependent_debug_name (&dyn->base, target);
	g_string_append (target, " { c=");
	dependent_debug_name (dyn->container, target);

	g_string_append (target, ", s=[");
	for (l = dyn->singles; l; l = l->next) {
		GnmValueRange const *v = l->data;
		rangeref_as_string (target, gnm_expr_conventions_default, &v->cell, &pp);
		if (l->next)
			g_string_append (target, ", ");
	}

	g_string_append (target, "], r=[");
	for (l = dyn->ranges; l; l = l->next) {
		GnmValueRange const *v = l->data;
		rangeref_as_string (target, gnm_expr_conventions_default, &v->cell, &pp);
		if (l->next)
			g_string_append (target, ", ");
	}

	g_string_append (target, "] }");
	g_print ("%s\n", target->str);
	g_string_free (target, TRUE);
}

static void
cb_dump_name_dep (gpointer key, G_GNUC_UNUSED gpointer value,
		  gpointer closure)
{
	GnmDependent *dep = key;
	GString *target = closure;

	if (target->str[target->len - 1] != '[')
		g_string_append (target, ", ");
	dependent_debug_name (dep, target);
}

static void
dump_name_dep (gpointer key, G_GNUC_UNUSED gpointer value,
	       G_GNUC_UNUSED gpointer closure)
{
	GnmNamedExpr *nexpr = key;
	GString *target = g_string_new (NULL);

	g_string_append (target, "    ");
	if (!nexpr->active) g_string_append_c (target, '(');
	g_string_append (target, nexpr->name->str);
	if (!nexpr->active) g_string_append_c (target, ')');
	g_string_append (target, " -> [");
	if (nexpr->dependents)
		g_hash_table_foreach (nexpr->dependents, cb_dump_name_dep, target);
	g_string_append (target, "]");

	g_print ("%s\n", target->str);
	g_string_free (target, TRUE);
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

	gnm_dep_container_sanity_check (deps);

	for (i = (SHEET_MAX_ROWS - 1) / BUCKET_SIZE; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL && g_hash_table_size (hash) > 0) {
			g_print ("  Bucket %d (%d-%d): Range hash size %d: range over which cells in list depend\n",
				 i, i * BUCKET_SIZE, (i + 1) * BUCKET_SIZE - 1,
				 g_hash_table_size (hash));
			g_hash_table_foreach (hash,
					      dump_range_dep, NULL);
		}
	}

	if (deps->single_hash && g_hash_table_size (deps->single_hash) > 0) {
		g_print ("  Single hash size %d: cell on which list of cells depend\n",
			 g_hash_table_size (deps->single_hash));
		g_hash_table_foreach (deps->single_hash,
				      dump_single_dep, NULL);
	}

	if (deps->dynamic_deps && g_hash_table_size (deps->dynamic_deps) > 0) {
		g_print ("  Dynamic hash size %d: cells that depend on dynamic dependencies\n",
			 g_hash_table_size (deps->dynamic_deps));
		g_hash_table_foreach (deps->dynamic_deps,
				      dump_dynamic_dep, NULL);
	}

	if (deps->referencing_names && g_hash_table_size (deps->referencing_names) > 0) {
		g_print ("  Names whose expressions reference this sheet mapped to dependencies\n");
		g_hash_table_foreach (deps->referencing_names,
				      dump_name_dep, NULL);
	}
}

void
gnm_dep_container_sanity_check (GnmDepContainer const *deps)
{
	GnmDependent const *dep;
	GHashTable *seenb4;

	if (deps->head && !deps->tail)
		g_warning ("Dependency container %p has head, but no tail.", deps);
	if (deps->tail && !deps->head)
		g_warning ("Dependency container %p has tail, but no head.", deps);
	if (deps->head && deps->head->prev_dep)
		g_warning ("Dependency container %p has head, but not at the beginning.", deps);
	if (deps->tail && deps->tail->next_dep)
		g_warning ("Dependency container %p has tail, but not at the end.", deps);

	seenb4 = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (dep = deps->head; dep; dep = dep->next_dep) {
		if (dep->prev_dep && (dep->prev_dep->next_dep != dep))
			g_warning ("Dependency container %p has left double-link failure at %p.", deps, dep);
		if (dep->next_dep && (dep->next_dep->prev_dep != dep))
			g_warning ("Dependency container %p has right double-link failure at %p.", deps, dep);
		if (!dep->next_dep && dep != deps->tail)
			g_warning ("Dependency container %p ends before its tail.", deps);
		if (!dependent_is_linked (dep))
			g_warning ("Dependency container %p contains unlinked dependency %p.", deps, dep);
		if (g_hash_table_lookup (seenb4, dep)) {
			g_warning ("Dependency container %p is circular.", deps);
			break;
		}
		g_hash_table_insert (seenb4, (gpointer)dep, (gpointer)dep);
	}
	g_hash_table_destroy (seenb4);
}

/**
 * dependent_debug_name :
 * @dep : The dependent we are interested in.
 *
 * A useful little debugging utility.
 */
void
dependent_debug_name (GnmDependent const *dep, GString *target)
{
	int t;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep_classes);

	if (dep->sheet != NULL) {
		g_string_append (target, dep->sheet->name_quoted);
		g_string_append_c (target, '!');
	} else
		g_warning ("Invalid dep, missing sheet");

	t = dependent_type (dep);
	if (t != DEPENDENT_CELL) {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		(*klass->debug_name) (dep, target);
	} else
		g_string_append (target, cell_name (DEP_TO_CELL (dep)));
}

